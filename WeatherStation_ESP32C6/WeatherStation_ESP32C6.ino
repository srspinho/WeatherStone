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
 *     USB CDC On Boot: Enabled  (para ver o Serial Monitor pela USB-C)
 *
 *  Bibliotecas (Library Manager):
 *     - GFX Library for Arduino  (moononournation)   >= 1.5.0
 *     - U8g2                     (olikraus)          - só as fontes
 *     - ArduinoJson              (Benoit Blanchon)   >= 7.0
 *
 *  Configure Wi-Fi, localização e fuso horário em settings.h
 * ============================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <U8g2lib.h> // apenas para as fontes u8g2_font_*
#include <time.h>

#include "settings.h"
#include "weather_icons.h"

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

static int dayOfWeek(int y, int m, int d) // 0 = domingo (algoritmo de Sakamoto)
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
enum Align
{
  AL_LEFT,
  AL_CENTER,
  AL_RIGHT
};

static int16_t textWidth(const char *s, int16_t *xOffset = nullptr)
{
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(s, 0, 100, &x1, &y1, &w, &h);
  if (xOffset)
    *xOffset = x1;
  return (int16_t)w;
}

// Desenha texto com (x, y) = ponto de alinhamento na linha de base.
// Retorna a coordenada x logo após o texto.
static int16_t drawText(const char *s, int16_t x, int16_t y, Align align, uint16_t color,
                        const uint8_t *font)
{
  gfx->setFont(font);
  gfx->setTextColor(color);
  int16_t xo;
  int16_t w = textWidth(s, &xo);
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
  const int16_t y = 315, gap = 12;
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
// Telas
// ===========================================================================
static void showMessage(const char *line1, const char *line2 = nullptr)
{
  const int16_t W = gfx->width();
  gfx->fillScreen(C_BG);
  drawWeatherIcon(gfx, W / 2, 100, 96, 2, true, C_BG);
  drawText("Weather Station", W / 2, 180, AL_CENTER, C_TEXT, u8g2_font_helvB12_tf);
  drawText(line1, W / 2, 220, AL_CENTER, C_ACCENT, u8g2_font_helvB10_tf);
  if (line2)
    drawText(line2, W / 2, 240, AL_CENTER, C_TEXT_DIM, u8g2_font_helvR08_tf);
  gfx->flush();
}

// Cabeçalho comum: cidade, relógio grande e data
static void drawHeader()
{
  const int16_t W = gfx->width();
  char buf[32];

  drawText(LOCATION_NAME, W / 2, 12, AL_CENTER, C_TEXT_DIM, u8g2_font_helvB08_tf);

  if (timeIsValid())
  {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    drawText(buf, W / 2, 48, AL_CENTER, C_TEXT, u8g2_font_logisoso32_tn);
    snprintf(buf, sizeof(buf), "%s, %02d %s", WDAY_LONG[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
    drawText(buf, W / 2, 65, AL_CENTER, C_TEXT_DIM, u8g2_font_helvR10_tf);
  }
  else
  {
    drawText("--:--", W / 2, 48, AL_CENTER, C_TEXT_DIM, u8g2_font_logisoso32_tn);
  }

  // indicador de dados desatualizados (falhas seguidas de atualização)
  bool stale = !wx.valid || (timeIsValid() && time(nullptr) - wx.updatedAt > 3 * UPDATE_INTERVAL_SECS);
  if (stale)
    gfx->fillCircle(W - 8, 8, 3, RGB565(255, 60, 60));
  if (WiFi.status() != WL_CONNECTED)
    drawText("sem Wi-Fi", 4, 12, AL_LEFT, RGB565(255, 90, 90), u8g2_font_helvR08_tf);

  gfx->drawFastHLine(10, 73, W - 20, C_PANEL_LINE);
}

// Página 1: condições atuais + previsão para 3 dias
static void drawMainPage()
{
  const int16_t W = gfx->width();
  char buf[32];

  // Ícone grande à esquerda
  drawWeatherIcon(gfx, 44, 118, 84, wx.code, wx.isDay, C_BG);

  // Temperatura grande à direita
  if (isnan(wx.temp))
    snprintf(buf, sizeof(buf), "--");
  else
    snprintf(buf, sizeof(buf), "%ld", lroundf(wx.temp));
  gfx->setFont(u8g2_font_logisoso42_tn);
  int16_t tw = textWidth(buf);
  int16_t tx = 128 - tw / 2 - 4;
  int16_t end = drawText(buf, tx, 128, AL_LEFT, C_TEXT, u8g2_font_logisoso42_tn);
  gfx->fillCircle(end + 7, 92, 5, C_TEXT); // símbolo de grau
  gfx->fillCircle(end + 7, 92, 2, C_BG);

  // Máxima / mínima de hoje
  char tmax[12], tmin[12];
  fmtTemp(tmax, sizeof(tmax), wx.days[0].tmax);
  fmtTemp(tmin, sizeof(tmin), wx.days[0].tmin);
  gfx->setFont(u8g2_font_helvB10_tf);
  int16_t wMax = textWidth(tmax), wMin = textWidth(tmin);
  int16_t sx = 128 - (wMax + 10 + wMin) / 2;
  int16_t ex = drawText(tmax, sx, 150, AL_LEFT, C_TEMP_MAX, u8g2_font_helvB10_tf);
  drawText(tmin, ex + 10, 150, AL_LEFT, C_TEMP_MIN, u8g2_font_helvB10_tf);

  // Descrição
  drawTextFit(wmoToText(wx.code), W / 2, 180, W - 8, C_TEXT);

  // Painel: umidade / vento / chuva
  gfx->fillRoundRect(4, 189, W - 8, 42, 8, C_PANEL);
  const int16_t colW = (W - 8) / 3;
  const char *labels[] = {"Umidade", "Vento", "Chuva"};
  char values[3][16];
  if (wx.humidity >= 0)
    snprintf(values[0], 16, "%d%%", wx.humidity);
  else
    snprintf(values[0], 16, "--");
  if (!isnan(wx.wind))
    snprintf(values[1], 16, "%ld km/h", lroundf(wx.wind));
  else
    snprintf(values[1], 16, "--");
  if (wx.days[0].pop >= 0)
    snprintf(values[2], 16, "%d%%", wx.days[0].pop);
  else
    snprintf(values[2], 16, "--");
  for (int i = 0; i < 3; i++)
  {
    int16_t cx = 4 + colW * i + colW / 2;
    drawText(labels[i], cx, 204, AL_CENTER, C_TEXT_DIM, u8g2_font_helvR08_tf);
    drawText(values[i], cx, 222, AL_CENTER, C_TEXT, u8g2_font_helvB10_tf);
    if (i > 0)
      gfx->drawFastVLine(4 + colW * i, 195, 30, C_PANEL_LINE);
  }

  // Previsão: próximos 3 dias
  gfx->fillRoundRect(4, 236, W - 8, 72, 8, C_PANEL);
  for (int i = 1; i <= 3; i++)
  {
    const DayForecast &d = wx.days[i];
    int16_t cx = 4 + colW * (i - 1) + colW / 2;
    drawText(WDAY_SHORT[d.wday], cx, 251, AL_CENTER, C_ACCENT, u8g2_font_helvB10_tf);
    drawWeatherIcon(gfx, cx, 271, 32, d.code, true, C_PANEL);
    fmtTemp(tmax, sizeof(tmax), d.tmax);
    fmtTemp(tmin, sizeof(tmin), d.tmin);
    gfx->setFont(u8g2_font_helvB08_tf);
    int16_t a = textWidth(tmax), b = textWidth(tmin);
    int16_t x0 = cx - (a + 5 + b) / 2;
    int16_t e = drawText(tmax, x0, 302, AL_LEFT, C_TEMP_MAX, u8g2_font_helvB08_tf);
    drawText(tmin, e + 5, 302, AL_LEFT, C_TEMP_MIN, u8g2_font_helvB08_tf);
    if (i > 1)
      gfx->drawFastVLine(4 + colW * (i - 1), 242, 60, C_PANEL_LINE);
  }
}

// Página 2: detalhes
static void drawDetailsPage()
{
  const int16_t W = gfx->width();
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

  gfx->fillRoundRect(4, 80, W - 8, 210, 8, C_PANEL);
  for (int i = 0; i < 8; i++)
  {
    int16_t y = 80 + 26 * i;
    if (i > 0)
      gfx->drawFastHLine(12, y, W - 24, C_PANEL_LINE);
    drawText(rows[i].label, 12, y + 18, AL_LEFT, C_TEXT_DIM, u8g2_font_helvR10_tf);
    drawText(rows[i].value, W - 12, y + 18, AL_RIGHT, C_TEXT, u8g2_font_helvB10_tf);
  }

  if (wx.updatedAt > 0)
  {
    struct tm t;
    localtime_r(&wx.updatedAt, &t);
    snprintf(buf, sizeof(buf), "Atualizado às %02d:%02d", t.tm_hour, t.tm_min);
    drawText(buf, W / 2, 304, AL_CENTER, C_TEXT_DIM, u8g2_font_helvR08_tf);
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

  const char *url =
      "http://api.open-meteo.com/v1/forecast"
      "?latitude=" LATITUDE "&longitude=" LONGITUDE
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
static void connectWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
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
    showMessage(line, WIFI_SSID);
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf("[wifi] conectado, IP %s\n", WiFi.localIP().toString().c_str());
  }
  else
  {
    Serial.println("[wifi] falha ao conectar - tentando novamente em segundo plano");
    statusLed(STATUS_LED_LEVEL, 0, 0);
    showMessage("Falha no Wi-Fi", "verifique settings.h");
    delay(2000);
  }
}

static void syncTime()
{
  configTzTime(TIMEZONE, NTP_SERVER_1, NTP_SERVER_2);
  if (WiFi.status() != WL_CONNECTED)
    return;
  showMessage("Sincronizando hora...");
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
// Botão BOOT: troca de página
// ===========================================================================
static bool buttonPressed()
{
  static bool lastState = HIGH;
  static uint32_t lastChange = 0;
  bool state = digitalRead(BTN_BOOT);
  if (state != lastState && millis() - lastChange > 40)
  {
    lastChange = millis();
    lastState = state;
    return state == LOW; // borda de descida
  }
  return false;
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

  showMessage("Iniciando...");
  connectWiFi();
  syncTime();

  if (WiFi.status() == WL_CONNECTED)
  {
    showMessage("Buscando previsão...");
    if (fetchWeather())
      nextFetchDelay = UPDATE_INTERVAL_SECS * 1000UL;
    else
    {
      statusLed(STATUS_LED_LEVEL, 0, 0);
      nextFetchDelay = 60 * 1000UL;
    }
    lastFetchMs = millis();
  }

  updateBrightness();
  lastPageChange = millis();
  needRedraw = true;
}

void loop()
{
  uint32_t nowMs = millis();

  // Reconexão do Wi-Fi
  if (WiFi.status() != WL_CONNECTED && nowMs - lastReconnect > 15000)
  {
    lastReconnect = nowMs;
    Serial.println("[wifi] reconectando...");
    WiFi.reconnect();
  }

  // Atualização periódica da previsão
  if (WiFi.status() == WL_CONNECTED &&
      (lastFetchMs == 0 || nowMs - lastFetchMs >= nextFetchDelay))
  {
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

  // Troca de página: botão ou automática
  if (buttonPressed())
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
