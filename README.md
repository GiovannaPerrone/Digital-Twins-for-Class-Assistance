# Digital Twin — Gestão de Salas de Aula

Sistema embarcado que combina monitoramento ambiental em tempo real (temperatura e umidade) com um botão físico de assistência, que envia alertas a inspetores. Faz parte de um trabalho acadêmico sobre Digital Twin aplicado à gestão de salas de aula (Ibmec Barra).

## Visão geral do sistema

O projeto tem 3 camadas:

1. **Hardware** — ESP8266 (NodeMCU) lendo um sensor DHT11 (temperatura/umidade), 3 botões físicos de assistência (Técnico, Material, Outros) e um LED RGB como indicador local de status. *(concluído e validado)*
2. **Conectividade** — o ESP8266 publica os dados lidos via protocolo MQTT (broker público HiveMQ) e assina um tópico de comando para fechamento remoto de chamados. *(concluído e validado)*
3. **Representação virtual** — uma planta baixa 2D que reflete em tempo real o estado da sala (temperatura atual + alertas de botão), visível para os inspetores. *(em desenvolvimento)*

## Hardware

| Componente | Função |
|---|---|
| ESP8266 NodeMCU (ESP-12E) | Microcontrolador principal, Wi-Fi nativo |
| DHT11 (módulo 3 pinos) | Sensor de temperatura e umidade |
| 3x botão físico (push button) | Chamadas de assistência: Técnico, Material, Outros |
| LED RGB (módulo KY-009, 4 pinos, cátodo comum) | Indicador local de status (verde/amarelo/vermelho) |
| 3x resistor 220Ω | Limitação de corrente nos canais R/G/B do LED |

## Mapeamento de pinos

| Pino NodeMCU | Conectado a |
|---|---|
| 3V3 | DHT11 VCC |
| D4 | DHT11 DATA |
| D1 | Botão — Técnico |
| D2 | Botão — Material |
| D3 | Botão — Outros (compartilha função com o botão FLASH da placa — não pressionar durante boot) |
| D7 | LED RGB — Vermelho |
| D6 | LED RGB — Verde |
| D5 | LED RGB — Azul |
| GND | Comum (DHT11, os 3 botões, cátodo do LED) |

> Nota: os pinos D6/D7 estão invertidos em relação ao "esperado" (D6→R, D7→G) de propósito — na montagem física, os fios de R e G do módulo ficaram trocados na protoboard, e a correção foi feita invertendo os `#define` no firmware em vez de remontar o circuito.

Diagrama de conexões: [`docs/diagrama_conexoes.png`](docs/diagrama_conexoes.png)

## Firmware

- [`firmware/DTclassroom.ino`](firmware/DTclassroom.ino) — leitura do DHT11, detecção dos 3 botões, controle do LED RGB e comunicação MQTT (publicação de status + assinatura de comandos). Testado e validado em protoboard.

### Bibliotecas necessárias (Arduino IDE)

- Suporte de placa **esp8266** (via Boards Manager, URL: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`)
- **DHT sensor library** (Adafruit)
- **Adafruit Unified Sensor** (dependência da anterior)
- **PubSubClient** (comunicação MQTT)

Placa: `NodeMCU 1.0 (ESP-12E Module)`

## Customizações

O firmware parte de bibliotecas padrão do ecossistema ESP8266/Arduino, mas várias adaptações foram feitas para atender aos requisitos específicos do projeto:

- **Lógica de prioridade do LED**: implementação própria (não vem de nenhuma biblioteca) que decide qual cor exibir com base no estado combinado de temperatura e chamados — vermelho (chamado aberto) tem prioridade sobre âmbar (temperatura acima do limite), que tem prioridade sobre verde (normal). O amarelo é obtido acendendo os canais vermelho e verde simultaneamente, já que o módulo KY-009 não tem canal amarelo dedicado.
- **Payload MQTT customizado**: estrutura JSON própria publicada de forma retained, contendo identificador da sala, leituras de temperatura/umidade e o estado de cada um dos três tipos de chamado (aberto/fechado, com tempo decorrido).
- **Tópico de comando por sala**: o firmware assina um tópico MQTT específico da sala para permitir que o dashboard feche remotamente um chamado aberto, sem exigir que o inspetor esteja na mesma rede local da sala.
- **Correção de pinagem R/G**: ajuste no código para inverter os pinos usados pelos canais vermelho e verde do LED, compensando uma inversão física na fiação (ver nota na seção de mapeamento de pinos acima) sem precisar remontar o circuito.
- **Conectividade Wi-Fi**: configuração ajustada para lidar com a limitação do ESP8266 de não suportar redes 5GHz — validado usando hotspot com compatibilidade forçada para 2.4GHz.

## Status atual

- [x] Montagem física do circuito (DHT11 + 3 botões + LED RGB) em protoboard
- [x] Firmware de leitura do DHT11, botões e controle do LED, validado via Monitor Serial
- [x] Conectividade Wi-Fi + MQTT (publicação de status e recebimento de comandos)
- [ ] Dashboard / planta baixa 2D (representação virtual)
- [ ] Montagem final soldada + case impressa em 3D

## Próximos passos

Ver seção "Status atual" acima. O projeto precisa estar pronto para uso (não só protótipo em protoboard) até a segunda prova, incluindo case impressa em 3D.
