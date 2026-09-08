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

// Média dos preços de um distrito: TODOS os postos, só os atualizados nos
// últimos 14 dias (como o maisgasolina). Se houver poucos recentes, usa todos.
async function mediaPreco(distrito: number, comb: number): Promise<number | null> {
  const url =
    `https://precoscombustiveis.dgeg.gov.pt/api/PrecoComb/PesquisarPostos` +
    `?idsTiposComb=${comb}&idMarca=&idTipoPosto=&idDistrito=${distrito}` +
    `&idsMunicipios=&qtdPorPagina=5000&pagina=1`;
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
    const agora = Date.now(), LIM = 14 * 864e5;
    const recentes: number[] = [], todos: number[] = [];
    for (const p of arr) {
      const v = parseFloat(String(p.Preco).replace(",", ".").replace(/[^0-9.]/g, ""));
      if (isNaN(v) || v <= 0.3 || v >= 4) continue;
      todos.push(v);
      const d = Date.parse(String(p.DataAtualizacao || "").replace(" ", "T"));
      if (!isNaN(d) && agora - d <= LIM) recentes.push(v);
    }
    const usar = recentes.length >= 5 ? recentes : todos;
    if (!usar.length) return null;
    return usar.reduce((a, b) => a + b, 0) / usar.length;
  } catch (_) {
    return null;
  }
}

// Segunda-feira a que a previsão se aplica (fim da semana - 6 dias).
function segundaDaPrevisao(html: string): string | null {
  const meses: Record<string, number> = { janeiro: 0, fevereiro: 1, marco: 2,
    abril: 3, maio: 4, junho: 5, julho: 6, agosto: 7, setembro: 8, outubro: 9,
    novembro: 10, dezembro: 11 };
  const m = html.match(/semana de \d{1,2} a (\d{1,2})\s+(?:de\s+)?([A-Za-zçÇà-ÿ]+)/i);
  if (!m) return null;
  const endDay = parseInt(m[1]);
  const mes = m[2].toLowerCase().normalize("NFD").replace(/[̀-ͯ]/g, "");
  const mi = meses[mes];
  if (mi === undefined) return null;
  const now = new Date();
  let end = new Date(Date.UTC(now.getUTCFullYear(), mi, endDay));
  const diff = (end.getTime() - now.getTime()) / 864e5;
  if (diff < -180) end = new Date(Date.UTC(now.getUTCFullYear() + 1, mi, endDay));
  if (diff > 300)  end = new Date(Date.UTC(now.getUTCFullYear() - 1, mi, endDay));
  return new Date(end.getTime() - 6 * 864e5).toISOString().slice(0, 10);
}

// Previsão da próxima semana (scraping de precocombustiveis.pt).
async function obterPrevisao(): Promise<
  { gasolina: number | null; gasoleo: number | null; desde: string | null }
> {
  try {
    const r = await fetch("https://precocombustiveis.pt/proxima-semana/", {
      headers: { "User-Agent": UA },
    });
    const html = await r.text();
    const g = html.match(/gasolina 95 em cerca de[^(]*\(([+-]?\d+[.,]\d+)/i);
    const d = html.match(/leo simples em cerca de[^(]*\(([+-]?\d+[.,]\d+)/i);
    const num = (m: RegExpMatchArray | null) =>
      m ? parseFloat(m[1].replace(",", ".")) : null;
    return { gasolina: num(g), gasoleo: num(d), desde: segundaDaPrevisao(html) };
  } catch (_) {
    return { gasolina: null, gasoleo: null, desde: null };
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
    desde: pv.desde,
    atualizado: new Date().toISOString(),
  });

  return new Response(
    JSON.stringify({ ok: true, inseridos: rows.length, previsao: pv }),
    { headers: { "Content-Type": "application/json" } },
  );
});
