// ============================================================================
// PlantaFog.jsx - a planta baixa do 1o andar com TODAS as salas monitoradas.
//
// Arquivo NOVO: o Planta.jsx original continua intacto. A diferenca e que
// la so a 120 tinha placa; aqui qualquer sala da planta pode estar sendo
// monitorada, porque o simulador publica as outras no mesmo formato.
//
// A geometria e exatamente a do Planta.jsx - nenhuma coordenada mudou.
// ============================================================================

// Salas que podem ter placa. Antes so a 120 estava nesta situacao;
// as outras vinham da lista OUTRAS, desenhadas sempre em cinza.
//
// "n" e o identificador que viaja no topico MQTT (digitaltwin/sala<n>/status);
// "rotulo" e o que aparece no desenho. Os dois so diferem nos laboratorios,
// porque "TECH 2" tem espaco e espaco em topico MQTT da problema.
const SALAS = [
  { n: "115",   x: 210, y: 45,  w: 90,  h: 95 },
  { n: "117",   x: 210, y: 148, w: 90,  h: 68 },
  { n: "118",   x: 210, y: 223, w: 90,  h: 68 },
  { n: "119",   x: 210, y: 298, w: 90,  h: 82 },
  { n: "120",   x: 210, y: 387, w: 90,  h: 62 },
  { n: "121",   x: 55,  y: 452, w: 100, h: 55 },
  { n: "tech3", x: 60,  y: 68,  w: 110, h: 88, rotulo: "TECH 3" },
  { n: "tech2", x: 60,  y: 160, w: 110, h: 88, rotulo: "TECH 2" },
];

// Comodos que nunca terao placa: so dao contexto ao desenho.
const SEM_PLACA = [
  { t: "WC C", x: 70, y: 298, w: 60, h: 40 },
  { t: "WC F", x: 70, y: 342, w: 60, h: 48 },
  { t: "WC M", x: 70, y: 396, w: 60, h: 48 },
];

const COR = {
  ok:   "var(--ok)",
  call: "var(--call)",
  temp: "var(--temp)",
  off:  "var(--off)",
};

export default function PlantaFog({ salas }) {
  // Indexa por numero para achar cada sala sem varrer a lista toda vez.
  const porNumero = Object.fromEntries(salas.map((s) => [s.sala, s]));

  return (
    <section className="card">
      <h2>Planta baixa</h2>

      <svg className="plan" viewBox="30 0 290 520">
        {/* escadas e corredor */}
        <rect x="170" y="10"  width="40" height="30"  fill="#0A0F13" />
        <rect x="175" y="45"  width="30" height="435" fill="#121A21" stroke="var(--room-line)" />
        <rect x="170" y="482" width="40" height="30"  fill="#0A0F13" />

        {/* deposito de materiais e area "em breve" */}
        <rect x="60" y="45" width="110" height="18" fill="var(--room)" stroke="var(--room-line)" />
        <text x="115" y="58" textAnchor="middle" fontSize="8.5" fill="var(--txt-dim)">materiais</text>
        <rect x="40" y="253" width="130" height="38" fill="var(--room)"
              stroke="var(--room-line)" strokeDasharray="3 3" />

        {/* comodos sem monitoramento */}
        {SEM_PLACA.map((r) => (
          <g key={r.t}>
            <rect x={r.x} y={r.y} width={r.w} height={r.h} rx="3"
                  fill="var(--room)" stroke="var(--room-line)" />
            <text x={r.x + r.w / 2} y={r.y + r.h / 2 + 4} textAnchor="middle"
                  fontSize="10.5" fill="var(--txt-dim)">{r.t}</text>
          </g>
        ))}

        {/* salas monitoradas */}
        {SALAS.map((r) => {
          const sala = porNumero[r.n];
          const cx = r.x + r.w / 2;
          const rotulo = r.rotulo ?? r.n;

          // Sem nenhuma mensagem dessa sala: ela existe no predio, mas
          // nao tem placa publicando. Cinza, como qualquer outro comodo.
          if (!sala) {
            return (
              <g key={r.n}>
                <rect x={r.x} y={r.y} width={r.w} height={r.h} rx="3"
                      fill="var(--room)" stroke="var(--room-line)" />
                <text x={cx} y={r.y + r.h / 2 + 4} textAnchor="middle"
                      fontSize="10.5" fill="var(--txt-dim)">{rotulo}</text>
              </g>
            );
          }

          const cor = COR[sala.est];
          const semSinal = sala.est === "off";

          return (
            <g key={r.n} className={sala.est === "call" ? "alarm" : ""}>
              <rect x={r.x} y={r.y} width={r.w} height={r.h} rx="4"
                    fill={cor} fillOpacity={semSinal ? 0.15 : 0.32}
                    stroke={cor} strokeWidth="2.5"
                    strokeDasharray={semSinal ? "5 4" : "0"} />
              <text x={cx} y={r.y + r.h / 2 - 3} textAnchor="middle"
                    fontSize="14" fontWeight="700" fill="var(--txt)">{rotulo}</text>
              <text x={cx} y={r.y + r.h / 2 + 15} textAnchor="middle"
                    fontSize="11" className="mono" fill="var(--txt)">
                {semSinal || sala.temperatura == null
                  ? "sem sinal"
                  : `${sala.temperatura.toFixed(1)} °C`}
              </text>
            </g>
          );
        })}
      </svg>

      {/* Legenda: cor + texto, para nao depender so da cor */}
      <div className="legend">
        <span><i className="dot" style={{ background: "var(--ok)" }} /> Normal</span>
        <span><i className="dot" style={{ background: "var(--call)" }} /> Chamado</span>
        <span><i className="dot" style={{ background: "var(--temp)" }} /> Temperatura</span>
        <span><i className="dot" style={{ background: "var(--off)" }} /> Sem comunicação</span>
      </div>
    </section>
  );
}
