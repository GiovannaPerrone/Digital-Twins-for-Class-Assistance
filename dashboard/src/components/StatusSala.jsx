// ============================================================================
// StatusSala.jsx — o número grande da temperatura e a localização por escrito.
// ============================================================================
import { LIMITE_MIN, LIMITE_MAX } from "../regras";

export default function StatusSala({ sala, est }) {
  const semSinal = est === "off";

  // Cor do número: cinza se sem sinal, âmbar se fora da faixa, normal se ok.
  const cor = semSinal ? "var(--off)"
            : est === "temp" ? "var(--temp)"
            : "var(--txt)";

  return (
    <section className="card">
      <h2>Sala monitorada</h2>
      <div className="status-row">
        <div className="big" style={{ color: cor }}>
          {semSinal ? "--" : `${sala.temperatura.toFixed(1)}°`}
        </div>
        <div>
          {/* Localização por extenso, como pede o objetivo do projeto */}
          <div className="loc">
            Sala {sala?.sala ?? "120"} · {sala?.andar ?? 1}º andar
          </div>
          <div className="dim">
            faixa aceitável: {LIMITE_MIN}–{LIMITE_MAX} °C
            {!semSinal && ` · umidade ${sala.umidade.toFixed(0)}%`}
          </div>
        </div>
      </div>
    </section>
  );
}
