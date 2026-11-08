/*
 * ============================================================================
 *  Configuração pelo celular: Wi-Fi e cidade
 * ============================================================================
 *  - Modo de configuração (portal): a estação cria a rede AP_NAME e um DNS
 *    "captive" faz a página abrir sozinha no celular (endereço 192.168.4.1).
 *  - A mesma página também fica disponível na rede de casa, em
 *    http://weatherstation.local ou pelo IP da estação.
 *  - Wi-Fi, cidade e fuso ficam gravados na flash (Preferences / NVS).
 *  - A busca de cidades usa a API de geocodificação do Open-Meteo
 *    (somente cidades do Brasil).
 * ============================================================================
 */

// ===========================================================================
// Configuração persistente
// ===========================================================================
static void loadConfig()
{
  prefs.begin("wxcfg", false);
  cfg.ssid = prefs.getString("ssid", WIFI_SSID);
  cfg.pass = prefs.getString("pass", WIFI_PASS);
  cfg.city = prefs.getString("city", DEFAULT_CITY);
  cfg.uf = prefs.getString("uf", DEFAULT_UF);
  cfg.lat = prefs.getFloat("lat", DEFAULT_LAT);
  cfg.lon = prefs.getFloat("lon", DEFAULT_LON);
  cfg.utcOffset = prefs.getInt("utc", DEFAULT_UTC_OFFSET);
  Serial.printf("[cfg] rede '%s', cidade %s-%s (%.4f, %.4f), UTC%+ld s\n", cfg.ssid.c_str(),
                cfg.city.c_str(), cfg.uf.c_str(), cfg.lat, cfg.lon, (long)cfg.utcOffset);
}

static void applyTimezone()
{
  char tz[24];
  buildTz(cfg.utcOffset, tz, sizeof(tz));
  setenv("TZ", tz, 1);
  tzset();
}

// Chamado pelo fetchWeather com o "utc_offset_seconds" da cidade
static void updateUtcOffset(int32_t secs)
{
  if (secs == cfg.utcOffset)
    return;
  cfg.utcOffset = secs;
  prefs.putInt("utc", secs);
  applyTimezone();
  needRedraw = true;
}

// Executado quando o Wi-Fi conecta (no boot ou após nova configuração)
static void onWifiConnected()
{
  Serial.printf("[wifi] conectado a '%s', IP %s\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str());
  char tz[24];
  buildTz(cfg.utcOffset, tz, sizeof(tz));
  configTzTime(tz, NTP_SERVER_1, NTP_SERVER_2);
  if (!mdnsStarted && MDNS.begin(HOSTNAME))
  {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;
  }
  fetchNow = true;
  needRedraw = true;
}

// ===========================================================================
// Lista de redes Wi-Fi
// ===========================================================================
static void scanNetworks()
{
  Serial.println("[wifi] procurando redes...");
  int n = WiFi.scanNetworks();
  scanHtml = "";
  if (n <= 0)
  {
    scanHtml = F("<p class=\"muted\">Nenhuma rede encontrada. Toque em \"Atualizar lista\".</p>");
    WiFi.scanDelete();
    return;
  }

  // ordena por sinal (mais forte primeiro)
  std::vector<int> idx;
  for (int i = 0; i < n; i++)
    idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [](int a, int b)
            { return WiFi.RSSI(a) > WiFi.RSSI(b); });

  std::vector<String> seen;
  for (int i : idx)
  {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0 || std::find(seen.begin(), seen.end(), ssid) != seen.end())
      continue;
    seen.push_back(ssid);
    int quality = constrain(2 * (WiFi.RSSI(i) + 100), 0, 100);
    bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    String e = htmlEscape(ssid);
    scanHtml += "<div class=\"net\" data-s=\"" + e + "\" onclick=\"pick(this)\"><span>" + e +
                "</span><span class=\"muted\">" + (open ? "" : "&#128274; ") + String(quality) +
                "%</span></div>";
    if (seen.size() >= 20)
      break;
  }
  WiFi.scanDelete();
}

