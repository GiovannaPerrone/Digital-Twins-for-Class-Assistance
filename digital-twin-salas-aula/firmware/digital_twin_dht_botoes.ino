/*
  Digital Twin - Monitoramento de sala de aula
  Fase 1: leitura do DHT11 + 3 botões de assistência (sem LED por enquanto)

  Mapeamento de pinos (validado no KiCad):
    3V3 -> DHT11 VCC
    D4  -> DHT11 DATA
    D1  -> Botao TECNICO
    D2  -> Botao MATERIAL
    D3  -> Botao OUTROS  (cuidado: e o mesmo pino do botao FLASH da placa,
                           nao aperte ele durante o boot/upload)
    GND -> comum (DHT11, os 3 botoes)

  Biblioteca necessaria: "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor"
  Instale pelo Gerenciador de Bibliotecas do Arduino IDE antes de compilar.
*/

#include <DHT.h>

#define DHTPIN D4
#define DHTTYPE DHT11

#define BOTAO_TECNICO D1
#define BOTAO_MATERIAL D2
#define BOTAO_OUTROS D3

DHT dht(DHTPIN, DHTTYPE);

unsigned long ultimaLeituraDHT = 0;
const unsigned long intervaloDHT = 2000; // DHT11 so atualiza a cada 1-2s

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(BOTAO_TECNICO, INPUT_PULLUP);
  pinMode(BOTAO_MATERIAL, INPUT_PULLUP);
  pinMode(BOTAO_OUTROS, INPUT_PULLUP);

  Serial.println();
  Serial.println("Digital Twin - iniciando...");
}

void loop() {
  // Leitura periodica do DHT11
  if (millis() - ultimaLeituraDHT >= intervaloDHT) {
    ultimaLeituraDHT = millis();

    float umidade = dht.readHumidity();
    float temperatura = dht.readTemperature();

    if (isnan(umidade) || isnan(temperatura)) {
      Serial.println("Erro ao ler o DHT11 (confira a fiacao)");
    } else {
      Serial.print("Temperatura: ");
      Serial.print(temperatura);
      Serial.print(" C | Umidade: ");
      Serial.print(umidade);
      Serial.println(" %");
    }
  }

  // Leitura dos botoes (INPUT_PULLUP -> pressionado = LOW)
  if (digitalRead(BOTAO_TECNICO) == LOW) {
    Serial.println(">> Chamado: problema TECNICO");
    delay(300); // debounce simples
  }

  if (digitalRead(BOTAO_MATERIAL) == LOW) {
    Serial.println(">> Chamado: falta de MATERIAL");
    delay(300);
  }

  if (digitalRead(BOTAO_OUTROS) == LOW) {
    Serial.println(">> Chamado: OUTROS");
    delay(300);
  }
}
