# Dashboard de Combustíveis — ESP32 + TFT

Dashboard num ecrã TFT ligado a um ESP32, com **3 painéis** (swipe/tap para navegar):

1. **Preços atuais** — média do distrito + data (toca no rodapé p/ mudar distrito).
2. **Histórico** — gráfico dos preços ao longo do tempo.
3. **Variação semanal** — variação prevista para a próxima 2ª-feira.

## Arquitetura

O scraping é feito por um **GitHub Action** (1x/dia) que escreve numa base de dados
**Supabase**; o ESP32 apenas **lê**:

```
GitHub Actions (cron)  ──scrape DGEG + previsão──▶  Supabase (precos, previsao)  ──REST──▶  ESP32 (só lê)
   scripts/coletar.mjs, 18 distritos
```

| Dados | Origem | Notas |
|-------|--------|-------|
| Preços + histórico | GitHub Action → API **DGEG** | Recolhe os 18 distritos 1x/dia. Histórico completo no servidor (não depende do ESP32 estar ligado). |
| Previsão semanal | GitHub Action → **precocombustiveis.pt** | Guardada na tabela `previsao`. |
| Leitura no ecrã | **Supabase REST** (chave anon) | O ESP32 só lê; sem scraping nem NVS de histórico. |

**Configura primeiro o backend:** segue [`supabase/README.md`](supabase/README.md)
(criar projeto + tabelas + secrets do GitHub), depois mete `SUPABASE_URL` e
`SUPABASE_ANON` no `config.h`.

> O coletor é [`scripts/coletar.mjs`](scripts/coletar.mjs) (Node, sem dependências),
> agendado em [`.github/workflows/coletar.yml`](.github/workflows/coletar.yml).
> Testa-o localmente com `node scripts/coletar.mjs` (modo dry-run, sem gravar).
> A Edge Function em `supabase/functions/` é uma alternativa opcional (não é precisa).

## Hardware

Placa: **ESP32-2432S028** ("Cheap Yellow Display" / CYD) — ecrã ILI9341 240×320
já integrado. **Não é preciso ligar nenhum fio**: basta ligar por USB e carregar.

Pinos (já definidos no `platformio.ini`, aqui só para referência):

| Sinal | GPIO |
|-------|------|
| MISO / MOSI / SCLK | 12 / 13 / 14 |
| CS / DC / RST | 15 / 2 / -1 |
| Backlight (BL) | 21 |
| LED RGB (não usado) | 4 / 16 / 17 |
| Touch XPT2046 (não usado) | 25 / 32 / 33 / 36 / 39 |

## Instalação

### Opção A — PlatformIO (recomendado)

1. Edita [`include/config.h`](include/config.h): **Wi-Fi**, **distrito** e **combustíveis**.
2. Em [`platformio.ini`](platformio.ini): escolhe o **controlador** (`ILI9341_DRIVER` ou
   `ST7789_DRIVER`) e confirma os **pinos**.
3. Liga o ESP32 e:
   ```bash
   pio run -t upload && pio device monitor
   ```

### Opção B — Arduino IDE

1. Instala as bibliotecas: **TFT_eSPI** (Bodmer), **ArduinoJson** (v7) e
   **XPT2046_Touchscreen** (Paul Stoffregen).
2. Configura o TFT_eSPI: edita `TFT_eSPI/User_Setup.h` (ou usa um `User_Setup`
   próprio) com o controlador e os pinos da CYD. **Inclui `#define LOAD_FONT6`**
   se quiseres o preço na fonte grande (o código já funciona sem, com a font 4).
3. Abre `arduino_ide/combustiveis_dashboard/combustiveis_dashboard.ino`
   (o `config.h` está ao lado).
4. Placa: *ESP32 Dev Module*. Carrega.

## Ligar ao WiFi (no ecrã tátil — sem passwords no código)

Não há SSID/password no código. No arranque o ESP32 tenta a última rede guardada;
se não houver ou falhar, mostra **no ecrã** a lista de redes à volta:

1. Toca na tua rede.
2. Escreve a password no **teclado do ecrã** (`aA` maiúsculas, `123` símbolos,
   `<-` apagar, `espaco`, `X` cancelar).
3. `OK` → liga e guarda na flash.

**Trocar de rede** (noutro local): carrega no botão **BOOT** da placa → reabre o
seletor. Redes abertas: carrega `OK` sem password.

Se os toques ficarem desalinhados, põe `TOUCH_RAW_LOG 1` no `config.h`, vê os
valores em bruto no Serial e ajusta `TOUCH_MIN/MAX_*`.

## Usar o dashboard (tátil)

- **Swipe** para os lados (ou **tap**) troca de painel — sem esperar pela rotação.
- No **Painel 1**, toca em **"mudar distrito"** (rodapé) para escolher o distrito
  numa lista no ecrã. Fica guardado na flash e o histórico desse distrito aparece
  logo (vem do Supabase, já completo).

## Configuração rápida (`config.h`)

```c
#define SUPABASE_URL  "https://XXXXXXXX.supabase.co"
#define SUPABASE_ANON "a-tua-chave-anon"
#define ID_DISTRITO   11       // distrito INICIAL (depois muda-se no ecrã)
#define ID_COMB_1     3201     // Gasolina simples 95
#define ID_COMB_2     2101     // Gasóleo simples
```

**IDs de combustível** (de `GetTiposCombustiveis`): 3201 Gasolina 95 · 3400 Gasolina 98
· 2101 Gasóleo simples · 2105 Gasóleo especial · 1120 GPL Auto.

**Distritos**: 1 Aveiro · 2 Beja · 3 Braga · 4 Bragança · 5 Castelo Branco ·
6 Coimbra · 7 Évora · 8 Faro · 9 Guarda · 10 Leiria · 11 Lisboa · 12 Portalegre ·
13 Porto · 14 Santarém · 15 Setúbal · 16 Viana do Castelo · 17 Vila Real · 18 Viseu.

## Notas técnicas

- **HTTPS**: `WiFiClientSecure` com `setInsecure()` (não valida certificado — chega
  para ler uma API pública). O ESP32 fala só com o Supabase.
- **Dados**: o scraping da DGEG/previsão é feito na Edge Function (servidor); o
  ESP32 apenas lê JSON pequeno via REST (`/rest/v1/precos`, `/rest/v1/previsao`).
- **Persistência no ESP32**: só guarda credenciais WiFi e o distrito escolhido (NVS).
- **Ritmos**: ESP32 relê o Supabase de 6 em 6 h; a coleta no servidor é 1x/dia;
  rotação de painel a cada 8 s (interrompível por toque).

## Se algo correr mal

- **Cores trocadas/invertidas** (vermelho↔azul, ou negativo): troca
  `ILI9341_2_DRIVER` por `ILI9341_DRIVER` no `platformio.ini`/`User_Setup.h`, ou
  junta `tft.invertDisplay(true);` a seguir a `tft.init()`.
- **Ecrã branco/nada**: controlador ou pinos errados.
- **Preços a `-,---` / "Sem historico"**: vê o Serial (115200). `[supa] ... HTTP=`
  indica o problema — 401 = chave anon errada; 404 = URL errado; sem linha = WiFi.
  Confirma também que a Edge Function já correu pelo menos uma vez (backfill de hoje).
- **Gráfico vazio no início**: o histórico começa na 1ª execução da função e cresce
  1 ponto/dia (a DGEG não dá dados passados).
- **Previsão "sem previsao"**: o site pode ter mudado o texto — ajusta
  `ANCORA_PREV_*` em `config.h` (as âncoras são o texto imediatamente antes do número).
