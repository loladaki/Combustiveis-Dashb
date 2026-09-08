// ============================================================================
//  Dashboard de Preços de Combustível — ESP32-2432S028 (CYD)
//  Dados: backend Supabase (ver pasta /supabase). O ESP32 só LÊ.
//
//  3 painéis (swipe/tap para navegar):
//    1) Preços atuais + distrito + data   (toca no rodapé para mudar de distrito)
//    2) Gráfico do histórico
//    3) Variação prevista para a próxima 2a-feira
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <time.h>
#include "config.h"

#ifndef TFT_BACKLIGHT_ON            // alguns User_Setup.h definem TFT_BL mas não isto
#define TFT_BACKLIGHT_ON HIGH
#endif

// ------------------------------ Cores ---------------------------------------
#define COR_FUNDO     0x0841
#define COR_CARTAO    0x18E3
#define COR_TEXTO     TFT_WHITE
#define COR_SUAVE     0x8410
#define COR_G1        0x07FF       // ciano  -> combustível 1
#define COR_G2        0xFD20       // laranja-> combustível 2
#define COR_SOBE      0xF9A6       // a subir
#define COR_DESCE     0x2FEB       // a descer
#define COR_TITULO    0xFEA0

// ------------------------------ Objetos -------------------------------------
TFT_eSPI            tft = TFT_eSPI();
Preferences         prefs;

// Touch XPT2046 (pinos fixos da CYD; barramento SPI separado do ecrã)
#define PIN_BOOT   0
#define TOUCH_CLK  25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS   33
#define TOUCH_IRQ  36
SPIClass            tsSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

// ------------------------------ Estado --------------------------------------
struct Precos   { float g1 = NAN, g2 = NAN; bool ok = false; } atual;
struct Previsao { float g1 = NAN, g2 = NAN; bool online = false; } prev;

float serie1[HIST_MAX]; int serieN1 = 0;   // histórico do combustível 1 (mais antigo->recente)
float serie2[HIST_MAX]; int serieN2 = 0;

int           painelAtual   = 0;
unsigned long tRefresh = 0, tRotate = 0;
int           distritoAtual = ID_DISTRITO;

// Tecla do teclado no ecrã (no topo: o Arduino IDE gera protótipos no início).
enum { A_CHAR, A_SHIFT, A_MODE, A_BS, A_SPACE, A_OK };
struct Tecla { int x, y, w, h; char c; uint8_t acao; };

// ============================================================================
//  Utilitários
// ============================================================================
static String dataHojePT() {
  time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  char b[16];
  snprintf(b, sizeof(b), "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
  return String(b);
}

static const char* nomeDistrito(int id) {
  static const char* n[] = {
    "distrito", "Aveiro", "Beja", "Braga", "Braganca", "Castelo Branco",
    "Coimbra", "Evora", "Faro", "Guarda", "Leiria", "Lisboa", "Portalegre",
    "Porto", "Santarem", "Setubal", "Viana Castelo", "Vila Real", "Viseu" };
  return (id >= 1 && id <= 18) ? n[id] : n[0];
}

// ============================================================================
//  Backend Supabase (só leitura, via REST/PostgREST)
// ============================================================================
static bool supaGet(const String& path, String& out) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient http;
  http.setTimeout(15000);
  http.setConnectTimeout(15000);
  String url = String(SUPABASE_URL) + "/rest/v1/" + path;
  if (!http.begin(client, url)) return false;
  http.addHeader("apikey", SUPABASE_ANON);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[supa] %s -> HTTP %d\n", path.c_str(), code);
    http.end();
    return false;
  }
  out = http.getString();
  http.end();
  return true;
}

// Lê a série histórica de um combustível para o distrito atual.
static int carregarSerie(int comb, float* dest, int maxN) {
  String path = "precos?distrito=eq." + String(distritoAtual) +
                "&combustivel=eq." + String(comb) +
                "&order=data.asc&select=preco&limit=" + String(maxN);
  String body;
  if (!supaGet(path, body)) return 0;

  JsonDocument doc;
  if (deserializeJson(doc, body)) return 0;
  int n = 0;
  for (JsonObject o : doc.as<JsonArray>())
    if (n < maxN) dest[n++] = o["preco"].as<float>();
  return n;
}

