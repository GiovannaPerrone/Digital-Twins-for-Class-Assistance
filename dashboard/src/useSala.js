import mqtt from "mqtt";
import { useEffect, useRef, useState } from "react";

const BROKER  = "wss://broker.hivemq.com:8884/mqtt";
const STATUS  = "digitaltwin/sala120/status";
const COMANDO = "digitaltwin/sala120/comando";

export function useSala() {
  const [sala, setSala] = useState(null);
  const [vistoEm, setVistoEm] = useState(null);
  const client = useRef(null);

  useEffect(() => {
    const c = mqtt.connect(BROKER);
    client.current = c;
    c.on("connect", () => c.subscribe(STATUS));
    c.on("message", (_, payload) => {
      try {
        setSala(JSON.parse(payload.toString()));
        setVistoEm(Date.now());
      } catch {
        console.warn("payload inválido:", payload.toString());
      }
    });
    return () => c.end();
  }, []);

  const fechar = (cat) => client.current?.publish(COMANDO, `fechar:${cat}`);
  return { sala, vistoEm, fechar };
}
