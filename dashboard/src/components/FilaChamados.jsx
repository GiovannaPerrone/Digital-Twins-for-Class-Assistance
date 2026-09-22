// ============================================================================
// FilaChamados.jsx — a lista do que precisa de atenção, mais antigo primeiro.
// ============================================================================
import {
  CATEGORIAS, tempFora, segundosAbertos, formatarDuracao,
} from "../regras";

export default function FilaChamados({ sala, est, vistoEm, agora, tempForaDesde, onFechar }) {
  const local = `Sala ${sala?.sala ?? "120"}, ${sala?.andar ?? 1}º andar`;

  // ---- Sem comunicação: um único aviso, nada mais é confiável ----
  if (est === "off") {
    return (
      <section className="card">
        <h2>Chamados abertos</h2>
        <div className="item off">
          <div className="badge" style={{ background: "var(--off)" }}>!</div>
          <div className="item-body">
            <div className="item-title">{local}</div>
            <div className="item-meta">
              {vistoEm
                ? <>Sem comunicação há <span className="mono">
                    {formatarDuracao(Math.floor((agora - vistoEm) / 1000))}</span></>
                : "Aguardando a primeira mensagem da placa…"}
            </div>
          </div>
        </div>
      </section>
    );
  }

  // ---- Monta a lista: um item por chamado aberto + temperatura ----
  const itens = [];

  for (const [cat, info] of Object.entries(CATEGORIAS)) {
    if (!sala.chamados?.[cat]) continue;
    const seg = segundosAbertos(sala, cat, vistoEm, agora);
    itens.push({ chave: cat, seg, tipo: "call", cat, info });
  }

  if (tempFora(sala) && tempForaDesde) {
    const seg = Math.floor((agora - tempForaDesde) / 1000);
    itens.push({ chave: "temp", seg, tipo: "temp" });
  }

  // Mais tempo em aberto = mais antigo = vem primeiro
  itens.sort((a, b) => b.seg - a.seg);

  return (
    <section className="card">
      <h2>Chamados abertos</h2>

      {itens.length === 0 && <div className="empty">Nenhum chamado aberto.</div>}

      <div className="queue">
        {itens.map((i) =>
          i.tipo === "call" ? (
            <div className="item" key={i.chave}>
              <div className="badge" style={{ background: "var(--call)" }}>{i.info.letra}</div>
              <div className="item-body">
                <div className="item-top">
                  <span className="item-title">{i.info.nome}</span>
                  <span className="item-meta">· {local}</span>
                </div>
                <div className="item-meta">
                  {i.info.desc} · há <span className="mono">{formatarDuracao(i.seg)}</span>
                </div>
              </div>
              {/* Só envia o comando. Quem apaga o chamado é a placa. */}
              <button className="btn-close" onClick={() => onFechar(i.cat)}>Fechar</button>
            </div>
          ) : (
            <div className="item temp" key={i.chave}>
              <div className="badge" style={{ background: "var(--temp)" }}>°C</div>
              <div className="item-body">
                <div className="item-top">
                  <span className="item-title">Temperatura {sala.temperatura.toFixed(1)} °C</span>
                  <span className="item-meta">· {local}</span>
                </div>
                <div className="item-meta">
                  fora da faixa há <span className="mono">{formatarDuracao(i.seg)}</span>
                  {" · encerra sozinho ao normalizar"}
                </div>
              </div>
            </div>
          )
        )}
      </div>
    </section>
  );
}
