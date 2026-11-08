/*
 * ============================================================================
 *  Weather Station para Waveshare ESP32-C6-LCD-1.47
 * ============================================================================
 *  Inspirado no projeto ThingPulse "esp8266-weather-station-color"
 *  (https://github.com/ThingPulse/esp8266-weather-station-color), reescrito
 *  para a placa Waveshare ESP32-C6 com display IPS 1.47" ST7789 (172 x 320).
 *
 *  Principais mudanças em relação ao original:
 *   - ESP8266 + ILI9341 (240x320) + touch  ->  ESP32-C6 + ST7789 (172x320)
 *   - Mini Grafx                           ->  GFX Library for Arduino (Arduino_GFX)
 *     com framebuffer (Arduino_Canvas): sem flicker ao redesenhar
 *   - OpenWeatherMap (exige chave)         ->  Open-Meteo (grátis, sem chave)
 *   - Ícones bitmap                        ->  ícones vetoriais coloridos e escaláveis
 *   - Touch para trocar de tela            ->  botão BOOT (GPIO9) + troca automática
 *   - Extras: brilho noturno automático, LED RGB de status, textos em PT-BR
 *
 *  Placa (Arduino IDE): "ESP32C6 Dev Module"  (core esp32 da Espressif >= 3.0)
 *     Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"  (o sketch não cabe na padrão)
 *     USB CDC On Boot: Enabled  (para ver o Serial Monitor pela USB-C)
 *
 *  Bibliotecas (Library Manager):
 *     - GFX Library for Arduino  (moononournation)   >= 1.5.0
 *     - U8g2                     (olikraus)          - só as fontes
 *     - ArduinoJson              (Benoit Blanchon)   >= 7.0
 *
 *  Wi-Fi e cidade são configurados pelo celular (veja web_config.ino):
 *  na primeira vez, ou segurando o botão BOOT por 3 s, a estação cria a rede
 *  "WeatherStation" e abre a página de configuração.
 * ============================================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include <Arduino_GFX_Library.h>
#include <U8g2lib.h> // apenas para as fontes u8g2_font_*
#include <time.h>

#include "settings.h"
#include "weather_icons.h"
#include "web_assets.h"

// ---------------------------------------------------------------------------
// Pinos da placa Waveshare ESP32-C6-LCD-1.47
// ---------------------------------------------------------------------------
#define LCD_MOSI 6
#define LCD_SCK  7
#define LCD_CS   14
#define LCD_DC   15
#define LCD_RST  21
#define LCD_BL   22
#define LCD_MISO 5   // compartilhado com o cartão microSD
#define SD_CS    4   // mantido em HIGH para o cartão SD não interferir no barramento
#define BTN_BOOT 9   // botão BOOT (ativo em LOW)
#define RGB_LED  8   // WS2812 onboard

#define LCD_WIDTH  172
#define LCD_HEIGHT 320

// ---------------------------------------------------------------------------
// Display: ST7789 172x320 (offset de coluna 34) + framebuffer em RAM
// ---------------------------------------------------------------------------
Arduino_DataBus *bus = new Arduino_HWSPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, LCD_MISO);
Arduino_GFX *panel = new Arduino_ST7789(bus, LCD_RST, 0 /* rotação */, true /* IPS */,
                                        LCD_WIDTH, LCD_HEIGHT,
                                        34 /* col offset 1 */, 0 /* row offset 1 */,
                                        34 /* col offset 2 */, 0 /* row offset 2 */);
// O canvas é girado (rotação 1 ou 3) para obter 320 x 172 na horizontal
#if (DISPLAY_ROTATION != 1) && (DISPLAY_ROTATION != 3)
#error "DISPLAY_ROTATION deve ser 1 ou 3 (layout horizontal)"
#endif
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel, 0, 0, DISPLAY_ROTATION);

// ---------------------------------------------------------------------------
// Dados de tempo
// ---------------------------------------------------------------------------
struct DayForecast
{
  int code = -1;
  float tmax = NAN, tmin = NAN;
  int pop = -1;   // probabilidade de chuva (%)
  float uv = NAN; // índice UV máximo
  int wday = 0;   // 0 = domingo
};

