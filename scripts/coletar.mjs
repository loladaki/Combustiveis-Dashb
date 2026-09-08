// ============================================================================
//  Coletor de preços de combustível -> Supabase
//  Corre no GitHub Actions (1x/dia) ou localmente:  node scripts/coletar.mjs
//  Sem dependências: usa o fetch nativo do Node 18+.
//
//  Variáveis de ambiente:
//    SUPABASE_URL                 ex: https://XXXX.supabase.co
//    SUPABASE_SERVICE_ROLE_KEY    service_role key (NUNCA no ESP32/repo público)
//  Sem elas, corre em modo "dry run" (só imprime, não grava).
// ============================================================================

const DISTRITOS = Array.from({ length: 18 }, (_, i) => i + 1);
const COMBS = [3201, 2101]; // 3201 = Gasolina 95, 2101 = Gasóleo simples
const UA =
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " +
  "(KHTML, like Gecko) Chrome/124.0 Safari/537.36";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Média dos preços de um distrito: TODOS os postos, só os atualizados nos
// últimos 14 dias (como o maisgasolina). Se houver poucos recentes, usa todos.
async function mediaPreco(distrito, comb) {
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
    const recentes = [], todos = [];
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
  } catch {
    return null;
  }
}

async function obterPrevisao() {
  try {
    const r = await fetch("https://precocombustiveis.pt/proxima-semana/", {
      headers: { "User-Agent": UA },
    });
    const html = await r.text();
    const g = html.match(/gasolina 95 em cerca de[^(]*\(([+-]?\d+[.,]\d+)/i);
    const d = html.match(/leo simples em cerca de[^(]*\(([+-]?\d+[.,]\d+)/i);
    const num = (m) => (m ? parseFloat(m[1].replace(",", ".")) : null);
    return { gasolina: num(g), gasoleo: num(d) };
  } catch {
    return { gasolina: null, gasoleo: null };
  }
}

async function upsert(url, key, tabela, linhas) {
  const r = await fetch(`${url}/rest/v1/${tabela}`, {
    method: "POST",
    headers: {
      apikey: key,
      Authorization: `Bearer ${key}`,
      "Content-Type": "application/json",
      Prefer: "resolution=merge-duplicates",
    },
    body: JSON.stringify(linhas),
  });
  if (!r.ok) throw new Error(`${tabela}: HTTP ${r.status} ${await r.text()}`);
}

async function main() {
  const URL = process.env.SUPABASE_URL;
  const KEY = process.env.SUPABASE_SERVICE_ROLE_KEY;
  const dry = !URL || !KEY;
  if (dry) console.log("[dry-run] sem SUPABASE_URL/KEY — só imprime, não grava.");

  const hoje = new Date().toISOString().slice(0, 10); // AAAA-MM-DD (UTC)
  const rows = [];
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
      await sleep(150);
    }
    process.stdout.write(".");
  }
  console.log(`\n[precos] ${rows.length} linhas para ${hoje}`);

  const pv = await obterPrevisao();
  console.log(`[previsao] gasolina=${pv.gasolina} gasoleo=${pv.gasoleo}`);

  if (dry) {
    console.log(JSON.stringify(rows.slice(0, 4), null, 2), "...");
    return;
  }

  if (rows.length) await upsert(URL, KEY, "precos", rows);
  await upsert(URL, KEY, "previsao", [{
    id: 1, gasolina: pv.gasolina, gasoleo: pv.gasoleo,
    atualizado: new Date().toISOString(),
  }]);
  console.log("[ok] gravado no Supabase.");
}

main().catch((e) => { console.error(e); process.exit(1); });
