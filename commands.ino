void processCommand(String command) {
  command.trim();
  command.toLowerCase();

  if (command == "help")
    cmdHelp();
  else if (command == "status")
    cmdStatus();
  else if (command == "poll")
    cmdPoll();
  // else if (command == "config")
  //   cmdConfig();
  else if (command == "ble")
    cmdBle();
  else if (command == "state")
    cmdState();
  else if (command == "version")
    cmdVersion();
  else if (command == "uptime")
    cmdUptime();
  else if (command == "restart")
    cmdRestart();
  else if (command == "log")
    cmdLog();
  else
    write_log("Unknown command. Type 'help'.");
}

void write_log(String log) {
  blinkLED(3, 50);
  Serial.println(">>>> " + log);
}

void cmdHelp() {
  Serial.println();
  Serial.println("Available commands");
  Serial.println("------------------");
  Serial.println("help");
  Serial.println("status");
  Serial.println("poll");
  Serial.println("config");
  Serial.println("ble");
  Serial.println("state");
  Serial.println("version");
  Serial.println("uptime");
  Serial.println("restart");
  Serial.println("log");
}

void cmdStatus() {
  Serial.println();
  Serial.println("==================================================");
  Serial.println("              BMS CONTROLLER STATUS");
  Serial.println("==================================================");

  // ---------- Controller ----------
  Serial.println("Controller");

  Serial.print("State           : ");
  switch (currentState) {
    case SYSTEM_BATTERY:
      Serial.println("BATTERY");
      break;

    case SYSTEM_GRID:
      Serial.println("GRID");
      break;

    default:
      Serial.println("UNKNOWN");
      break;
  }

  Serial.print("Reason          : ");
  switch (lastReason) {
    case REASON_LOW_SOC:
      Serial.println("LOW SOC");
      break;

    case REASON_LOW_VOLTAGE:
      Serial.println("LOW VOLTAGE");
      break;

    case REASON_HIGH_CURRENT:
      Serial.println("HIGH CURRENT");
      break;

    default:
      Serial.println("NONE");
      break;
  }

  Serial.println();

  // ---------- Battery 1 ----------
  Serial.println("Battery 1");

  Serial.printf("Connected       : %s\n",
                connected1 ? "Yes" : "No");

  Serial.printf("SOC             : %d %%\n", soc1);
  Serial.printf("Voltage         : %.2f V\n", voltage1);
  Serial.printf("Current         : %.2f A\n", current1);

  Serial.println();

  // ---------- Battery 2 ----------
  Serial.println("Battery 2");

  Serial.printf("Connected       : %s\n",
                connected2 ? "Yes" : "No");

  Serial.printf("SOC             : %d %%\n", soc2);
  Serial.printf("Voltage         : %.2f V\n", voltage2);
  Serial.printf("Current         : %.2f A\n", current2);

  Serial.println();

  // ---------- System ----------
  Serial.println("System");

  Serial.printf("Average SOC     : %d %%\n",
                systemData.avgSoC);

  Serial.printf("Minimum Voltage : %.2f V\n",
                systemData.minVoltage);

  Serial.printf("Net Current     : %.2f A\n",
                systemData.netCurrent);

  Serial.printf("Load Current    : %.2f A\n",
                systemData.totalAbsCurrent);

  if (systemData.netCurrent > 0.5)
    Serial.println("Direction       : CHARGING");
  else if (systemData.netCurrent < -0.5)
    Serial.println("Direction       : DISCHARGING");
  else
    Serial.println("Direction       : IDLE");

  Serial.println();

  // ---------- BLE ----------
  Serial.println("BLE");

  Serial.printf("Battery 1 Retry : %u\n", retryCount1);
  Serial.printf("Battery 2 Retry : %u\n", retryCount2);

  Serial.println();

  // ---------- WIFI ----------
  Serial.println();
  Serial.println("WiFi");

  Serial.printf("Connected       : %s\n",
                WiFi.status() == WL_CONNECTED ? "Yes" : "No");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP Address      : ");
    Serial.println(WiFi.localIP());

    Serial.printf("RSSI            : %d dBm\n", WiFi.RSSI());
  }

  // ---------- Uptime ----------
  unsigned long sec = millis() / 1000;

  uint32_t days = sec / 86400;
  sec %= 86400;

  uint8_t hours = sec / 3600;
  sec %= 3600;

  uint8_t mins = sec / 60;
  sec %= 60;

  Serial.printf("Uptime          : %luD %02u:%02u:%02lu\n",
                days,
                hours,
                mins,
                sec);

  Serial.println("==================================================");
}

void cmdPoll() {
  write_log("Manually Polling Batteries...");

  batteryUpdateMask = 0;

  if (connected1 && pWriteChar1)
    pWriteChar1->writeValue(basicInfoCmd, sizeof(basicInfoCmd), true);

  if (connected2 && pWriteChar2)
    pWriteChar2->writeValue(basicInfoCmd, sizeof(basicInfoCmd), true);
}

void cmdVersion() {
  Serial.print("Firmware : ");
  Serial.println(FW_VERSION);

  Serial.print("Compiled : ");
  Serial.println(__DATE__);
  Serial.println(__TIME__);
}

void cmdUptime() {
  unsigned long sec = millis() / 1000;

  Serial.printf("Uptime : %lu seconds\n", sec);
}

void cmdBle() {
  Serial.println();

  Serial.println("BLE Status");

  Serial.printf("Battery 1 : %s\n",
                connected1 ? "Connected" : "Disconnected");

  Serial.printf("Battery 2 : %s\n",
                connected2 ? "Connected" : "Disconnected");
}

void cmdState() {
  Serial.print("State : ");

  switch (currentState) {
    case SYSTEM_GRID:
      Serial.println("GRID");
      break;

    case SYSTEM_BATTERY:
      Serial.println("BATTERY");
      break;

    default:
      Serial.println("UNKNOWN");
  }
}

void cmdRestart() {
  Serial.println("Restarting...");

  delay(100);

  ESP.restart();
}

void cmdLog() {
  write_log("Logging data to api...");
  networkBusy = true;
  sendToGoogleSheet();
  networkBusy = false;
}