struct WeatherData
{
  bool valid = false;
  float temp = NAN, feels = NAN, pressure = NAN, wind = NAN, precip = NAN;
  int humidity = -1, windDir = 0, code = -1;
  bool isDay = true;
  DayForecast days[4]; // [0] = hoje
  char sunrise[6] = "--:--";
  char sunset[6] = "--:--";
  time_t updatedAt = 0;
} wx;

// ---------------------------------------------------------------------------
// Configuração (gravada na flash; editada pela página web)
// ---------------------------------------------------------------------------
struct Config
{
  String ssid, pass;
  String city, uf;
  float lat = 0, lon = 0;
  int32_t utcOffset = 0; // segundos
} cfg;

Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

bool portalActive = false;
bool portalAutomatic = false;
uint32_t portalStartMs = 0;
uint32_t portalStopAt = 0;
bool staConnecting = false;
bool staBeginDone = false;
uint32_t staConnectStart = 0;
int connectResult = 0; // 0 = nada, 1 = conectou, 2 = falhou
String pendingSsid, pendingPass;
String scanHtml;
bool mdnsStarted = false;
bool wasConnected = false;
bool fetchNow = false;

// ---------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------
#define NUM_PAGES 2
int page = 0;
bool needRedraw = true;
uint32_t lastPageChange = 0;
uint32_t lastFetchMs = 0;
uint32_t nextFetchDelay = 0; // 0 = buscar o quanto antes
uint32_t lastReconnect = 0;
int lastMinute = -1;
int currentBrightness = -1;

static const char *WDAY_SHORT[] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb"};
static const char *WDAY_LONG[] = {"Domingo", "Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado"};
static const char *MONTHS[] = {"Jan", "Fev", "Mar", "Abr", "Mai", "Jun",
                               "Jul", "Ago", "Set", "Out", "Nov", "Dez"};

// ===========================================================================
// Utilitários
// ===========================================================================
static void statusLed(uint8_t r, uint8_t g, uint8_t b)
{
#if USE_STATUS_LED
  rgbLedWrite(RGB_LED, r, g, b);
#endif
}

static void setBrightness(int value)
{
  value = constrain(value, 0, 255);
  if (value != currentBrightness)
  {
    ledcWrite(LCD_BL, value);
    currentBrightness = value;
  }
}

static bool timeIsValid()
{
  return time(nullptr) > 1700000000; // depois de nov/2023 => NTP sincronizado
}

// Dia da semana, 0 = domingo (algoritmo de Sakamoto)
static int dayOfWeek(int y, int m, int d)
{
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3)
    y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static const char *windDirText(int deg)
{
  static const char *dirs[] = {"N", "NE", "L", "SE", "S", "SO", "O", "NO"};
  return dirs[((deg % 360) + 22) / 45 % 8];
}

// ---------------------------------------------------------------------------
// Texto
// ---------------------------------------------------------------------------
static int16_t textWidthOfs(const char *s, int16_t *xOffset)
{
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(s, 0, 100, &x1, &y1, &w, &h);
  if (xOffset)
    *xOffset = x1;
  return (int16_t)w;
}

static int16_t textWidth(const char *s)
{
  return textWidthOfs(s, nullptr);
}

// Desenha texto com (x, y) = ponto de alinhamento na linha de base.
// Retorna a coordenada x logo após o texto.
static int16_t drawText(const char *s, int16_t x, int16_t y, Align align, uint16_t color,
                        const uint8_t *font)
{
  gfx->setFont(font);
  gfx->setTextColor(color);
  int16_t xo;
  int16_t w = textWidthOfs(s, &xo);
  int16_t cx = x;
  if (align == AL_CENTER)
    cx = x - xo - w / 2;
  else if (align == AL_RIGHT)
    cx = x - xo - w;
  else
    cx = x - xo;
  gfx->setCursor(cx, y);
  gfx->print(s);
  return cx + xo + w;
}

// Escolhe a maior fonte em que o texto cabe em maxWidth
static void drawTextFit(const char *s, int16_t x, int16_t y, int16_t maxWidth, uint16_t color)
{
  static const uint8_t *fonts[] = {u8g2_font_helvB14_tf, u8g2_font_helvB12_tf,
                                   u8g2_font_helvB10_tf, u8g2_font_helvB08_tf};
  for (const uint8_t *f : fonts)
  {
    gfx->setFont(f);
    if (textWidth(s) <= maxWidth || f == u8g2_font_helvB08_tf)
    {
      drawText(s, x, y, AL_CENTER, color, f);
      return;
    }
  }
}

