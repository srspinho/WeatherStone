/*
 * Ícones de tempo desenhados vetorialmente (escaláveis) com primitivas da
 * Arduino_GFX. Cada ícone é projetado numa grade de 64 x 64 e escalado para o
 * tamanho pedido, então o mesmo código gera o ícone grande (tela principal) e
 * os pequenos (previsão).
 *
 * Os códigos de tempo seguem a tabela WMO usada pelo Open-Meteo:
 *   0 céu limpo | 1,2 parcialmente nublado | 3 encoberto | 45,48 neblina
 *   51-57 garoa | 61-67 chuva | 71-77 neve | 80-82 pancadas de chuva
 *   85,86 pancadas de neve | 95-99 trovoada
 */
#pragma once

#include <Arduino_GFX_Library.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Paleta
// ---------------------------------------------------------------------------
#define C_BG          RGB565(8, 14, 28)
#define C_PANEL       RGB565(22, 32, 54)
#define C_PANEL_LINE  RGB565(44, 60, 92)
#define C_TEXT        RGB565(235, 240, 248)
#define C_TEXT_DIM    RGB565(140, 155, 180)
#define C_ACCENT      RGB565(80, 180, 255)
#define C_TEMP_MAX    RGB565(255, 140, 90)
#define C_TEMP_MIN    RGB565(110, 190, 255)

#define C_SUN         RGB565(255, 196, 40)
#define C_SUN_CORE    RGB565(255, 160, 20)
#define C_MOON        RGB565(235, 232, 205)
#define C_CLOUD       RGB565(232, 238, 246)
#define C_CLOUD_SHADE RGB565(150, 162, 182)
#define C_CLOUD_DARK  RGB565(120, 128, 146)
#define C_CLOUD_DARK2 RGB565(84, 90, 106)
#define C_RAIN        RGB565(70, 160, 255)
#define C_SNOW        RGB565(225, 242, 255)
#define C_BOLT        RGB565(255, 222, 0)
#define C_FOG         RGB565(165, 172, 186)

// Alinhamento de texto (fica num .h porque o Arduino IDE gera os protótipos
// das funções do .ino antes de qualquer tipo declarado nele)
enum Align
{
  AL_LEFT,
  AL_CENTER,
  AL_RIGHT
};

enum WeatherKind
{
  WK_CLEAR,
  WK_PARTLY,
  WK_OVERCAST,
  WK_FOG,
  WK_DRIZZLE,
  WK_RAIN,
  WK_SHOWERS,
  WK_SNOW,
  WK_THUNDER,
  WK_UNKNOWN
};

static WeatherKind wmoToKind(int code)
{
  switch (code)
  {
  case 0:
    return WK_CLEAR;
  case 1:
  case 2:
    return WK_PARTLY;
  case 3:
    return WK_OVERCAST;
  case 45:
  case 48:
    return WK_FOG;
  case 51:
  case 53:
  case 55:
  case 56:
  case 57:
    return WK_DRIZZLE;
  case 61:
  case 63:
  case 65:
  case 66:
  case 67:
    return WK_RAIN;
  case 80:
  case 81:
  case 82:
    return WK_SHOWERS;
  case 71:
  case 73:
  case 75:
  case 77:
  case 85:
  case 86:
    return WK_SNOW;
  case 95:
  case 96:
  case 99:
    return WK_THUNDER;
  default:
    return WK_UNKNOWN;
  }
}

// Descrição em português (UTF-8)
static const char *wmoToText(int code)
{
  switch (code)
  {
  case 0: return "Céu limpo";
  case 1: return "Poucas nuvens";
  case 2: return "Parcialmente nublado";
  case 3: return "Nublado";
  case 45: return "Neblina";
  case 48: return "Neblina com geada";
  case 51: return "Garoa fraca";
  case 53: return "Garoa";
  case 55: return "Garoa forte";
  case 56:
  case 57: return "Garoa congelante";
  case 61: return "Chuva fraca";
  case 63: return "Chuva";
  case 65: return "Chuva forte";
  case 66:
  case 67: return "Chuva congelante";
  case 71: return "Neve fraca";
  case 73: return "Neve";
  case 75: return "Neve forte";
  case 77: return "Grãos de neve";
  case 80: return "Pancadas fracas";
  case 81: return "Pancadas de chuva";
  case 82: return "Pancadas fortes";
  case 85:
  case 86: return "Pancadas de neve";
  case 95: return "Trovoadas";
  case 96:
  case 99: return "Trovoadas c/ granizo";
  default: return "---";
  }
}