static void carregarPrevisao() {
  String body;
  if (!supaGet("previsao?select=gasolina,gasoleo&limit=1", body)) { prev.online = false; return; }
  JsonDocument doc;
  if (deserializeJson(doc, body)) { prev.online = false; return; }
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() == 0) { prev.online = false; return; }
  JsonObject o = arr[0];
  prev.g1 = o["gasolina"].isNull() ? NAN : o["gasolina"].as<float>();
  prev.g2 = o["gasoleo"].isNull()  ? NAN : o["gasoleo"].as<float>();
  prev.online = true;
}

// Atualiza tudo a partir do Supabase (histórico + preço atual + previsão).
static void atualizarDados() {
  serieN1 = carregarSerie(ID_COMB_1, serie1, HIST_MAX);
  serieN2 = carregarSerie(ID_COMB_2, serie2, HIST_MAX);
  if (serieN1 > 0) atual.g1 = serie1[serieN1 - 1];
  if (serieN2 > 0) atual.g2 = serie2[serieN2 - 1];
  atual.ok = (serieN1 > 0 && serieN2 > 0);
  carregarPrevisao();
  Serial.printf("[dados] distrito=%d n1=%d n2=%d g1=%.3f g2=%.3f prev=%d\n",
                distritoAtual, serieN1, serieN2, atual.g1, atual.g2, prev.online);
}

// ============================================================================
//  Touch
// ============================================================================
static bool lerToque(int& x, int& y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  x = constrain(map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, tft.width()),  0, tft.width()  - 1);
  y = constrain(map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, tft.height()), 0, tft.height() - 1);
  if (TOUCH_RAW_LOG) Serial.printf("[touch] raw=%d,%d -> %d,%d\n", p.x, p.y, x, y);
  while (ts.touched()) delay(10);
  delay(40);
  return true;
}
static void esperarToque(int& x, int& y) { while (!lerToque(x, y)) delay(15); }

#define G_NADA (-1)
#define G_TAP   0
#define G_NEXT  1
#define G_PREV  2
static int lerGesto(int& tapX, int& tapY) {
  if (!ts.touched()) return G_NADA;
  TS_Point p = ts.getPoint();
  int x0 = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, tft.width());
  int lastx = x0, lasty = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, tft.height());
  unsigned long t0 = millis();
  while (ts.touched() && millis() - t0 < 3000) {
    TS_Point q = ts.getPoint();
    lastx = map(q.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, tft.width());
    lasty = map(q.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, tft.height());
    delay(10);
  }
  delay(30);
  tapX = constrain(lastx, 0, tft.width() - 1);
  tapY = constrain(lasty, 0, tft.height() - 1);
  int dx = lastx - x0;
  if (dx <= -40) return G_NEXT;
  if (dx >=  40) return G_PREV;
  return G_TAP;
}

// ============================================================================
//  UI — helpers de desenho
// ============================================================================
static void textoCentrado(const char* s, int x, int y, int fonte, uint16_t cor) {
  tft.setTextColor(cor, COR_FUNDO);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(s, x, y, fonte);
  tft.setTextDatum(TL_DATUM);
}

static void cabecalho(const char* titulo) {
  tft.setTextSize(1);
  tft.fillScreen(COR_FUNDO);
  tft.fillRect(0, 0, tft.width(), 26, COR_CARTAO);
  tft.setTextColor(COR_TITULO, COR_CARTAO);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(titulo, 8, 13, 4);
  tft.setTextColor(WiFi.isConnected() ? COR_DESCE : COR_SOBE, COR_CARTAO);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(WiFi.isConnected() ? "WiFi" : "---", tft.width() - 8, 13, 2);
  tft.setTextDatum(TL_DATUM);
}

static void pontosPainel() {
  int y = tft.height() - 8, cx = tft.width() / 2;
  for (int i = 0; i < 3; i++)
    tft.fillCircle(cx + (i - 1) * 16, y, 3, i == painelAtual ? COR_TITULO : COR_SUAVE);
}