static void fmtTemp(char *buf, size_t n, float t)
{
  if (isnan(t))
    snprintf(buf, n, "--°");
  else
    snprintf(buf, n, "%ld°", lroundf(t));
}

static void drawPageDots()
{
  const int16_t y = gfx->height() - 5, gap = 12;
  int16_t x = gfx->width() / 2 - (NUM_PAGES - 1) * gap / 2;
  for (int i = 0; i < NUM_PAGES; i++)
  {
    if (i == page)
      gfx->fillCircle(x + i * gap, y, 3, C_ACCENT);
    else
      gfx->drawCircle(x + i * gap, y, 2, C_TEXT_DIM);
  }
}

// ===========================================================================
// Telas (layout horizontal: 320 x 172)
// ===========================================================================
static void showMessage(const char *line1, const char *line2)
{
  const int16_t H = gfx->height();
  gfx->fillScreen(C_BG);
  drawWeatherIcon(gfx, 72, H / 2, 100, 2, true, C_BG);
  drawText("Weather Station", 222, 66, AL_CENTER, C_TEXT, u8g2_font_helvB12_tf);
  drawText(line1, 222, 98, AL_CENTER, C_ACCENT, u8g2_font_helvB10_tf);
  if (line2)
    drawText(line2, 222, 118, AL_CENTER, C_TEXT_DIM, u8g2_font_helvR08_tf);
  gfx->flush();
}

