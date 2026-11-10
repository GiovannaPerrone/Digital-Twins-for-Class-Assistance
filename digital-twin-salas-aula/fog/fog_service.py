"""
fog_service.py - no de fog do Digital Twin das salas de aula

O QUE ESTE PROGRAMA FAZ
-----------------------
Hoje quem decide se a sala esta "ok", "quente" ou "offline" e o navegador
(o arquivo regras.js do painel). Isso quer dizer que a regra mora no cliente:
se ninguem abrir o painel, ninguem avalia nada e nada fica registrado.

Este servico move essa decisao para um computador dentro do predio - a
camada de fog. Ele:

  1. assina  digitaltwin/+/status   (o "+" vale para qualquer sala)
  2. avalia cada leitura com as mesmas regras do painel
  3. grava tudo em SQLite (historico que antes nao existia)
  4. publica  digitaltwin/fog/painel  ja avaliado, retido
  5. marca a sala como offline quando ela para de publicar

O painel continua funcionando igual, mas agora ele le um resultado pronto
em vez de calcular; e o sistema inteiro continua de pe mesmo se a internet
do predio cair, porque placa, broker, servico e painel estao na mesma rede
local.

COMO RODAR (Windows, PowerShell)
--------------------------------
    pip install paho-mqtt
    python fog_service.py

Para encerrar: Ctrl+C.
"""

import json
import os
import signal
import sqlite3
import sys
import threading
import time
from datetime import datetime, timezone

import paho.mqtt.client as mqtt

# =====================================================================
#  CONFIGURACAO - so esta parte muda de maquina para maquina
# =====================================================================

BROKER = os.environ.get("FOG_BROKER", "localhost")
PORTA = int(os.environ.get("FOG_PORTA", "1883"))
USUARIO = os.environ.get("FOG_USUARIO", "sala")
SENHA = os.environ.get("FOG_SENHA", "sala2026")

TOPICO_STATUS = "digitaltwin/+/status"      # entrada: todas as salas
TOPICO_PAINEL = "digitaltwin/fog/painel"    # saida: visao consolidada
TOPICO_COMANDO = "digitaltwin/{sala}/comando"

BANCO = os.environ.get("FOG_BANCO", "fog.db")

# --- as mesmas regras do regras.js do painel -------------------------
LIMITE_MIN = 18.0        # abaixo disso a sala esta fria demais
LIMITE_MAX = 26.0        # acima disso a sala esta quente demais
TIMEOUT_S = int(os.environ.get("FOG_TIMEOUT_S", "60"))   # sem publicar = offline
INTERVALO_AVALIACAO_S = 2.0   # de quanto em quanto tempo reavalia e publica


# =====================================================================
#  BANCO DE DADOS
# =====================================================================

def abrir_banco(caminho):
    """Cria (se preciso) e abre o arquivo SQLite com as tres tabelas."""
    con = sqlite3.connect(caminho, check_same_thread=False)
    con.execute("PRAGMA journal_mode=WAL")   # permite ler enquanto escreve
    con.executescript(
        """
        -- toda leitura recebida, para montar o historico de temperatura
        CREATE TABLE IF NOT EXISTS leituras (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            sala        TEXT    NOT NULL,
            andar       INTEGER,
            temperatura REAL,
            umidade     REAL,
            estado      TEXT    NOT NULL,
            recebido_em TEXT    NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_leituras_sala
            ON leituras (sala, recebido_em);

        -- um registro por chamado: quando abriu, quando fechou
        CREATE TABLE IF NOT EXISTS eventos (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            sala         TEXT NOT NULL,
            categoria    TEXT NOT NULL,       -- tecnico | material | outro | temperatura
            aberto_em    TEXT NOT NULL,
            fechado_em   TEXT,
            duracao_s    INTEGER
        );
        CREATE INDEX IF NOT EXISTS idx_eventos_sala
            ON eventos (sala, aberto_em);

        -- quanto tempo o fog levou entre receber e republicar avaliado
        CREATE TABLE IF NOT EXISTS latencia (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            sala        TEXT NOT NULL,
            ms          REAL NOT NULL,
            salas_ativas INTEGER NOT NULL,
            medido_em   TEXT NOT NULL
        );
        """
    )
    con.commit()
    return con


def agora_iso():
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


# =====================================================================
#  REGRAS - espelho fiel do regras.js do painel
# =====================================================================

CATEGORIAS = ("tecnico", "material", "outro")


def temperatura_fora(temp):
    """True quando a temperatura sai da faixa aceita."""
    if temp is None:
        return False
    return temp < LIMITE_MIN or temp > LIMITE_MAX


def tem_chamado(chamados):
    """True se qualquer um dos tres botoes esta com chamado aberto."""
    return any(chamados.get(c) for c in CATEGORIAS)


def calcular_estado(sala):
    """
    Decide o estado da sala. A ordem importa e e a mesma do painel:

        offline  >  chamado  >  temperatura  >  ok

    Uma sala offline nao tem como ter chamado confiavel, entao offline
    vence tudo; um chamado aberto por uma pessoa vence um aviso
    automatico de temperatura.
    """
    if sala["offline"]:
        return "offline"
    if tem_chamado(sala["chamados"]):
        return "chamado"
    if temperatura_fora(sala["temperatura"]):
        return "temperatura"
    return "ok"


