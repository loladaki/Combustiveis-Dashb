-- ============================================================================
--  Esquema da base de dados (correr no SQL Editor do Supabase, 1 vez)
-- ============================================================================

-- Preços por dia/distrito/combustível. O "preço atual" = última linha.
create table if not exists public.precos (
  data        date     not null,
  distrito    smallint not null check (distrito between 1 and 18),
  combustivel int      not null,
  preco       real     not null,
  primary key (data, distrito, combustivel)
);
create index if not exists idx_precos_lookup
  on public.precos (distrito, combustivel, data);

-- Previsão semanal (linha única, id = 1). Variação em €/L.
-- 'desde' = segunda-feira a que a previsão se aplica (para saber se já entrou em vigor).
create table if not exists public.previsao (
  id         smallint primary key default 1,
  gasolina   real,
  gasoleo    real,
  desde      date,
  atualizado timestamptz default now()
);
-- Se a tabela já existia sem a coluna:
alter table public.previsao add column if not exists desde date;

-- ---- Segurança (RLS): leitura pública, escrita só pela Edge Function ----
alter table public.precos    enable row level security;
alter table public.previsao  enable row level security;

drop policy if exists "leitura publica precos"   on public.precos;
drop policy if exists "leitura publica previsao" on public.previsao;

create policy "leitura publica precos"   on public.precos   for select using (true);
create policy "leitura publica previsao" on public.previsao for select using (true);
-- Não criamos policies de INSERT/UPDATE: a chave anon (do ESP32) só lê.
-- A Edge Function usa a service_role key, que ignora o RLS e pode escrever.