// Cabeçalho comum: relógio grande à esquerda, data e cidade à direita
static void drawHeader()
{
  const int16_t W = gfx->width();
  char buf[32];

  if (timeIsValid())
  {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    drawText(buf, 8, 40, AL_LEFT, C_TEXT, u8g2_font_logisoso32_tn);
    snprintf(buf, sizeof(buf), "%s, %02d %s", WDAY_LONG[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
    drawText(buf, W - 8, 21, AL_RIGHT, C_TEXT, u8g2_font_helvB10_tf);
  }
  else
  {
    drawText("--:--", 8, 40, AL_LEFT, C_TEXT_DIM, u8g2_font_logisoso32_tn);
  }

  if (WiFi.status() != WL_CONNECTED)
    drawText("sem Wi-Fi", W - 8, 38, AL_RIGHT, RGB565(255, 90, 90), u8g2_font_helvB08_tf);
  else
  {
    String loc = cfg.uf.length() ? cfg.city + " - " + cfg.uf : cfg.city;
    drawText(loc.c_str(), W - 8, 38, AL_RIGHT, C_TEXT_DIM, u8g2_font_helvR08_tf);
  }

  // indicador de dados desatualizados (falhas seguidas de atualização)
  bool stale = !wx.valid || (timeIsValid() && time(nullptr) - wx.updatedAt > 3 * UPDATE_INTERVAL_SECS);
  if (stale)
    gfx->fillCircle(W - 4, 4, 3, RGB565(255, 60, 60));

  gfx->drawFastHLine(8, 47, W - 16, C_PANEL_LINE);
}

// Página 1: condições atuais (esquerda) + previsão para 3 dias (direita)
static void drawMainPage()
{
  char buf[40];

  // ---- Lado esquerdo: agora (x 0..160) ----
  drawWeatherIcon(gfx, 38, 86, 68, wx.code, wx.isDay, C_BG);

  if (isnan(wx.temp))
    snprintf(buf, sizeof(buf), "--");
  else
    snprintf(buf, sizeof(buf), "%ld", lroundf(wx.temp));
  gfx->setFont(u8g2_font_logisoso42_tn);
  int16_t tw = textWidth(buf);
  int16_t tx = 116 - tw / 2 - 5;
  int16_t end = drawText(buf, tx, 106, AL_LEFT, C_TEXT, u8g2_font_logisoso42_tn);
  gfx->fillCircle(end + 7, 70, 5, C_TEXT); // símbolo de grau
  gfx->fillCircle(end + 7, 70, 2, C_BG);

  // Máxima / mínima de hoje
  char tmax[12], tmin[12];
  fmtTemp(tmax, sizeof(tmax), wx.days[0].tmax);
  fmtTemp(tmin, sizeof(tmin), wx.days[0].tmin);
  gfx->setFont(u8g2_font_helvB10_tf);
  int16_t wMax = textWidth(tmax), wMin = textWidth(tmin);
  int16_t sx = 116 - (wMax + 10 + wMin) / 2;
  int16_t ex = drawText(tmax, sx, 124, AL_LEFT, C_TEMP_MAX, u8g2_font_helvB10_tf);
  drawText(tmin, ex + 10, 124, AL_LEFT, C_TEMP_MIN, u8g2_font_helvB10_tf);

  // Descrição
  drawTextFit(wmoToText(wx.code), 80, 145, 154, C_TEXT);

  // Umidade e vento
  char hum[12], wind[16];
  if (wx.humidity >= 0)
    snprintf(hum, sizeof(hum), "%d%%", wx.humidity);
  else
    snprintf(hum, sizeof(hum), "--");
  if (!isnan(wx.wind))
    snprintf(wind, sizeof(wind), "%ld km/h", lroundf(wx.wind));
  else
    snprintf(wind, sizeof(wind), "--");
  int16_t x = drawText("Umid. ", 6, 162, AL_LEFT, C_TEXT_DIM, u8g2_font_helvR08_tf);
  x = drawText(hum, x + 2, 162, AL_LEFT, C_TEXT, u8g2_font_helvB08_tf);
  x = drawText("Vento ", x + 8, 162, AL_LEFT, C_TEXT_DIM, u8g2_font_helvR08_tf);
  drawText(wind, x + 2, 162, AL_LEFT, C_TEXT, u8g2_font_helvB08_tf);

  // ---- Lado direito: próximos 3 dias (x 164..316) ----
  const int16_t px = 164, pw = 152, colW = pw / 3;
  gfx->fillRoundRect(px, 53, pw, 110, 8, C_PANEL);
  for (int i = 1; i <= 3; i++)
  {
    const DayForecast &d = wx.days[i];
    int16_t cx = px + colW * (i - 1) + colW / 2;
    drawText(WDAY_SHORT[d.wday], cx, 70, AL_CENTER, C_ACCENT, u8g2_font_helvB10_tf);
    drawWeatherIcon(gfx, cx, 94, 34, d.code, true, C_PANEL);
    fmtTemp(tmax, sizeof(tmax), d.tmax);
    fmtTemp(tmin, sizeof(tmin), d.tmin);
    drawText(tmax, cx, 127, AL_CENTER, C_TEMP_MAX, u8g2_font_helvB10_tf);
    drawText(tmin, cx, 143, AL_CENTER, C_TEMP_MIN, u8g2_font_helvB10_tf);
    if (d.pop >= 0)
    {
      snprintf(buf, sizeof(buf), "%d%%", d.pop);
      drawText(buf, cx, 157, AL_CENTER, C_TEXT_DIM, u8g2_font_helvR08_tf);
    }
    if (i > 1)
      gfx->drawFastVLine(px + colW * (i - 1), 59, 98, C_PANEL_LINE);
  }
}

// Página 2: detalhes em duas colunas
static void drawDetailsPage()
{
  char buf[32];

  struct Row
  {
    const char *label;
    char value[20];
  } rows[8];

  rows[0].label = "Sensação";
  fmtTemp(rows[0].value, sizeof(rows[0].value), wx.feels);
  rows[1].label = "Umidade";
  if (wx.humidity >= 0)
    snprintf(rows[1].value, 20, "%d%%", wx.humidity);
  else
    snprintf(rows[1].value, 20, "--");
  rows[2].label = "Vento";
  if (!isnan(wx.wind))
    snprintf(rows[2].value, 20, "%ld km/h %s", lroundf(wx.wind), windDirText(wx.windDir));
  else
    snprintf(rows[2].value, 20, "--");
  rows[3].label = "Pressão";
  if (!isnan(wx.pressure))
    snprintf(rows[3].value, 20, "%ld hPa", lroundf(wx.pressure));
  else
    snprintf(rows[3].value, 20, "--");
  rows[4].label = "Chuva hoje";
  if (wx.days[0].pop >= 0)
    snprintf(rows[4].value, 20, "%d%%", wx.days[0].pop);
  else
    snprintf(rows[4].value, 20, "--");
  rows[5].label = "Índice UV";
  if (!isnan(wx.days[0].uv))
    snprintf(rows[5].value, 20, "%.1f", wx.days[0].uv);
  else
    snprintf(rows[5].value, 20, "--");
  rows[6].label = "Nascer do sol";
  snprintf(rows[6].value, 20, "%s", wx.sunrise);
  rows[7].label = "Pôr do sol";
  snprintf(rows[7].value, 20, "%s", wx.sunset);

  const int16_t top = 53, rowH = 25, colW = 154;
  for (int c = 0; c < 2; c++)
  {
    int16_t x0 = 4 + c * (colW + 4);
    gfx->fillRoundRect(x0, top, colW, rowH * 4 + 2, 8, C_PANEL);
    for (int r = 0; r < 4; r++)
    {
      const Row &row = rows[c * 4 + r];
      int16_t y = top + 1 + rowH * r;
      if (r > 0)
        gfx->drawFastHLine(x0 + 8, y, colW - 16, C_PANEL_LINE);
      drawText(row.label, x0 + 8, y + 17, AL_LEFT, C_TEXT_DIM, u8g2_font_helvR10_tf);
      drawText(row.value, x0 + colW - 8, y + 17, AL_RIGHT, C_TEXT, u8g2_font_helvB10_tf);
    }
  }

  if (wx.updatedAt > 0)
  {
    struct tm t;
    localtime_r(&wx.updatedAt, &t);
    snprintf(buf, sizeof(buf), "Atualizado às %02d:%02d", t.tm_hour, t.tm_min);
    drawText(buf, 8, 168, AL_LEFT, C_TEXT_DIM, u8g2_font_helvR08_tf);
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    // endereço da página de configuração na rede de casa
    snprintf(buf, sizeof(buf), "%s", WiFi.localIP().toString().c_str());
    drawText(buf, gfx->width() - 8, 168, AL_RIGHT, C_TEXT_DIM, u8g2_font_helvR08_tf);
  }
}

static void render()
{
  gfx->fillScreen(C_BG);
  drawHeader();
  if (page == 0)
    drawMainPage();
  else
    drawDetailsPage();
  drawPageDots();
  gfx->flush();
}

// ===========================================================================
// Open-Meteo
// ===========================================================================
static bool fetchWeather()
{
  if (WiFi.status() != WL_CONNECTED)
    return false;

  String url =
      "http://api.open-meteo.com/v1/forecast"
      "?latitude=" + String(cfg.lat, 4) + "&longitude=" + String(cfg.lon, 4) +
      "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
      "precipitation,weather_code,pressure_msl,wind_speed_10m,wind_direction_10m"
      "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
      "precipitation_probability_max,sunrise,sunset,uv_index_max"
      "&timezone=auto&forecast_days=4&wind_speed_unit=kmh";

  statusLed(0, 0, STATUS_LED_LEVEL);
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.useHTTP10(true); // evita "chunked transfer encoding"
  if (!http.begin(url))
  {
    Serial.println("[wx] http.begin falhou");
    return false;
  }
  int status = http.GET();
  if (status != HTTP_CODE_OK)
  {
    Serial.printf("[wx] HTTP %d\n", status);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err)
  {
    Serial.printf("[wx] JSON: %s\n", err.c_str());
    return false;
  }

  // fuso horário da cidade (timezone=auto)
  if (!doc["utc_offset_seconds"].isNull())
    updateUtcOffset(doc["utc_offset_seconds"].as<int32_t>());

  JsonObject cur = doc["current"];
  wx.temp = cur["temperature_2m"] | NAN;
  wx.feels = cur["apparent_temperature"] | NAN;
  wx.humidity = cur["relative_humidity_2m"] | -1;
  wx.isDay = (cur["is_day"] | 1) == 1;
  wx.precip = cur["precipitation"] | NAN;
  wx.code = cur["weather_code"] | -1;
  wx.pressure = cur["pressure_msl"] | NAN;
  wx.wind = cur["wind_speed_10m"] | NAN;
  wx.windDir = cur["wind_direction_10m"] | 0;

  JsonObject daily = doc["daily"];
  for (int i = 0; i < 4; i++)
  {
    DayForecast &d = wx.days[i];
    d.code = daily["weather_code"][i] | -1;
    d.tmax = daily["temperature_2m_max"][i] | NAN;
    d.tmin = daily["temperature_2m_min"][i] | NAN;
    d.pop = daily["precipitation_probability_max"][i] | -1;
    d.uv = daily["uv_index_max"][i] | NAN;
    const char *date = daily["time"][i] | ""; // "AAAA-MM-DD"
    int y, m, dd;
    if (sscanf(date, "%d-%d-%d", &y, &m, &dd) == 3)
      d.wday = dayOfWeek(y, m, dd);
  }
  const char *sr = daily["sunrise"][0] | ""; // "AAAA-MM-DDTHH:MM"
  const char *ss = daily["sunset"][0] | "";
  if (strlen(sr) >= 16)
    snprintf(wx.sunrise, sizeof(wx.sunrise), "%.5s", sr + 11);
  if (strlen(ss) >= 16)
    snprintf(wx.sunset, sizeof(wx.sunset), "%.5s", ss + 11);

  wx.valid = true;
  wx.updatedAt = time(nullptr);
  Serial.printf("[wx] ok: %.1f°C, código %d, umidade %d%%\n", wx.temp, wx.code, wx.humidity);

  // pisca verde rapidamente
  statusLed(0, STATUS_LED_LEVEL, 0);
  delay(150);
  statusLed(0, 0, 0);
  return true;
}

// ===========================================================================
// Wi-Fi / hora
// ===========================================================================
// Tenta conectar na rede salva, mostrando o progresso no display.
// Retorna false se não há rede salva ou se não conectou em 20 s.
static bool connectWiFi()
{
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (cfg.ssid.length() == 0)
    return false;

  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  statusLed(0, 0, STATUS_LED_LEVEL);

  char dots[8] = "";
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000)
  {
    size_t n = strlen(dots);
    if (n >= 5)
      dots[0] = '\0';
    else
    {
      dots[n] = '.';
      dots[n + 1] = '\0';
    }
    char line[48];
    snprintf(line, sizeof(line), "Conectando%s", dots);
    showMessage(line, cfg.ssid.c_str());
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED)
    return true;

  Serial.println("[wifi] falha ao conectar");
  statusLed(STATUS_LED_LEVEL, 0, 0);
  return false;
}

static void waitForTime()
{
  showMessage("Sincronizando hora...", nullptr);
  uint32_t start = millis();
  while (!timeIsValid() && millis() - start < 10000)
    delay(200);
}

static void updateBrightness()
{
  if (!timeIsValid())
  {
    setBrightness(BRIGHTNESS_DAY);
    return;
  }
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  bool night;
  if (NIGHT_START_HOUR > NIGHT_END_HOUR)
    night = t.tm_hour >= NIGHT_START_HOUR || t.tm_hour < NIGHT_END_HOUR;
  else
    night = t.tm_hour >= NIGHT_START_HOUR && t.tm_hour < NIGHT_END_HOUR;
  setBrightness(night ? BRIGHTNESS_NIGHT : BRIGHTNESS_DAY);
}

// ===========================================================================
// Botão BOOT: toque curto = próxima tela, segurar 3 s = modo de configuração
// ===========================================================================
#define BTN_NONE  0
#define BTN_SHORT 1
#define BTN_LONG  2

static int readButton()
{
  static bool down = false, longFired = false;
  static uint32_t downAt = 0, lastChange = 0;
  bool pressed = digitalRead(BTN_BOOT) == LOW;
  uint32_t now = millis();
  if (pressed != down && now - lastChange > 40)
  {
    lastChange = now;
    down = pressed;
    if (pressed)
    {
      downAt = now;
      longFired = false;
    }
    else if (!longFired)
      return BTN_SHORT;
  }
  if (down && !longFired && now - downAt >= 3000)
  {
    longFired = true;
    return BTN_LONG;
  }
  return BTN_NONE;
}

// ===========================================================================
// setup / loop
// ===========================================================================
void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("\nWeather Station - ESP32-C6-LCD-1.47");

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  pinMode(BTN_BOOT, INPUT_PULLUP);
  statusLed(0, 0, 0);

  if (!gfx->begin(40000000))
  {
    Serial.println("Erro ao iniciar o display (memória insuficiente para o framebuffer?)");
    while (true)
    {
      statusLed(STATUS_LED_LEVEL, 0, 0);
      delay(300);
      statusLed(0, 0, 0);
      delay(300);
    }
  }
  gfx->setUTF8Print(true); // acentos (é, ã, ç ...) e o símbolo °
  gfx->setTextWrap(false);

  ledcAttach(LCD_BL, 5000, 8);
  setBrightness(BRIGHTNESS_DAY);

  showMessage("Iniciando...", nullptr);
  loadConfig();
  applyTimezone();

  // Inicializa o Wi-Fi (e a pilha TCP/IP) ANTES de qualquer servidor de rede:
  // no core 3.x, abrir um servidor antes disso faz o ESP32 reiniciar.
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  setupWebServer();

  if (connectWiFi())
  {
    wasConnected = true;
    onWifiConnected();
    waitForTime();
    showMessage("Buscando previsão...", nullptr);
    if (fetchWeather())
      nextFetchDelay = UPDATE_INTERVAL_SECS * 1000UL;
    else
    {
      statusLed(STATUS_LED_LEVEL, 0, 0);
      nextFetchDelay = 60 * 1000UL;
    }
    lastFetchMs = millis();
    fetchNow = false;
  }
  else
  {
    // sem rede salva, ou ela não respondeu: abre o modo de configuração
    // (se a rede salva voltar e ninguém usar o portal, ele fecha sozinho)
    startPortal(cfg.ssid.length() > 0);
  }

  updateBrightness();
  lastPageChange = millis();
  needRedraw = true;
}

