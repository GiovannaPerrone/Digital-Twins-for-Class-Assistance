// ============================================================================
// App.jsx — junta tudo: pega os dados, calcula o estado, monta a tela.
// ============================================================================
import { useCallback, useRef, useState } from "react";
import "./App.css";

import { useSala } from "./useSala";
import { useAgora } from "./useAgora";
import { useAvisoNovoChamado, tocarBip } from "./useAvisoNovoChamado";
import { estado, tempFora, CATEGORIAS } from "./regras";

import Planta from "./components/Planta";
import StatusSala from "./components/StatusSala";
import FilaChamados from "./components/FilaChamados";

export default function App() {
  const { sala, vistoEm, fechar } = useSala();   // dados do MQTT
  const agora = useAgora();                      // relógio de 1 s
  const est = estado(sala, vistoEm, agora);      // "ok" | "call" | "temp" | "off"

  // ---- Quando a temperatura saiu da faixa ----
  // A placa não informa isso, então o painel registra por conta própria.
  const tempForaDesde = useRef(null);
  if (tempFora(sala)) {
    if (tempForaDesde.current === null) tempForaDesde.current = agora;
  } else {
    tempForaDesde.current = null;
  }

  // ---- Aviso de novo chamado: bip + mensagem temporária ----
  const [aviso, setAviso] = useState(null);
  const avisar = useCallback((cat) => {
    tocarBip();
    setAviso(`Novo chamado — ${CATEGORIAS[cat].nome}`);
    setTimeout(() => setAviso(null), 4000);
  }, []);
  useAvisoNovoChamado(sala, avisar);

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
        <Planta est={est} sala={sala} />

        <div>
          <StatusSala sala={sala} est={est} />
          <FilaChamados
            sala={sala}
            est={est}
            vistoEm={vistoEm}
            agora={agora}
            tempForaDesde={tempForaDesde.current}
            onFechar={fechar}
          />
        </div>
      </div>

      {aviso && <div className="toast">{aviso}</div>}
    </div>
  );
}
