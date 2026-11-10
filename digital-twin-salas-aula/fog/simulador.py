"""
simulador.py - gera N salas virtuais ao lado da placa real

POR QUE EXISTE
--------------
O projeto tem uma placa so. Para mostrar que a arquitetura aguenta um
andar inteiro - e para medir o que acontece com a latencia quando o
numero de salas cresce - este script publica exatamente o mesmo JSON
que o firmware publica, em salas diferentes.

Do ponto de vista do broker e do no de fog nao ha diferenca nenhuma
entre a sala 120 (real) e a sala 121 (simulada): mesmo topico, mesmo
formato, mesmo intervalo de 10 s. A placa real continua ligada e
aparece no painel junto com as outras.

COMO RODAR (Windows, PowerShell)
--------------------------------
    python simulador.py              # as salas da planta, pula a 120 (a real)
    python simulador.py --demo       # chamados aparecendo rapido, para apresentar
    python simulador.py 25           # 25 salas (teste de escala)
    python simulador.py 50 --rapido  # 50 salas publicando a cada 1 s

Para encerrar: Ctrl+C.
"""

import json
import os
import random
import signal
import sys
import time

import paho.mqtt.client as mqtt

BROKER = os.environ.get("FOG_BROKER", "localhost")
PORTA = int(os.environ.get("FOG_PORTA", "1883"))
USUARIO = os.environ.get("FOG_USUARIO", "sala")
SENHA = os.environ.get("FOG_SENHA", "sala2026")

SALA_REAL = "120"              # a placa de verdade; o simulador nao usa

# As salas que existem na planta baixa do painel (components/PlantaFog.jsx).
# Usar estas faz o andar inteiro acender na demonstracao, em vez de
# aparecerem salas que ninguem reconhece.
# "tech2"/"tech3" sao os laboratorios: o topico vira digitaltwin/salatech2/status
# e o painel mostra "TECH 2". Nome sem espaco porque topico MQTT com espaco da
# dor de cabeca em quase toda biblioteca.
SALAS_DA_PLANTA = ["115", "117", "118", "119", "121", "tech2", "tech3"]

# Acima do tamanho da planta o simulador inventa numeros para o teste de
# escala (10, 25, 50 salas). Essas nao aparecem no desenho, so na fila,
# no resumo e nas medidas de latencia - que e o que o teste precisa.
PRIMEIRA_SALA_EXTRA = 122
INTERVALO_S = 10.0             # mesmo batimento do firmware
CATEGORIAS = ("tecnico", "material", "outro")

# Probabilidade, a cada ciclo, de uma sala abrir ou fechar um chamado.
# Com --demo os chamados aparecem bem mais rapido: serve para a
# apresentacao, em que ninguem vai esperar dez minutos por um evento.
P_ABRIR = 0.04
P_FECHAR = 0.25
P_ABRIR_DEMO = 0.20
P_FECHAR_DEMO = 0.12

DEMO = False   # ligado pelo argumento --demo


class SalaVirtual:
    """Imita uma placa: temperatura que anda devagar e botoes que alternam."""

    def __init__(self, numero, andar):
        self.numero = str(numero)
        self.andar = andar
        self.temperatura = random.uniform(20.0, 25.0)
        self.umidade = random.uniform(45.0, 65.0)
        self.chamados = {c: False for c in CATEGORIAS}
        self.aberto_em = {c: 0.0 for c in CATEGORIAS}

        # uma em cada seis salas tem ar-condicionado ruim e esquenta
        self.esquenta = random.random() < 0.17

    def passo(self):
        """Avanca a simulacao em um ciclo."""
        deriva = random.uniform(-0.3, 0.45 if self.esquenta else 0.3)
        self.temperatura = max(15.0, min(34.0, self.temperatura + deriva))
        self.umidade = max(30.0, min(85.0, self.umidade + random.uniform(-1.5, 1.5)))

        agora = time.time()
        p_abrir = P_ABRIR_DEMO if DEMO else P_ABRIR
        p_fechar = P_FECHAR_DEMO if DEMO else P_FECHAR
        for cat in CATEGORIAS:
            if self.chamados[cat]:
                if random.random() < p_fechar:
                    self.chamados[cat] = False
                    self.aberto_em[cat] = 0.0
            elif random.random() < p_abrir:
                self.chamados[cat] = True
                self.aberto_em[cat] = agora

    def fechar(self, categoria):
        """Atende o comando que o painel manda quando o inspetor fecha."""
        if categoria in self.chamados and self.chamados[categoria]:
            self.chamados[categoria] = False
            self.aberto_em[categoria] = 0.0
            return True
        return False

    def json(self):
        """Byte a byte o mesmo formato do publicarStatus() do firmware."""
        agora = time.time()
        return json.dumps({
            "sala": self.numero,
            "andar": self.andar,
            "temperatura": round(self.temperatura, 1),
            "umidade": round(self.umidade, 1),
            "chamados": {c: self.chamados[c] for c in CATEGORIAS},
            "segundos": {
                c: int(agora - self.aberto_em[c]) if self.chamados[c] else 0
                for c in CATEGORIAS
            },
        }, separators=(",", ":"))


