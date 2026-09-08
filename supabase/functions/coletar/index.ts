// ============================================================================
//  Edge Function "coletar" — corre 1x/dia (ver agendar.sql)
//  Faz scraping da DGEG (18 distritos x 2 combustíveis) + previsão e grava.
//  Deploy:  supabase functions deploy coletar
//  Testar:  supabase functions invoke coletar   (ou o net.http_post do cron)
// ============================================================================
import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const DISTRITOS = Array.from({ length: 18 }, (_, i) => i + 1);
const COMBS = [3201, 2101]; // 3201 = Gasolina 95, 2101 = Gasóleo simples
const UA =
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
  "(KHTML, like Gecko) Chrome/124.0 Safari/537.36";

// Média dos preços de um combustível num distrito (via API da DGEG).
async function mediaPreco(distrito: number, comb: number): Promise<number | null> {
  const url =
    `https://precoscombustiveis.dgeg.gov.pt/api/PrecoComb/PesquisarPostos` +
    `?idsTiposComb=${comb}&idMarca=&idTipoPosto=&idDistrito=${distrito}` +
    `&idsMunicipios=&qtdPorPagina=60&pagina=1`;
  try {
    const r = await fetch(url, {
      headers: {
        "User-Agent": UA,
        "Accept": "application/json, text/plain, */*",
        "Referer": "https://precoscombustiveis.dgeg.gov.pt/",
      },
    });
    if (!r.ok) return null;
    const j = await r.json();
    const arr = j?.resultado ?? [];
    let soma = 0, n = 0;
    for (const p of arr) {
      const v = parseFloat(String(p.Preco).replace(",", ".").replace(/[^0-9.]/g, ""));
      if (!isNaN(v) && v > 0.3 && v < 4) { soma += v; n++; }
    }
    return n ? soma / n : null;
  } catch (_) {
    return null;
  }
}

// Previsão da próxima semana (scraping de precocombustiveis.pt).
async function obterPrevisao(): Promise<{ gasolina: number | null; gasoleo: number | null }> {
  try {
    const r = await fetch("https://precocombustiveis.pt/proxima-semana/", {
      headers: { "User-Agent": UA },
    });
    const html = await r.text();
    const g = html.match(/gasolina 95 em cerca de[^(]*\(([+-]?\d+[.,]\d+)/i);
    const d = html.match(/leo simples em cerca de[^(]*\(([+-]?\d+[.,]\d+)/i);
    const num = (m: RegExpMatchArray | null) =>
      m ? parseFloat(m[1].replace(",", ".")) : null;
    return { gasolina: num(g), gasoleo: num(d) };
  } catch (_) {
    return { gasolina: null, gasoleo: null };
  }
}

Deno.serve(async () => {
  const supabase = createClient(
    Deno.env.get("SUPABASE_URL")!,
    Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
  );

  const hoje = new Date().toISOString().slice(0, 10); // AAAA-MM-DD (UTC)
  const rows: Array<Record<string, unknown>> = [];

  for (const dist of DISTRITOS) {
    for (const comb of COMBS) {
      const m = await mediaPreco(dist, comb);
      if (m !== null) {
        rows.push({
          data: hoje,
          distrito: dist,
          combustivel: comb,
          preco: Math.round(m * 1000) / 1000,
        });
      }
      await new Promise((res) => setTimeout(res, 150)); // não martelar a DGEG
    }
  }

  if (rows.length) {
    await supabase.from("precos").upsert(rows, { onConflict: "data,distrito,combustivel" });
  }

  const pv = await obterPrevisao();
  await supabase.from("previsao").upsert({
    id: 1,
    gasolina: pv.gasolina,
    gasoleo: pv.gasoleo,
    atualizado: new Date().toISOString(),
  });

  return new Response(
    JSON.stringify({ ok: true, inseridos: rows.length, previsao: pv }),
    { headers: { "Content-Type": "application/json" } },
  );
});
