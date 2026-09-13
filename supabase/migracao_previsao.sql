-- ============================================================================
--  Migração da tabela 'previsao': de linha única (id) para histórico por semana.
--  Corre isto UMA VEZ no SQL Editor (a tabela antiga só tinha 1 linha vazia).
-- ============================================================================
drop table if exists public.previsao cascade;

create table public.previsao (
  desde      date primary key,   -- 2a-feira a que a previsão se aplica
  gasolina   real,               -- variação €/L
  gasoleo    real,
  atualizado timestamptz default now()
);

alter table public.previsao enable row level security;
create policy "leitura publica previsao" on public.previsao for select using (true);
