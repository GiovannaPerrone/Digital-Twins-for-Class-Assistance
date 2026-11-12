/*
  Digital Twin - Wi-Fi + MQTT (broker publico HiveMQ) + LED RGB de status

  O ESP8266 le o DHT11 e os 3 botoes de chamado, publica tudo num broker
  MQTT publico, e agora tambem acende um LED RGB (modulo KY-009) pra dar
  um status visual na propria sala, sem precisar olhar o painel:

    verde   -> tudo normal
    amarelo -> temperatura acima do limite, sem chamado aberto
    vermelho -> algum chamado aberto (tecnico, material ou outro) - prioridade maxima

  Mapeamento de pinos:
    3V3 -> DHT11 VCC
    D4  -> DHT11 DATA
    D1  -> Botao TECNICO
    D2  -> Botao MATERIAL
    D3  -> Botao OUTRO
    D6  -> LED RGB Vermelho
    D7  -> LED RGB Verde
    D5  -> LED RGB Azul
    D8  -> livre (nao usado ainda; cuidado, precisa ficar em LOW no boot)
    A0  -> Microfone MAX9814 OUT (VDD -> 3V3, GND -> GND, GAIN -> VDD = 40 dB, AR solto)
    GND -> comum (DHT11, os 3 botoes, catodo comum do LED, microfone)

  Bibliotecas necessarias (alem das ja instaladas):
    - PubSubClient (Nick O'Leary)
    - DHT sensor library (Adafruit) + Adafruit Unified Sensor

  Ajustes feitos nesta versao:
    1. LED RGB de status adicionado (funcao atualizarLedStatus()).
    2. mqttClient.setBufferSize(400) - o buffer padrao do PubSubClient
       e de so 128 bytes, menor que o JSON de status que a gente monta;
       sem isso, publish() falhava calado.
    3. Client ID unico por placa (usa o chip ID), em vez de um texto
       fixo - evita que o broker derrube a conexao quando duas sessoes
       usam o mesmo ID.
    5. Microfone MAX9814 no A0: dois estalos de dedos seguidos abrem um
       chamado da categoria "outro" (funcao lerMicrofone()). Parametros
       logo antes dela.
    4. Removido o #include do ESP8266WebServer (nao tem servidor web
       embutido aqui) e as variaveis que sobraram sem uso (topicoSala,
       ultimaLeituraDHT, intervaloDHT).
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include "credenciais.h"

// >>> Broker MQTT <<<
const char* mqttBroker = "broker.emqx.io";
const int mqttPorta = 1883;

const char* TOPICO_STATUS  = "digitaltwin/sala120/status";
const char* TOPICO_COMANDO = "digitaltwin/sala120/comando";
const char* TOPICO_ONLINE  = "digitaltwin/sala120/online";

const char* SALA  = "120";
const int   ANDAR = 1;
const unsigned long INTERVALO_PUBLICACAO  = 10000UL;   // 10 s
const unsigned long INTERVALO_TEMPERATURA = 600000UL;  // 10 min (troque por 10000UL so pra demonstracao)
const unsigned long INTERVALO_RECONEXAO   = 5000UL;    // 5 s


#define DHTPIN D4
#define DHTTYPE DHT11

#define BOTAO_TECNICO D1
#define BOTAO_MATERIAL D2
#define BOTAO_OUTRO D3

// D6 e D7 trocados de propósito: na fiação real, o pino que fisicamente
// chega no R do módulo está ligado ao D7, e o que chega no G está no D6.
// Mais fácil inverter aqui do que remexer nos jumpers da protoboard.
#define LED_R D6   // conferido com o teste do LED em 12/nov: D6 acende vermelho
#define LED_G D7   // e D7 acende verde
#define LED_B D5

const float TEMPERATURA_ALERTA = 26.0;  // acima disso, LED fica amarelo (sem chamado aberto)

DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

float temperaturaAtual = 0;
float umidadeAtual = 0;
bool temLeitura = false;
unsigned long proximaLeituraDHT = 2000;

bool chamadoTecnico = false;
bool chamadoMaterial = false;
bool chamadoOutro = false;
unsigned long inicioTecnico = 0;
unsigned long inicioMaterial = 0;
unsigned long inicioOutro = 0;

struct Botao {
  uint8_t       pino;
  int           leituraAnterior;
  int           estadoEstavel;
  unsigned long mudouEm;
};
const unsigned long DEBOUNCE_MS = 50;

Botao botaoTecnico  = { BOTAO_TECNICO,  HIGH, HIGH, 0 };
Botao botaoMaterial = { BOTAO_MATERIAL, HIGH, HIGH, 0 };
Botao botaoOutro    = { BOTAO_OUTRO,    HIGH, HIGH, 0 };

unsigned long ultimaPublicacao = 0;
const unsigned long intervaloPublicacao = 10000;

// Controle de tempo
unsigned long ultimaTentativaMQTT = 0;
bool          jaTentouMQTT        = false;
bool          wifiAvisado         = false;

// ID unico do client MQTT, montado no setup() a partir do chip ID
// da propria placa (ex: "ESP8266_Sala120_A1B2C3D4").
char clientId[40];

// Acende o LED RGB de acordo com a prioridade: chamado aberto > temperatura
// alta > normal. So mexe nos pinos, nao publica nada no MQTT.
void atualizarLedStatus() {
  bool temChamado = chamadoTecnico || chamadoMaterial || chamadoOutro;
  bool temperaturaAlta = temLeitura && temperaturaAtual >= TEMPERATURA_ALERTA;

  if (temChamado) {
    digitalWrite(LED_R, HIGH);   // vermelho
    digitalWrite(LED_G, LOW);
    digitalWrite(LED_B, LOW);
  } else if (temperaturaAlta) {
    digitalWrite(LED_R, HIGH);   // amarelo (vermelho + verde)
    digitalWrite(LED_G, HIGH);
    digitalWrite(LED_B, LOW);
  } else {
    digitalWrite(LED_R, LOW);    // verde
    digitalWrite(LED_G, HIGH);
    digitalWrite(LED_B, LOW);
  }
}

void publicarStatus() {
  if (!mqttClient.connected() || !temLeitura) return;

  char t[10], u[10];
  dtostrf(temperaturaAtual, 1, 1, t);   // 22.4
  dtostrf(umidadeAtual,     1, 1, u);

  // Há quantos segundos cada chamado está aberto (0 se estiver fechado).
  unsigned long agora = millis();
  unsigned long segTecnico  = chamadoTecnico  ? (agora - inicioTecnico)  / 1000 : 0;
  unsigned long segMaterial = chamadoMaterial ? (agora - inicioMaterial) / 1000 : 0;
  unsigned long segOutro    = chamadoOutro    ? (agora - inicioOutro)    / 1000 : 0;

  char json[320];
  snprintf(json, sizeof(json),
    "{\"sala\":\"%s\",\"andar\":%d,\"temperatura\":%s,\"umidade\":%s,"
    "\"chamados\":{\"tecnico\":%s,\"material\":%s,\"outro\":%s},"
    "\"segundos\":{\"tecnico\":%lu,\"material\":%lu,\"outro\":%lu}}",
    SALA, ANDAR, t, u,
    chamadoTecnico  ? "true" : "false",
    chamadoMaterial ? "true" : "false",
    chamadoOutro    ? "true" : "false",
    segTecnico, segMaterial, segOutro);

  // true = retida: quem abrir o painel depois recebe o estado na hora.
  if (!mqttClient.publish(TOPICO_STATUS, json, true)) {
    Serial.println("Falha ao publicar status no broker MQTT");
  }
  ultimaPublicacao = agora;
}

void aoReceberComando(char* topico, byte* payload, unsigned int tamanho) {
  // O payload chega como bytes soltos, sem o marcador de fim de texto
  // que o C exige. Copiamos para "cmd" e colocamos esse marcador ('\0').
  char cmd[32];
  unsigned int n = tamanho < sizeof(cmd) - 1 ? tamanho : sizeof(cmd) - 1;
  memcpy(cmd, payload, n);
  cmd[n] = '\0';

  Serial.print("<< comando: ");
  Serial.println(cmd);

  // strcmp compara dois textos: devolve 0 quando são iguais.
  if (strcmp(cmd, "fechar:tecnico") == 0) {
    chamadoTecnico = false;
    inicioTecnico  = 0;
  } else if (strcmp(cmd, "fechar:material") == 0) {
    chamadoMaterial = false;
    inicioMaterial  = 0;
  } else if (strcmp(cmd, "fechar:outro") == 0) {
    chamadoOutro = false;
    inicioOutro  = 0;
  } else {
    return;   // comando desconhecido: ignora e não publica nada
  }

  atualizarLedStatus();
  publicarStatus();   // confirma ao painel que o chamado foi fechado
}

bool foiApertado(Botao &b) {
  unsigned long agora = millis();
  int leitura = digitalRead(b.pino);

  // 1. A leitura mudou? Anota o momento e espera estabilizar.
  if (leitura != b.leituraAnterior) {
    b.mudouEm = agora;
    b.leituraAnterior = leitura;
  }

  // 2. Ficou estável por 50 ms e é diferente do último valor aceito?
  if (agora - b.mudouEm >= DEBOUNCE_MS && leitura != b.estadoEstavel) {
    b.estadoEstavel = leitura;
    return leitura == LOW;   // true só se a mudança foi para APERTADO
  }

  return false;
}

// Motivo da ultima queda do Wi-Fi (preenchido pelo evento do ESP8266).
// Codigos comuns: 2 auth expirou, 8 AP mandou sair, 15 handshake falhou,
// 200 beacon timeout (perdeu o sinal do AP), 201 AP nao encontrado, 202 senha errada.
volatile int  ultimoMotivoWifi = -1;
volatile bool wifiCaiuAviso    = false;
WiFiEventHandler handlerWifiDesconectou;

void conectarWifi() {
  Serial.print("Conectando na rede ");
  Serial.println(NOME_DA_REDE);

  WiFi.begin(NOME_DA_REDE, SENHA_DA_REDE);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Conectado! Endereco IP: ");
  Serial.println(WiFi.localIP());
}

void conectarMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Conectando no broker MQTT como ");
    Serial.print(clientId);
    Serial.print("...");

    if (mqttClient.connect(clientId)) {
      Serial.println(" conectado.");
      mqttClient.subscribe(TOPICO_COMANDO);
      publicarStatus();
    } else {
      Serial.print(" falhou, codigo=");
      Serial.print(mqttClient.state());
      Serial.println(" tentando de novo em 5 segundos");
      delay(5000);
    }
  }
}

void lerTemperatura() {
  // Comparacao segura mesmo quando millis() da a volta (~49 dias).
  if ((long)(millis() - proximaLeituraDHT) < 0) return;

  float u = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(u) || isnan(t)) {
    Serial.println("Falha na leitura do DHT11, tentando em 2 s");
    proximaLeituraDHT = millis() + 2000;
    return;
  }

  temperaturaAtual = t;
  umidadeAtual     = u;
  proximaLeituraDHT = millis() + INTERVALO_TEMPERATURA;

  Serial.print("temperatura ");
  Serial.print(temperaturaAtual, 1);
  Serial.print(" C, umidade ");
  Serial.print(u, 0);
  Serial.println("%");

  atualizarLedStatus();

  if (!temLeitura) {       // primeira leitura: publica sem esperar os 10 s
    temLeitura = true;
    publicarStatus();
  }
}

void lerBotoes() {
  // Cada botão: se foi apertado, inverte o booleano (false -> true -> false),
  // anota o horário se abriu, e publica o novo estado.

  if (foiApertado(botaoTecnico)) {
    chamadoTecnico = !chamadoTecnico;
    inicioTecnico  = chamadoTecnico ? millis() : 0;
    Serial.println(chamadoTecnico ? ">> tecnico: ABERTO" : ">> tecnico: encerrado");
    atualizarLedStatus();
    publicarStatus();
  }

  if (foiApertado(botaoMaterial)) {
    chamadoMaterial = !chamadoMaterial;
    inicioMaterial  = chamadoMaterial ? millis() : 0;
    Serial.println(chamadoMaterial ? ">> material: ABERTO" : ">> material: encerrado");
    atualizarLedStatus();
    publicarStatus();
  }

  if (foiApertado(botaoOutro)) {
    chamadoOutro = !chamadoOutro;
    inicioOutro  = chamadoOutro ? millis() : 0;
    Serial.println(chamadoOutro ? ">> outro: ABERTO" : ">> outro: encerrado");
    atualizarLedStatus();
    publicarStatus();
  }
}

// ---------------------------------------------------------------------
// Microfone MAX9814 no A0: dois estalos de dedos seguidos abrem um chamado.
//
// Como funciona (sem machine learning, so amplitude e tempo):
//   1. Le o A0 a cada INTERVALO_AMOSTRA_US e acompanha devagar o nivel
//      central do sinal (~400 com o MAX9814) e o ruido de fundo.
//   2. Um som "alto" comeca quando o desvio passa de FATOR_SOBRE_RUIDO vezes
//      o ruido de fundo (nunca menos que LIMIAR_MINIMO) e termina quando fica
//      SILENCIO_FIM_ESTALO_MS abaixo da metade desse limiar.
//   3. Se o som foi curto (ate DURACAO_MAXIMA_ESTALO_MS), conta como estalo.
//      Voz, musica e batidas longas passam disso e sao ignoradas.
//   4. Dois estalos com INTERVALO_MIN a INTERVALO_MAX entre eles abrem o
//      chamado escolhido em TIPO_CHAMADO_ESTALO. O estalo so ABRE: pra fechar,
//      use o botao ou o painel.
// ---------------------------------------------------------------------
#define PINO_MICROFONE A0

const bool MOSTRAR_DEBUG_ESTALO = true;   // imprime cada som curto detectado (use pra calibrar)
const int  TIPO_CHAMADO_ESTALO  = 2;      // 0 = tecnico, 1 = material, 2 = outro

const unsigned long INTERVALO_AMOSTRA_US           = 4000;  // 250 Hz (1000 = 1 kHz derrubava o Wi-Fi; se estabilizar, da pra tentar 2000)
const unsigned long TEMPO_AQUECIMENTO_MS           = 2000;  // ignora estalos logo apos ligar
const float         LIMIAR_MINIMO                  = 90.0;  // em unidades do ADC (0 a 1023)
const float         FRACAO_DO_PICO_NO_FIM          = 0.25;  // o som "acabou" quando cai abaixo dessa fracao do pico
const float         FATOR_SOBRE_RUIDO              = 6.0;
const unsigned long DURACAO_MAXIMA_ESTALO_MS       = 80;
const unsigned long SILENCIO_FIM_ESTALO_MS         = 15;
const unsigned long INTERVALO_MIN_ENTRE_ESTALOS_MS = 150;
const unsigned long INTERVALO_MAX_ENTRE_ESTALOS_MS = 900;
const unsigned long PAUSA_APOS_ESTALO_DUPLO_MS     = 3000;  // evita abrir de novo por engano

float         centroDoSinal       = 0;
float         ruidoMedio          = 2;
bool          microfoneIniciado   = false;
unsigned long microfoneIniciadoEm = 0;
unsigned long ultimaAmostraUs     = 0;

bool          dentroDeSomAlto     = false;
unsigned long inicioSomAltoMs     = 0;
unsigned long ultimoSomAltoMs     = 0;
float         picoDoSomAlto       = 0;

int           estalosContados     = 0;
unsigned long primeiroEstaloMs    = 0;
unsigned long ignorarEstalosAteMs = 0;

void abrirChamadoPeloEstalo() {
  const char*    nome;
  bool*          chamado;
  unsigned long* inicio;

  if (TIPO_CHAMADO_ESTALO == 1) {
    nome = "material"; chamado = &chamadoMaterial; inicio = &inicioMaterial;
  } else if (TIPO_CHAMADO_ESTALO == 2) {
    nome = "outro";    chamado = &chamadoOutro;    inicio = &inicioOutro;
  } else {
    nome = "tecnico";  chamado = &chamadoTecnico;  inicio = &inicioTecnico;
  }

  ignorarEstalosAteMs = millis() + PAUSA_APOS_ESTALO_DUPLO_MS;
  if (*chamado) return;   // ja estava aberto: nada a fazer

  *chamado = true;
  *inicio  = millis();
  Serial.print(">> estalo duplo: ");
  Serial.print(nome);
  Serial.println(": ABERTO");
  atualizarLedStatus();
  publicarStatus();
}

void registrarEstalo(unsigned long inicioMs) {
  if (estalosContados == 1) {
    unsigned long intervalo = inicioMs - primeiroEstaloMs;
    if (intervalo < INTERVALO_MIN_ENTRE_ESTALOS_MS) return;   // eco do mesmo estalo
    if (intervalo <= INTERVALO_MAX_ENTRE_ESTALOS_MS) {
      estalosContados = 0;
      abrirChamadoPeloEstalo();
      return;
    }
    // passou da janela: este estalo vira o primeiro de uma nova tentativa
  }
  estalosContados  = 1;
  primeiroEstaloMs = inicioMs;
}

void lerMicrofone() {
  unsigned long agoraUs = micros();
  if (agoraUs - ultimaAmostraUs < INTERVALO_AMOSTRA_US) return;
  ultimaAmostraUs = agoraUs;

  unsigned long agoraMs = millis();
  float valor = analogRead(PINO_MICROFONE);

  if (!microfoneIniciado) {
    centroDoSinal       = valor;
    microfoneIniciado   = true;
    microfoneIniciadoEm = agoraMs;
    return;
  }

  float desvio = fabs(valor - centroDoSinal);
  float limiar = max(LIMIAR_MINIMO, FATOR_SOBRE_RUIDO * ruidoMedio);

  // Acompanha devagar o nivel central e o ruido de fundo, mas so enquanto
  // nao ha som alto (senao o proprio estalo "ensinaria" o limiar a subir).
  if (!dentroDeSomAlto && desvio < limiar) {
    centroDoSinal += (valor - centroDoSinal) * 0.001;
    ruidoMedio    += (desvio - ruidoMedio)   * 0.002;
  }

  if (agoraMs - microfoneIniciadoEm < TEMPO_AQUECIMENTO_MS) return;
  if ((long)(agoraMs - ignorarEstalosAteMs) < 0) return;

  if (!dentroDeSomAlto) {
    if (desvio >= limiar) {
      dentroDeSomAlto = true;
      inicioSomAltoMs = agoraMs;
      ultimoSomAltoMs = agoraMs;
      picoDoSomAlto   = desvio;
    }
    return;
  }

  // Dentro de um som alto: acompanha o pico e o ultimo instante "alto".
  if (desvio > picoDoSomAlto) picoDoSomAlto = desvio;
  float limiarFim = max(limiar * 0.5f, picoDoSomAlto * FRACAO_DO_PICO_NO_FIM);
  if (desvio >= limiarFim) ultimoSomAltoMs = agoraMs;
  if (agoraMs - ultimoSomAltoMs < SILENCIO_FIM_ESTALO_MS) return;   // ainda nao acabou

  // O som acabou: foi curto o bastante pra ser um estalo?
  dentroDeSomAlto = false;
  unsigned long duracaoMs = ultimoSomAltoMs - inicioSomAltoMs;
  bool pareceEstalo = duracaoMs <= DURACAO_MAXIMA_ESTALO_MS;

  if (MOSTRAR_DEBUG_ESTALO) {
    Serial.print(pareceEstalo ? "[mic] estalo" : "[mic] som longo, ignorado");
    Serial.print("  pico=");     Serial.print(picoDoSomAlto, 0);
    Serial.print("  duracao=");  Serial.print(duracaoMs);
    Serial.print(" ms  limiar="); Serial.print(limiar, 0);
    Serial.print("  ruido=");    Serial.println(ruidoMedio, 1);
  }

  if (pareceEstalo) {
    registrarEstalo(inicioSomAltoMs);
  } else {
    estalosContados = 0;   // som longo (voz, batida) quebra a sequencia
  }
}

// ===== MICROFONE PELO PINO DIGITAL (D0) =====
// A saida do MAX9814 fica parada em ~1,2 V no silencio: o pino digital le sempre
// o mesmo valor. Com som forte o sinal oscila e o pino alterna 0/1 rapido.
// Uma rajada curta de alternancias = estalo; dois estalos no intervalo certo
// abrem o chamado (mesma logica de registrarEstalo() do microfone analogico).
#define PINO_MICROFONE_DIGITAL D0

// Calibrado em 12/nov: estalos reais deram 16 a 24 transicoes em 2 a 7 ms;
// ruido deu ate 12 transicoes, ou durou 17 ms ou mais.
const int           TRANSICOES_MINIMAS_SOM     = 14;   // menos que isso e ruido solto
const unsigned long DURACAO_MAXIMA_ESTALO_DIGITAL_MS = 15;  // estalo e curto: mais que isso nao conta
const unsigned long SILENCIO_FIM_SOM_DIGITAL_MS = 15;  // sem alternar por 15 ms = som acabou

int           nivelAnteriorMicDigital  = -1;
bool          somDigitalAtivo          = false;
unsigned long inicioSomDigitalMs       = 0;
unsigned long ultimaTransicaoMs        = 0;
int           transicoesNoSom          = 0;
unsigned long ultimoAvisoNivelMs       = 0;

void lerMicrofoneDigital() {
  unsigned long agoraMs = millis();
  int nivel = digitalRead(PINO_MICROFONE_DIGITAL);

  if (nivelAnteriorMicDigital < 0) {          // primeira leitura
    nivelAnteriorMicDigital = nivel;
    Serial.printf("[mic D0] nivel em silencio = %d\n", nivel);
    return;
  }

  if (nivel != nivelAnteriorMicDigital) {
    nivelAnteriorMicDigital = nivel;
    if (!somDigitalAtivo) {
      somDigitalAtivo    = true;
      inicioSomDigitalMs = agoraMs;
      transicoesNoSom    = 0;
    }
    transicoesNoSom++;
    ultimaTransicaoMs = agoraMs;
  }

  if (!somDigitalAtivo) return;
  if (agoraMs - ultimaTransicaoMs < SILENCIO_FIM_SOM_DIGITAL_MS) return;   // ainda tem som

  // O som acabou
  somDigitalAtivo = false;
  if (transicoesNoSom < TRANSICOES_MINIMAS_SOM) return;    // ruido solto, ignora

  unsigned long duracaoMs = ultimaTransicaoMs - inicioSomDigitalMs;
  bool pareceEstalo = duracaoMs <= DURACAO_MAXIMA_ESTALO_DIGITAL_MS;

  if (MOSTRAR_DEBUG_ESTALO) {
    Serial.printf("[mic D0] %s  transicoes=%d  duracao=%lu ms\n",
                  pareceEstalo ? "estalo" : "som longo, ignorado", transicoesNoSom, duracaoMs);
  }

  if (agoraMs < TEMPO_AQUECIMENTO_MS) return;
  if ((long)(agoraMs - ignorarEstalosAteMs) < 0) return;

  if (pareceEstalo) {
    registrarEstalo(inicioSomDigitalMs);
  } else {
    estalosContados = 0;
  }
}

// ===== MODO TESTE DE WI-FI =====
// true  = so conecta no Wi-Fi e imprime o estado a cada 2 s (sem MQTT, DHT11, mic, botoes).
// false = projeto normal.
const bool MODO_TESTE_WIFI = false;
unsigned long ultimaImpressaoTesteWifi = 0;
// true = alem do Wi-Fi, conecta no MQTT e publica "ola" a cada 5 s num topico de teste.
const bool TESTE_COM_MQTT = true;
unsigned long ultimaTentativaTesteMqtt = 0;
unsigned long ultimaPublicacaoTesteMqtt = 0;

void iniciarTesteWifi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  handlerWifiDesconectou = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& evento) {
    ultimoMotivoWifi = evento.reason;
    wifiCaiuAviso = true;
  });
  WiFi.begin(NOME_DA_REDE, SENHA_DA_REDE);
  if (TESTE_COM_MQTT) {
    snprintf(clientId, sizeof(clientId), "ESP8266_Teste_%08X", ESP.getChipId());
    mqttClient.setServer(mqttBroker, mqttPorta);
    mqttClient.setKeepAlive(30);
    mqttClient.setSocketTimeout(10);
  }
  Serial.println("\n*** MODO TESTE DE WI-FI (so Wi-Fi + MQTT simples, nada mais) ***");
}

void rodarTesteWifi() {
  if (wifiCaiuAviso) {
    wifiCaiuAviso = false;
    Serial.printf("[%lu s] QUEDA, motivo=%d\n", millis() / 1000, ultimoMotivoWifi);
  }
  if (millis() - ultimaImpressaoTesteWifi >= 2000) {
    ultimaImpressaoTesteWifi = millis();
    Serial.printf("[%lu s] status=%d  rssi=%d dBm\n", millis() / 1000, WiFi.status(), WiFi.RSSI());
  }

  if (TESTE_COM_MQTT && WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      if (millis() - ultimaTentativaTesteMqtt >= 5000) {
        ultimaTentativaTesteMqtt = millis();
        bool conectou = mqttClient.connect(clientId);
        Serial.printf("[%lu s] MQTT %s (codigo=%d)\n", millis() / 1000, conectou ? "conectado" : "falhou", mqttClient.state());
      }
    } else {
      mqttClient.loop();
      if (millis() - ultimaPublicacaoTesteMqtt >= 5000) {
        ultimaPublicacaoTesteMqtt = millis();
        mqttClient.publish("teste/dtclassroom/ping", "ola");
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  // Diagnostico: diz por que a placa (re)iniciou. "External System" e normal logo apos o upload;
  // "Hardware Watchdog", "Exception" ou "Power on" no meio do uso indicam travamento ou queda de energia.
  Serial.printf("\n=== Inicio. Motivo do ultimo reset: %s ===\n", ESP.getResetReason().c_str());
  if (MODO_TESTE_WIFI) {
    iniciarTesteWifi();
    return;
  }
  dht.begin();

  pinMode(BOTAO_TECNICO,  INPUT_PULLUP);
  pinMode(BOTAO_MATERIAL, INPUT_PULLUP);
  pinMode(BOTAO_OUTRO,    INPUT_PULLUP);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(PINO_MICROFONE_DIGITAL, INPUT);   // saida do MAX9814 tambem ligada no D0
  // TESTE DO LED: acende um pino por vez (1 s cada) pra conferir a fiacao.
  // Depois de conferir, pode mudar TESTAR_LED_NO_INICIO para false.
  const bool TESTAR_LED_NO_INICIO = false;
  if (TESTAR_LED_NO_INICIO) {
    const uint8_t pinosLed[3]   = {LED_R, LED_G, LED_B};
    const char*   nomesLed[3]   = {"D6 (deveria ser VERMELHO)", "D7 (deveria ser VERDE)", "D5 (deveria ser AZUL)"};
    for (int posicao = 0; posicao < 3; posicao++) {
      digitalWrite(LED_R, LOW); digitalWrite(LED_G, LOW); digitalWrite(LED_B, LOW);
      digitalWrite(pinosLed[posicao], HIGH);
      Serial.print("[teste LED] so o pino ");
      Serial.println(nomesLed[posicao]);
      delay(1500);
    }
    digitalWrite(LED_R, LOW); digitalWrite(LED_G, LOW); digitalWrite(LED_B, LOW);
  }
  atualizarLedStatus();   // ja acende verde assim que liga, antes de conectar

  // Monta um client ID unico por placa (chip ID em hexadecimal),
  // pra nao colidir com outras conexoes usando o mesmo nome.
  snprintf(clientId, sizeof(clientId), "ESP8266_Sala%s_%08X", SALA, ESP.getChipId());

  // Desliga o modem sleep do Wi-Fi: com ele ligado, alguns roteadores/hotspots
  // derrubam a conexao da ESP8266. Gasta um pouco mais de energia, sem problema no USB.
  WiFi.persistent(false);                     // nao regrava a configuracao do Wi-Fi na flash
  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);         // modo mais compativel com hotspot de celular
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true);
  handlerWifiDesconectou = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& evento) {
    ultimoMotivoWifi = evento.reason;
    wifiCaiuAviso = true;
  });
  conectarWifi();

  mqttClient.setServer(mqttBroker, mqttPorta);
  mqttClient.setKeepAlive(30);       // padrao 15 s: derruba facil em rede com atraso
  mqttClient.setSocketTimeout(10);   // espera ate 10 s por resposta antes de desistir
  mqttClient.setCallback(aoReceberComando);
  mqttClient.setBufferSize(400);   // buffer padrao (128) e pequeno demais pro JSON de status
  conectarMQTT();
}

// Reconexao que NAO trava o resto: tenta uma vez a cada INTERVALO_RECONEXAO e
// devolve o controle, assim botoes e microfone continuam funcionando com a
// rede caindo. (conectarMQTT(), que trava, continua sendo usada so no setup.)
unsigned long ultimaTentativaMqtt = 0;

unsigned long ultimaTentativaWifi = 0;

void reconectarMQTTSemTravar() {
  if (wifiCaiuAviso) {
    wifiCaiuAviso = false;
    Serial.print("[wifi] desconectou (");
    Serial.print(millis() / 1000);
    Serial.print(" s desde o inicio), motivo=");
    Serial.println(ultimoMotivoWifi);
  }

  // Sem Wi-Fi nao adianta tentar o broker: religa o Wi-Fi (a cada 10 s) e sai.
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - ultimaTentativaWifi >= 10000) {
      ultimaTentativaWifi = millis();
      Serial.print("[wifi] sem conexao (status=");
      Serial.print(WiFi.status());
      Serial.println("), religando...");
      WiFi.disconnect();
      WiFi.begin(NOME_DA_REDE, SENHA_DA_REDE);
    }
    return;
  }

  if (mqttClient.connected()) return;
  if (millis() - ultimaTentativaMqtt < INTERVALO_RECONEXAO) return;
  ultimaTentativaMqtt = millis();

  Serial.print("Reconectando no broker MQTT... ");
  if (mqttClient.connect(clientId)) {
    Serial.println("conectado.");
    mqttClient.subscribe(TOPICO_COMANDO);
    publicarStatus();
  } else {
    Serial.print("falhou, codigo=");
    Serial.print(mqttClient.state());
    // diagnostico: separa "Wi-Fi caiu" de "internet/broker inacessivel"
    Serial.print(" | wifi=");
    Serial.print(WiFi.status() == WL_CONNECTED ? "ok" : "CAIU");
    Serial.print(" rssi=");
    Serial.print(WiFi.RSSI());
    IPAddress ipBroker;
    Serial.print(" dns=");
    Serial.print(WiFi.hostByName(mqttBroker, ipBroker) ? ipBroker.toString() : String("FALHOU"));
    Serial.println(" (nova tentativa em 5 segundos)");
  }
}

// TESTE: false = nao le o microfone (analogRead no A0 pode derrubar o Wi-Fi do ESP8266).
// Se com false o Wi-Fi ficar estavel, o problema e a leitura do microfone.
const bool MICROFONE_LIGADO = false;          // leitura pelo A0 (derruba o Wi-Fi do ESP8266)
const bool MICROFONE_DIGITAL_LIGADO = true;   // leitura pelo D0 (so 0/1, nao usa o conversor analogico)
// Chaves de teste: tire uma peca por vez pra achar o que derruba o Wi-Fi.
const bool DHT11_LIGADO   = true;
const bool BOTOES_LIGADOS = true;

void loop() {
  if (MODO_TESTE_WIFI) {
    rodarTesteWifi();
    return;
  }
  reconectarMQTTSemTravar();
  mqttClient.loop();

  if (DHT11_LIGADO) {
    lerTemperatura();
  }

  if (BOTOES_LIGADOS) {
    lerBotoes();
  }

  if (MICROFONE_LIGADO) {
    lerMicrofone();
  }

  if (MICROFONE_DIGITAL_LIGADO) {
    lerMicrofoneDigital();
  }

  if (millis() - ultimaPublicacao >= intervaloPublicacao) {
    publicarStatus();
  }
}
