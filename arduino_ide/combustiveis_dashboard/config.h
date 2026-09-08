#pragma once
// ============================================================================
//  Configuração do dashboard de combustíveis
//  Os dados vêm agora de um backend Supabase (ver pasta /supabase).
//  Edita apenas este ficheiro.
// ============================================================================

// ---- Supabase (obrigatório) ----
// Do teu projeto: Project Settings > API. Usa a chave *anon* (pública, só leitura).
#define SUPABASE_URL   "https://kpgrqgdkyueffdxvlfzf.supabase.co"
#define SUPABASE_ANON  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImtwZ3JxZ2RreXVlZmZkeHZsZnpmIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODg4MTgzNTgsImV4cCI6MjEwNDM5NDM1OH0.UmsVVJqhasDyV9UAScs-yX0-RJ5Rb6hOBTiZP5LKT44"

// ---- Localização inicial (podes mudar no ecrã) ----
// Distritos: 1 Aveiro, 2 Beja, 3 Braga, 4 Bragança, 5 Castelo Branco,
// 6 Coimbra, 7 Évora, 8 Faro, 9 Guarda, 10 Leiria, 11 Lisboa, 12 Portalegre,
// 13 Porto, 14 Santarém, 15 Setúbal, 16 Viana do Castelo, 17 Vila Real, 18 Viseu
#define ID_DISTRITO    11        // Lisboa (distrito INICIAL; muda-se no ecrã)

// ---- Combustíveis a mostrar (têm de existir no backend) ----
// 3201 Gasolina simples 95 | 3400 Gasolina 98 | 2101 Gasóleo simples
#define ID_COMB_1      3201
#define NOME_COMB_1    "Gasolina 95"
#define ID_COMB_2      2101
#define NOME_COMB_2    "Gasoleo"

// ---- Comportamento ----
#define REFRESH_MS       (6UL * 60UL * 60UL * 1000UL)  // relê o Supabase de 6 em 6 h
#define PANEL_ROTATE_MS  (8UL * 1000UL)                // rotação automática de painel
#define HIST_MAX         60      // máximo de pontos do gráfico a pedir

// ---- Fuso horário (Portugal continental) ----
#define TZ_INFO        "WET0WEST,M3.5.0/1,M10.5.0"

// ---- Wi-Fi: seleção no ecrã tátil (sem passwords fixas) ----
#define WIFI_TIMEOUT_MS   15000

// ---- Calibração do touch (XPT2046) — valores típicos da CYD ----
#define TOUCH_MIN_X    200
#define TOUCH_MAX_X    3700
#define TOUCH_MIN_Y    240
#define TOUCH_MAX_Y    3800
#define TOUCH_RAW_LOG  0          // 1 = imprime coordenadas em bruto no Serial