// ---------------------------------------------------------------------------
//  Painel 1 — Preços atuais + distrito + data
// ---------------------------------------------------------------------------
static void painelPrecos() {
  cabecalho("Precos atuais");

  auto cartao = [&](int y, uint16_t cor, const char* nome, float preco) {
    tft.fillRoundRect(10, y, tft.width() - 20, 70, 8, COR_CARTAO);
    tft.fillRect(10, y, 6, 70, cor);
    tft.setTextSize(1);
    tft.setTextColor(COR_SUAVE, COR_CARTAO); tft.setTextDatum(TL_DATUM);
    tft.drawString(nome, 26, y + 6, 2);
    char b[16];
    if (isnan(preco)) strcpy(b, "-,---");
    else snprintf(b, sizeof(b), "%.3f", preco);
    tft.setTextColor(cor, COR_CARTAO); tft.setTextDatum(MR_DATUM);
    tft.setTextSize(2);
    tft.drawString(b, tft.width() - 24, y + 44, 4);
    tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
  };

  cartao(34,  COR_G1, NOME_COMB_1, atual.g1);
  cartao(112, COR_G2, NOME_COMB_2, atual.g2);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COR_SUAVE, COR_FUNDO);
  String rodape = "EUR/L  -  " + String(nomeDistrito(distritoAtual)) + "  -  " + dataHojePT();
  tft.drawString(rodape, tft.width() / 2, 192, 2);
  tft.setTextColor(COR_TITULO, COR_FUNDO);
  tft.drawString("toca aqui p/ mudar distrito", tft.width() / 2, 210, 2);
  tft.setTextDatum(TL_DATUM);

  pontosPainel();
}

// ---------------------------------------------------------------------------
//  Painel 2 — Gráfico do histórico
// ---------------------------------------------------------------------------
static void desenharSerie(const float* v, int count, uint16_t cor,
                          int x0, int y0, int w, int h, float lo, float hi) {
  if (count < 1 || hi <= lo) return;
  int prevx = 0, prevy = 0;
  for (int i = 0; i < count; i++) {
    int x = (count == 1) ? x0 + w / 2 : x0 + (w * i) / (count - 1);
    int y = y0 + h - (int)((v[i] - lo) / (hi - lo) * h);
    if (i > 0) tft.drawLine(prevx, prevy, x, y, cor);
    tft.fillCircle(x, y, 2, cor);
    prevx = x; prevy = y;
  }
}

static void painelGrafico() {
  cabecalho("Historico");
  int n = min(serieN1, serieN2);

  if (n < 1) {
    textoCentrado("Sem historico", tft.width() / 2, 100, 4, COR_TEXTO);
    textoCentrado("verifica o Supabase / WiFi", tft.width() / 2, 130, 2, COR_SUAVE);
    pontosPainel();
    return;
  }

  int gx = 40, gy = 40, gw = tft.width() - 55, gh = 140;
  tft.drawRect(gx, gy, gw, gh, COR_SUAVE);

  float lo = 9e9, hi = -9e9;
  for (int i = 0; i < n; i++) {
    lo = min(lo, min(serie1[i], serie2[i]));
    hi = max(hi, max(serie1[i], serie2[i]));
  }
  float margem = (hi - lo) * 0.1f + 0.005f;
  lo -= margem; hi += margem;

  desenharSerie(serie1, n, COR_G1, gx, gy, gw, gh, lo, hi);
  desenharSerie(serie2, n, COR_G2, gx, gy, gw, gh, lo, hi);

  char b[12];
  tft.setTextDatum(MR_DATUM); tft.setTextColor(COR_SUAVE, COR_FUNDO);
  snprintf(b, sizeof(b), "%.3f", hi); tft.drawString(b, gx - 3, gy + 6, 1);
  snprintf(b, sizeof(b), "%.3f", lo); tft.drawString(b, gx - 3, gy + gh - 6, 1);
  tft.setTextDatum(TL_DATUM);

  tft.fillCircle(gx + 6, gy + gh + 14, 4, COR_G1);
  tft.setTextColor(COR_TEXTO, COR_FUNDO);
  tft.drawString(NOME_COMB_1, gx + 16, gy + gh + 8, 2);
  int meio = gx + gw / 2;
  tft.fillCircle(meio + 6, gy + gh + 14, 4, COR_G2);
  tft.drawString(NOME_COMB_2, meio + 16, gy + gh + 8, 2);

  char sub[40]; snprintf(sub, sizeof(sub), "%s - %d dias", nomeDistrito(distritoAtual), n);
  textoCentrado(sub, tft.width() / 2, gy + gh + 34, 1, COR_SUAVE);

  pontosPainel();
}

