// ============================================================================
// regras.js — toda a lógica de negócio do painel, em funções puras.
// "Função pura" = recebe dados, devolve resultado, não mexe em nada fora.
// Por isso dá para testar e reaproveitar sem depender do React.
// ============================================================================

// Faixa de temperatura aceitável, em °C.
export const LIMITE_MIN = 18;
export const LIMITE_MAX = 26;

// Tempo sem receber mensagem até considerar a placa caída.
// ⚠ Precisa ser MAIOR que o intervalo em que o firmware publica.
//    Se a placa publica a cada 10 s, 60 s dá margem de sobra.
export const TIMEOUT_MS = 60_000;

// As três categorias de chamado, na mesma grafia do firmware.
export const CATEGORIAS = {
  tecnico:  { letra: "T", nome: "Técnico",  desc: "projetor, ar-condicionado" },
  material: { letra: "M", nome: "Material", desc: "canetas, apagador" },
  outro:    { letra: "O", nome: "Outro",    desc: "outros problemas" },
};

// A placa está sem comunicação?
// Se nunca chegou mensagem, ou se a última é mais antiga que o timeout.
export const offline = (vistoEm, agora) =>
  !vistoEm || agora - vistoEm > TIMEOUT_MS;

// A temperatura está fora da faixa?
export const tempFora = (sala) =>
  !!sala && (sala.temperatura < LIMITE_MIN || sala.temperatura > LIMITE_MAX);

// Existe pelo menos um chamado aberto?
export const temChamado = (sala) =>
  !!sala && Object.values(sala.chamados ?? {}).some(Boolean);

// O estado da sala, na ordem de prioridade:
// sem comunicação > chamado > temperatura > normal
export function estado(sala, vistoEm, agora) {
  if (!sala || offline(vistoEm, agora)) return "off";
  if (temChamado(sala)) return "call";
  if (tempFora(sala))   return "temp";
  return "ok";
}

// Há quanto tempo um chamado está aberto, em segundos.
// A placa envia quantos segundos o chamado tinha QUANDO publicou;
// somamos o tempo que passou desde que a mensagem chegou.
// Assim o contador anda sozinho entre uma publicação e outra.
export function segundosAbertos(sala, cat, vistoEm, agora) {
  const base = sala?.segundos?.[cat] ?? 0;
  return base + Math.floor((agora - vistoEm) / 1000);
}

// 42 -> "42s" · 125 -> "2min 5s" · 3700 -> "1h 1min"
export function formatarDuracao(s) {
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  return m < 60 ? `${m}min ${s % 60}s` : `${Math.floor(m / 60)}h ${m % 60}min`;
}