// ---------------------------------------------------------------------------
// Contexto de desenho: converte coordenadas da grade 64x64 para pixels
// ---------------------------------------------------------------------------
struct IconCtx
{
  Arduino_GFX *g;
  float x0, y0, s;
  uint16_t bg;

  int16_t X(float gx) const { return (int16_t)lroundf(x0 + gx * s); }
  int16_t Y(float gy) const { return (int16_t)lroundf(y0 + gy * s); }
  int16_t R(float gr) const
  {
    int16_t r = (int16_t)lroundf(gr * s);
    return r < 1 ? 1 : r;
  }
};

// Linha grossa com pontas arredondadas (em pixels)
static void thickLine(Arduino_GFX *g, float x0, float y0, float x1, float y1, float w, uint16_t col)
{
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  int16_t r = (int16_t)(w / 2.0f);
  if (len < 0.01f)
  {
    g->fillCircle((int16_t)x0, (int16_t)y0, r < 1 ? 1 : r, col);
    return;
  }
  float nx = -dy / len * w / 2.0f, ny = dx / len * w / 2.0f;
  g->fillTriangle(lroundf(x0 + nx), lroundf(y0 + ny), lroundf(x0 - nx), lroundf(y0 - ny),
                  lroundf(x1 + nx), lroundf(y1 + ny), col);
  g->fillTriangle(lroundf(x0 - nx), lroundf(y0 - ny), lroundf(x1 - nx), lroundf(y1 - ny),
                  lroundf(x1 + nx), lroundf(y1 + ny), col);
  if (r >= 1)
  {
    g->fillCircle(lroundf(x0), lroundf(y0), r, col);
    g->fillCircle(lroundf(x1), lroundf(y1), r, col);
  }
}

// Linha grossa em coordenadas da grade
static void gLine(const IconCtx &c, float x0, float y0, float x1, float y1, float w, uint16_t col)
{
  float pw = w * c.s;
  if (pw < 1.0f)
    pw = 1.0f;
  thickLine(c.g, c.x0 + x0 * c.s, c.y0 + y0 * c.s, c.x0 + x1 * c.s, c.y0 + y1 * c.s, pw, col);
}

static void drawSun(const IconCtx &c, float cx, float cy, float r)
{
  for (int i = 0; i < 8; i++)
  {
    float a = i * (float)M_PI / 4.0f;
    float ca = cosf(a), sa = sinf(a);
    gLine(c, cx + ca * (r + 4.0f), cy + sa * (r + 4.0f),
          cx + ca * (r + 9.0f), cy + sa * (r + 9.0f), 3.0f, C_SUN);
  }
  c.g->fillCircle(c.X(cx), c.Y(cy), c.R(r), C_SUN);
  c.g->fillCircle(c.X(cx), c.Y(cy), c.R(r * 0.72f), C_SUN_CORE);
}

static void drawMoon(const IconCtx &c, float cx, float cy, float r)
{
  c.g->fillCircle(c.X(cx), c.Y(cy), c.R(r), C_MOON);
  c.g->fillCircle(c.X(cx + r * 0.50f), c.Y(cy - r * 0.35f), c.R(r * 0.80f), c.bg);
}

// Nuvem com ~44 x 26 unidades de grade, origem no canto superior esquerdo
static void cloudShape(const IconCtx &c, float ox, float oy, float k, float grow, uint16_t col)
{
  c.g->fillCircle(c.X(ox + 12 * k), c.Y(oy + 16 * k), c.R(10 * k + grow), col);
  c.g->fillCircle(c.X(ox + 25 * k), c.Y(oy + 12 * k), c.R(12 * k + grow), col);
  c.g->fillCircle(c.X(ox + 35 * k), c.Y(oy + 17 * k), c.R(9 * k + grow), col);
  int16_t x = c.X(ox + 2 * k - grow), y = c.Y(oy + 16 * k - grow);
  int16_t w = c.X(ox + 44 * k + grow) - x, h = c.Y(oy + 26 * k + grow) - y;
  c.g->fillRoundRect(x, y, w, h, c.R(5 * k + grow), col);
}

static void drawCloud(const IconCtx &c, float ox, float oy, float k, uint16_t fill, uint16_t shade)
{
  // contorno/sombra deslocado para baixo dá sensação de volume
  cloudShape(c, ox, oy + 1.5f, k, 1.2f, shade);
  cloudShape(c, ox, oy, k, 0.0f, fill);
}

