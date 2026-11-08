/*
 * ============================================================================
 *  Weather Station - ESP32-C6-LCD-1.47 (Waveshare)
 *  Configurações do usuário
 * ============================================================================
 *  Adaptação do projeto ThingPulse "esp8266-weather-station-color"
 *  (https://github.com/ThingPulse/esp8266-weather-station-color) para a placa
 *  Waveshare ESP32-C6 com LCD IPS 1.47" (ST7789, 172 x 320).
 * ============================================================================
 */
#pragma once

// ---------------------------------------------------------------------------
// Wi-Fi (o ESP32-C6 só conecta em redes 2.4 GHz)
// ---------------------------------------------------------------------------
#define WIFI_SSID "SUA_REDE_WIFI"
#define WIFI_PASS "SUA_SENHA_WIFI"

// ---------------------------------------------------------------------------
// Localização
// Pegue a latitude/longitude da sua cidade no Google Maps (botão direito no
// mapa -> clique nas coordenadas para copiar).
// ---------------------------------------------------------------------------
#define LOCATION_NAME "Sao Paulo"   // nome exibido no topo da tela (UTF-8 ok: "São Paulo")
#define LATITUDE      "-23.5505"
#define LONGITUDE     "-46.6333"

// ---------------------------------------------------------------------------
// Fuso horário (formato POSIX TZ)
//   Brasília / São Paulo / Rio / BH / Sul ....... "<-03>3"
//   Manaus / Cuiabá / Campo Grande .............. "<-04>4"
//   Rio Branco .................................. "<-05>5"
//   Fernando de Noronha ......................... "<-02>2"
//   Lisboa ...................................... "WET0WEST,M3.5.0/1,M10.5.0"
// ---------------------------------------------------------------------------
#define TIMEZONE "<-03>3"

#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "a.st1.ntp.br"

// ---------------------------------------------------------------------------
// Atualização dos dados de tempo (o Open-Meteo é gratuito e não precisa de
// chave de API; não exagere na frequência).
// ---------------------------------------------------------------------------
#define UPDATE_INTERVAL_SECS (10 * 60)  // 10 minutos

// ---------------------------------------------------------------------------
// Telas
// ---------------------------------------------------------------------------
// Tempo (ms) em cada tela antes de passar para a próxima. 0 = não alterna
// sozinho (troca apenas pelo botão BOOT).
#define PAGE_INTERVAL_MS 10000

// Orientação do display: 0 = USB-C para cima, 2 = USB-C para baixo (180°)
#define DISPLAY_ROTATION 0

// ---------------------------------------------------------------------------
// Brilho do backlight (0..255) e modo noturno
// ---------------------------------------------------------------------------
#define BRIGHTNESS_DAY    200
#define BRIGHTNESS_NIGHT  25
#define NIGHT_START_HOUR  22  // a partir das 22:00 ...
#define NIGHT_END_HOUR    7   // ... até as 07:00 usa BRIGHTNESS_NIGHT

// ---------------------------------------------------------------------------
// LED RGB onboard (WS2812 no GPIO8) como indicador de status
//   azul = conectando / atualizando, verde = ok (pisca), vermelho = erro
// ---------------------------------------------------------------------------
#define USE_STATUS_LED   1
#define STATUS_LED_LEVEL 12   // intensidade máxima de cada cor (0..255)
