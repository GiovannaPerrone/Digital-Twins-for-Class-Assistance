/*
  Teste de QoS MQTT na placa (ESP8266) - Digital Twin sala 120

  Este sketch NAO substitui o DTclassroom.ino: e so para medir. Ele usa a
  biblioteca "MQTT" (Joel Gaehwiler / 256dpi), porque a PubSubClient so publica
  em QoS 0.

  Como funciona (round trip medido no proprio ESP, sem depender de relogio do PC):
    1. a placa publica um JSON de status (igual ao do firmware) em .../ping
    2. o script eco_qos.py, rodando no PC, devolve a mesma mensagem em .../pong
    3. a placa mede o tempo ate o pong voltar (micros) e conta o que se perdeu
    4. repete para QoS 0, 1 e 2, e publica o resumo de cada fase em .../resultado

  Antes de gravar:
    - Biblioteca: Sketch > Include Library > Manage Libraries > "MQTT" (Joel Gaehwiler)
    - Copie o seu credenciais.h para esta mesma pasta
    - SESSAO abaixo tem que ser igual ao --sessao do eco_qos.py
*/

#include <ESP8266WiFi.h>
#include <MQTT.h>
#include "credenciais.h"

// ---------------- configuracao do teste ----------------
const char* BROKER = "broker.hivemq.com";
const int   PORTA  = 1883;
const char* SESSAO = "gio01";                     // igual ao --sessao do eco_qos.py

const int           N_MSGS          = 100;        // mensagens por nivel de QoS
const unsigned long INTERVALO_MS    = 250;        // tempo entre publicacoes
const unsigned long ESPERA_FINAL_MS = 4000;       // espera os ultimos pongs
const int           NIVEIS[]        = {0, 1, 2};
const int           N_NIVEIS        = 3;
// --------------------------------------------------------

char TOPICO_PING[80], TOPICO_PONG[80], TOPICO_CONTROLE[80];
char TOPICO_EVENTO[80], TOPICO_RESULTADO[80];
char clientId[40];

WiFiClient rede;
MQTTClient mqtt(512);

int  qosAtual = 0;
int  pongQos  = -1;           // QoS da assinatura do pong (-1 = ainda nao assinou)
bool iniciar  = false;
bool testeEmAndamento = false;
bool concluido = false;

unsigned long tEnvio[N_MSGS];     // micros() no momento do envio
float rttMs[N_MSGS];              // ida e volta (ms)
float pubMs[N_MSGS];              // quanto tempo a chamada publish() demorou (ms)
bool  pongRecebido[N_MSGS];
bool  publicou[N_MSGS];
int   pongsDuplicados = 0;
int   pongsAtrasados  = 0;        // pong de outra fase chegando tarde
int   falhasPublish   = 0;
int   reconexoes      = 0;
int   ultimoErro      = 0;

int lerCampo(const String &texto, const char* chave) {
  int i = texto.indexOf(chave);
  if (i < 0) return -1;
  return texto.substring(i + strlen(chave)).toInt();
}

void aoReceber(String &topico, String &payload) {
  unsigned long agora = micros();   // carimba o mais cedo possivel

  if (topico == TOPICO_CONTROLE) {
    if (payload == "iniciar" && !testeEmAndamento && !concluido) iniciar = true;
    return;
  }
  if (topico != TOPICO_PONG) return;

  int q   = lerCampo(payload, "\"q\":");
  int seq = lerCampo(payload, "\"seq\":");
  if (q != qosAtual) { pongsAtrasados++; return; }
  if (seq < 0 || seq >= N_MSGS) return;
  if (pongRecebido[seq]) { pongsDuplicados++; return; }

  pongRecebido[seq] = true;
  rttMs[seq] = (agora - tEnvio[seq]) / 1000.0;
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
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void conectarMQTT() {
  while (!mqtt.connected()) {
    if (WiFi.status() != WL_CONNECTED) {
      delay(500);
      continue;
    }
    Serial.print("Conectando no broker... ");
    if (mqtt.connect(clientId)) {
      Serial.println("ok");
      mqtt.subscribe(TOPICO_CONTROLE, 1);
      if (pongQos >= 0) mqtt.subscribe(TOPICO_PONG, pongQos);   // reconexao no meio de uma fase
    } else {
      Serial.print("falhou, erro=");
      Serial.println((int)mqtt.lastError());
      delay(2000);
    }
  }
}

// espera atendendo a rede (nunca usa delay longo sem chamar mqtt.loop())
void esperar(unsigned long ms) {
  unsigned long fim = millis() + ms;
  while ((long)(millis() - fim) < 0) {
    mqtt.loop();
    delay(1);
  }
}

void ordenar(float *v, int n) {
  for (int i = 1; i < n; i++) {
    float x = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; }
    v[j + 1] = x;
  }
}

float percentil(float *ordenado, int n, int p) {
  if (n == 0) return 0;
  int indice = (int)((n - 1) * p / 100.0 + 0.5);
  return ordenado[indice];
}

