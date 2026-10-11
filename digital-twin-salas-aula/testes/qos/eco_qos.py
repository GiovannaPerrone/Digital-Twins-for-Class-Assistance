#!/usr/bin/env python3
"""
Lado do PC do teste de QoS na placa (par do teste_qos_placa.ino).

Faz tres coisas:
  1. espera a placa avisar que esta pronta e manda "iniciar"
  2. devolve cada mensagem de .../ping em .../pong, no mesmo QoS em que chegou
  3. conta o que chegou de verdade no PC (perda de IDA) e junta com o resumo
     que a placa publica no fim de cada fase (perda de ida+volta e latencias)

Uso:
  pip install paho-mqtt
  python eco_qos.py --sessao gio01
  (depois grave/reinicie a placa; ela comeca sozinha quando o script responder)
"""
import argparse
import csv
import json
import threading

import paho.mqtt.client as mqtt

ap = argparse.ArgumentParser()
ap.add_argument("--sessao", default="gio01", help="igual a SESSAO no .ino")
ap.add_argument("--host", default="broker.hivemq.com")
ap.add_argument("--porta", type=int, default=1883)
ap.add_argument("--fases", type=int, default=3, help="quantas fases de QoS a placa vai rodar")
ap.add_argument("--saida", default="resultados_placa")
args = ap.parse_args()

base = f"digitaltwin/teste_qos/{args.sessao}"
T_PING, T_PONG = f"{base}/ping", f"{base}/pong"
T_CONTROLE, T_EVENTO, T_RESULTADO = f"{base}/controle", f"{base}/evento", f"{base}/resultado"

ida_unicas = {0: set(), 1: set(), 2: set()}   # seq que chegaram ao PC, por QoS
ida_duplicadas = {0: 0, 1: 0, 2: 0}
resultados = []
terminou = threading.Event()


def ao_conectar(c, userdata, flags, reason_code, properties):
    c.subscribe([(T_PING, 2), (T_EVENTO, 1), (T_RESULTADO, 1)])
    print(f"Conectado. Sessao '{args.sessao}'. Aguardando a placa (reinicie ela no botao RST)...")


def ao_receber(c, userdata, msg):
    if msg.topic == T_PING:
        dados = json.loads(msg.payload)
        q, seq = dados["q"], dados["seq"]
        if seq in ida_unicas[q]:
            ida_duplicadas[q] += 1
        else:
            ida_unicas[q].add(seq)
        c.publish(T_PONG, msg.payload, qos=msg.qos)      # eco no mesmo QoS

    elif msg.topic == T_EVENTO and msg.payload == b"pronta":
        print("Placa pronta. Iniciando o teste...")
        c.publish(T_CONTROLE, "iniciar", qos=1)

    elif msg.topic == T_RESULTADO:
        r = json.loads(msg.payload)
        q = r["qos"]
        env = r["env"]
        chegaram_pc = len(ida_unicas[q])
        r["pc_recebidas"] = chegaram_pc
        r["perda_ida_pct"] = 100 * (env - chegaram_pc) / env
        r["perda_volta_pct"] = 100 * (chegaram_pc - r["pong"]) / chegaram_pc if chegaram_pc else 100.0
        r["perda_total_pct"] = 100 * (env - r["pong"]) / env
        r["dup_ida"] = ida_duplicadas[q]
        resultados.append(r)
        print(f"  fase QoS {q} concluida: ida {r['perda_ida_pct']:.1f}% | volta {r['perda_volta_pct']:.1f}% "
              f"| RTT mediano {r['rtt_med']} ms")
        if len(resultados) >= args.fases:
            terminou.set()


cliente = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"dt-eco-{args.sessao}")
cliente.on_connect = ao_conectar
cliente.on_message = ao_receber
cliente.connect(args.host, args.porta, keepalive=30)
cliente.loop_start()

try:
    terminou.wait()
except KeyboardInterrupt:
    print("\nInterrompido; salvando o que ja chegou.")
cliente.loop_stop()
cliente.disconnect()

if resultados:
    resultados.sort(key=lambda r: r["qos"])
    campos = ["qos", "env", "pc_recebidas", "perda_ida_pct", "dup_ida", "pong", "perda_volta_pct",
              "perda_total_pct", "rtt_media", "rtt_med", "rtt_p95", "rtt_max", "pub_med", "pub_max",
              "falhas_pub", "erro", "reconex", "dup", "atras", "rssi"]
    with open(f"{args.saida}.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=campos, extrasaction="ignore")
        w.writeheader()
        w.writerows(resultados)

    print("\nQoS | env | ida% | volta% | total% | RTT media | mediana | p95 | max | pub(ms) | falhas | reconex | RSSI")
    for r in resultados:
        print(f"{r['qos']:>3} | {r['env']:>3} | {r['perda_ida_pct']:>4.1f} | {r['perda_volta_pct']:>6.1f} | "
              f"{r['perda_total_pct']:>6.1f} | {r['rtt_media']:>9} | {r['rtt_med']:>7} | {r['rtt_p95']:>5} | "
              f"{r['rtt_max']:>5} | {r['pub_med']:>7} | {r['falhas_pub']:>6} | {r['reconex']:>7} | {r['rssi']:>4}")
    print(f"\nSalvo em {args.saida}.csv")
