# Backend — Supabase + GitHub Actions

O scraping corre no **GitHub Actions** ([`../scripts/coletar.mjs`](../scripts/coletar.mjs),
agendado em [`../.github/workflows/coletar.yml`](../.github/workflows/coletar.yml)) e
grava na base de dados **Supabase**. O ESP32 só **lê**.

```
GitHub Actions (cron 1x/dia)  ──scrape DGEG + previsão──▶  Supabase (precos, previsao)  ──REST──▶  ESP32
```

## Passos (uma vez)

1. **Cria um projeto grátis** em https://supabase.com.
2. **SQL Editor** → cola e corre [`schema.sql`](schema.sql) (cria as tabelas + RLS).
3. **Põe o repositório no GitHub** (se ainda não está):
   ```bash
   git add . && git commit -m "dashboard combustiveis"
   git branch -M main
   git remote add origin https://github.com/<user>/<repo>.git
   git push -u origin main
   ```
4. No GitHub: **Settings → Secrets and variables → Actions → New repository secret**,
   cria dois secrets (valores em Supabase → Project Settings → API):
   - `SUPABASE_URL` = `https://XXXXXXXX.supabase.co`
   - `SUPABASE_SERVICE_ROLE_KEY` = a **service_role** key (secreta!)
5. **Actions** → workflow *coletar-combustiveis* → **Run workflow** (corre já e
   preenche o dia de hoje). A partir daí corre sozinho todos os dias.

## Ligar o ESP32

Em `include/config.h` (e na cópia do sketch Arduino):

```c
#define SUPABASE_URL   "https://XXXXXXXX.supabase.co"
#define SUPABASE_ANON  "a-tua-chave-anon"    // Project Settings > API > anon public
```

> A chave **anon** (pública, só leitura) vai no ESP32. A **service_role** só existe
> nos *secrets* do GitHub — nunca no dispositivo nem commitada no repo.

## Testar o coletor localmente (opcional)

```bash
node scripts/coletar.mjs          # dry-run: só imprime (36 linhas + previsão)
```
Para gravar mesmo, define as variáveis antes de correr:
```bash
SUPABASE_URL=... SUPABASE_SERVICE_ROLE_KEY=... node scripts/coletar.mjs
```

## Notas

- **Histórico**: a DGEG só dá o preço de hoje, por isso o histórico começa na 1ª
  execução — mas cresce para **todos os distritos** no servidor, sem o ESP32 ligado.
- **Chaves de combustível**: recolhe `3201` (Gasolina 95) e `2101` (Gasóleo). Muda
  em `scripts/coletar.mjs` (`COMBS`) e no `config.h` se quiseres outros.
- **Tabelas**: `precos(data, distrito, combustivel, preco)` e
  `previsao(id, gasolina, gasoleo, atualizado)`.
- A pasta `functions/` (Edge Function) + `agendar.sql` são uma **alternativa** ao
  GitHub Actions; com o Actions não precisas delas.