# =====================================================================
#  ESTADO EM MEMORIA
# =====================================================================

class Fog:
    def __init__(self, con):
        self.con = con
        self.salas = {}                 # sala -> dicionario de estado
        self.trava = threading.Lock()   # o MQTT e o laco de avaliacao
                                        # mexem no mesmo dicionario
        self.rodando = True
        self.cliente = None
        self.recebidas = 0

    # ---------------------------------------------------------------
    def registrar_leitura(self, sala_id, dados, chegou_em):
        """Guarda o que a placa (ou o simulador) acabou de publicar."""
        with self.trava:
            antes = self.salas.get(sala_id)
            chamados_antes = antes["chamados"] if antes else {c: False for c in CATEGORIAS}

            sala = {
                "sala": sala_id,
                "andar": dados.get("andar"),
                "temperatura": dados.get("temperatura"),
                "umidade": dados.get("umidade"),
                "chamados": {c: bool(dados.get("chamados", {}).get(c)) for c in CATEGORIAS},
                "segundos": {c: int(dados.get("segundos", {}).get(c, 0)) for c in CATEGORIAS},
                "visto_em": chegou_em,
                "offline": False,
            }
            sala["estado"] = calcular_estado(sala)
            self.salas[sala_id] = sala
            self.recebidas += 1

            # historico da leitura
            self.con.execute(
                "INSERT INTO leituras (sala, andar, temperatura, umidade, estado, recebido_em)"
                " VALUES (?,?,?,?,?,?)",
                (sala_id, sala["andar"], sala["temperatura"], sala["umidade"],
                 sala["estado"], agora_iso()),
            )

            # chamados que abriram ou fecharam desde a leitura anterior
            for cat in CATEGORIAS:
                if sala["chamados"][cat] and not chamados_antes.get(cat):
                    self.con.execute(
                        "INSERT INTO eventos (sala, categoria, aberto_em) VALUES (?,?,?)",
                        (sala_id, cat, agora_iso()),
                    )
                elif chamados_antes.get(cat) and not sala["chamados"][cat]:
                    self.con.execute(
                        "UPDATE eventos SET fechado_em = ?,"
                        " duracao_s = CAST((julianday(?) - julianday(aberto_em)) * 86400 AS INTEGER)"
                        " WHERE sala = ? AND categoria = ? AND fechado_em IS NULL",
                        (agora_iso(), agora_iso(), sala_id, cat),
                    )

            self.con.commit()
            return sala

    # ---------------------------------------------------------------
    def esquecer(self, sala_id):
        """Tira a sala do painel e fecha os chamados que ficaram abertos."""
        with self.trava:
            if sala_id not in self.salas:
                return False
            del self.salas[sala_id]
            self.con.execute(
                "UPDATE eventos SET fechado_em = ?,"
                " duracao_s = CAST((julianday(?) - julianday(aberto_em)) * 86400 AS INTEGER)"
                " WHERE sala = ? AND fechado_em IS NULL",
                (agora_iso(), agora_iso(), sala_id),
            )
            self.con.commit()
            return True

    # ---------------------------------------------------------------
    def marcar_offline(self):
        """
        Quem nao publica ha TIMEOUT_S segundos vira offline.

        Isto e uma coisa que so o fog consegue fazer: a propria placa nao
        tem como avisar que caiu, e o painel so percebia enquanto estivesse
        aberto na tela de alguem.
        """
        agora = time.time()
        mudou = False
        with self.trava:
            for sala in self.salas.values():
                offline = (agora - sala["visto_em"]) > TIMEOUT_S
                if offline != sala["offline"]:
                    sala["offline"] = offline
                    sala["estado"] = calcular_estado(sala)
                    mudou = True
                    print(f"[fog] sala {sala['sala']}: "
                          f"{'PERDEU contato' if offline else 'voltou'}")
        return mudou

    # ---------------------------------------------------------------
    def montar_painel(self):
        """
        Monta o JSON consolidado que o painel consome.

        A fila de chamados ja vem ordenada aqui: mais antigo primeiro.
        Antes essa ordenacao era feita no navegador, uma vez por aba
        aberta; agora e feita uma vez so, no fog.
        """
        with self.trava:
            salas = sorted(self.salas.values(), key=lambda s: s["sala"])
            fila = []
            for sala in salas:
                for cat in CATEGORIAS:
                    if sala["chamados"][cat]:
                        fila.append({
                            "sala": sala["sala"],
                            "andar": sala["andar"],
                            "categoria": cat,
                            "segundos": sala["segundos"][cat],
                        })
            fila.sort(key=lambda c: c["segundos"], reverse=True)

            resumo = {
                "ok": sum(1 for s in salas if s["estado"] == "ok"),
                "chamado": sum(1 for s in salas if s["estado"] == "chamado"),
                "temperatura": sum(1 for s in salas if s["estado"] == "temperatura"),
                "offline": sum(1 for s in salas if s["estado"] == "offline"),
            }

            return {
                "gerado_em": agora_iso(),
                "limites": {"min": LIMITE_MIN, "max": LIMITE_MAX},
                "resumo": resumo,
                "fila": fila,
                "salas": [
                    {k: v for k, v in s.items() if k != "visto_em"}
                    for s in salas
                ],
            }

    # ---------------------------------------------------------------
    def publicar_painel(self):
        painel = self.montar_painel()
        self.cliente.publish(TOPICO_PAINEL, json.dumps(painel), qos=0, retain=True)
        return painel

    def registrar_latencia(self, sala_id, ms):
        with self.trava:
            ativas = len(self.salas)
        self.con.execute(
            "INSERT INTO latencia (sala, ms, salas_ativas, medido_em) VALUES (?,?,?,?)",
            (sala_id, ms, ativas, agora_iso()),
        )
        self.con.commit()