// ===========================================================================
// Páginas
// ===========================================================================
static void sendPage(const String &body, int refreshSecs, const char *refreshUrl)
{
  String html;
  html.reserve(body.length() + 3000);
  html += FPSTR(HTML_HEAD_1);
  if (refreshSecs > 0)
    html += "<meta http-equiv=\"refresh\" content=\"" + String(refreshSecs) + ";url=" + refreshUrl + "\">";
  html += FPSTR(HTML_HEAD_2);
  html += body;
  html += FPSTR(HTML_TAIL);
  server.send(200, "text/html; charset=utf-8", html);
}

static void redirectTo(const char *url)
{
  server.sendHeader("Location", url, true);
  server.send(302, "text/plain", "");
}

static void handleRoot()
{
  String b;
  b += F("<div class=\"card\"><h2>Wi-Fi</h2>");
  if (WiFi.status() == WL_CONNECTED)
    b += "<p class=\"ok\">Conectado a <b>" + htmlEscape(WiFi.SSID()) + "</b></p><p class=\"muted\">IP " +
         WiFi.localIP().toString() + " &middot; http://" HOSTNAME ".local</p>";
  else if (cfg.ssid.length())
    b += "<p class=\"err\">Sem conexão com <b>" + htmlEscape(cfg.ssid) + "</b></p>";
  else
    b += F("<p class=\"muted\">Nenhuma rede configurada.</p>");
  b += F("<a class=\"btn\" href=\"/wifi\">Escolher rede Wi-Fi</a></div>");

  b += F("<div class=\"card\"><h2>Cidade</h2>");
  b += "<p><b>" + htmlEscape(cfg.city) + (cfg.uf.length() ? " - " + htmlEscape(cfg.uf) : String("")) +
       "</b><br><span class=\"muted\">" + String(cfg.lat, 4) + ", " + String(cfg.lon, 4) + "</span></p>";
  b += F("<a class=\"btn\" href=\"/city\">Escolher cidade</a></div>");

  if (portalActive)
    b += F("<div class=\"card\"><a class=\"btn\" href=\"/finish\">Concluir configuração</a>"
           "<p class=\"muted\">A estação sai do modo de configuração e desliga a rede "
           AP_NAME ".</p></div>");
  sendPage(b, 0, nullptr);
}

static void handleWifi()
{
  if (server.hasArg("rescan") || scanHtml.length() == 0)
    scanNetworks();

  String b;
  b += F("<div class=\"card\"><h2>1. Escolha a rede Wi-Fi</h2>");
  b += scanHtml;
  b += F("<a class=\"btn2\" href=\"/wifi?rescan=1\">&#8635; Atualizar lista</a></div>"
         "<div class=\"card\"><h2>2. Digite a senha</h2>"
         "<form method=\"post\" action=\"/wifi\">"
         "<input id=\"s\" name=\"s\" placeholder=\"Nome da rede\" required>"
         "<input id=\"p\" name=\"p\" type=\"password\" placeholder=\"Senha\">"
         "<label><input type=\"checkbox\" onclick=\"showPw(this)\"> Mostrar senha</label>"
         "<button type=\"submit\">Conectar</button></form>"
         "<p class=\"muted\">Somente redes de 2.4 GHz.</p></div>"
         "<a href=\"/\">&larr; Voltar</a>");
  sendPage(b, 0, nullptr);
}

static void handleWifiSave()
{
  String s = server.arg("s");
  s.trim();
  if (s.length() == 0)
  {
    redirectTo("/wifi");
    return;
  }
  pendingSsid = s;
  pendingPass = server.arg("p");
  connectResult = 0;
  staConnecting = true;
  staConnectStart = millis();

  String b = "<div class=\"card\"><h2>Conectando a " + htmlEscape(pendingSsid) +
             "...</h2><p class=\"muted\">Acompanhe também pelo display. "
             "Se o celular sair da rede " AP_NAME ", conecte-se nela de novo.</p></div>";
  sendPage(b, 5, "/wifi/status");
  needRedraw = true;
  // a conexão começa no loop, depois que a resposta foi enviada
}

