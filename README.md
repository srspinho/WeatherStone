# Weather Station – Waveshare ESP32-C6-LCD-1.47

Estação de previsão do tempo para a placa **Waveshare ESP32-C6-LCD-1.47**
(ESP32-C6 + display IPS 1.47" ST7789), usando o display **na horizontal (320 × 172)**, inspirada no projeto
[ThingPulse esp8266-weather-station-color](https://github.com/ThingPulse/esp8266-weather-station-color).

## O que muda em relação ao projeto original

| Original (ESP8266)                     | Esta versão (ESP32-C6)                                      |
|----------------------------------------|-------------------------------------------------------------|
| ILI9341 240×320 + touch XPT2046        | ST7789 172×320 integrado na placa                           |
| Mini Grafx (paleta de 4/16 cores)      | GFX Library for Arduino com framebuffer de 16 bits (sem flicker) |
| OpenWeatherMap (precisa de chave API)  | **Open-Meteo** (gratuito, sem cadastro e sem chave)          |
| Ícones em bitmap                       | Ícones vetoriais coloridos, desenhados em qualquer tamanho  |
| Touch para trocar de tela              | Botão **BOOT** + troca automática de telas                  |
| Wi-Fi e cidade fixos no código         | **Configuração pelo celular** (escolha da rede e busca de cidades) |
| —                                      | Brilho reduzido à noite, LED RGB de status, textos em PT-BR |

## Telas

1. **Agora** – relógio e data no topo; à esquerda ícone, temperatura, máx/mín,
   descrição, umidade e vento; à direita a previsão dos próximos 3 dias
   (ícone, máx/mín e chance de chuva).
2. **Próximas horas** – gráfico das próximas 12 horas: linha de temperatura,
   barras com a chance de chuva, ícone do tempo a cada 3 horas e faixas mais
   escuras nas horas da noite.
3. **Detalhes** – duas colunas com sensação térmica, umidade, vento com direção,
   pressão, chance de chuva, índice UV, nascer e pôr do sol, e o horário da última atualização.

As telas alternam a cada 10 s (configurável) e também podem ser trocadas com um toque no botão **BOOT**.
Um ponto vermelho no canto superior direito indica que os dados estão desatualizados.

## Configuração pelo celular (Wi-Fi e cidade)

Não é preciso editar o código para trocar de rede ou de cidade.

1. Na primeira vez que a estação liga (ou quando ela não consegue conectar na
   rede salva), ou **segurando o botão BOOT por 3 segundos**, ela entra no
   **modo de configuração** e o display mostra as instruções.
2. No celular, conecte na rede Wi-Fi **`WeatherStation`**. A página de
   configuração abre sozinha. Se não abrir, acesse `http://192.168.4.1`.
3. Toque na sua rede, digite a senha e toque em **Conectar**.
4. Toque em **Escolher cidade**, digite o nome (ex.: *Campinas*) e escolha
   na lista. Só aparecem cidades do Brasil. O fuso horário é ajustado sozinho.
5. Toque em **Concluir configuração**.

Depois disso, a mesma página fica disponível na sua rede em
**http://weatherstation.local**, ou pelo IP que aparece no rodapé da tela
"Detalhes". Por ela dá para trocar a cidade a qualquer momento.

Tudo fica gravado na memória flash da placa e continua salvo quando você
desliga a placa ou envia o sketch de novo.

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
   - *Partition Scheme*: **Huge APP (3MB No OTA/1MB SPIFFS)** (com a partição padrão o sketch não cabe: "Sketch too big")
4. Não é preciso editar nada: Wi-Fi e cidade são configurados pelo celular (veja acima).
5. Carregue o sketch. Se a placa não for detectada, segure **BOOT**, aperte **RST** e solte **BOOT**.

## Personalização

Tudo fica em `WeatherStation_ESP32C6/settings.h`:

- `UPDATE_INTERVAL_SECS` – intervalo de atualização da previsão (padrão 10 min)
- `PAGE_INTERVAL_MS` – tempo em cada tela (0 = só troca pelo botão)
- `DISPLAY_ROTATION` – `1` ou `3` (horizontal); se a imagem ficar de cabeça para baixo, troque um pelo outro
- `BRIGHTNESS_DAY`, `BRIGHTNESS_NIGHT`, `NIGHT_START_HOUR`, `NIGHT_END_HOUR`
- `USE_STATUS_LED` – liga/desliga o LED RGB de status
- `AP_NAME` / `AP_PASSWORD` – nome e senha da rede do modo de configuração
- `DEFAULT_CITY`, `DEFAULT_LAT`, `DEFAULT_LON` – cidade usada antes da primeira configuração

Cores ficam no topo de `WeatherStation_ESP32C6/weather_icons.h`.

## Créditos

Ideia e layout geral: [ThingPulse](https://thingpulse.com) – projeto original sob licença MIT.
Dados de tempo: [Open-Meteo.com](https://open-meteo.com) (CC BY 4.0).
