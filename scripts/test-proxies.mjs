// Testa vários proxies para obter precocombustiveis.pt a partir do GitHub.
const TARGET = "https://precocombustiveis.pt/proxima-semana/";
const enc = encodeURIComponent(TARGET);
const proxies = {
  direto:          TARGET,
  jina:            "https://r.jina.ai/" + TARGET,
  allorigins_raw:  "https://api.allorigins.win/raw?url=" + enc,
  allorigins_get:  "https://api.allorigins.win/get?url=" + enc,
  corsproxy:       "https://corsproxy.io/?url=" + enc,
  codetabs:        "https://api.codetabs.com/v1/proxy/?quest=" + TARGET,
  thingproxy:      "https://thingproxy.freeboard.io/fetch/" + TARGET,
};
for (const [name, url] of Object.entries(proxies)) {
  try {
    const r = await fetch(url, {
      headers: { "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/124.0",
                 "Accept-Language": "pt-PT,pt;q=0.9" },
    });
    const t = await r.text();
    const found = /gasolina 95 em cerca de/i.test(t);
    console.log(`${name.padEnd(16)} status=${r.status} len=${t.length} found=${found}`);
  } catch (e) {
    console.log(`${name.padEnd(16)} ERRO ${e}`);
  }
}