static void drawDrops(const IconCtx &c, int n, float y, float len, uint16_t col)
{
  static const float xs[] = {20, 29, 38, 47};
  for (int i = 0; i < n && i < 4; i++)
  {
    float x = xs[i] + (n == 3 ? 4.5f : 0.0f);
    float yy = y + ((i & 1) ? 4.0f : 0.0f);
    gLine(c, x, yy, x - 3.0f, yy + len, 2.6f, col);
  }
}

static void drawFlake(const IconCtx &c, float cx, float cy, float r)
{
  for (int i = 0; i < 3; i++)
  {
    float a = i * (float)M_PI / 3.0f + (float)M_PI / 2.0f;
    gLine(c, cx - cosf(a) * r, cy - sinf(a) * r, cx + cosf(a) * r, cy + sinf(a) * r, 1.8f, C_SNOW);
  }
}

static void drawBolt(const IconCtx &c)
{
  c.g->fillTriangle(c.X(38), c.Y(36), c.X(26), c.Y(51), c.X(36), c.Y(49), C_BOLT);
  c.g->fillTriangle(c.X(30), c.Y(62), c.X(41), c.Y(46), c.X(29), c.Y(48), C_BOLT);
}

/*
 * Desenha o ícone centrado em (cx, cy) com lado `size` pixels.
 * `bg` deve ser a cor do fundo onde o ícone está (usada no recorte da lua).
 */
static void drawWeatherIcon(Arduino_GFX *g, int16_t cx, int16_t cy, int16_t size,
                            int code, bool isDay, uint16_t bg)
{
  IconCtx c;
  c.g = g;
  c.s = size / 64.0f;
  c.x0 = cx - 32.0f * c.s;
  c.y0 = cy - 32.0f * c.s;
  c.bg = bg;

  switch (wmoToKind(code))
  {
  case WK_CLEAR:
    if (isDay)
      drawSun(c, 32, 32, 13);
    else
      drawMoon(c, 30, 32, 18);
    break;

  case WK_PARTLY:
    if (isDay)
      drawSun(c, 23, 22, 10);
    else
      drawMoon(c, 22, 20, 12);
    drawCloud(c, 16, 26, 1.0f, C_CLOUD, C_CLOUD_SHADE);
    break;

  case WK_OVERCAST:
    drawCloud(c, 4, 14, 0.85f, C_CLOUD_SHADE, C_CLOUD_DARK);
    drawCloud(c, 16, 26, 1.0f, C_CLOUD, C_CLOUD_SHADE);
    break;

  case WK_FOG:
    drawCloud(c, 10, 6, 1.0f, C_CLOUD_SHADE, C_CLOUD_DARK);
    gLine(c, 12, 42, 50, 42, 3.2f, C_FOG);
    gLine(c, 18, 49, 56, 49, 3.2f, C_FOG);
    gLine(c, 10, 56, 44, 56, 3.2f, C_FOG);
    break;

  case WK_DRIZZLE:
    drawCloud(c, 10, 8, 1.0f, C_CLOUD, C_CLOUD_SHADE);
    drawDrops(c, 3, 42, 5, C_RAIN);
    break;

  case WK_RAIN:
    drawCloud(c, 10, 8, 1.0f, C_CLOUD_SHADE, C_CLOUD_DARK);
    drawDrops(c, 4, 41, 10, C_RAIN);
    break;

  case WK_SHOWERS:
    if (isDay)
      drawSun(c, 44, 18, 8);
    drawCloud(c, 6, 10, 1.0f, C_CLOUD, C_CLOUD_SHADE);
    drawDrops(c, 3, 43, 9, C_RAIN);
    break;

  case WK_SNOW:
    drawCloud(c, 10, 8, 1.0f, C_CLOUD, C_CLOUD_SHADE);
    drawFlake(c, 22, 47, 4.5f);
    drawFlake(c, 34, 54, 4.5f);
    drawFlake(c, 46, 46, 4.5f);
    break;

  case WK_THUNDER:
    drawCloud(c, 10, 6, 1.0f, C_CLOUD_DARK, C_CLOUD_DARK2);
    drawDrops(c, 1, 42, 8, C_RAIN);
    drawBolt(c);
    break;

  default:
    drawCloud(c, 10, 18, 1.0f, C_CLOUD_SHADE, C_CLOUD_DARK);
    break;
  }
}