// ---------------------------------------------------------------------------
//  Painel 3 — Variação prevista
// ---------------------------------------------------------------------------
static void cartaoPrevisao(int y, uint16_t cor, const char* nome, float atualP, float delta) {
  tft.fillRoundRect(10, y, tft.width() - 20, 70, 8, COR_CARTAO);
  tft.fillRect(10, y, 6, 70, cor);
  tft.setTextSize(1);
  tft.setTextColor(COR_SUAVE, COR_CARTAO); tft.setTextDatum(TL_DATUM);
  tft.drawString(nome, 26, y + 8, 2);

  if (isnan(delta)) {
    tft.setTextColor(COR_TEXTO, COR_CARTAO);
    tft.drawString("sem previsao", 26, y + 34, 2);
    return;
  }

  float proj = isnan(atualP) ? NAN : atualP + delta;
  int   cent = (int)roundf(delta * 100.0f);
  uint16_t c = cent > 0 ? COR_SOBE : (cent < 0 ? COR_DESCE : COR_SUAVE);
  const char* seta = cent > 0 ? "^" : (cent < 0 ? "v" : "=");

  char b[24];
  snprintf(b, sizeof(b), "%s %+d cent", seta, cent);
  tft.setTextColor(c, COR_CARTAO);
  tft.drawString(b, 26, y + 30, 4);

  if (!isnan(proj)) snprintf(b, sizeof(b), "~ %.3f EUR/L", proj);
  else              strcpy(b, "~ -,--- EUR/L");
  tft.setTextColor(COR_TEXTO, COR_CARTAO); tft.setTextDatum(MR_DATUM);
  tft.drawString(b, tft.width() - 26, y + 44, 4);
  tft.setTextDatum(TL_DATUM);
}

static void painelPrevisao() {
  cabecalho("Variacao semanal");
  cartaoPrevisao(34,  COR_G1, NOME_COMB_1, atual.g1, prev.g1);
  cartaoPrevisao(112, COR_G2, NOME_COMB_2, atual.g2, prev.g2);
  tft.setTextColor(COR_SUAVE, COR_FUNDO); tft.setTextDatum(MC_DATUM);
  tft.drawString(prev.online ? "Alteracao prevista para 2a-feira"
                             : "sem previsao disponivel",
                 tft.width() / 2, 198, 2);
  tft.setTextDatum(TL_DATUM);
  pontosPainel();
}

static void desenharPainel() {
  switch (painelAtual) {
    case 0: painelPrecos();   break;
    case 1: painelGrafico();  break;
    case 2: painelPrevisao(); break;
  }
}

// ============================================================================
//  Wi-Fi — seleção no ecrã tátil (sem credenciais fixas)
// ============================================================================
static bool carregarCredenciais(String& ssid, String& pass) {
  prefs.begin("wifi", true);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  prefs.end();
  return ssid.length() > 0;
}
static void guardarCredenciais(const String& ssid, const String& pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}
static int carregarDistrito() {
  prefs.begin("cfg", true);
  int d = prefs.getInt("distrito", ID_DISTRITO);
  prefs.end();
  return (d >= 1 && d <= 18) ? d : ID_DISTRITO;
}
static void guardarDistrito(int d) {
  prefs.begin("cfg", false);
  prefs.putInt("distrito", d);
  prefs.end();
}

static bool tentarLigar(const String& ssid, const String& pass, int timeoutMs = WIFI_TIMEOUT_MS) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < (unsigned long)timeoutMs) delay(200);
  return WiFi.isConnected();
}

