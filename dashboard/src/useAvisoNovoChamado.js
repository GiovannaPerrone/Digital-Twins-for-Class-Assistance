// ============================================================================
// useAvisoNovoChamado.js — dispara um aviso quando um chamado ABRE.
//
// Compara os chamados da mensagem atual com os da anterior. Só avisa na
// transição false -> true. Assim, a cada nova publicação da placa o aviso
// não toca de novo para um chamado que já estava aberto.
// ============================================================================
import { useEffect, useRef } from "react";

export function useAvisoNovoChamado(sala, aoAbrir) {
  const anterior = useRef(null);   // os chamados da última mensagem

  useEffect(() => {
    const atual = sala?.chamados;

    // Na primeira mensagem (anterior ainda null) não avisa nada:
    // chamados que já estavam abertos quando o painel abriu não são "novos".
    if (atual && anterior.current) {
      for (const cat of Object.keys(atual)) {
        if (atual[cat] && !anterior.current[cat]) aoAbrir(cat);
      }
    }
    anterior.current = atual ?? null;
  }, [sala, aoAbrir]);
}

// Um "bip" curto gerado pelo próprio navegador, sem arquivo de áudio.
// ⚠ Navegadores bloqueiam som até o usuário interagir com a página:
//    o inspetor precisa clicar uma vez na tela para liberar.
let ctx;
export function tocarBip() {
  try {
    ctx = ctx || new (window.AudioContext || window.webkitAudioContext)();
    const osc = ctx.createOscillator();
    const vol = ctx.createGain();
    osc.frequency.value = 880;
    osc.connect(vol);
    vol.connect(ctx.destination);
    vol.gain.setValueAtTime(0.15, ctx.currentTime);
    vol.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 0.4);
    osc.start();
    osc.stop(ctx.currentTime + 0.4);
  } catch {
    // Sem suporte a áudio: segue sem som.
  }
}
