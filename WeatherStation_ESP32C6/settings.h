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
// Wi-Fi e cidade
//
// Normalmente NÃO é preciso editar nada aqui: na primeira vez que a estação
// liga sem configuração (ou segurando o botão BOOT por 3 segundos) ela entra
// no MODO DE CONFIGURAÇÃO, cria a rede Wi-Fi AP_NAME e abre uma página no
// celular para escolher a rede, digitar a senha e buscar a cidade.
// Depois disso a mesma página continua acessível na sua rede em
// http://weatherstation.local (ou pelo IP mostrado na tela "Detalhes").
//
// Os valores abaixo são usados só enquanto nada foi salvo pela página.
// ---------------------------------------------------------------------------
#define WIFI_SSID ""            // opcional: rede padrão (2.4 GHz)
#define WIFI_PASS ""

#define DEFAULT_CITY       "São Paulo"
#define DEFAULT_UF         "SP"
#define DEFAULT_LAT        -23.5505f
#define DEFAULT_LON        -46.6333f
#define DEFAULT_UTC_OFFSET (-3 * 3600)  // ajustado sozinho conforme a cidade

// Rede criada pela estação no modo de configuração
#define AP_NAME     "WeatherStation"
#define AP_PASSWORD ""                 // vazio = rede aberta (mín. 8 caracteres se usar)
#define HOSTNAME    "weatherstation"   // http://weatherstation.local

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

// Orientação do display (layout horizontal, 320 x 172):
//   1 ou 3 - se a imagem aparecer de cabeça para baixo, troque 1 <-> 3
#define DISPLAY_ROTATION 1

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
