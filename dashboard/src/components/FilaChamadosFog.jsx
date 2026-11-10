// ============================================================================
// FilaChamadosFog.jsx - a fila do andar inteiro, mais antigo primeiro.
//
// Arquivo NOVO: o FilaChamados.jsx original continua intacto. A diferenca:
// la a fila era de UMA sala e a ordenacao era feita aqui no navegador;
// aqui a fila ja chega ordenada do no de fog e atravessa varias salas.
// ============================================================================
import { CATEGORIAS, formatarDuracao } from "../regras";
import { nomeSala } from "../useSalas";

export default function FilaChamadosFog({ fila, salas, vistoEm, agora, onFechar }) {
  // O fog manda quantos segundos o chamado tinha QUANDO publicou; somamos
  // o tempo decorrido desde que a mensagem chegou, para o contador andar
  // sozinho entre uma publicacao e outra. Mesma conta do regras.js.
  const decorrido = vistoEm ? Math.floor((agora - vistoEm) / 1000) : 0;

  // Salas quentes ou frias entram na fila depois dos chamados de pessoas:
  // um botao apertado por um professor vale mais que um aviso automatico.
  const foraDeFaixa = salas.filter((s) => s.est === "temp");

  const vazio = fila.length === 0 && foraDeFaixa.length === 0;

  return (
    <section className="card">
      <h2>Chamados abertos</h2>

      {vazio && <div className="empty">Nenhum chamado aberto no andar.</div>}

      <div className="queue">
        {fila.map((c) => {
          const info = CATEGORIAS[c.categoria];
          return (
            <div className="item" key={`${c.sala}-${c.categoria}`}>
              <div className="badge" style={{ background: "var(--call)" }}>
                {info?.letra ?? "?"}
              </div>
              <div className="item-body">
                <div className="item-top">
                  <span className="item-title">{info?.nome ?? c.categoria}</span>
                  <span className="item-meta">· {nomeSala(c.sala)}, {c.andar}º andar</span>
                </div>
                <div className="item-meta">
                  {info?.desc} · há{" "}
                  <span className="mono">{formatarDuracao(c.segundos + decorrido)}</span>
                </div>
              </div>
              {/* So envia o comando. Quem apaga o chamado e a placa. */}
              <button className="btn-close" onClick={() => onFechar(c.sala, c.categoria)}>
                Fechar
              </button>
            </div>
          );
        })}

        {foraDeFaixa.map((s) => (
          <div className="item temp" key={`temp-${s.sala}`}>
            <div className="badge" style={{ background: "var(--temp)" }}>°C</div>
            <div className="item-body">
              <div className="item-top">
                <span className="item-title">
                  Temperatura {s.temperatura.toFixed(1)} °C
                </span>
                <span className="item-meta">· {nomeSala(s.sala)}, {s.andar}º andar</span>
              </div>
              <div className="item-meta">
                fora da faixa · encerra sozinho ao normalizar
              </div>
            </div>
          </div>
        ))}
      </div>
    </section>
  );
}