void executarFase(int qos) {
  qosAtual = qos;
  for (int i = 0; i < N_MSGS; i++) {
    pongRecebido[i] = false; publicou[i] = false; rttMs[i] = 0; pubMs[i] = 0;
  }
  pongsDuplicados = pongsAtrasados = falhasPublish = reconexoes = ultimoErro = 0;

  Serial.print("\n=== Fase QoS ");
  Serial.print(qos);
  Serial.println(" ===");

  pongQos = qos;
  mqtt.subscribe(TOPICO_PONG, qos);
  esperar(1000);

  unsigned long t0 = millis();
  for (int seq = 0; seq < N_MSGS; seq++) {
    if (!mqtt.connected()) { reconexoes++; conectarMQTT(); }

    char msg[256];
    snprintf(msg, sizeof(msg),
      "{\"q\":%d,\"seq\":%d,\"sala\":\"120\",\"andar\":1,\"temperatura\":25.0,\"umidade\":60.0,"
      "\"chamados\":{\"tecnico\":false,\"material\":false,\"outro\":false},"
      "\"segundos\":{\"tecnico\":0,\"material\":0,\"outro\":0}}", qos, seq);

    unsigned long inicio = micros();
    tEnvio[seq] = inicio;
    bool ok = mqtt.publish(TOPICO_PING, msg, false, qos);
    pubMs[seq] = (micros() - inicio) / 1000.0;
    publicou[seq] = ok;
    if (!ok) { falhasPublish++; ultimoErro = (int)mqtt.lastError(); }

    unsigned long alvo = t0 + (unsigned long)(seq + 1) * INTERVALO_MS;
    while ((long)(millis() - alvo) < 0) { mqtt.loop(); delay(1); }
  }
  esperar(ESPERA_FINAL_MS);

  // ---- estatisticas ----
  float rtts[N_MSGS], pubs[N_MSGS];
  int nPong = 0, nPub = 0;
  for (int i = 0; i < N_MSGS; i++) {
    if (pongRecebido[i]) rtts[nPong++] = rttMs[i];
    if (publicou[i])     pubs[nPub++]  = pubMs[i];
  }
  ordenar(rtts, nPong);
  ordenar(pubs, nPub);

  float rttMedia = 0;
  for (int i = 0; i < nPong; i++) rttMedia += rtts[i];
  if (nPong) rttMedia /= nPong;

  float rttMediana = percentil(rtts, nPong, 50);
  float rttP95     = percentil(rtts, nPong, 95);
  float rttMax     = nPong ? rtts[nPong - 1] : 0;
  float pubMediana = percentil(pubs, nPub, 50);
  float pubMax     = nPub ? pubs[nPub - 1] : 0;
  float perda      = 100.0 * (N_MSGS - nPong) / N_MSGS;
  int   rssi       = WiFi.RSSI();

  Serial.printf("QOS=%d env=%d pong=%d perda_rtt=%.1f%% rtt_media=%.1f rtt_med=%.1f p95=%.1f max=%.1f "
                "pub_med=%.1f pub_max=%.1f falhas_pub=%d erro=%d reconex=%d dup=%d atras=%d rssi=%d\n",
                qos, N_MSGS, nPong, perda, rttMedia, rttMediana, rttP95, rttMax,
                pubMediana, pubMax, falhasPublish, ultimoErro, reconexoes,
                pongsDuplicados, pongsAtrasados, rssi);

  char resumo[320];
  snprintf(resumo, sizeof(resumo),
    "{\"qos\":%d,\"env\":%d,\"pong\":%d,\"rtt_media\":%.1f,\"rtt_med\":%.1f,\"rtt_p95\":%.1f,"
    "\"rtt_max\":%.1f,\"pub_med\":%.1f,\"pub_max\":%.1f,\"falhas_pub\":%d,\"erro\":%d,"
    "\"reconex\":%d,\"dup\":%d,\"atras\":%d,\"rssi\":%d}",
    qos, N_MSGS, nPong, rttMedia, rttMediana, rttP95, rttMax, pubMediana, pubMax,
    falhasPublish, ultimoErro, reconexoes, pongsDuplicados, pongsAtrasados, rssi);
  if (!mqtt.connected()) conectarMQTT();
  mqtt.publish(TOPICO_RESULTADO, resumo, false, 1);

  mqtt.unsubscribe(TOPICO_PONG);
  pongQos = -1;
  esperar(500);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  snprintf(TOPICO_PING,      sizeof(TOPICO_PING),      "digitaltwin/teste_qos/%s/ping",      SESSAO);
  snprintf(TOPICO_PONG,      sizeof(TOPICO_PONG),      "digitaltwin/teste_qos/%s/pong",      SESSAO);
  snprintf(TOPICO_CONTROLE,  sizeof(TOPICO_CONTROLE),  "digitaltwin/teste_qos/%s/controle",  SESSAO);
  snprintf(TOPICO_EVENTO,    sizeof(TOPICO_EVENTO),    "digitaltwin/teste_qos/%s/evento",    SESSAO);
  snprintf(TOPICO_RESULTADO, sizeof(TOPICO_RESULTADO), "digitaltwin/teste_qos/%s/resultado", SESSAO);
  snprintf(clientId, sizeof(clientId), "ESP8266_TesteQoS_%08X", ESP.getChipId());

  conectarWifi();

  mqtt.begin(BROKER, PORTA, rede);
  mqtt.onMessage(aoReceber);
  mqtt.setKeepAlive(30);
  mqtt.setTimeout(3000);          // publish com QoS 1/2 espera o ack ate 3 s
  conectarMQTT();
}

void loop() {
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();

  if (concluido) { delay(50); return; }

  if (!iniciar) {
    // avisa o PC que esta pronta (repete ate ele mandar "iniciar")
    static unsigned long ultimoAviso = 0;
    if (millis() - ultimoAviso > 3000) {
      mqtt.publish(TOPICO_EVENTO, "pronta", false, 1);
      Serial.println("Aguardando eco_qos.py (rode o script no PC)...");
      ultimoAviso = millis();
    }
    delay(10);
    return;
  }

  testeEmAndamento = true;
  iniciar = false;
  for (int k = 0; k < N_NIVEIS; k++) executarFase(NIVEIS[k]);
  testeEmAndamento = false;
  concluido = true;
  Serial.println("\nFIM. Resultados enviados ao PC. Aperte RST para repetir.");
}
