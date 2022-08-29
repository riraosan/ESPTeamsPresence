/**
 * ESPTeamsPresence -- A standalone Microsoft Teams presence light
 *   based on ESP32 and RGB neopixel LEDs.
 *   https://github.com/toblum/ESPTeamsPresence
 *
 * Copyright (C) 2020 Tobias Blum <make@tobiasblum.de>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * modified by @riraosan.github.io
 */

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <FS.h>
#include <SPIFFS.h>

#include <IotWebConf.h>
#include <ArduinoJson.h>
#include <WS2812FX.h>

#include <ESP32_RMT_Driver.h>
#include <Request.h>
#include <config.h>
#include <filter.h>

#include <esp32-hal-log.h>

#define STRING_LEN  64
#define INTEGER_LEN 16
char paramClientIdValue[STRING_LEN];
char paramTenantValue[STRING_LEN];
char paramPollIntervalValue[INTEGER_LEN];
char paramNumLedsValue[INTEGER_LEN];

// Add parameter
IotWebConfSeparator separator         = IotWebConfSeparator();
IotWebConfParameter paramClientId     = IotWebConfParameter("Client-ID (Generic ID: 3837bbf0-30fb-47ad-bce8-f460ba9880c3)", "clientId", paramClientIdValue, STRING_LEN, "text", "e.g. 3837bbf0-30fb-47ad-bce8-f460ba9880c3", "3837bbf0-30fb-47ad-bce8-f460ba9880c3");
IotWebConfParameter paramTenant       = IotWebConfParameter("Tenant hostname / ID", "tenantId", paramTenantValue, STRING_LEN, "text", "e.g. contoso.onmicrosoft.com");
IotWebConfParameter paramPollInterval = IotWebConfParameter("Presence polling interval (sec) (default: 30)", "pollInterval", paramPollIntervalValue, INTEGER_LEN, "number", "10..300", DEFAULT_POLLING_PRESENCE_INTERVAL, "min='10' max='300' step='5'");
IotWebConfParameter paramNumLeds      = IotWebConfParameter("Number of LEDs (default: 16)", "numLeds", paramNumLedsValue, INTEGER_LEN, "number", "1..500", "16", "min='1' max='500' step='1'");

// IotWebConf
// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "ESP32MSGraph";
// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "12345678";

DNSServer dnsServer;
WebServer server(80);

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword);

String access_token  = "";
String refresh_token = "";
String id_token      = "";

// LED Task
TaskHandle_t TaskNeopixel;

unsigned int expires = 0;

extern String  user_code;
extern String  device_code;
extern uint8_t interval;

uint8_t       state     = SMODEINITIAL;
uint8_t       laststate = SMODEINITIAL;
unsigned long tsPolling = 0;

// WS2812FX
WS2812FX ws2812fx = WS2812FX(NUMLEDS, DATAPIN, NEO_GRB + NEO_KHZ800);
int      numberLeds;

ESP32_RMT_Driver blinker;

// OTA update
HTTPUpdateServer httpUpdater;

StaticJsonDocument<200> loginFilter;         //初回ログインに使用
StaticJsonDocument<200> tokenFilter;         //トークン取得に使用
StaticJsonDocument<200> refleshtokenFilter;  //トークン再取得に使用
StaticJsonDocument<200> presenceFilter;      //在籍情報取得時に使用

// Handler: Wifi connected
void onWifiConnected() {
  state = SMODEWIFICONNECTED;
}

// Config was saved
void onConfigSaved() {
  log_d("Configuration was updated.");
  ws2812fx.setLength(atoi(paramNumLedsValue));
}

