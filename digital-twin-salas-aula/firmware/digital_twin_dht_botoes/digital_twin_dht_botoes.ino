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

// >>> PREENCHE AQUI com os dados da sua rede Wi-Fi <<<
const char* nomeDaRede = "iPhone de Giovanna";
const char* senhaDaRede = "espaqui!!";
const char* mqttBroker = "broker.hivemq.com";
const int mqttPorta = 1883;
const char* topicoSala = "digitaltwin/sala120/status";

#define DHTPIN D4
#define DHTTYPE DHT11

#define BOTAO_TECNICO D1
#define BOTAO_MATERIAL D2
#define BOTAO_OUTROS D3

DHT dht(DHTPIN, DHTTYPE);
ESP8266WebServer servidor(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

float temperaturaAtual = 0;
float umidadeAtual = 0;

bool alertaAtivo = false;
String tipoAlertaAtivo = "nenhum";
unsigned long horaInicioAlerta = 0;

unsigned long ultimaLeituraDHT = 0;
const unsigned long intervaloDHT = 2000;

// Pagina do dashboard: planta baixa (SVG) + status ao vivo.
// A sala 120 (id="sala120") muda de cor via JavaScript, conforme
// o JSON devolvido por /dados.
const char PAGINA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Digital Twin - Sala 120</title>
<style>
  body { font-family: Arial, sans-serif; background: #f4f4f4; margin: 0; padding: 20px; text-align: center; }
  h1 { font-size: 20px; color: #222; }
  .mapa { background: #fff; border-radius: 10px; padding: 10px; display: inline-block; box-shadow: 0 1px 4px rgba(0,0,0,0.15); }
  .sala-label { font-size: 11px; font-weight: bold; fill: #222; }
  .outra-sala { fill: #f7b563; stroke: #a86a1f; stroke-width: 1; }
  .servico { fill: #8fd0a0; stroke: #2c7a43; stroke-width: 1; }
  .tech { fill: #7fb8e0; stroke: #2c6a94; stroke-width: 1; }
  .corredor { fill: #d9d9d9; stroke: #aaa; stroke-width: 1; }
  .escada { fill: #333; }
  .cartao { max-width: 340px; margin: 16px auto 0; background: #fff; border-radius: 10px; padding: 14px 18px; box-shadow: 0 1px 4px rgba(0,0,0,0.15); text-align: left; }
  .status-badge { display: inline-block; padding: 4px 12px; border-radius: 999px; color: #fff; font-weight: bold; font-size: 13px; }
  .linha { margin: 8px 0; font-size: 14px; color: #333; }
</style>
</head>
<body>
  <h1>Digital Twin - Sala 120</h1>

  <div class="mapa">
    <svg viewBox="0 0 380 520" width="320" height="440">
      <rect x="170" y="10" width="40" height="30" class="escada"/>
      <text x="190" y="55" text-anchor="middle" class="sala-label">escada</text>

      <rect x="175" y="45" width="30" height="435" class="corredor"/>

      <rect x="60" y="45" width="110" height="18" fill="#e6d6f0" stroke="#8a5fa8"/>
      <text x="115" y="58" text-anchor="middle" font-size="9">materiais</text>

      <rect x="60" y="68" width="110" height="88" class="tech"/>
      <text x="115" y="115" text-anchor="middle" class="sala-label">TECH 3</text>

      <rect x="60" y="160" width="110" height="88" class="tech"/>
      <text x="115" y="207" text-anchor="middle" class="sala-label">TECH 2</text>

      <rect x="40" y="253" width="130" height="38" fill="#d9d9d9" stroke="#aaa"/>
      <text x="105" y="275" text-anchor="middle" font-size="10">em breve...</text>

      <rect x="70" y="298" width="60" height="40" class="servico"/>
      <text x="100" y="321" text-anchor="middle" font-size="10">WC C</text>

      <rect x="70" y="342" width="60" height="48" class="servico"/>
      <text x="100" y="369" text-anchor="middle" font-size="10">WC F</text>

      <rect x="70" y="396" width="60" height="48" class="servico"/>
      <text x="100" y="423" text-anchor="middle" font-size="10">WC M</text>

      <rect x="55" y="452" width="100" height="55" class="outra-sala"/>
      <text x="105" y="483" text-anchor="middle" class="sala-label">121</text>

      <rect x="210" y="45" width="90" height="95" class="outra-sala"/>
      <text x="255" y="96" text-anchor="middle" class="sala-label">115</text>

      <rect x="210" y="148" width="90" height="68" class="outra-sala"/>
      <text x="255" y="185" text-anchor="middle" class="sala-label">117</text>

      <rect x="210" y="223" width="90" height="68" class="outra-sala"/>
      <text x="255" y="260" text-anchor="middle" class="sala-label">118</text>

      <rect x="210" y="298" width="90" height="82" class="outra-sala"/>
      <text x="255" y="342" text-anchor="middle" class="sala-label">119</text>

      <rect id="sala120" x="210" y="387" width="70" height="45" class="outra-sala"/>
      <text x="245" y="413" text-anchor="middle" class="sala-label">120</text>

      <rect x="170" y="482" width="40" height="30" class="escada"/>
      <text x="190" y="475" text-anchor="middle" class="sala-label">escada</text>
    </svg>
  </div>

  <div class="cartao">
    <div class="linha"><b>Status:</b> <span id="status" class="status-badge">carregando...</span></div>
    <div class="linha"><b>Temperatura:</b> <span id="temperatura">--</span> C</div>
    <div class="linha"><b>Umidade:</b> <span id="umidade">--</span> %</div>
    <div class="linha"><b>Chamado:</b> <span id="alerta">--</span></div>
    <div class="linha"><b>Em aberto ha:</b> <span id="tempo">--</span></div>
  </div>

<script>
async function atualizar() {
  try {
    const resposta = await fetch('/dados');
    const dados = await resposta.json();

    document.getElementById('temperatura').textContent = dados.temperatura.toFixed(1);
    document.getElementById('umidade').textContent = dados.umidade.toFixed(1);
    document.getElementById('alerta').textContent = dados.tipo_alerta;
    document.getElementById('tempo').textContent = dados.alerta_ativo ? (dados.segundos_em_aberto + 's') : '--';

    const sala = document.getElementById('sala120');
    const badge = document.getElementById('status');

    if (dados.alerta_ativo) {
      sala.style.fill = '#e05252';
      sala.style.stroke = '#8c1f1f';
      badge.textContent = 'ALERTA: ' + dados.tipo_alerta.toUpperCase();
      badge.style.background = '#e05252';
    } else {
      sala.style.fill = '#4caf6f';
      sala.style.stroke = '#2c7a43';
      badge.textContent = 'NORMAL';
      badge.style.background = '#4caf6f';
    }
  } catch (erro) {
    document.getElementById('status').textContent = 'sem conexao';
    document.getElementById('status').style.background = '#999';
  }
}

atualizar();
setInterval(atualizar, 2000);
</script>
</body>
</html>
)rawliteral";

void conectarWifi() {
  Serial.print("Conectando na rede ");
  Serial.println(nomeDaRede);

  WiFi.begin(nomeDaRede, senhaDaRede);

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
      Serial.println(" conectado!");
    } else {
      Serial.print(" falhou, codigo=");
      Serial.print(mqttClient.state());
      Serial.println(" tentando de novo em 5 segundos");
      delay(5000);
    }
  }
}



void tratarPaginaPrincipal() {
  servidor.send_P(200, "text/html", PAGINA_HTML);
}

void tratarDados() {
  String json = "{";
  json += "\"temperatura\":" + String(temperaturaAtual, 1) + ",";
  json += "\"umidade\":" + String(umidadeAtual, 1) + ",";
  json += "\"alerta_ativo\":" + String(alertaAtivo ? "true" : "false") + ",";
  json += "\"tipo_alerta\":\"" + tipoAlertaAtivo + "\",";
  json += "\"segundos_em_aberto\":" + String(alertaAtivo ? (millis() - horaInicioAlerta) / 1000 : 0);
  json += "}";

  servidor.send(200, "application/json", json);
}

// Abre ou encerra um chamado, dependendo do estado atual.
void alternarChamado(String tipo, String rotulo) {
  if (alertaAtivo && tipoAlertaAtivo == tipo) {
    // mesmo botao que abriu o chamado -> encerra
    alertaAtivo = false;
    tipoAlertaAtivo = "nenhum";
    Serial.println(">> Chamado " + rotulo + " ENCERRADO");
    mqttClient.publish(topicoSala, "encerrado");
  } else {
    // chamado novo (ou troca de tipo, se ja tinha outro aberto)
    alertaAtivo = true;
    tipoAlertaAtivo = tipo;
    horaInicioAlerta = millis();
    mqttClient.publish(topicoSala, tipo.c_str());
    Serial.println(">> Chamado: " + rotulo);
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(BOTAO_TECNICO, INPUT_PULLUP);
  pinMode(BOTAO_MATERIAL, INPUT_PULLUP);
  pinMode(BOTAO_OUTROS, INPUT_PULLUP);

  conectarWifi();
  mqttClient.setServer(mqttBroker, mqttPorta);
  conectarMQTT();

  servidor.on("/", tratarPaginaPrincipal);
  servidor.on("/dados", tratarDados);
  servidor.begin();
  Serial.println("Servidor web iniciado.");
}

void loop() {
  servidor.handleClient();
  if (!mqttClient.connected()) {
  conectarMQTT();
}
mqttClient.loop();

  if (millis() - ultimaLeituraDHT >= intervaloDHT) {
    ultimaLeituraDHT = millis();

    float umidade = dht.readHumidity();
    float temperatura = dht.readTemperature();

    if (!isnan(umidade) && !isnan(temperatura)) {
      temperaturaAtual = temperatura;
      umidadeAtual = umidade;
    }
  }

  if (digitalRead(BOTAO_TECNICO) == LOW) {
    alternarChamado("tecnico", "problema TECNICO");
    delay(300); // debounce
  }

  if (digitalRead(BOTAO_MATERIAL) == LOW) {
    alternarChamado("material", "falta de MATERIAL");
    delay(300);
  }

  if (digitalRead(BOTAO_OUTROS) == LOW) {
    alternarChamado("outros", "OUTROS");
    delay(300);
  }
}