// ---- Seletor de redes (lista tátil) ----
static String escolherRede() {
  const int TOPO = 32, ALT = 34, VIS = 5;
  int desloc = 0, n = 0;
  auto scan = [&]() {
    tft.fillScreen(COR_FUNDO);
    textoCentrado("A procurar redes...", tft.width() / 2, tft.height() / 2, 4, COR_TEXTO);
    WiFi.mode(WIFI_STA); WiFi.disconnect();
    n = WiFi.scanNetworks(); desloc = 0;
  };
  scan();
  while (true) {
    tft.fillScreen(COR_FUNDO);
    tft.fillRect(0, 0, tft.width(), 26, COR_CARTAO);
    tft.setTextSize(1);
    tft.setTextColor(COR_TITULO, COR_CARTAO); tft.setTextDatum(ML_DATUM);
    tft.drawString("Escolhe a rede WiFi", 8, 13, 2); tft.setTextDatum(TL_DATUM);
    if (n <= 0) textoCentrado("Nenhuma rede encontrada", tft.width() / 2, 120, 2, COR_SUAVE);
    else for (int i = 0; i < VIS && (desloc + i) < n; i++) {
      int idx = desloc + i, y = TOPO + i * ALT;
      tft.fillRoundRect(6, y, tft.width() - 12, ALT - 4, 4, COR_CARTAO);
      tft.setTextColor(COR_TEXTO, COR_CARTAO); tft.setTextDatum(ML_DATUM);
      String s = WiFi.SSID(idx); if (s.length() > 22) s = s.substring(0, 21) + "~";
      tft.drawString(s, 14, y + (ALT - 4) / 2, 2);
      tft.setTextColor(COR_SUAVE, COR_CARTAO); tft.setTextDatum(MR_DATUM);
      tft.drawString(WiFi.encryptionType(idx) == WIFI_AUTH_OPEN ? "aberta" : "*",
                     tft.width() - 16, y + (ALT - 4) / 2, 2);
      tft.setTextDatum(TL_DATUM);
    }
    int by = tft.height() - 28;
    tft.fillRoundRect(6, by, 60, 24, 4, COR_CARTAO);
    tft.fillRoundRect(72, by, 60, 24, 4, COR_CARTAO);
    tft.fillRoundRect(tft.width() - 96, by, 90, 24, 4, COR_CARTAO);
    tft.setTextColor(COR_TEXTO, COR_CARTAO); tft.setTextDatum(MC_DATUM);
    tft.drawString("cima", 36, by + 12, 2);
    tft.drawString("baixo", 102, by + 12, 2);
    tft.drawString("reprocurar", tft.width() - 51, by + 12, 2);
    tft.setTextDatum(TL_DATUM);
    int tx, ty; esperarToque(tx, ty);
    if (ty >= by) {
      if (tx < 66)       { if (desloc >= VIS) desloc -= VIS; }
      else if (tx < 132) { if (desloc + VIS < n) desloc += VIS; }
      else               scan();
      continue;
    }
    for (int i = 0; i < VIS && (desloc + i) < n; i++) {
      int y = TOPO + i * ALT;
      if (ty >= y && ty < y + ALT - 4) { String s = WiFi.SSID(desloc + i); WiFi.scanDelete(); return s; }
    }
  }
}

// ---- Teclado no ecrã ----
static int construirTeclado(int pagina, Tecla* T) {
  const char *r0, *r1, *r2;
  if      (pagina == 2) { r0 = "1234567890"; r1 = "@#$%&*-_+="; r2 = ".,:;!?/()"; }
  else if (pagina == 1) { r0 = "QWERTYUIOP"; r1 = "ASDFGHJKL";  r2 = "ZXCVBNM";   }
  else                  { r0 = "qwertyuiop"; r1 = "asdfghjkl";  r2 = "zxcvbnm";   }
  int n = 0, kw = 30, gap = 2, kh = 32;
  auto linha = [&](const char* r, int y) {
    int len = strlen(r), tot = len * kw + (len - 1) * gap, x0 = (tft.width() - tot) / 2;
    for (int i = 0; i < len; i++) T[n++] = { x0 + i * (kw + gap), y, kw, kh, r[i], A_CHAR };
  };
  linha(r0, 44); linha(r1, 80);
  T[n++] = { 1, 116, 44, kh, 0, A_SHIFT };
  { int len = strlen(r2), tot = len * kw + (len - 1) * gap, x0 = (tft.width() - tot) / 2;
    for (int i = 0; i < len; i++) T[n++] = { x0 + i * (kw + gap), 116, kw, kh, r2[i], A_CHAR }; }
  T[n++] = { tft.width() - 45, 116, 44, kh, 0, A_BS };
  T[n++] = { 1,   152, 58,  kh, 0,   A_MODE };
  T[n++] = { 63,  152, 150, kh, ' ', A_SPACE };
  T[n++] = { 217, 152, 100, kh, 0,   A_OK };
  return n;
}