void loop()
{
  uint32_t nowMs = millis();

  webLoop();

  // Conexão / reconexão do Wi-Fi
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected)
    onWifiConnected();
  if (connected != wasConnected)
    needRedraw = true;
  wasConnected = connected;
  if (!connected && !staConnecting && cfg.ssid.length() &&
      nowMs - lastReconnect > (portalActive ? 30000UL : 15000UL))
  {
    lastReconnect = nowMs;
    Serial.println("[wifi] reconectando...");
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  }

  // Atualização periódica da previsão
  if (connected && !staConnecting &&
      (fetchNow || lastFetchMs == 0 || nowMs - lastFetchMs >= nextFetchDelay))
  {
    fetchNow = false;
    lastFetchMs = nowMs;
    if (fetchWeather())
      nextFetchDelay = UPDATE_INTERVAL_SECS * 1000UL;
    else
    {
      statusLed(STATUS_LED_LEVEL, 0, 0);
      nextFetchDelay = 60 * 1000UL; // tenta de novo em 1 minuto
    }
    needRedraw = true;
  }

  // Botão BOOT
  int btn = readButton();
  if (btn == BTN_LONG)
  {
    if (portalActive)
      stopPortal();
    else
      startPortal(false);
  }

  // Modo de configuração: tela própria, atualizada a cada segundo
  if (portalActive)
  {
    static uint32_t lastPortalDraw = 0;
    if (needRedraw || nowMs - lastPortalDraw > 1000)
    {
      needRedraw = false;
      lastPortalDraw = nowMs;
      drawPortalScreen();
    }
    delay(5);
    return;
  }

  // Troca de página: botão ou automática
  if (btn == BTN_SHORT)
  {
    page = (page + 1) % NUM_PAGES;
    lastPageChange = nowMs;
    needRedraw = true;
  }
#if PAGE_INTERVAL_MS > 0
  if (nowMs - lastPageChange >= PAGE_INTERVAL_MS)
  {
    page = (page + 1) % NUM_PAGES;
    lastPageChange = nowMs;
    needRedraw = true;
  }
#endif

  // Relógio: redesenha quando o minuto muda
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  if (t.tm_min != lastMinute)
  {
    lastMinute = t.tm_min;
    updateBrightness();
    needRedraw = true;
  }

  if (needRedraw)
  {
    needRedraw = false;
    render();
  }

  delay(20);
}
