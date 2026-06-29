void connectWifi() {
  logInfo("Connecting to WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 10000) {
      logError("WiFi connection timeout.");
      wifiConnected = false;
      return;
    }

    delay(500);
    Serial.print(".");
  }

  wifiConnected = true;

  logInfo("WiFi Connected.");
  Serial.print("IP : ");
  Serial.println(WiFi.localIP());
}

void checkWifi() {
  bool currentlyConnected = (WiFi.status() == WL_CONNECTED);

  if (currentlyConnected) {
    if (!wifiConnected) {
      wifiConnected = true;
      logInfo("WiFi Reconnected.");
      Serial.print("IP : ");
      Serial.println(WiFi.localIP());
    }

    return;
  }

  if (wifiConnected) {
    wifiConnected = false;
    logWarning("WiFi Lost.");
  }

  if (millis() - lastWifiRetry >= WIFI_RETRY_INTERVAL) {
    lastWifiRetry = millis();

    logInfo("Retrying WiFi...");

    WiFi.disconnect(true);
    delay(100);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      logInfo("WiFi Connected.");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiConnected = true;
      logInfo("IP Address: " + WiFi.localIP().toString());
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      wifiConnected = false;
      logWarning("WiFi Disconnected.");
      break;
  }
}

void sendToGoogleSheet() {
  if (!wifiConnected) {
    logError("WiFi not connected.");
    return;
  }

  Serial.println("========== WIFI ==========");
  Serial.print("SSID : ");
  Serial.println(WiFi.SSID());

  Serial.print("IP   : ");
  Serial.println(WiFi.localIP());

  Serial.print("RSSI : ");
  Serial.println(WiFi.RSSI());

  Serial.print("Status : ");
  Serial.println(WiFi.status());

  Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("WiFi Channel : %d\n", WiFi.channel());

  Serial.println("==========================");

  logInfo("Preparing HTTP request...");

  const char* GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycby1SD4z38ntTy8Zco--RpBMzoQBZPbrwjhz8zbDkeedbZqPbOgAeKYtxFlxOxKu364/exec";
  // const char* GOOGLE_SCRIPT_URL = "https://postman-echo.com/post";

  logInfo(String("URL: ") + GOOGLE_SCRIPT_URL);

  HTTPClient http;

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  logInfo("Opening connection...");
  bool connected = http.begin(secureClient, GOOGLE_SCRIPT_URL);

  if (!connected) {
    logError("http.begin() failed.");
    return;
  }

  http.addHeader("Content-Type", "application/json");
  http.setConnectTimeout(5000);  // Give the WiFi handshake 5 seconds of priority
  http.setReuse(true);           // Keep the connection open for faster logging later

  String jsonPayload =
    "{\"b1v\":" + String(voltage1) + ",\"b1i\":" + String(current1) + ",\"b1soc\":" + String(soc1) + ",\"b2v\":" + String(voltage2) + ",\"b2i\":" + String(current2) + ",\"b2soc\":" + String(soc2) + ",\"totalI\":" + String(systemData.netCurrent) + "}";

  logInfo("Payload:");
  Serial.println(jsonPayload);

  logInfo("Sending POST...");
  Serial.printf("Heap Before POST : %u\n", ESP.getFreeHeap());
  int httpCode = http.POST(jsonPayload);
  Serial.printf("Heap After POST  : %u\n", ESP.getFreeHeap());
  Serial.printf("Content-Type: %s\n", http.header("Content-Type").c_str());

  if (httpCode > 0) {
    String response = http.getString();
    Serial.printf("HTTP Code : %d\n", httpCode);
    Serial.println("Response:");
    Serial.println(response);
  } else {
    Serial.printf("HTTP Error : %s\n",
                  http.errorToString(httpCode).c_str());
  }

  http.end();
  Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());

  logInfo("HTTP finished.");

  delay(1000);

  Serial.printf("Heap One Second Later : %u\n", ESP.getFreeHeap());
}