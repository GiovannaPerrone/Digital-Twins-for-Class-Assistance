// ============================================================================
// ResumoAndar.jsx - quantas salas em cada estado, e a saude do no de fog.
//
// Arquivo NOVO. Ocupa o lugar que o StatusSala.jsx ocupava: la o destaque
// era a temperatura da unica sala; aqui, com o andar inteiro, o que importa
// e quantas salas precisam de atencao agora.
// ============================================================================

export default function ResumoAndar({ resumo, limites, salas, conectado, fogMudo }) {
  const precisam = resumo.chamado + resumo.temperatura;

  // Cor do numero grande: vermelho se ha chamado, ambar se so temperatura.
  const cor = resumo.chamado > 0 ? "var(--call)"
            : resumo.temperatura > 0 ? "var(--temp)"
            : "var(--txt)";

  // Estado do proprio no de fog. Sem ele o painel inteiro fica cego,
  // entao vale dizer isso na tela em vez de mostrar dados velhos calado.
  const fog = !conectado ? { txt: "broker fora do ar", cor: "var(--off)" }
            : fogMudo    ? { txt: "fog sem publicar", cor: "var(--off)" }
            :              { txt: "fog ativo", cor: "var(--ok)" };

  return (
    <section className="card">
      <h2>Andar monitorado</h2>

      <div className="status-row">
        <div className="big" style={{ color: cor }}>{precisam}</div>
        <div>
          <div className="loc">
            {precisam === 1 ? "sala precisa de atenção" : "salas precisam de atenção"}
            {" · "}{salas.length} monitorada{salas.length === 1 ? "" : "s"}
          </div>
          <div className="dim">
            faixa aceitável: {limites.min}–{limites.max} °C
          </div>
        </div>
      </div>

      <div className="legend">
        <span><i className="dot" style={{ background: "var(--ok)" }} /> {resumo.ok} normal</span>
        <span><i className="dot" style={{ background: "var(--call)" }} /> {resumo.chamado} chamado</span>
        <span><i className="dot" style={{ background: "var(--temp)" }} /> {resumo.temperatura} temperatura</span>
        <span><i className="dot" style={{ background: "var(--off)" }} /> {resumo.offline} sem comunicação</span>
        <span><i className="dot" style={{ background: fog.cor }} /> {fog.txt}</span>
      </div>
    </section>
  );
}
