# Camada de fog — passo a passo (Windows)

Tudo roda no mesmo notebook: broker, serviço de fog, simulador e painel.
A única coisa que continua fora é a placa ESP8266 da sala 120.

```
  placa 120 ──┐
              │  MQTT/TCP 1883
  simulador ──┼──────────────►  Mosquitto  ──►  fog_service.py  ──┐
  (121…)      │                    │                 (SQLite)     │
              │                    │ WebSocket 9001               │
              │                    ▼                              │
              └────────────────  painel React  ◄──────────────────┘
                                                digitaltwin/fog/painel
```

---

## 1. Instalar o Mosquitto

PowerShell **como administrador**:

```powershell
winget install EclipseFoundation.Mosquitto
```

Instala em `C:\Program Files\mosquitto\`. Crie a pasta de configuração:

```powershell
mkdir C:\mosquitto\data
copy mosquitto.conf C:\mosquitto\mosquitto.conf
```

## 2. Criar usuário e senha

```powershell
cd "C:\Program Files\mosquitto"
.\mosquitto_passwd.exe -c C:\mosquitto\senhas.txt sala
```

Ele pede a senha duas vezes. Use `sala2026` — é a que está nos scripts e no
`useSalas.js`. Se usar outra, troque nos três lugares.

## 3. Subir o broker

```powershell
cd "C:\Program Files\mosquitto"
.\mosquitto.exe -c C:\mosquitto\mosquitto.conf -v
```

Deixe essa janela aberta. Tem que aparecer:

```
Opening ipv4 listen socket on port 1883.
Opening websockets listen socket on port 9001.
```

Se **não** aparecer a linha do 9001, o build não tem WebSocket — nesse caso
use a versão oficial de <https://mosquitto.org/download/> em vez do winget.

## 4. Liberar o firewall (só para a placa chegar)

PowerShell **como administrador**, uma vez só:

```powershell
New-NetFirewallRule -DisplayName "Mosquitto MQTT" -Direction Inbound `
  -Protocol TCP -LocalPort 1883,9001 -Action Allow -Profile Private
```

Descubra o IP do notebook — é ele que vai no firmware:

```powershell
ipconfig | Select-String IPv4
```

## 5. Subir o serviço de fog

Janela nova:

```powershell
pip install paho-mqtt
python fog_service.py
```

Saída esperada:

```
[fog] banco: C:\...\fog.db
[fog] conectado em localhost:1883
[fog] assinando digitaltwin/+/status
```

## 6. Subir o simulador

Janela nova:

```powershell
python simulador.py          # 9 salas virtuais
python simulador.py 25       # 25 salas
python simulador.py 50 --rapido   # 50 salas, 1 publicação por segundo
```

Ele nunca usa a sala 120: essa é a placa de verdade.

## 7. Apontar o painel para o fog

No `dashboard/`, copie `useSalas.js` para `src/` e, no `App.jsx`, troque:

```js
import { useSala } from "./useSala";          // antes
import { useSalas } from "./useSalas";        // depois
```

O `useSala.js` antigo continua no projeto; dá para voltar atrás trocando o
import de volta. Depois:

```powershell
cd dashboard
npm run dev
```

## 8. Mudança no firmware — combine com a Giovanna antes

O arquivo é dela. São quatro linhas, em `digital_twin_dht_botoes.ino`:

```cpp
// antes
const char* mqttBroker = "broker.hivemq.com";
const int mqttPorta = 1883;
...
if (mqttClient.connect("ESP8266_Sala120")) {

// depois
const char* mqttBroker = "192.168.0.XX";   // o IP do notebook (passo 4)
const int mqttPorta = 1883;
...
if (mqttClient.connect("ESP8266_Sala120", "sala", "sala2026")) {
```

O resto do firmware não muda: mesmos tópicos, mesmo JSON, mesma lógica de
botões. A placa e o notebook precisam estar na mesma rede Wi-Fi.

---

## Os experimentos que isso libera

| # | Experimento | Como rodar | Onde sai o número |
|---|---|---|---|
| 1 | Latência de processamento × número de salas | `simulador.py` com 1, 10, 25 e 50 | tabela `latencia`, coluna `salas_ativas` |
| 2 | Custo do nó de fog | Gerenciador de Tarefas durante o teste de 50 salas | CPU e memória do `mosquitto.exe` e do `python.exe` |
| 3 | Operação com a internet do prédio caída | tire o cabo / desligue o roteador do campus, mantenha o Wi-Fi local | placa, painel e banco seguem funcionando |
| 4 | Detecção de sala fora do ar | desligue a placa e espere 60 s | `[fog] sala 120: PERDEU contato`, estado `offline` no painel |
| 5 | Histórico | depois de uma aula inteira | tabelas `leituras` e `eventos` |

Consulta pronta para a tabela do artigo:

```powershell
python -c "import sqlite3; [print('%2d salas: n=%4d  media=%5.2f ms  p95=%5.2f ms' % r) for r in sqlite3.connect('fog.db').execute('SELECT salas_ativas, COUNT(*), AVG(ms), MAX(ms) FROM latencia GROUP BY salas_ativas ORDER BY salas_ativas')]"
```

## A limitação para declarar no artigo

Broker, serviço de fog, simulador e painel rodam todos no mesmo notebook.
A latência medida é a de processamento do nó de fog — **não** inclui a rede
entre o fog e o painel, que num prédio de verdade seriam máquinas diferentes.
Escrever isso explicitamente é mais seguro do que deixar o revisor descobrir.

## .gitignore

```
fog/fog.db
fog/fog.db-wal
fog/fog.db-shm
C:/mosquitto/senhas.txt
```
