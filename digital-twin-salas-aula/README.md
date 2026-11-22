# Digital Twin — Gestão de Salas de Aula

Sistema embarcado que combina monitoramento ambiental em tempo real (temperatura e umidade) com três formas de pedir assistência — botões físicos e estalo duplo de dedos captado por microfone —, que enviam alertas a inspetores. Faz parte de um trabalho acadêmico sobre Digital Twin aplicado à gestão de salas de aula (Ibmec Barra).

## Visão geral do sistema

O projeto tem 3 camadas:

1. **Hardware** — ESP8266 (NodeMCU) lendo um sensor DHT11 (temperatura/umidade), 3 botões físicos de assistência (Técnico, Material, Outros), um microfone MAX9814 (abertura de chamado por estalo duplo) e um LED RGB como indicador local de status. *(concluído e validado em protoboard)*
2. **Conectividade** — o ESP8266 publica os dados lidos via protocolo MQTT (broker público EMQX) e assina um tópico de comando para fechamento remoto de chamados. *(concluído e validado)*
3. **Representação virtual** — painel web (React) com planta baixa 2D do andar, que reflete em tempo real o estado de cada sala (temperatura atual + alertas de chamado) e mostra a fila de chamados para os inspetores. *(funcionando com o simulador; a placa real ainda não está ligada a ele, ver "Camada de fog e painel")*
4. **Fog** — broker Mosquitto local + serviço Python que guarda histórico em SQLite e detecta sala fora do ar, rodando num notebook da própria rede, sem depender de internet externa. *(montado; ver [`fog/README_fog.md`](fog/README_fog.md))*

## Hardware

| Componente | Função |
|---|---|
| ESP8266 NodeMCU (ESP-12E) | Microcontrolador principal, Wi-Fi nativo |
| DHT11 (módulo 3 pinos) | Sensor de temperatura e umidade |
| 3x botão físico (push button) | Chamadas de assistência: Técnico, Material, Outros |
| Microfone MAX9814 (módulo com AGC) | Detecção de estalo duplo de dedos, que abre um chamado sem tocar no dispositivo |
| LED RGB (módulo KY-009, 4 pinos, cátodo comum) | Indicador local de status (verde/amarelo/vermelho) |
| 3x resistor 220Ω | Limitação de corrente nos canais R/G/B do LED |
| Case impressa em 3D (base, tampa e 3 pinos de botão) | Abriga a placa; ver seção "Case impressa em 3D" |

## Mapeamento de pinos

| Pino NodeMCU | Conectado a |
|---|---|
| 3V3 | DHT11 VCC, MAX9814 VDD e GAIN (ganho de 40 dB) |
| D4 | DHT11 DATA |
| D1 | Botão — Técnico |
| D2 | Botão — Material |
| D3 | Botão — Outros (compartilha função com o botão FLASH da placa — não pressionar durante boot) |
| D6 | LED RGB — Vermelho |
| D7 | LED RGB — Verde |
| D5 | LED RGB — Azul |
| D0 | MAX9814 OUT (leitura digital — ver seção "Microfone" abaixo) |
| A0 | **Livre** — não usar para o microfone (derruba o Wi-Fi) |
| GND | Comum (DHT11, os 3 botões, cátodo do LED, MAX9814) |

> Nota: a ordem dos fios R/G do LED foi conferida com um teste que acende um canal por vez no boot (constante `TESTAR_LED_NO_INICIO` no firmware). Se a montagem mudar e as cores saírem trocadas, ative o teste, veja qual pino acende qual cor e ajuste os `#define LED_R/LED_G/LED_B`.

Diagrama de conexões: [`docs/diagrama_conexoes.png`](docs/diagrama_conexoes.png) *(ainda mostra a versão sem microfone)*

## Case impressa em 3D

Impressa numa Creality Ender-3 V3 SE (camada 0,2 mm, 2 paredes). Medidas em mm, com origem no centro da perfboard.

**Base** (a que está impressa hoje):

| Item | Valor |
|---|---|
| Externo | 90,8 × 110,8 × 22,8 |
| Parede / vão interno | 2,4 / 86 × 106 × 20,4 |
| Apoios da placa | 4 apoios de 5 mm de altura nos furos da perfboard (±32, ±42), com furo-guia de 2,6 mm |
| Postes dos parafusos da tampa | Ø 7 nos cantos, furo-guia de 2,6 mm em (±40, ±50) |
| USB | entalhe de 16 mm na parede curta, centrado em x = +1,8, aberto até a borda (a tampa fecha por cima) |
| Ventilação do DHT11 | 5 frestas na parede longa, centradas em y = −31,5 (altura do sensor) |