# =====================================================================
#  CALLBACKS MQTT
# =====================================================================

def ao_conectar(cliente, fog, flags, reason_code, properties=None):
    if getattr(reason_code, "is_failure", reason_code != 0):
        print(f"[fog] falha ao conectar no broker: {reason_code}")
        return
    print(f"[fog] conectado em {BROKER}:{PORTA}")
    cliente.subscribe(TOPICO_STATUS, qos=0)
    print(f"[fog] assinando {TOPICO_STATUS}")


def ao_receber(cliente, fog, msg):
    t0 = time.perf_counter()

    # digitaltwin/sala120/status -> ["digitaltwin", "sala120", "status"]
    partes = msg.topic.split("/")
    if len(partes) != 3:
        return
    sala_id = partes[1].replace("sala", "")

    # Payload vazio e como o MQTT apaga uma mensagem retida: a sala saiu
    # do ar de proposito (o simulador faz isso ao encerrar). Tiramos do painel.
    if not msg.payload:
        if fog.esquecer(sala_id):
            print(f"[fog] sala {sala_id}: removida do painel")
            fog.publicar_painel()
        return

    try:
        dados = json.loads(msg.payload.decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as erro:
        print(f"[fog] payload invalido em {msg.topic}: {erro}")
        return

    sala = fog.registrar_leitura(sala_id, dados, time.time())
    fog.publicar_painel()

    ms = (time.perf_counter() - t0) * 1000
    fog.registrar_latencia(sala_id, ms)

    if fog.recebidas <= 20 or fog.recebidas % 50 == 0:
        temp = sala["temperatura"]
        temp_txt = f"{temp:.1f}C" if temp is not None else "--"
        print(f"[fog] sala {sala_id}: {sala['estado']:<11} {temp_txt:>7}  "
              f"({ms:.1f} ms, {fog.recebidas} msgs)")


def ao_desconectar(cliente, fog, flags, reason_code, properties=None):
    if fog.rodando:
        print(f"[fog] desconectado ({reason_code}), tentando reconectar...")


# =====================================================================
#  LACO DE AVALIACAO
# =====================================================================

def laco_avaliacao(fog):
    """
    Roda em paralelo ao MQTT. Serve para o caso em que NADA chega:
    uma sala que caiu nao publica, entao so um relogio percebe a ausencia.
    """
    while fog.rodando:
        time.sleep(INTERVALO_AVALIACAO_S)
        if fog.marcar_offline():
            fog.publicar_painel()


# =====================================================================
#  PRINCIPAL
# =====================================================================

def main():
    print("=" * 62)
    print(" No de fog - Digital Twin das salas de aula")
    print("=" * 62)

    con = abrir_banco(BANCO)
    print(f"[fog] banco: {os.path.abspath(BANCO)}")

    fog = Fog(con)

    try:
        cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                              client_id="fog-service", userdata=fog)
    except AttributeError:                      # paho-mqtt 1.x
        cliente = mqtt.Client(client_id="fog-service", userdata=fog)

    cliente.username_pw_set(USUARIO, SENHA)
    cliente.on_connect = ao_conectar
    cliente.on_message = ao_receber
    cliente.on_disconnect = ao_desconectar
    fog.cliente = cliente

    def encerrar(*_):
        print("\n[fog] encerrando...")
        fog.rodando = False
        cliente.disconnect()

    signal.signal(signal.SIGINT, encerrar)
    signal.signal(signal.SIGTERM, encerrar)

    try:
        cliente.connect(BROKER, PORTA, keepalive=30)
    except OSError as erro:
        print(f"[fog] nao consegui falar com o broker em {BROKER}:{PORTA}")
        print(f"      {erro}")
        print("      O Mosquitto esta rodando? (net start mosquitto)")
        return 1

    threading.Thread(target=laco_avaliacao, args=(fog,), daemon=True).start()
    cliente.loop_forever()

    # resumo ao sair - util para colar no artigo
    cur = con.execute("SELECT COUNT(*), AVG(ms), MAX(ms) FROM latencia")
    n, media, maximo = cur.fetchone()
    if n:
        print(f"[fog] {n} mensagens processadas, "
              f"latencia media {media:.2f} ms, maxima {maximo:.2f} ms")
    con.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