// どのようなWiFiクライアントを使ってもよい、とする。
// iotWebConf - Initializing the configuration.
void initIotWeb(void) {
#ifdef DISABLECERTCHECK
  log_d("WARNING: Checking of HTTPS certificates disabled.");
#endif
#ifdef LED_BUILTIN
  iotWebConf.setStatusPin(LED_BUILTIN);
#endif
  iotWebConf.setWifiConnectionTimeoutMs(5000);
  iotWebConf.addParameter(&separator);
  iotWebConf.addParameter(&paramClientId);
  iotWebConf.addParameter(&paramTenant);
  iotWebConf.addParameter(&paramPollInterval);
  iotWebConf.addParameter(&paramNumLeds);
  // iotWebConf.setFormValidator(&formValidator);
  // iotWebConf.getApTimeoutParameter()->visible = true;
  // iotWebConf.getApTimeoutParameter()->defaultValue = "10";
  iotWebConf.setWifiConnectionCallback(&onWifiConnected);
  iotWebConf.setConfigSavedCallback(&onConfigSaved);
  iotWebConf.setupUpdateServer(&httpUpdater);
  iotWebConf.skipApStartup();
  iotWebConf.init();

  // HTTP server - Set up required URL handlers on the web server.
  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, [] { iotWebConf.handleConfig(); });
  server.on("/config", HTTP_POST, [] { iotWebConf.handleConfig(); });
  server.on("/upload", HTTP_GET, [] { handleMinimalUpload(); });
  server.on("/api/startDevicelogin", HTTP_GET, [] { handleStartDevicelogin(); });
  server.on("/api/settings", HTTP_GET, [] { handleGetSettings(); });
  server.on("/api/clearSettings", HTTP_GET, [] { handleClearSettings(); });
  server.on("/fs/delete", HTTP_DELETE, handleFileDelete);
  server.on("/fs/list", HTTP_GET, handleFileList);
  server.on(
      "/fs/upload", HTTP_POST, []() {
        server.send(200, "text/plain", "");
      },
      handleFileUpload);

  // server.onNotFound([](){ iotWebConf.handleNotFound(); });
  server.onNotFound([]() {
    iotWebConf.handleNotFound();
    if (!handleFileRead(server.uri())) {
      server.send(404, "text/plain", "FileNotFound");
    }
  });
}

/**
 * Multicore
 */
void neopixelTask(void* parameter) {
  for (;;) {
    ws2812fx.service();
    vTaskDelay(10);
  }
}

void customShow(void) {
  uint8_t* pixels = ws2812fx.getPixels();
  // numBytes is one more then the size of the ws2812fx's *pixels array.
  // the extra byte is used by the driver to insert the LED reset pulse at the end.
  uint16_t numBytes = ws2812fx.getNumBytes() + 1;
  rmt_write_sample(RMT_CHANNEL_0, pixels, numBytes, false);  // channel 0
}

void initLED(void) {
  ws2812fx.init();
  blinker.begin(RMT_CHANNEL_0, ws2812fx.getPin());

  ws2812fx.start();

  numberLeds = atoi(paramNumLedsValue);
  if (numberLeds < 1) {
    log_d("Number of LEDs not given, using 16.");
    numberLeds = NUMLEDS;
  }
  ws2812fx.setLength(numberLeds);
  ws2812fx.setCustomShow(customShow);

  // Pin neopixel logic to core 0
  xTaskCreatePinnedToCore(neopixelTask, "Neopixels", 1000, NULL, 1, &TaskNeopixel, 0);
}

/**
 * Main functions
 */
void setup() {
  // WiFi不具合対策
  pinMode(0, OUTPUT);
  digitalWrite(0, LOW);

  Serial.begin(115200);
  log_d("setup() Starting up...");

  deserializeJson(loginFilter, _loginFilter);
  deserializeJson(refleshtokenFilter, _refleshtokenFilter);
  deserializeJson(presenceFilter, _presenceFilter);

  // SPIFFS.begin() - Format if mount failed
  log_d("SPIFFS.begin()");
  if (!SPIFFS.begin(true)) {
    log_d("SPIFFS Mount Failed");
    return;
  }

  initIotWeb();
  initLED();

  log_d("setup() ready...");
}

void loop() {
  // iotWebConf - doLoop should be called as frequently as possible.
  iotWebConf.doLoop();

  statemachine();
}