**Tampa** (reimpressa em 19/nov): 90,8 × 110,8, chapa de 2,4 mm, 6 lábios de encaixe com folga de 0,4 mm da parede interna, 4 furos de 3,6 mm com rebaixo para parafuso M3 autorroscante, furos de 7,6 mm para os 3 botões, 8 mm para o LED e 6 mm para o microfone. Imprimir com a face de fora na mesa, sem suporte.

**Pinos dos botões**: 3 peças (cabeça Ø 10 × 1,5 mm, haste Ø 7 × 6,7 mm) que atravessam a tampa e apertam os push buttons; em repouso a cabeça fica 0,5 mm acima da tampa.

> A primeira tampa impressa (84,8 × 104,8) era de uma versão posterior e mais compacta da base, com os postes em (±38,2, ±48,2), e por isso não encaixava. A tampa nova foi gerada a partir da geometria exata da base impressa e conferida no computador (sem interferência, furos alinhados com os postes).

## Firmware

- [`firmware/DTclassroom/DTclassroom.ino`](firmware/DTclassroom/DTclassroom.ino) — leitura do DHT11, detecção dos 3 botões, detecção de estalo duplo pelo microfone, controle do LED RGB e comunicação MQTT (publicação de status + assinatura de comandos). Testado e validado em protoboard.
- [`firmware/teste_wifi/teste_wifi.ino`](firmware/teste_wifi/teste_wifi.ino) — sketch mínimo de diagnóstico: só conecta no Wi-Fi e imprime estado e sinal (RSSI) a cada 2 s, mostrando o motivo de cada queda. Serve para separar problema de rede/placa de problema do firmware.

### Credenciais do Wi-Fi

O nome e a senha da rede ficam em `credenciais.h`, que **não vai para o Git** (está no `.gitignore`). Para montar o seu, copie `firmware/DTclassroom/credenciais.exemplo.h` para `credenciais.h` na mesma pasta e preencha `NOME_DA_REDE` e `SENHA_DA_REDE`. O `teste_wifi` precisa de uma cópia do mesmo arquivo dentro da pasta dele.

### Bibliotecas necessárias (Arduino IDE)