static void handleWifiStatus()
{
  String b;
  if (staConnecting)
  {
    b = "<div class=\"card\"><h2>Conectando a " + htmlEscape(pendingSsid) + "...</h2></div>";
    sendPage(b, 3, "/wifi/status");
    return;
  }
  if (connectResult == 1)
  {
    b = "<div class=\"card\"><h2 class=\"ok\">&#10004; Conectado!</h2><p>Rede <b>" + htmlEscape(cfg.ssid) +
        "</b><br><span class=\"muted\">IP " + WiFi.localIP().toString() +
        "</span></p><a class=\"btn\" href=\"/city\">Próximo: escolher a cidade</a></div>";
  }
  else if (connectResult == 2)
  {
    b = "<div class=\"card\"><h2 class=\"err\">Não foi possível conectar</h2><p>Rede <b>" +
        htmlEscape(pendingSsid) + "</b>. Verifique a senha e se a rede é de 2.4 GHz.</p>"
        "<a class=\"btn\" href=\"/wifi\">Tentar novamente</a></div>";
  }
  else
  {
    redirectTo("/");
    return;
  }
  sendPage(b, 0, nullptr);
}

static void handleCity()
{
  String b;
  if (WiFi.status() != WL_CONNECTED)
  {
    b = F("<div class=\"card\"><h2>Escolher cidade</h2><p class=\"err\">A estação precisa estar "
          "conectada à internet para buscar cidades.</p>"
          "<a class=\"btn\" href=\"/wifi\">Configurar Wi-Fi</a></div><a href=\"/\">&larr; Voltar</a>");
    sendPage(b, 0, nullptr);
    return;
  }

  String q = server.arg("q");
  q.trim();
  b += F("<div class=\"card\"><h2>Escolher cidade</h2><form method=\"get\" action=\"/city\">");
  b += "<input name=\"q\" placeholder=\"Ex.: Campinas\" value=\"" + htmlEscape(q) + "\" required autofocus>";
  b += F("<button type=\"submit\">Buscar</button></form></div>");

  if (q.length())
  {
    b += F("<div class=\"card\"><h2>Resultados</h2>");
    String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + urlEncode(q) +
                 "&count=10&language=pt&format=json&countryCode=BR";
    WiFiClientSecure client;
    client.setInsecure(); // apenas leitura de dados públicos
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(10000);
    http.useHTTP10(true);
    int found = 0;
    if (http.begin(client, url))
    {
      int status = http.GET();
      if (status == HTTP_CODE_OK)
      {
        JsonDocument doc;
        if (!deserializeJson(doc, http.getString()))
        {
          for (JsonObject r : doc["results"].as<JsonArray>())
          {
            String name = r["name"] | "";
            String state = r["admin1"] | "";
            float lat = r["latitude"] | NAN;
            float lon = r["longitude"] | NAN;
            if (name.length() == 0 || isnan(lat) || isnan(lon))
              continue;
            String uf = ufFromState(state);
            long pop = r["population"] | 0L;
            String link = "/city/save?n=" + urlEncode(name) + "&u=" + urlEncode(uf.length() ? uf : state) +
                          "&la=" + String(lat, 4) + "&lo=" + String(lon, 4);
            b += "<a class=\"btn2\" href=\"" + link + "\"><b>" + htmlEscape(name) + "</b> - " +
                 htmlEscape(state);
            if (pop > 0)
              b += "<br><span class=\"muted\">" + String(pop) + " habitantes</span>";
            b += F("</a>");
            found++;
          }
        }
      }
      else
      {
        Serial.printf("[geo] HTTP %d\n", status);
      }
      http.end();
    }
    if (found == 0)
      b += F("<p class=\"muted\">Nenhuma cidade encontrada. Confira o nome (sem a sigla do estado).</p>");
    b += F("</div>");
  }
  b += F("<a href=\"/\">&larr; Voltar</a>");
  sendPage(b, 0, nullptr);
}

static void handleCitySave()
{
  String name = server.arg("n");
  float lat = server.arg("la").toFloat();
  float lon = server.arg("lo").toFloat();
  if (name.length() == 0 || (lat == 0 && lon == 0))
  {
    redirectTo("/city");
    return;
  }
  cfg.city = name;
  cfg.uf = server.arg("u");
  cfg.lat = lat;
  cfg.lon = lon;
  prefs.putString("city", cfg.city);
  prefs.putString("uf", cfg.uf);
  prefs.putFloat("lat", cfg.lat);
  prefs.putFloat("lon", cfg.lon);
  Serial.printf("[cfg] nova cidade: %s - %s (%.4f, %.4f)\n", cfg.city.c_str(), cfg.uf.c_str(), lat, lon);

  wx.valid = false;
  fetchNow = true;
  needRedraw = true;

  String b = "<div class=\"card\"><h2 class=\"ok\">&#10004; Cidade salva</h2><p><b>" + htmlEscape(cfg.city) +
             (cfg.uf.length() ? " - " + htmlEscape(cfg.uf) : String("")) +
             "</b></p><p class=\"muted\">A previsão será atualizada em instantes.</p>";
  if (portalActive)
    b += F("<a class=\"btn\" href=\"/finish\">Concluir configuração</a>");
  else
    b += F("<a class=\"btn\" href=\"/\">Voltar</a>");
  b += F("</div>");
  sendPage(b, 0, nullptr);
}

