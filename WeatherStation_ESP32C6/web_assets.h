/*
 * Recursos da página de configuração (HTML/CSS) e funções auxiliares.
 * Ficam num .h para não confundir o gerador de protótipos do Arduino IDE.
 */
#pragma once

#include <Arduino.h>
#include <ctype.h>

static const char HTML_HEAD_1[] PROGMEM =
    "<!DOCTYPE html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Weather Station</title>";

static const char HTML_HEAD_2[] PROGMEM = R"HTML(<style>
*{box-sizing:border-box}
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#080e1c;color:#ebf0f8;
max-width:480px;margin:0 auto;padding:16px;line-height:1.4}
h1{font-size:1.35em;margin:4px 0 12px}
h2{font-size:1.05em;margin:0 0 8px;color:#50b4ff}
.card{background:#16203a;border-radius:14px;padding:14px;margin:12px 0}
input{width:100%;padding:12px;margin:6px 0;border-radius:10px;border:1px solid #2c3c5c;
background:#0b1220;color:#ebf0f8;font-size:1em}
button,.btn{display:block;width:100%;padding:12px;margin:8px 0;border:0;border-radius:10px;
background:#2f86d8;color:#fff;font-size:1em;font-weight:600;text-align:center;text-decoration:none;cursor:pointer}
.btn2{display:block;padding:12px;margin:6px 0;border-radius:10px;background:#0b1220;
border:1px solid #2c3c5c;color:#ebf0f8;text-decoration:none}
.net{display:flex;justify-content:space-between;align-items:center;padding:11px 4px;
border-bottom:1px solid #22304e;cursor:pointer}
.net:last-child{border-bottom:0}
.muted{color:#8c9bb4;font-size:.9em}
.ok{color:#5fd068}.err{color:#ff6b6b}
label{display:flex;gap:8px;align-items:center;font-size:.95em}
label input{width:auto;margin:0}
a{color:#50b4ff}
</style></head><body><h1>&#9925; Weather Station</h1>)HTML";

static const char HTML_TAIL[] PROGMEM = R"HTML(<script>
function pick(e){document.getElementById('s').value=e.dataset.s;var p=document.getElementById('p');p.value='';p.focus();}
function showPw(c){document.getElementById('p').type=c.checked?'text':'password';}
</script></body></html>)HTML";

static String htmlEscape(const String &s)
{
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++)
  {
    char c = s[i];
    switch (c)
    {
    case '&': o += F("&amp;"); break;
    case '<': o += F("&lt;"); break;
    case '>': o += F("&gt;"); break;
    case '"': o += F("&quot;"); break;
    case '\'': o += F("&#39;"); break;
    default: o += c;
    }
  }
  return o;
}

static String urlEncode(const String &s)
{
  static const char hex[] = "0123456789ABCDEF";
  String o;
  o.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++)
  {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      o += (char)c;
    else
    {
      o += '%';
      o += hex[c >> 4];
      o += hex[c & 15];
    }
  }
  return o;
}

// Nome do estado (como vem do Open-Meteo, campo admin1) -> sigla
static const char *ufFromState(const String &state)
{
  static const char *const table[][2] = {
      {"Acre", "AC"}, {"Alagoas", "AL"}, {"Amapá", "AP"}, {"Amazonas", "AM"},
      {"Bahia", "BA"}, {"Ceará", "CE"}, {"Distrito Federal", "DF"},
      {"Espírito Santo", "ES"}, {"Goiás", "GO"}, {"Maranhão", "MA"},
      {"Mato Grosso", "MT"}, {"Mato Grosso do Sul", "MS"}, {"Minas Gerais", "MG"},
      {"Pará", "PA"}, {"Paraíba", "PB"}, {"Paraná", "PR"}, {"Pernambuco", "PE"},
      {"Piauí", "PI"}, {"Rio de Janeiro", "RJ"}, {"Rio Grande do Norte", "RN"},
      {"Rio Grande do Sul", "RS"}, {"Rondônia", "RO"}, {"Roraima", "RR"},
      {"Santa Catarina", "SC"}, {"São Paulo", "SP"}, {"Sergipe", "SE"},
      {"Tocantins", "TO"}};
  for (const auto &row : table)
    if (state.equalsIgnoreCase(row[0]))
      return row[1];
  return "";
}

// Monta a variável TZ (POSIX) a partir do deslocamento UTC em segundos.
// Ex.: -10800 -> "<-03>3"
static void buildTz(int32_t offsetSecs, char *buf, size_t n)
{
  int32_t a = offsetSecs < 0 ? -offsetSecs : offsetSecs;
  int h = a / 3600, m = (a % 3600) / 60;
  char sign = offsetSecs < 0 ? '-' : '+';
  char posix = offsetSecs < 0 ? '+' : '-'; // POSIX usa o sinal invertido
  if (m)
    snprintf(buf, n, "<%c%02d%02d>%c%d:%02d", sign, h, m, posix, h, m);
  else
    snprintf(buf, n, "<%c%02d>%c%d", sign, h, posix, h);
}
