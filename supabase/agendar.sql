-- ============================================================================
--  Agendamento diário da coleta (correr no SQL Editor, 1 vez)
--  Substitui XXXXXXXX pelo teu ref de projeto e SERVICE_ROLE_KEY_AQUI pela
--  service_role key (Project Settings > API). NÃO ponhas a service_role no ESP32.
-- ============================================================================
create extension if not exists pg_cron;
create extension if not exists pg_net;

-- Remove agendamento anterior com o mesmo nome (se existir)
select cron.unschedule('coletar-combustiveis')
where exists (select 1 from cron.job where jobname = 'coletar-combustiveis');

-- Todos os dias às 07:10 UTC (~08:10 em Lisboa no inverno).
select cron.schedule(
  'coletar-combustiveis',
  '10 7 * * *',
  $$
  select net.http_post(
    url     := 'https://XXXXXXXX.supabase.co/functions/v1/coletar',
    headers := jsonb_build_object(
      'Content-Type', 'application/json',
      'Authorization', 'Bearer SERVICE_ROLE_KEY_AQUI'
    ),
    body    := '{}'::jsonb
  );
  $$
);

-- Ver os agendamentos:            select * from cron.job;
-- Ver execuções recentes:         select * from cron.job_run_details order by start_time desc limit 10;