- Suporte de placa **esp8266** (via Boards Manager, URL: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`)
- **DHT sensor library** (Adafruit)
- **Adafruit Unified Sensor** (dependência da anterior)
- **PubSubClient** (comunicação MQTT)

Placa: `NodeMCU 1.0 (ESP-12E Module)`. Se o upload falhar no meio (`Failed to write compressed data to flash`), baixe `Tools > Upload Speed` para 115200 e use um cabo USB curto ligado direto no PC.

### MQTT

| Item | Valor |
|---|---|
| Broker | `broker.emqx.io`, porta 1883 |
| Status (publicação retained, a cada 10 s e a cada mudança) | `digitaltwin/sala120/status` |
| Comando (fechamento remoto de chamado) | `digitaltwin/sala120/comando` |
| Presença | `digitaltwin/sala120/online` |

> O dashboard precisa estar apontando para o **mesmo broker** que o firmware. O projeto começou no HiveMQ público e foi trocado para o EMQX durante os testes de estabilidade. Hoje **nenhuma das duas versões do painel fala com o EMQX**: o `useSala.js` antigo ainda aponta para o HiveMQ e o `useSalas.js` (fog) aponta para o Mosquitto local. Ver a seção seguinte.

## Camada de fog e painel

- [`fog/`](fog) — Mosquitto local (`mosquitto.conf`, MQTT na 1883 e WebSocket na 9001, com usuário e senha), `fog_service.py` (assina `digitaltwin/+/status`, grava leituras, eventos e latência em SQLite, marca a sala como offline após 60 s sem notícia e publica o resumo em `digitaltwin/fog/painel`) e `simulador.py` (salas virtuais 121 em diante; nunca usa a 120, que é a placa real). Passo a passo de instalação no Windows e experimentos em [`fog/README_fog.md`](fog/README_fog.md).
- [`../dashboard/`](../dashboard) — painel React + Vite. A versão atual (`App.jsx` com `useSalas`, `PlantaFog`, `ResumoAndar`, `FilaChamadosFog`) lê o broker do fog em `ws://localhost:9001`. A versão anterior, de uma sala só (`useSala`, `Planta`, `FilaChamados`), continua no projeto.

**Para a placa real entrar no fog** falta mudar o firmware: `mqttBroker` passa a ser o IP do notebook do fog (placa e notebook na mesma rede) e o `connect()` passa a enviar usuário e senha do Mosquitto. Tópicos e JSON não mudam. Enquanto isso não for feito, a placa publica no EMQX público e o painel só mostra salas simuladas.


### Chaves de configuração no firmware

Constantes no `DTclassroom.ino` para ligar/desligar partes sem apagar código:

| Constante | Padrão | Para que serve |
|---|---|---|
| `MICROFONE_DIGITAL_LIGADO` | `true` | Detecção de estalo pelo D0 |
| `MICROFONE_LIGADO` | `false` | Detecção antiga pelo A0 (mantida só para referência — derruba o Wi-Fi) |
| `DHT11_LIGADO` / `BOTOES_LIGADOS` | `true` | Desligar a leitura de uma peça para isolar defeitos |
| `MODO_TESTE_WIFI` | `false` | Roda só Wi-Fi (e MQTT simples, se `TESTE_COM_MQTT`) sem nenhum sensor |
| `TESTAR_LED_NO_INICIO` | `false` | Acende R, G e B um por vez no boot para conferir a fiação do LED |
| `MOSTRAR_DEBUG_ESTALO` | `true` | Imprime cada som curto detectado (usado para calibrar) |
| `TIPO_CHAMADO_ESTALO` | `2` | Chamado aberto pelo estalo duplo: 0 = técnico, 1 = material, 2 = outro |
| `TEMPERATURA_ALERTA` | `26.0` | Acima disso, o LED fica amarelo (se não houver chamado) |

## Microfone e estalo duplo

O estalo duplo abre um chamado (por padrão, "Outros"); ele **só abre** — o fechamento é pelo botão ou pelo dashboard. Todo o processamento é feito na própria placa: nenhum áudio é gravado ou enviado pela rede, só o evento "chamado aberto".

**Por que o microfone está no D0 e não no A0.** A primeira versão lia o MAX9814 pelo A0 (conversor analógico) a 1 kHz. Isso derrubava o Wi-Fi do ESP8266 repetidamente (motivos 200/201 — o rádio deixava de enxergar o ponto de acesso). Os testes de isolamento mostraram:

- só Wi-Fi: estável; Wi-Fi + MQTT: estável; + DHT11 + botões: estável;
- ligando a leitura do A0 (a 1 kHz ou a 250 Hz): o Wi-Fi caía em poucos minutos.

A solução foi ligar a saída do MAX9814 num **pino digital**. Em silêncio a saída fica parada em ~1,2 V e o pino lê sempre o mesmo nível; com um som forte o sinal oscila e o pino alterna entre 0 e 1 muito rápido. O firmware conta essas alternâncias:

1. Uma rajada de alternâncias termina quando o pino fica 15 ms sem mudar.
2. A rajada conta como **estalo** se teve pelo menos `TRANSICOES_MINIMAS_SOM` (14) alternâncias e durou no máximo `DURACAO_MAXIMA_ESTALO_DIGITAL_MS` (15 ms).
3. Dois estalos com 150 a 900 ms entre eles abrem o chamado; depois disso o microfone fica 3 s surdo para não abrir de novo.

Calibração (12/nov, protoboard): estalos reais deram 16 a 24 alternâncias em 2 a 7 ms; ruídos (fala, batidas na mesa) deram até 12 alternâncias, ou duraram 17 ms ou mais. Se o ambiente mudar, use as linhas `[mic D0]` do Monitor Serial para reajustar os dois limites.

## Conectividade e diagnóstico

O firmware reconecta sozinho, sem travar botões e microfone enquanto a rede está fora:

- **Wi-Fi**: se cair, tenta religar a cada 10 s; cada queda é impressa com o código de motivo (`[wifi] desconectou, motivo=...`). Modem sleep desligado (`WIFI_NONE_SLEEP`) e modo 802.11g forçado, que é mais compatível com hotspot de celular.
- **MQTT**: tenta reconectar a cada 5 s, com keepalive de 30 s e timeout de 10 s; quando falha, imprime se o Wi-Fi está de pé, o RSSI e se o DNS do broker resolveu.
- No boot, o firmware imprime o motivo do último reset (`ESP.getResetReason()`), para diferenciar queda de energia/travamento de simples perda de rede.

Lições dos testes com hotspot de celular:

- O ESP8266 só conecta em **2,4 GHz** (configurar a banda do hotspot).
- Em celulares que fazem hotspot **enquanto estão conectados a outro Wi-Fi**, o hotspot segue o canal desse Wi-Fi e pode sumir por instantes; desligar o Wi-Fi do celular e usar só dados móveis deixa o hotspot estável.
- Códigos de motivo mais comuns: 200 (perdeu o sinal do ponto de acesso), 201 (não encontrou a rede), 2/8/15 (o ponto de acesso derrubou a conexão), 202 (senha errada).

## Customizações

O firmware parte de bibliotecas padrão do ecossistema ESP8266/Arduino, mas várias adaptações foram feitas para atender aos requisitos específicos do projeto:

- **Lógica de prioridade do LED**: implementação própria (não vem de nenhuma biblioteca) que decide qual cor exibir com base no estado combinado de temperatura e chamados — vermelho (chamado aberto) tem prioridade sobre âmbar (temperatura acima do limite), que tem prioridade sobre verde (normal). O amarelo é obtido acendendo os canais vermelho e verde simultaneamente, já que o módulo KY-009 não tem canal amarelo dedicado.
- **Payload MQTT customizado**: estrutura JSON própria publicada de forma retained, contendo identificador da sala, leituras de temperatura/umidade e o estado de cada um dos três tipos de chamado (aberto/fechado, com tempo decorrido).
- **Tópico de comando por sala**: o firmware assina um tópico MQTT específico da sala para permitir que o dashboard feche remotamente um chamado aberto, sem exigir que o inspetor esteja na mesma rede local da sala.
- **Detecção de estalo duplo sem conversor analógico**: leitura da saída do microfone por pino digital, contando alternâncias de nível, para não interferir no Wi-Fi do ESP8266 (ver seção "Microfone e estalo duplo").
- **Reconexão sem bloqueio**: Wi-Fi e MQTT se recuperam sozinhos sem parar a leitura de botões e microfone.
- **Conectividade Wi-Fi**: configuração ajustada para lidar com a limitação do ESP8266 de não suportar redes 5GHz — validado usando hotspot com compatibilidade forçada para 2.4GHz.

## Testes

- [`testes/qos`](testes/qos) — comparação dos níveis de QoS 0, 1 e 2 do MQTT na placa (latência, perda de mensagens e custo do `publish()`), usada no artigo. O QoS 2 dobrou a latência, e os QoS 1 e 2 travam a placa cerca de 1 s a cada publicação, sem ganho consistente de confiabilidade; por isso o firmware segue em QoS 0. As instruções para reproduzir estão no README da pasta.
- Isolamento das quedas de Wi-Fi (12/nov) — `teste_wifi` e as chaves do firmware, ligando uma peça por vez; resultado descrito na seção "Microfone e estalo duplo".

## Status atual

- [x] Montagem física do circuito (DHT11 + 3 botões + LED RGB + microfone) em protoboard
- [x] Firmware de leitura do DHT11, botões e controle do LED, validado via Monitor Serial
- [x] Conectividade Wi-Fi + MQTT (publicação de status e recebimento de comandos), com reconexão automática
- [x] Abertura de chamado por estalo duplo (microfone no D0), calibrada e sem derrubar o Wi-Fi
- [x] Avaliação de QoS do MQTT na placa (3 rodadas, ver [`testes/qos`](testes/qos))
- [x] Projeto da case para impressão 3D (base, tampa e pinos dos botões)
- [x] Impressão da base e dos pinos
- [x] Tampa refeita para a base impressa (19/nov)
- [ ] Teste de encaixe da tampa nova e montagem na case
- [ ] Montagem final soldada em perfboard (microfone no D0, não no A0)
- [x] Painel React com planta do andar e fila de chamados, ligado ao fog
- [x] Camada de fog (Mosquitto local + serviço Python + SQLite + simulador)
- [ ] Firmware apontando para o broker do fog (IP do notebook + usuário/senha)
- [ ] Atualizar o diagrama de conexões com o microfone
- [ ] Atualizar o comentário de cabeçalho do `DTclassroom.ino` (ainda diz HiveMQ e microfone no A0)

## Próximos passos

1. Testar o encaixe da tampa nova, montar a placa na case e conferir os 3 botões, o LED e o microfone com a tampa fechada.
2. Soldar o circuito na perfboard com o microfone no D0.
3. Ligar a placa real ao fog e rodar os experimentos de [`fog/README_fog.md`](fog/README_fog.md).
4. Recalibrar o estalo duplo com a sala cheia e comparar o DHT11 com um termômetro de referência.
5. Para várias salas: configuração por sala (número da sala e andar sem recompilar) e suporte à rede do campus.
