// Teste isolado de Wi-Fi: conecta e fica imprimindo o estado a cada 2 s.
// Sem MQTT, DHT11, microfone ou botoes. Usa o mesmo credenciais.h do projeto.
#include <ESP8266WiFi.h>
#include "credenciais.h"

volatile int  motivoDaQueda = -1;
volatile bool quedaPendente = false;
WiFiEventHandler tratadorDeQueda;
unsigned long ultimaImpressao = 0;

void setup() {
  Serial.begin(115200);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  tratadorDeQueda = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& evento) {
    motivoDaQueda = evento.reason;
    quedaPendente = true;
  });
  WiFi.begin(NOME_DA_REDE, SENHA_DA_REDE);
  Serial.println("\nTeste de Wi-Fi iniciado");
}

void loop() {
  if (quedaPendente) {
    quedaPendente = false;
    Serial.printf("[%lu s] QUEDA, motivo=%d\n", millis() / 1000, motivoDaQueda);
  }
  if (millis() - ultimaImpressao >= 2000) {
    ultimaImpressao = millis();
    Serial.printf("[%lu s] status=%d  rssi=%d dBm\n", millis() / 1000, WiFi.status(), WiFi.RSSI());
  }
}
