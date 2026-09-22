// ============================================================================
// Planta.jsx — a planta baixa do 1º andar, com a sala 120 colorida pelo estado.
// A geometria é a mesma do SVG que está no firmware da equipe.
// ============================================================================

// Cômodos sem placa: desenhados em cinza neutro, só para dar contexto.
const OUTRAS = [
  { x: 60,  y: 68,  w: 110, h: 88, t: "TECH 3" },
  { x: 60,  y: 160, w: 110, h: 88, t: "TECH 2" },
  { x: 70,  y: 298, w: 60,  h: 40, t: "WC C" },
  { x: 70,  y: 342, w: 60,  h: 48, t: "WC F" },
  { x: 70,  y: 396, w: 60,  h: 48, t: "WC M" },
  { x: 55,  y: 452, w: 100, h: 55, t: "121" },
  { x: 210, y: 45,  w: 90,  h: 95, t: "115" },
  { x: 210, y: 148, w: 90,  h: 68, t: "117" },
  { x: 210, y: 223, w: 90,  h: 68, t: "118" },
  { x: 210, y: 298, w: 90,  h: 82, t: "119" },
];

// Cor de cada estado (as variáveis estão no index.css).
const COR = {
  ok:   "var(--ok)",
  call: "var(--call)",
  temp: "var(--temp)",
  off:  "var(--off)",
};

export default function Planta({ est, sala }) {
  const cor = COR[est];
  const semSinal = est === "off";

  return (
    <section className="card">
      <h2>Planta baixa</h2>

      <svg className="plan" viewBox="30 0 290 520">
        {/* escadas e corredor */}
        <rect x="170" y="10"  width="40" height="30"  fill="#0A0F13" />
        <rect x="175" y="45"  width="30" height="435" fill="#121A21" stroke="var(--room-line)" />
        <rect x="170" y="482" width="40" height="30"  fill="#0A0F13" />

        {/* depósito de materiais e área "em breve" */}
        <rect x="60" y="45" width="110" height="18" fill="var(--room)" stroke="var(--room-line)" />
        <text x="115" y="58" textAnchor="middle" fontSize="8.5" fill="var(--txt-dim)">materiais</text>
        <rect x="40" y="253" width="130" height="38" fill="var(--room)"
              stroke="var(--room-line)" strokeDasharray="3 3" />

        {/* cômodos sem monitoramento */}
        {OUTRAS.map((r) => (
          <g key={r.t}>
            <rect x={r.x} y={r.y} width={r.w} height={r.h} rx="3"
                  fill="var(--room)" stroke="var(--room-line)" />
            <text x={r.x + r.w / 2} y={r.y + r.h / 2 + 4} textAnchor="middle"
                  fontSize="10.5" fill="var(--txt-dim)">{r.t}</text>
          </g>
        ))}

        {/* SALA 120 — a única com placa. Pisca quando há chamado. */}
        <g className={est === "call" ? "alarm" : ""}>
          <rect x="210" y="387" width="90" height="62" rx="4"
                fill={cor} fillOpacity={semSinal ? 0.15 : 0.32}
                stroke={cor} strokeWidth="2.5"
                strokeDasharray={semSinal ? "5 4" : "0"} />
          <text x="255" y="414" textAnchor="middle" fontSize="14"
                fontWeight="700" fill="var(--txt)">120</text>
          <text x="255" y="434" textAnchor="middle" fontSize="11"
                className="mono" fill="var(--txt)">
            {semSinal ? "sem sinal" : `${sala.temperatura.toFixed(1)} °C`}
          </text>
        </g>
      </svg>

      {/* Legenda: cor + texto, para não depender só da cor */}
      <div className="legend">
        <span><i className="dot" style={{ background: "var(--ok)" }} /> Normal</span>
        <span><i className="dot" style={{ background: "var(--call)" }} /> Chamado</span>
        <span><i className="dot" style={{ background: "var(--temp)" }} /> Temperatura</span>
        <span><i className="dot" style={{ background: "var(--off)" }} /> Sem comunicação</span>
      </div>
    </section>
  );
}