static void desenharTeclado(int pagina, const String& out, const String& ssid, Tecla* T, int n) {
  tft.fillScreen(COR_FUNDO); tft.setTextSize(1);
  tft.setTextColor(COR_SUAVE, COR_FUNDO); tft.setTextDatum(TL_DATUM);
  String cab = "Password: " + ssid; if (cab.length() > 32) cab = cab.substring(0, 31) + "~";
  tft.drawString(cab, 6, 4, 2);
  tft.fillRoundRect(288, 2, 28, 18, 3, COR_CARTAO);
  tft.setTextColor(COR_SOBE, COR_CARTAO); tft.setTextDatum(MC_DATUM);
  tft.drawString("X", 302, 11, 2);
  tft.fillRoundRect(6, 22, 276, 18, 3, TFT_BLACK);
  tft.drawRoundRect(6, 22, 276, 18, 3, COR_SUAVE);
  tft.setTextColor(COR_TEXTO, TFT_BLACK); tft.setTextDatum(ML_DATUM);
  String vis = out; if (vis.length() > 34) vis = "~" + vis.substring(vis.length() - 33);
  tft.drawString(vis, 10, 31, 2);
  tft.setTextDatum(MC_DATUM);
  for (int i = 0; i < n; i++) {
    Tecla& k = T[i];
    uint16_t fundo = (k.acao == A_OK) ? COR_DESCE : COR_CARTAO;
    tft.fillRoundRect(k.x, k.y, k.w, k.h, 3, fundo);
    const char* lbl; char one[2] = {0, 0};
    switch (k.acao) {
      case A_CHAR:  one[0] = k.c; lbl = one; break;
      case A_SHIFT: lbl = (pagina == 1) ? "Aa" : "aA"; break;
      case A_MODE:  lbl = (pagina == 2) ? "ABC" : "123"; break;
      case A_BS:    lbl = "<-"; break;
      case A_SPACE: lbl = "espaco"; break;
      default:      lbl = "OK"; break;
    }
    tft.setTextColor((k.acao == A_OK) ? TFT_BLACK : COR_TEXTO, fundo);
    tft.drawString(lbl, k.x + k.w / 2, k.y + k.h / 2, 2);
  }
  tft.setTextDatum(TL_DATUM);
}

static bool lerPassword(const String& ssid, String& out) {
  out = ""; int pagina = 0;
  Tecla T[40]; int n = construirTeclado(pagina, T);
  desenharTeclado(pagina, out, ssid, T, n);
  while (true) {
    int tx, ty; esperarToque(tx, ty);
    if (ty < 22 && tx > 284) return false;
    bool redraw = false;
    for (int i = 0; i < n; i++) {
      Tecla& k = T[i];
      if (tx >= k.x && tx < k.x + k.w && ty >= k.y && ty < k.y + k.h) {
        switch (k.acao) {
          case A_CHAR:  if (out.length() < 63) out += k.c; redraw = true; break;
          case A_SHIFT: pagina = (pagina == 1) ? 0 : 1; n = construirTeclado(pagina, T); redraw = true; break;
          case A_MODE:  pagina = (pagina == 2) ? 0 : 2; n = construirTeclado(pagina, T); redraw = true; break;
          case A_BS:    if (out.length()) out.remove(out.length() - 1); redraw = true; break;
          case A_SPACE: if (out.length() < 63) out += ' '; redraw = true; break;
          case A_OK:    return true;
        }
        break;
      }
    }
    if (redraw) desenharTeclado(pagina, out, ssid, T, n);
  }
}

