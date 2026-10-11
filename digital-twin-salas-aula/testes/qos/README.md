# Teste de QoS do MQTT na placa

Compara os níveis de QoS 0, 1 e 2 no nó (ESP8266). Os resultados alimentam a seção "QoS Evaluation" do artigo.

## Arquivos

- `teste_qos_placa/teste_qos_placa.ino`: sketch da placa. Usa a biblioteca **MQTT** (arduino-mqtt, de Joel Gaehwiler), porque a PubSubClient só publica em QoS 0. Precisa de um `credenciais.h` na mesma pasta, igual ao do firmware (esse arquivo não vai pro Git).
- `eco_qos.py`: script do PC. Devolve cada mensagem no mesmo QoS e salva o CSV.
- `resultados/run1.csv`, `run2.csv`, `run3.csv`: três rodadas estáveis (broker público HiveMQ, hotspot de celular em 2,4 GHz).
- `resultados/piloto_instavel.csv`: primeira rodada, com queda de conexão. Não entra na análise.

## Como rodar

1. No Arduino IDE, instale a biblioteca MQTT, copie o `credenciais.h` para `teste_qos_placa/` e grave o sketch.
2. No PC: `pip install paho-mqtt`.
3. Rode `python eco_qos.py --sessao gio01 --saida run1`. A `--sessao` tem que ser igual à constante `SESSAO` do sketch.
4. Aperte RST na placa. O teste começa sozinho e leva de 5 a 8 minutos.
5. Para repetir, aperte RST de novo e troque o nome (`run2`, `run3`).
6. Ao final, regrave o firmware principal (`DTclassroom.ino`). Com o sketch de teste na placa, ela não lê botões nem temperatura.

## Resultado (média ± desvio padrão de 3 rodadas de 100 mensagens)

| QoS | Perda (%) | RTT mediano (ms) | publish() (ms) | Falhas |
|---|---|---|---|---|
| 0 | 2,0 ± 3,5 | 1431 ± 639 | 0,5 | 0 |
| 1 | 1,0 ± 1,0 | 1181 ± 213 | 874 ± 151 | 2 |
| 2 | 2,7 ± 1,2 | 2800 ± 527 | 1097 ± 29 | 8 |

Conclusão: o QoS 2 dobra a latência, e os QoS 1 e 2 travam a placa cerca de 1 s a cada publicação. Sem ganho consistente de confiabilidade neste cenário, o firmware segue em QoS 0.

## Limites

Um único cenário (hotspot móvel, broker público sem autenticação) e três rodadas. O tempo limite de confirmação nos QoS 1 e 2 foi de 3 s.
