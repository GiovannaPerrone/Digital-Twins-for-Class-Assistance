import { useSala } from "./useSala";

export default function App() {
  const { sala, fechar } = useSala();

  console.log(sala);

  return (
    <div>
      <h1>Painel da Inspetoria</h1>
      <pre>{JSON.stringify(sala, null, 2)}</pre>
      <button onClick={() => fechar("tecnico")}>Fechar técnico</button>
    </div>
  );
}