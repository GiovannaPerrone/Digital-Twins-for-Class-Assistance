/*
  Digital Twin - Fase 2: Wi-Fi + dashboard com planta baixa

  O proprio ESP8266 serve a pagina do dashboard (com a planta baixa
  embutida) e os dados em JSON, tudo pelo mesmo endereco - assim nao
  tem problema de rede/CORS/HTTPS misto, tudo roda na mesma origem.

  Cada botao funciona como um interruptor: aperta uma vez pra abrir
  o chamado (sala fica vermelha), aperta o MESMO botao de novo pra
  encerrar o chamado (sala volta a ficar verde). Se apertar um botao
  diferente enquanto ja tem um chamado aberto, o chamado troca pro
  novo tipo (só um alerta ativo por vez, já que o LED so mostra um
  status).

  Acesse http://<ip-da-placa>/  no navegador (mesma rede Wi-Fi)
  pra ver o dashboard. O endereco /dados devolve so o JSON puro.

  Mapeamento de pinos (igual ao firmware anterior):
    3V3 -> DHT11 VCC
    D4  -> DHT11 DATA
    D1  -> Botao TECNICO
    D2  -> Botao MATERIAL
    D3  -> Botao OUTROS
    GND -> comum

  Bibliotecas necessarias (alem das ja instaladas):
    - ESP8266WiFi (ja vem com o suporte de placa esp8266)
    - ESP8266WebServer (idem)
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <PubSubClient.h>
#include "credenciais.h"

// agora em credenciais.h: NOME_DA_REDE e SENHA_DA_REDE
const char* mqttBroker = "broker.hivemq.com";
const int mqttPorta = 1883;
const char* topicoSala = "digitaltwin/sala120/status";
const char* TOPICO_STATUS  = "digitaltwin/sala120/status";
const char* TOPICO_COMANDO = "digitaltwin/sala120/comando";
const char* TOPICO_ONLINE  = "digitaltwin/sala120/online";

const char* SALA  = "120";
const int   ANDAR = 1;
const unsigned long INTERVALO_PUBLICACAO  = 10000UL;   // 10 s
const unsigned long INTERVALO_TEMPERATURA = 600000UL;  // 10 min
const unsigned long INTERVALO_RECONEXAO   = 5000UL;    // 5 s


#define DHTPIN D4
#define DHTTYPE DHT11

#define BOTAO_TECNICO D1
#define BOTAO_MATERIAL D2
#define BOTAO_OUTRO D3

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

unsigned long ultimaLeituraDHT = 0;
const unsigned long intervaloDHT = 600000;

unsigned long ultimaPublicacao = 0;
const unsigned long intervaloPublicacao = 10000;


// Controle de tempo
unsigned long ultimaTentativaMQTT = 0;
bool          jaTentouMQTT        = false;
bool          wifiAvisado         = false;

char clientId[40];

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
  Serial.println("Acesse esse endereco no navegador (mesma rede Wi-Fi) pra ver o dashboard.");
}


void conectarMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Conectando no broker MQTT...");

    if (mqttClient.connect("ESP8266_Sala120")) {
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
  // Comparação segura mesmo quando millis() dá a volta (~49 dias).
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
    publicarStatus();
  }

  if (foiApertado(botaoMaterial)) {
    chamadoMaterial = !chamadoMaterial;
    inicioMaterial  = chamadoMaterial ? millis() : 0;
    Serial.println(chamadoMaterial ? ">> material: ABERTO" : ">> material: encerrado");
    publicarStatus();
  }

  if (foiApertado(botaoOutro)) {
    chamadoOutro = !chamadoOutro;
    inicioOutro  = chamadoOutro ? millis() : 0;
    Serial.println(chamadoOutro ? ">> outro: ABERTO" : ">> outro: encerrado");
    publicarStatus();
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();  

  pinMode(BOTAO_TECNICO,  INPUT_PULLUP);
  pinMode(BOTAO_MATERIAL, INPUT_PULLUP);
  pinMode(BOTAO_OUTRO,    INPUT_PULLUP);

  conectarWifi();

  mqttClient.setServer(mqttBroker, mqttPorta);
  mqttClient.setCallback(aoReceberComando);
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