def main():
    global DEMO
    quantidade = len(SALAS_DA_PLANTA)
    intervalo = INTERVALO_S

    for arg in sys.argv[1:]:
        if arg == "--rapido":
            intervalo = 1.0
        elif arg == "--demo":
            DEMO = True
            intervalo = 3.0
        elif arg.isdigit():
            quantidade = int(arg)
        else:
            print(f"argumento desconhecido: {arg}")
            return 2

    # Primeiro as salas da planta; depois, se pedirem mais, numeros extras.
    salas = [SalaVirtual(n, 1) for n in SALAS_DA_PLANTA[:quantidade]]
    numero = PRIMEIRA_SALA_EXTRA
    while len(salas) < quantidade:
        if str(numero) != SALA_REAL:
            salas.append(SalaVirtual(numero, 1 + (numero - 100) // 20))
        numero += 1

    na_planta = [s.numero for s in salas if s.numero in SALAS_DA_PLANTA]
    extras = len(salas) - len(na_planta)

    print("=" * 62)
    print(f" Simulador - {quantidade} salas virtuais, 1 publicacao a cada {intervalo:.0f} s"
          f"{'  [modo demonstracao]' if DEMO else ''}")
    print(f" Na planta: {', '.join(na_planta)}"
          f"   (a {SALA_REAL} e a placa real e nao entra aqui)")
    if extras:
        print(f" Fora da planta (teste de escala): {extras} sala(s), "
              f"{salas[len(na_planta)].numero} em diante")
    print("=" * 62)

    try:
        cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                              client_id="simulador-salas")
    except AttributeError:
        cliente = mqtt.Client(client_id="simulador-salas")

    cliente.username_pw_set(USUARIO, SENHA)

    # O painel fecha chamados publicando em digitaltwin/sala<N>/comando.
    # A placa real escuta o topico dela; aqui fazemos o mesmo pelas virtuais,
    # senao o botao "fechar" do painel nao faria nada nas salas simuladas.
    por_numero = {s.numero: s for s in salas}

    def ao_conectar(cli, _u, _f, reason_code, _p=None):
        cli.subscribe("digitaltwin/+/comando", qos=0)

    def ao_comando(cli, _u, msg):
        partes = msg.topic.split("/")
        if len(partes) != 3:
            return
        sala = por_numero.get(partes[1].replace("sala", ""))
        if sala is None:            # sala real: quem responde e a placa
            return
        cmd = msg.payload.decode("utf-8", "ignore")
        if cmd.startswith("fechar:") and sala.fechar(cmd.split(":", 1)[1]):
            print(f"[sim] painel fechou {cmd.split(':', 1)[1]} na sala {sala.numero}")
            cli.publish(f"digitaltwin/sala{sala.numero}/status",
                        sala.json(), qos=0, retain=True)

    cliente.on_connect = ao_conectar
    cliente.on_message = ao_comando

    try:
        cliente.connect(BROKER, PORTA, keepalive=30)
    except OSError as erro:
        print(f"[sim] nao consegui falar com o broker em {BROKER}:{PORTA}")
        print(f"      {erro}")
        return 1

    cliente.loop_start()

    rodando = {"ok": True}

    def encerrar(*_):
        rodando["ok"] = False

    signal.signal(signal.SIGINT, encerrar)
    signal.signal(signal.SIGTERM, encerrar)

    ciclos = 0
    try:
        while rodando["ok"]:
            inicio = time.perf_counter()
            abertos = 0

            for sala in salas:
                sala.passo()
                cliente.publish(f"digitaltwin/sala{sala.numero}/status",
                                sala.json(), qos=0, retain=True)
                abertos += sum(sala.chamados.values())

            ciclos += 1
            gasto = (time.perf_counter() - inicio) * 1000
            print(f"[sim] ciclo {ciclos:>4}: {len(salas)} salas publicadas em "
                  f"{gasto:.1f} ms, {abertos} chamado(s) aberto(s)")

            # dorme em pedacos pequenos para o Ctrl+C responder na hora
            fim = time.time() + intervalo
            while rodando["ok"] and time.time() < fim:
                time.sleep(0.1)
    finally:
        print("\n[sim] limpando as mensagens retidas das salas virtuais...")
        for sala in salas:
            cliente.publish(f"digitaltwin/sala{sala.numero}/status",
                            "", qos=0, retain=True)
        time.sleep(0.5)
        cliente.loop_stop()
        cliente.disconnect()
        print("[sim] encerrado.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
