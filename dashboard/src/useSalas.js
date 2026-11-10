// ============================================================================
// useSalas.js - le o painel ja avaliado pelo no de fog.
//
// Arquivo NOVO: nao substitui o useSala.js, fica ao lado dele. Para voltar
// atras basta trocar o import no App.jsx.
//
//   useSala.js   assina digitaltwin/sala120/status, recebe UMA sala crua,
//                e o regras.js do navegador decide o estado dela.
//
//   useSalas.js  assina digitaltwin/fog/painel, recebe TODAS as salas ja
//                avaliadas, com a fila de chamados ordenada.
//
// O calculo nao sumiu: ele saiu do navegador e passou a ser feito uma vez
// so, no no de fog, em vez de uma vez por aba aberta. Com o painel fechado
// o fog continua avaliando e gravando - coisa que antes nao acontecia.
// ============================================================================
import mqtt from "mqtt";
import { useEffect, useRef, useState } from "react";

// O broker agora e o notebook da sala, nao a HiveMQ publica.
// 9001 e o ouvinte WebSocket do Mosquitto: o navegador nao fala TCP cru.
const BROKER = "ws://localhost:9001";
const USUARIO = "sala";
const SENHA = "sala2026";

const PAINEL = "digitaltwin/fog/painel";
const COMANDO = (sala) => `digitaltwin/sala${sala}/comando`;

// Se o proprio fog parar, o painel congela sem avisar. 10 s porque o fog
// republica a cada leitura recebida e, no minimo, a cada 2 s de avaliacao.
const FOG_VELHO_MS = 10_000;

// O fog usa nomes por extenso; o painel e o CSS ja usavam estes aqui.
// Traduzir num lugar so evita mexer no Planta, no CSS e nas variaveis de cor.
const ESTADO = {
  ok: "ok",
  chamado: "call",
  temperatura: "temp",
  offline: "off",
};

// Os laboratorios viajam como "tech2"/"tech3" no topico MQTT, mas na tela
// precisam aparecer com o nome que esta na porta.
const ROTULOS = { tech2: "TECH 2", tech3: "TECH 3" };
export const nomeSala = (id) => ROTULOS[id] ?? `Sala ${id}`;

export function useSalas() {
  const [painel, setPainel] = useState(null);   // o JSON inteiro do fog
  const [vistoEm, setVistoEm] = useState(null); // quando chegou o ultimo
  const [conectado, setConectado] = useState(false);
  const cliente = useRef(null);

  useEffect(() => {
    const c = mqtt.connect(BROKER, {
      username: USUARIO,
      password: SENHA,
      reconnectPeriod: 2000,
    });
    cliente.current = c;

    c.on("connect", () => {
      setConectado(true);
      // A mensagem do painel e retida: ela chega no instante da assinatura,
      // sem esperar a proxima leitura de nenhuma sala.
      c.subscribe(PAINEL);
    });

    c.on("close", () => setConectado(false));
    c.on("error", (e) => console.warn("[mqtt]", e.message));

    c.on("message", (_topico, payload) => {
      try {
        setPainel(JSON.parse(payload.toString()));
        setVistoEm(Date.now());
      } catch {
        console.warn("[mqtt] painel invalido:", payload.toString());
      }
    });

    return () => c.end();
  }, []);

  // O inspetor fecha o chamado: o comando vai para a sala, igual a antes.
  // Na sala real quem responde e a placa; nas simuladas, o simulador.
  const fechar = (sala, categoria) =>
    cliente.current?.publish(COMANDO(sala), `fechar:${categoria}`);

  // Traduz o estado de cada sala para o vocabulario que o painel ja usava.
  const salas = (painel?.salas ?? []).map((s) => ({
    ...s,
    est: ESTADO[s.estado] ?? "off",
  }));

  return {
    salas,
    fila: painel?.fila ?? [],
    resumo: painel?.resumo ?? { ok: 0, chamado: 0, temperatura: 0, offline: 0 },
    limites: painel?.limites ?? { min: 18, max: 26 },
    conectado,
    vistoEm,
    // true quando o fog parou de publicar: o painel inteiro esta velho.
    fogMudo: vistoEm !== null && Date.now() - vistoEm > FOG_VELHO_MS,
    fechar,
  };
}