static void configurarWifiTactil() {
  while (true) {
    String ssid = escolherRede(), pass;
    if (!lerPassword(ssid, pass)) continue;
    tft.fillScreen(COR_FUNDO);
    textoCentrado("A ligar a", tft.width() / 2, 95, 2, COR_TEXTO);
    textoCentrado(ssid.c_str(), tft.width() / 2, 120, 4, COR_G1);
    if (tentarLigar(ssid, pass)) {
      guardarCredenciais(ssid, pass);
      textoCentrado("Ligado!", tft.width() / 2, 165, 4, COR_DESCE);
      Serial.printf("[wifi] %s  IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      delay(900); return;
    }
    textoCentrado("Falhou - tenta outra vez", tft.width() / 2, 165, 2, COR_SOBE);
    delay(1600);
  }
}

static void iniciarWifi(bool forcar = false) {
  String ssid, pass;
  if (!forcar && carregarCredenciais(ssid, pass)) {
    tft.fillScreen(COR_FUNDO);
    textoCentrado("A ligar ao WiFi...", tft.width() / 2, tft.height() / 2, 4, COR_TEXTO);
    if (tentarLigar(ssid, pass)) return;
  }
  configurarWifiTactil();
}

static void garantirWifi() {
  if (WiFi.isConnected()) return;
  String ssid, pass;
  if (carregarCredenciais(ssid, pass)) tentarLigar(ssid, pass, 10000);
}

// ---- Seletor de distrito ----
static void escolherDistrito() {
  const int TOPO = 32, ALT = 34, VIS = 5, TOTAL = 18;
  int desloc = ((distritoAtual - 1) / VIS) * VIS;
  while (true) {
    tft.fillScreen(COR_FUNDO);
    tft.fillRect(0, 0, tft.width(), 26, COR_CARTAO);
    tft.setTextSize(1);
    tft.setTextColor(COR_TITULO, COR_CARTAO); tft.setTextDatum(ML_DATUM);
    tft.drawString("Escolhe o distrito", 8, 13, 2); tft.setTextDatum(TL_DATUM);
    for (int i = 0; i < VIS && (desloc + i) < TOTAL; i++) {
      int id = desloc + i + 1, y = TOPO + i * ALT;
      bool sel = (id == distritoAtual);
      tft.fillRoundRect(6, y, tft.width() - 12, ALT - 4, 4, sel ? COR_G1 : COR_CARTAO);
      tft.setTextColor(sel ? COR_FUNDO : COR_TEXTO, sel ? COR_G1 : COR_CARTAO);
      tft.setTextDatum(ML_DATUM);
      tft.drawString(nomeDistrito(id), 14, y + (ALT - 4) / 2, 2);
      tft.setTextDatum(TL_DATUM);
    }
    int by = tft.height() - 28;
    tft.fillRoundRect(6, by, 60, 24, 4, COR_CARTAO);
    tft.fillRoundRect(72, by, 60, 24, 4, COR_CARTAO);
    tft.fillRoundRect(tft.width() - 96, by, 90, 24, 4, COR_CARTAO);
    tft.setTextColor(COR_TEXTO, COR_CARTAO); tft.setTextDatum(MC_DATUM);
    tft.drawString("cima", 36, by + 12, 2);
    tft.drawString("baixo", 102, by + 12, 2);
    tft.drawString("cancelar", tft.width() - 51, by + 12, 2);
    tft.setTextDatum(TL_DATUM);
    int tx, ty; esperarToque(tx, ty);
    if (ty >= by) {
      if (tx < 66)       { if (desloc >= VIS) desloc -= VIS; }
      else if (tx < 132) { if (desloc + VIS < TOTAL) desloc += VIS; }
      else               return;
      continue;
    }
    for (int i = 0; i < VIS && (desloc + i) < TOTAL; i++) {
      int y = TOPO + i * ALT;
      if (ty >= y && ty < y + ALT - 4) {
        int id = desloc + i + 1;
        if (id != distritoAtual) {
          distritoAtual = id; guardarDistrito(id);
          tft.fillScreen(COR_FUNDO);
          textoCentrado("A carregar dados...", tft.width() / 2, tft.height() / 2, 4, COR_TEXTO);
          atualizarDados();
        }
        return;
      }
    }
  }
}

// ============================================================================
//  Setup + Loop
// ============================================================================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOOT, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(COR_FUNDO);
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif

  tsSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(tsSPI);
  ts.setRotation(1);

  distritoAtual = carregarDistrito();
  iniciarWifi(digitalRead(PIN_BOOT) == LOW);

  configTzTime(TZ_INFO, "pt.pool.ntp.org", "pool.ntp.org");

  atualizarDados();

  tRefresh = millis();
  tRotate  = millis();
  desenharPainel();
}

void loop() {
  unsigned long agora = millis();

  // BOOT -> reabrir seletor de redes
  if (digitalRead(PIN_BOOT) == LOW) {
    delay(50);
    if (digitalRead(PIN_BOOT) == LOW) { configurarWifiTactil(); desenharPainel(); }
  }

  // Navegação tátil
  int gx, gy;
  int g = lerGesto(gx, gy);
  if (g == G_TAP && painelAtual == 0 && gy >= 185 && gy <= 220) {
    escolherDistrito(); desenharPainel(); tRotate = agora;
  } else if (g == G_NEXT || g == G_TAP) {
    painelAtual = (painelAtual + 1) % 3; desenharPainel(); tRotate = agora;
  } else if (g == G_PREV) {
    painelAtual = (painelAtual + 2) % 3; desenharPainel(); tRotate = agora;
  }

  if (agora - tRefresh >= REFRESH_MS) {
    tRefresh = agora;
    garantirWifi();
    atualizarDados();
    desenharPainel();
  }

  if (agora - tRotate >= PANEL_ROTATE_MS) {
    tRotate = agora;
    painelAtual = (painelAtual + 1) % 3;
    desenharPainel();
  }

  delay(50);
}