static void handleFinish()
{
  String b = F("<div class=\"card\"><h2 class=\"ok\">&#10004; Tudo pronto!</h2>"
               "<p>A estação vai sair do modo de configuração. Você já pode desconectar o celular "
               "da rede " AP_NAME ".</p>"
               "<p class=\"muted\">Para mudar algo depois, acesse <b>http://" HOSTNAME ".local</b> "
               "na sua rede ou segure o botão BOOT por 3 segundos.</p></div>");
  sendPage(b, 0, nullptr);
  portalStopAt = millis() + 1500;
  if (portalStopAt == 0)
    portalStopAt = 1;
}

static void handleNotFound()
{
  if (portalActive)
  {
    // "captive portal": qualquer endereço leva à página de configuração
    redirectTo("http://192.168.4.1/");
    return;
  }
  server.send(404, "text/plain", "Pagina nao encontrada");
}

static void setupWebServer()
{
  server.on("/", HTTP_GET, handleRoot);
  server.on("/wifi", HTTP_GET, handleWifi);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/wifi/status", HTTP_GET, handleWifiStatus);
  server.on("/city", HTTP_GET, handleCity);
  server.on("/city/save", HTTP_GET, handleCitySave);
  server.on("/finish", HTTP_GET, handleFinish);
  server.onNotFound(handleNotFound);
  server.begin();
}

// ===========================================================================
// Modo de configuração (access point + captive portal)
// ===========================================================================
static void startPortal(bool automatic)
{
  if (portalActive)
    return;
  Serial.println("[portal] iniciando modo de configuração");
  portalActive = true;
  portalAutomatic = automatic;
  portalStartMs = millis();
  portalLastClientMs = millis();
  portalStopAt = 0;

  WiFi.mode(WIFI_AP_STA);
  WiFi.setTxPower(WIFI_TX_POWER);
  scanNetworks(); // varre antes de tentar conectar (varredura e conexão disputam o rádio)
  WiFi.softAP(AP_NAME, strlen(AP_PASSWORD) ? AP_PASSWORD : nullptr);
  dnsServer.start(53, "*", WiFi.softAPIP());
  if (cfg.ssid.length() && WiFi.status() != WL_CONNECTED)
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  statusLed(STATUS_LED_LEVEL / 2, 0, STATUS_LED_LEVEL / 2);
  needRedraw = true;
}

static void stopPortal()
{
  Serial.println("[portal] saindo do modo de configuração");
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  if (cfg.ssid.length() == 0)
  {
    // nada para conectar: desliga o rádio até o usuário reabrir a configuração
    WiFi.mode(WIFI_OFF);
  }
  else
  {
    WiFi.mode(WIFI_STA);
    applyWifiPower();
    if (WiFi.status() != WL_CONNECTED)
    {
      reconnectDelay = 5000;
      lastReconnect = millis(); // o loop assume as próximas tentativas
      WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    }
  }
  portalActive = false;
  portalStopAt = 0;
  statusLed(0, 0, 0);
  lastPageChange = millis();
  needRedraw = true;
}

