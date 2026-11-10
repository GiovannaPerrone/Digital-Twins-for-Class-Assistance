// ============================================================================
// App.jsx - junta tudo: pega os dados do no de fog e monta a tela.
//
// O QUE MUDOU EM RELACAO A VERSAO ANTERIOR
// ----------------------------------------
// Antes:  useSala()  -> uma sala crua -> estado() do regras.js calculava
//         o estado aqui no navegador, a cada aba aberta.
//
// Agora:  useSalas() -> todas as salas JA avaliadas pelo no de fog, com a
//         fila de chamados ordenada. O navegador so desenha.
//
// Por isso sumiram daqui o estado(), o tempFora() e o tempForaDesde:
// quem faz essa conta agora e o fog_service.py, uma vez so, e ele continua
// fazendo mesmo com o painel fechado.
//
// O useSala.js e os componentes antigos continuam no projeto; para voltar
// atras basta restaurar este arquivo.
// ============================================================================
import { useCallback, useEffect, useRef, useState } from "react";
import "./App.css";

import { useSalas } from "./useSalas";
import { useAgora } from "./useAgora";
import { tocarBip } from "./useAvisoNovoChamado";
import { CATEGORIAS } from "./regras";

import PlantaFog from "./components/PlantaFog";
import ResumoAndar from "./components/ResumoAndar";
import FilaChamadosFog from "./components/FilaChamadosFog";

export default function App() {
  const { salas, fila, resumo, limites, conectado, vistoEm, fogMudo, fechar } = useSalas();
  const agora = useAgora();   // relogio de 1 s: tambem faz a tela reavaliar

  // ---- Aviso de novo chamado: bip + mensagem temporaria ----
  // Antes o useAvisoNovoChamado cuidava disso para uma sala so. Com o andar
  // inteiro, comparamos a fila atual com a anterior: o que apareceu e novo.
  const [aviso, setAviso] = useState(null);
  const filaAnterior = useRef(null);

  const avisar = useCallback((sala, cat) => {
    tocarBip();
    setAviso(`Novo chamado · Sala ${sala} · ${CATEGORIAS[cat]?.nome ?? cat}`);
    setTimeout(() => setAviso(null), 4000);
  }, []);

  useEffect(() => {
    const chaves = new Set(fila.map((c) => `${c.sala}-${c.categoria}`));

    // Na primeira carga a fila retida chega inteira de uma vez; se nao
    // pularmos esse caso, o painel bipa uma vez por chamado ja existente.
    if (filaAnterior.current === null) {
      filaAnterior.current = chaves;
      return;
    }

    for (const c of fila) {
      const chave = `${c.sala}-${c.categoria}`;
      if (!filaAnterior.current.has(chave)) avisar(c.sala, c.categoria);
    }
    filaAnterior.current = chaves;
  }, [fila, avisar]);

  return (
    <div className="wrap">
      <header>
        <div>
          <h1>Painel da Inspetoria</h1>
          <div className="sub">1º andar · protótipo de viabilidade</div>
        </div>
        <div className="clock">{new Date(agora).toLocaleTimeString("pt-BR")}</div>
      </header>

      <div className="grid">
        <PlantaFog salas={salas} />

        <div>
          <ResumoAndar
            resumo={resumo}
            limites={limites}
            salas={salas}
            conectado={conectado}
            fogMudo={fogMudo}
          />
          <FilaChamadosFog
            fila={fila}
            salas={salas}
            vistoEm={vistoEm}
            agora={agora}
            onFechar={fechar}
          />
        </div>
      </div>

      {aviso && <div className="toast">{aviso}</div>}
    </div>
  );
}
