// ============================================================================
// useAgora.js — devolve o horário atual, atualizado a cada segundo.
//
// Por que isso existe: se a placa parar de publicar, nenhuma mensagem nova
// chega, e o React nunca redesenharia a tela. A sala ficaria verde para
// sempre, mesmo com a placa desligada. Este "relógio" força um redesenho
// por segundo, e aí o cálculo de offline() consegue perceber a queda.
// Também é ele que faz os contadores de tempo em aberto andarem.
// ============================================================================
import { useEffect, useState } from "react";

export function useAgora(intervalo = 1000) {
  const [agora, setAgora] = useState(Date.now());

  useEffect(() => {
    const id = setInterval(() => setAgora(Date.now()), intervalo);
    return () => clearInterval(id);   // para o relógio quando o componente sai
  }, [intervalo]);

  return agora;
}