// Chamado a cada volta do loop
static void webLoop()
{
  server.handleClient();
  if (portalActive)
    dnsServer.processNextRequest();

  // Nova rede escolhida pela página: inicia a conexão
  if (staConnecting && staConnectStart != 0 && millis() - staConnectStart > 300 && !staBeginDone)
  {
    staBeginDone = true;
    Serial.printf("[wifi] tentando '%s'\n", pendingSsid.c_str());
    WiFi.disconnect();
    WiFi.begin(pendingSsid.c_str(), pendingPass.c_str());
  }
  if (staConnecting && staBeginDone)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      staConnecting = false;
      staBeginDone = false;
      connectResult = 1;
      cfg.ssid = pendingSsid;
      cfg.pass = pendingPass;
      prefs.putString("ssid", cfg.ssid);
      prefs.putString("pass", cfg.pass);
      wasConnected = true;
      onWifiConnected();
    }
    else if (millis() - staConnectStart > 20000)
    {
      Serial.println("[wifi] falha na nova rede, voltando para a anterior");
      staConnecting = false;
      staBeginDone = false;
      connectResult = 2;
      WiFi.disconnect();
      if (cfg.ssid.length())
        WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
      needRedraw = true;
    }
  }

  if (!portalActive)
    return;

  if (portalStopAt && (int32_t)(millis() - portalStopAt) >= 0)
  {
    stopPortal();
    return;
  }

  // Ninguém conectou na rede de configuração por PORTAL_TIMEOUT_MIN: desliga
  if (WiFi.softAPgetStationNum() > 0)
    portalLastClientMs = millis();
  if (!staConnecting && millis() - portalLastClientMs > PORTAL_TIMEOUT_MIN * 60000UL)
  {
    Serial.println("[portal] ninguém conectou; desligando a rede de configuração");
    stopPortal();
    return;
  }

  // Saída automática: a rede salva voltou e ninguém está usando o portal
  uint32_t idleLimit = portalAutomatic ? 60000UL : 600000UL;
  if (!staConnecting && cfg.ssid.length() && WiFi.status() == WL_CONNECTED &&
      WiFi.softAPgetStationNum() == 0 && millis() - portalStartMs > idleLimit)
    stopPortal();
}

// Tela mostrada no display durante o modo de configuração
static void drawPortalScreen()
{
  const int16_t W = gfx->width();
  gfx->fillScreen(C_BG);
  drawText("Configuração", 8, 24, AL_LEFT, C_ACCENT, u8g2_font_helvB14_tf);
  drawText("BOOT 3s: sair", W - 8, 22, AL_RIGHT, C_TEXT_DIM, u8g2_font_helvR08_tf);
  gfx->drawFastHLine(8, 33, W - 16, C_PANEL_LINE);

  drawText("1. No celular, conecte na rede Wi-Fi:", 8, 52, AL_LEFT, C_TEXT, u8g2_font_helvR10_tf);
  int16_t x = drawText(AP_NAME, 22, 73, AL_LEFT, C_TEXT, u8g2_font_helvB14_tf);
  if (strlen(AP_PASSWORD))
  {
    char buf[48];
    snprintf(buf, sizeof(buf), "senha: %s", AP_PASSWORD);
    drawText(buf, x + 10, 72, AL_LEFT, C_TEXT_DIM, u8g2_font_helvR08_tf);
  }
  drawText("2. A página abre sozinha. Se não abrir,", 8, 96, AL_LEFT, C_TEXT, u8g2_font_helvR10_tf);
  drawText("acesse http://192.168.4.1", 22, 114, AL_LEFT, C_TEXT, u8g2_font_helvB10_tf);

  gfx->fillRoundRect(4, 126, W - 8, 42, 8, C_PANEL);
  char line[64];
  uint16_t color = C_TEXT_DIM;
  if (staConnecting)
  {
    snprintf(line, sizeof(line), "Conectando em %s...", pendingSsid.c_str());
    color = C_ACCENT;
  }
  else if (WiFi.status() == WL_CONNECTED)
  {
    snprintf(line, sizeof(line), "Wi-Fi OK: %s", WiFi.SSID().c_str());
    color = RGB565(95, 208, 104);
  }
  else if (connectResult == 2)
  {
    snprintf(line, sizeof(line), "Falha ao conectar em %s", pendingSsid.c_str());
    color = RGB565(255, 107, 107);
  }
  else if (cfg.ssid.length())
    snprintf(line, sizeof(line), "Sem conexão com %s", cfg.ssid.c_str());
  else
    snprintf(line, sizeof(line), "Aguardando configuração do Wi-Fi...");
  drawText(line, 12, 143, AL_LEFT, color, u8g2_font_helvB10_tf);
  snprintf(line, sizeof(line), "Cidade: %s%s%s", cfg.city.c_str(), cfg.uf.length() ? " - " : "",
           cfg.uf.c_str());
  drawText(line, 12, 161, AL_LEFT, C_TEXT_DIM, u8g2_font_helvR08_tf);
  gfx->flush();
}
