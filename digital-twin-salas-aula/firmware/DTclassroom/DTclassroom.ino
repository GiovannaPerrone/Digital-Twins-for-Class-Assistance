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
    GND -> comum (DHT11, os 3 botoes, catodo comum do LED)

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
    4. Removido o #include do ESP8266WebServer (nao tem servidor web
       embutido aqui) e as variaveis que sobraram sem uso (topicoSala,
       ultimaLeituraDHT, intervaloDHT).
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include "credenciais.h"

// >>> Broker MQTT <<<
const char* mqttBroker = "broker.hivemq.com";
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
#define LED_R D7
#define LED_G D6
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

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(BOTAO_TECNICO,  INPUT_PULLUP);
  pinMode(BOTAO_MATERIAL, INPUT_PULLUP);
  pinMode(BOTAO_OUTRO,    INPUT_PULLUP);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  atualizarLedStatus();   // ja acende verde assim que liga, antes de conectar

  // Monta um client ID unico por placa (chip ID em hexadecimal),
  // pra nao colidir com outras conexoes usando o mesmo nome.
  snprintf(clientId, sizeof(clientId), "ESP8266_Sala%s_%08X", SALA, ESP.getChipId());

  conectarWifi();

  mqttClient.setServer(mqttBroker, mqttPorta);
  mqttClient.setCallback(aoReceberComando);
  mqttClient.setBufferSize(400);   // buffer padrao (128) e pequeno demais pro JSON de status
  conectarMQTT();
}

void loop() {
  if (!mqttClient.connected()) {
    conectarMQTT();
  }
  mqttClient.loop();

  lerTemperatura();

  lerBotoes();

  if (millis() - ultimaPublicacao >= intervaloPublicacao) {
    publicarStatus();
  }
}
