# Digital Twin — Gestão de Salas de Aula

Sistema embarcado que combina monitoramento ambiental em tempo real (temperatura e umidade) com um botão físico de assistência, que envia alertas a inspetores. Faz parte de um trabalho acadêmico sobre Digital Twin aplicado à gestão de salas de aula (Ibmec Barra).

## Visão geral do sistema

O projeto tem 3 camadas:

1. **Hardware** — ESP8266 (NodeMCU) lendo um sensor DHT11 (temperatura/umidade) e 3 botões físicos de assistência (Técnico, Material, Outros). *(concluído e validado)*
2. **Conectividade** — o ESP8266 se conecta ao Wi-Fi e envia os dados lidos para um backend. *(em desenvolvimento)*
3. **Representação virtual** — uma planta baixa 2D que reflete em tempo real o estado da sala (temperatura atual + alertas de botão), visível para os inspetores. *(em desenvolvimento)*

## Hardware

| Componente | Função |
|---|---|
| ESP8266 NodeMCU (ESP-12E) | Microcontrolador principal, Wi-Fi nativo |
| DHT11 (módulo 3 pinos) | Sensor de temperatura e umidade |
| 3x botão físico (push button) | Chamadas de assistência: Técnico, Material, Outros |
| LED RGB (módulo KY-009, 4 pinos) | Indicador de status (verde/amarelo/vermelho) — aguardando resistores |

## Mapeamento de pinos

| Pino NodeMCU | Conectado a |
|---|---|
| 3V3 | DHT11 VCC |
| D4 | DHT11 DATA |
| D1 | Botão — Técnico |
| D2 | Botão — Material |
| D3 | Botão — Outros (compartilha função com o botão FLASH da placa — não pressionar durante boot) |
| D6 | LED RGB — Vermelho |
| D7 | LED RGB — Verde |
| D5 | LED RGB — Azul |
| GND | Comum (DHT11, os 3 botões, cátodo do LED) |

Diagrama de conexões: [`docs/diagrama_conexoes.png`](docs/diagrama_conexoes.png)

## Firmware

- [`firmware/digital_twin_dht_botoes.ino`](firmware/digital_twin_dht_botoes.ino) — leitura do DHT11 + detecção dos 3 botões, com saída via Serial (115200 baud). Testado e validado em protoboard.

### Bibliotecas necessárias (Arduino IDE)

- Suporte de placa **esp8266** (via Boards Manager, URL: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`)
- **DHT sensor library** (Adafruit)
- **Adafruit Unified Sensor** (dependência da anterior)

Placa: `NodeMCU 1.0 (ESP-12E Module)`

## Status atual

- [x] Montagem física do circuito (DHT11 + 3 botões) em protoboard
- [x] Firmware de leitura do DHT11 + botões, validado via Monitor Serial
- [ ] LED RGB (aguardando resistores de 220Ω, chegada prevista 22/09)
- [ ] Conectividade Wi-Fi (ESP8266 enviando dados)
- [ ] Backend para receber os dados
- [ ] Dashboard / planta baixa 2D (representação virtual)
- [ ] Montagem final soldada + case impressa em 3D

## Próximos passos

Ver seção "Status atual" acima. O projeto precisa estar pronto para uso (não só protótipo em protoboard) até a segunda prova, incluindo case impressa em 3D.
