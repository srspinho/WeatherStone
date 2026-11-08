# Weather Station – Waveshare ESP32-C6-LCD-1.47

Estação de previsão do tempo para a placa **Waveshare ESP32-C6-LCD-1.47**
(ESP32-C6 + display IPS 1.47" ST7789 de 172 × 320), inspirada no projeto
[ThingPulse esp8266-weather-station-color](https://github.com/ThingPulse/esp8266-weather-station-color).

## O que muda em relação ao projeto original

| Original (ESP8266)                     | Esta versão (ESP32-C6)                                      |
|----------------------------------------|-------------------------------------------------------------|
| ILI9341 240×320 + touch XPT2046        | ST7789 172×320 integrado na placa                           |
| Mini Grafx (paleta de 4/16 cores)      | GFX Library for Arduino com framebuffer de 16 bits (sem flicker) |
| OpenWeatherMap (precisa de chave API)  | **Open-Meteo** (gratuito, sem cadastro e sem chave)          |
| Ícones em bitmap                       | Ícones vetoriais coloridos, desenhados em qualquer tamanho  |
| Touch para trocar de tela              | Botão **BOOT** + troca automática de telas                  |
| —                                      | Brilho reduzido à noite, LED RGB de status, textos em PT-BR |

## Telas

1. **Agora** – relógio, data, ícone grande, temperatura, máx/mín do dia,
   descrição, umidade, vento, chance de chuva e previsão dos próximos 3 dias.
2. **Detalhes** – sensação térmica, umidade, vento com direção, pressão,
   chance de chuva, índice UV, nascer e pôr do sol e horário da última atualização.

As telas alternam a cada 10 s (configurável) e também podem ser trocadas pelo botão **BOOT**.
Um ponto vermelho no canto superior direito indica que os dados estão desatualizados.

## Pinagem usada (fixa na placa)

| Função            | GPIO |
|-------------------|------|
| LCD MOSI          | 6    |
| LCD SCLK          | 7    |
| LCD CS            | 14   |
| LCD DC            | 15   |
| LCD RST           | 21   |
| LCD Backlight     | 22   |
| SD MISO / SD CS   | 5 / 4 (o CS do SD é mantido em HIGH) |
| LED RGB (WS2812)  | 8    |
| Botão BOOT        | 9    |

Nenhum fio extra é necessário.

## Como compilar (Arduino IDE 2.x)

1. **Core ESP32**: em *Preferências → URLs adicionais*, adicione
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` e instale
   **esp32 by Espressif Systems** versão **3.0 ou superior** (o ESP32-C6 só é suportado a partir da 3.x).
2. **Bibliotecas** (Ferramentas → Gerenciar bibliotecas):
   - `GFX Library for Arduino` (moononournation) ≥ 1.5
   - `U8g2` (olikraus) – usada apenas pelas fontes
   - `ArduinoJson` (Benoit Blanchon) ≥ 7
3. **Placa**: `ESP32C6 Dev Module`
   - *USB CDC On Boot*: **Enabled** (para ver o Serial Monitor)
   - *Flash Size*: 4MB
   - *Partition Scheme*: Default 4MB with spiffs
4. Edite **`WeatherStation_ESP32C6/settings.h`**: Wi-Fi (rede 2.4 GHz), nome da cidade, latitude/longitude e fuso horário.
5. Carregue o sketch. Se a placa não for detectada, segure **BOOT**, aperte **RST** e solte **BOOT**.

## Personalização

Tudo fica em `WeatherStation_ESP32C6/settings.h`:

- `UPDATE_INTERVAL_SECS` – intervalo de atualização da previsão (padrão 10 min)
- `PAGE_INTERVAL_MS` – tempo em cada tela (0 = só troca pelo botão)
- `DISPLAY_ROTATION` – `0` (USB-C em cima) ou `2` (USB-C embaixo), útil conforme a montagem no organizador
- `BRIGHTNESS_DAY`, `BRIGHTNESS_NIGHT`, `NIGHT_START_HOUR`, `NIGHT_END_HOUR`
- `USE_STATUS_LED` – liga/desliga o LED RGB de status

Cores ficam no topo de `WeatherStation_ESP32C6/weather_icons.h`.

## Créditos

Ideia e layout geral: [ThingPulse](https://thingpulse.com) – projeto original sob licença MIT.
Dados de tempo: [Open-Meteo.com](https://open-meteo.com) (CC BY 4.0).
