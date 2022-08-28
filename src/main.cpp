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
//#include <M5Station.h>
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

byte lastIotWebConfState;

// IotWebConf
// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "ESP32MSGraph";
// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "12345678";

DNSServer  dnsServer;
WebServer  server(80);
IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword);

String access_token  = "";
String refresh_token = "";
String id_token      = "";

String availability = "";
String activity     = "";

uint8_t retries = 0;

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

// Save context information to file in SPIFFS
void saveContext() {
  const size_t        capacity = JSON_OBJECT_SIZE(3) + 5000;
  DynamicJsonDocument contextDoc(capacity);
  contextDoc["access_token"]  = access_token.c_str();
  contextDoc["refresh_token"] = refresh_token.c_str();
  contextDoc["id_token"]      = id_token.c_str();

  File   contextFile  = SPIFFS.open(CONTEXT_FILE, FILE_WRITE);
  size_t bytesWritten = serializeJsonPretty(contextDoc, contextFile);
  contextFile.close();
  log_d("saveContext() - Success: %d", bytesWritten);
  log_d("%s", contextDoc.as<String>().c_str());
}

boolean loadContext() {
  File    file    = SPIFFS.open(CONTEXT_FILE);
  boolean success = false;

  if (!file) {
    log_d("loadContext() - No file found");
  } else {
    size_t size = file.size();
    if (size == 0) {
      log_d("loadContext() - File empty");
    } else {
      const int            capacity = JSON_OBJECT_SIZE(3) + 10000;
      DynamicJsonDocument  contextDoc(capacity);
      DeserializationError err = deserializeJson(contextDoc, file);

      if (err) {
        log_d("loadContext() - deserializeJson() failed with code: %s", err.c_str());
      } else {
        int numSettings = 0;
        if (!contextDoc["access_token"].isNull()) {
          access_token = contextDoc["access_token"].as<String>();
          numSettings++;
        }
        if (!contextDoc["refresh_token"].isNull()) {
          refresh_token = contextDoc["refresh_token"].as<String>();
          numSettings++;
        }
        if (!contextDoc["id_token"].isNull()) {
          id_token = contextDoc["id_token"].as<String>();
          numSettings++;
        }
        if (numSettings == 3) {
          success = true;
          log_d("loadContext() - Success");
          if (strlen(paramClientIdValue) > 0 && strlen(paramTenantValue) > 0) {
            log_d("loadContext() - Next: Refresh token.");
            state = SMODEREFRESHTOKEN;
          } else {
            log_d("loadContext() - No client id or tenant setting found.");
          }
        } else {
          log_e("loadContext() - ERROR Number of valid settings in file: %d, should be 3.", numSettings);
        }
        // log_d(contextDoc.as<String>());
      }
    }
    file.close();
  }

  return success;
}

void startMDNS() {
  log_d("startMDNS()");
  // Set up mDNS responder
  if (!MDNS.begin(thingName)) {
    log_d("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  // MDNS.addService("http", "tcp", 80);

  log_d("mDNS responder started: %s.local", thingName);
}

// Neopixel control
void setAnimation(uint8_t segment, uint8_t mode = 0, uint32_t color = 0, uint16_t speed = 3000, bool reverse = false) {
  uint16_t startLed, endLed = 0;

  // Support only one segment for the moment
  if (segment == 0) {
    startLed = 0;
    endLed   = numberLeds;
  }

  log_i("setAnimation: %d, %d-%d, Mode: %d, Color: %d, Speed: %d", segment, startLed, endLed, mode, color, speed);

  ws2812fx.setSegment(segment, startLed, endLed, mode, color, speed, reverse);
}

// - Activity
// Available,
// Away,
// BeRightBack,
// Busy,
// DoNotDisturb,
// InACall,
// InAConferenceCall,
// Inactive,
// InAMeeting,
// Offline,
// OffWork,
// OutOfOffice,
// PresenceUnknown,
// Presenting,
// UrgentInterruptionsOnly
void setPresenceAnimation() {
  if (activity.equals("Available")) {
    setAnimation(0, FX_MODE_STATIC, GREEN);
  }
  if (activity.equals("Away")) {
    setAnimation(0, FX_MODE_STATIC, YELLOW);
  }
  if (activity.equals("BeRightBack")) {
    setAnimation(0, FX_MODE_STATIC, ORANGE);
  }
  if (activity.equals("Busy")) {
    setAnimation(0, FX_MODE_STATIC, PURPLE);
  }
  if (activity.equals("DoNotDisturb") || activity.equals("UrgentInterruptionsOnly")) {
    setAnimation(0, FX_MODE_STATIC, PINK);
  }
  if (activity.equals("InACall")) {
    setAnimation(0, FX_MODE_BREATH, RED);
  }
  if (activity.equals("InAConferenceCall")) {
    setAnimation(0, FX_MODE_BREATH, RED, 9000);
  }
  if (activity.equals("Inactive")) {
    setAnimation(0, FX_MODE_BREATH, WHITE);
  }
  if (activity.equals("InAMeeting")) {
    setAnimation(0, FX_MODE_SCAN, RED);
  }
  if (activity.equals("Offline") || activity.equals("OffWork") || activity.equals("OutOfOffice") || activity.equals("PresenceUnknown")) {
    setAnimation(0, FX_MODE_STATIC, BLACK);
  }
  if (activity.equals("Presenting")) {
    setAnimation(0, FX_MODE_COLOR_WIPE, RED);
  }
}

/**
 * Application logic
 */

// Handler: Wifi connected
void onWifiConnected() {
  state = SMODEWIFICONNECTED;
}

// Poll for access token
void pollForToken() {
  String payload = "client_id=" + String(paramClientIdValue) + "&grant_type=urn:ietf:params:oauth:grant-type:device_code&device_code=" + device_code;

  DynamicJsonDocument responseDoc(JSON_OBJECT_SIZE(7) + 5000);

  bool res = requestJsonApi(responseDoc,
                            DeserializationOption::Filter(refleshtokenFilter),
                            "https://login.microsoftonline.com/" + String(paramTenantValue) + "/oauth2/v2.0/token",
                            payload);

  if (!res) {
    state = SMODEDEVICELOGINFAILED;
  } else if (responseDoc.containsKey("error")) {
    const char* _error             = responseDoc["error"];
    const char* _error_description = responseDoc["error_description"];

    if (strcmp(_error, "authorization_pending") == 0) {
      log_i("pollForToken() - Wating for authorization by user: %s", _error_description);
    } else {
      log_e("pollForToken() - Unexpected error: %s, %s", _error, _error_description);
      state = SMODEDEVICELOGINFAILED;
    }
  } else {
    if (responseDoc.containsKey("access_token") && responseDoc.containsKey("refresh_token") && responseDoc.containsKey("id_token")) {
      // Save tokens and expiration
      unsigned int _expires_in = responseDoc["expires_in"].as<unsigned int>();
      access_token             = responseDoc["access_token"].as<String>();
      refresh_token            = responseDoc["refresh_token"].as<String>();
      id_token                 = responseDoc["id_token"].as<String>();
      expires                  = millis() + (_expires_in * 1000);  // Calculate timestamp when token expires

      // Set state
      state = SMODEAUTHREADY;

      log_i("Set : SMODEAUTHREADY");
    } else {
      log_e("pollForToken() - Unknown response: ");
    }
  }
}

// Refresh the access token
bool refreshToken() {
  bool success = false;
  // See: https://docs.microsoft.com/de-de/azure/active-directory/develop/v1-protocols-oauth-code#refreshing-the-access-tokens
  String payload = "client_id=" + String(paramClientIdValue) + "&grant_type=refresh_token&refresh_token=" + refresh_token;
  log_d("refreshToken()");

  DynamicJsonDocument responseDoc(6144);  // from ArduinoJson Assistant

  bool res = requestJsonApi(responseDoc,
                            DeserializationOption::Filter(refleshtokenFilter),
                            "https://login.microsoftonline.com/" + String(paramTenantValue) + "/oauth2/v2.0/token",
                            payload);

  // Replace tokens and expiration
  if (res && responseDoc.containsKey("access_token") && responseDoc.containsKey("refresh_token")) {
    if (!responseDoc["access_token"].isNull()) {
      access_token = responseDoc["access_token"].as<String>();
      success      = true;
    }
    if (!responseDoc["refresh_token"].isNull()) {
      refresh_token = responseDoc["refresh_token"].as<String>();
      success       = true;
    }
    if (!responseDoc["id_token"].isNull()) {
      id_token = responseDoc["id_token"].as<String>();
    }
    if (!responseDoc["expires_in"].isNull()) {
      int _expires_in = responseDoc["expires_in"].as<unsigned int>();
      expires         = millis() + (_expires_in * 1000);  // Calculate timestamp when token expires
    }

    log_d("refreshToken() - Success");
    state = SMODEPOLLPRESENCE;
  } else {
    log_d("refreshToken() - Error:");
    // Set retry after timeout
    tsPolling = millis() + (DEFAULT_ERROR_RETRY_INTERVAL * 1000);
  }
  return success;
}

// Get presence information
void pollPresence() {
  log_d("pollPresence()");
  // See: https://github.com/microsoftgraph/microsoft-graph-docs/blob/ananya/api-reference/beta/resources/presence.md
  const size_t        capacity = JSON_OBJECT_SIZE(4) + 500;
  DynamicJsonDocument responseDoc(capacity);

  // TODO
  bool res = requestJsonApi(responseDoc,
                            DeserializationOption::Filter(presenceFilter),
                            "https://graph.microsoft.com/v1.0/me/presence",
                            "",
                            "GET",
                            true);

  if (!res) {
    state = SMODEPRESENCEREQUESTERROR;
    retries++;
    log_e("Presence request error. retry:#%d", retries);
  } else if (responseDoc.containsKey("error")) {
    const char* _error_code = responseDoc["error"]["code"];
    if (strcmp(_error_code, "InvalidAuthenticationToken")) {
      log_e("pollPresence() - Refresh needed");
      tsPolling = millis();
      state     = SMODEREFRESHTOKEN;
    } else {
      log_e("pollPresence() - Error: %s\n", _error_code);
      state = SMODEPRESENCEREQUESTERROR;
      retries++;
    }
  } else {
    log_i("success to get Presence");

    // Store presence info
    availability = responseDoc["availability"].as<String>();
    activity     = responseDoc["activity"].as<String>();
    retries      = 0;

    setPresenceAnimation();
  }
}

// Implementation of a statemachine to handle the different application states
void statemachine() {
  // Statemachine: Check states of iotWebConf to detect AP mode and WiFi Connection attempt
  byte iotWebConfState = iotWebConf.getState();
  if (iotWebConfState != lastIotWebConfState) {
    if (iotWebConfState == IOTWEBCONF_STATE_NOT_CONFIGURED || iotWebConfState == IOTWEBCONF_STATE_AP_MODE) {
      log_d("Detected AP mode");
      setAnimation(0, FX_MODE_THEATER_CHASE, WHITE);
    }
    if (iotWebConfState == IOTWEBCONF_STATE_CONNECTING) {
      log_d("WiFi connecting");
      state = SMODEWIFICONNECTING;
    }
  }
  lastIotWebConfState = iotWebConfState;

  // Statemachine: Wifi connection start
  if (state == SMODEWIFICONNECTING && laststate != SMODEWIFICONNECTING) {
    setAnimation(0, FX_MODE_THEATER_CHASE, BLUE);
  }

  // Statemachine: After wifi is connected
  if (state == SMODEWIFICONNECTED && laststate != SMODEWIFICONNECTED) {
    setAnimation(0, FX_MODE_THEATER_CHASE, GREEN);
    startMDNS();
    loadContext();
    // WiFi client
    log_d("Wifi connected, waiting for requests ...");
  }

  // Statemachine: Devicelogin started
  if (state == SMODEDEVICELOGINSTARTED) {
    // log_d("SMODEDEVICELOGINSTARTED");
    if (laststate != SMODEDEVICELOGINSTARTED) {
      setAnimation(0, FX_MODE_THEATER_CHASE, PURPLE);
      log_d("Device login failed");
    }
    if (millis() >= tsPolling) {
      pollForToken();
      tsPolling = millis() + (interval * 1000);
      log_d("pollForToken");
    }
  }

  // Statemachine: Devicelogin failed
  if (state == SMODEDEVICELOGINFAILED) {
    log_d("Device login failed");
    state = SMODEWIFICONNECTED;  // Return back to initial mode
  }

  // Statemachine: Auth is ready, start polling for presence immediately
  if (state == SMODEAUTHREADY) {
    saveContext();
    state     = SMODEPOLLPRESENCE;
    tsPolling = millis();
  }

  // Statemachine: Poll for presence information, even if there was a error before (handled below)
  if (state == SMODEPOLLPRESENCE) {
    if (millis() >= tsPolling) {
      log_i("%s", "Polling presence info ...");
      pollPresence();
      tsPolling = millis() + (atoi(paramPollIntervalValue) * 1000);
      log_i("--> Availability: %s, Activity: %s", availability.c_str(), activity.c_str());
    }

    if (getTokenLifetime() < TOKEN_REFRESH_TIMEOUT) {
      log_w("Token needs refresh, valid for %d s.", getTokenLifetime());
      state = SMODEREFRESHTOKEN;
    }
  }

  // Statemachine: Refresh token
  if (state == SMODEREFRESHTOKEN) {
    if (laststate != SMODEREFRESHTOKEN) {
      setAnimation(0, FX_MODE_THEATER_CHASE, RED);
    }
    if (millis() >= tsPolling) {
      boolean success = refreshToken();
      if (success) {
        saveContext();
      }
    }
  }

  // Statemachine: Polling presence failed
  if (state == SMODEPRESENCEREQUESTERROR) {
    if (laststate != SMODEPRESENCEREQUESTERROR) {
      retries = 0;
    }

    log_e("Polling presence failed, retry #%d.", retries);
    if (retries >= 5) {
      // Try token refresh
      state = SMODEREFRESHTOKEN;
    } else {
      state = SMODEPOLLPRESENCE;
    }
  }

  // Update laststate
  if (laststate != state) {
    laststate = state;
    log_d("======================================================================");
  }
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

/**
 * Main functions
 */
void setup() {
  // WiFi不具合対策
  // pinMode(0, OUTPUT);
  // digitalWrite(0, LOW);

  // M5.begin(true, false, false);

  // M5.Lcd.setCursor(0, 10);
  // M5.Lcd.setTextSize(3);
  // M5.Lcd.fillScreen(TFT_WHITE);
  // delay(1000);
  // M5.Lcd.fillScreen(TFT_BLACK);
  // M5.Lcd.setTextColor(TFT_WHITE);

  // M5.Lcd.print("Hello World. G0 is LOW.");

  deserializeJson(loginFilter, _loginFilter);
  deserializeJson(refleshtokenFilter, _refleshtokenFilter);
  deserializeJson(presenceFilter, _presenceFilter);

  Serial.begin(115200);
  log_d("setup() Starting up...");
  refactoring
// Serial.setDebugOutput(true);
#ifdef DISABLECERTCHECK
      log_d("WARNING: Checking of HTTPS certificates disabled.");
#endif

  // WS2812FX
  ws2812fx.init();

  blinker.begin(RMT_CHANNEL_0, ws2812fx.getPin());

  ws2812fx.start();
  setAnimation(0, FX_MODE_STATIC, WHITE);

// iotWebConf - Initializing the configuration.
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

  // WS2812FX
  numberLeds = atoi(paramNumLedsValue);
  if (numberLeds < 1) {
    log_d("Number of LEDs not given, using 16.");
    numberLeds = NUMLEDS;
  }
  ws2812fx.setLength(numberLeds);
  ws2812fx.setCustomShow(customShow);

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
      "/fs/upload", HTTP_POST, []() { server.send(200, "text/plain", ""); }, handleFileUpload);

  // server.onNotFound([](){ iotWebConf.handleNotFound(); });
  server.onNotFound([]() {
    iotWebConf.handleNotFound();
    if (!handleFileRead(server.uri())) {
      server.send(404, "text/plain", "FileNotFound");
    }
  });

  log_d("setup() ready...");

  // SPIFFS.begin() - Format if mount failed
  log_d("SPIFFS.begin()");
  if (!SPIFFS.begin(true)) {
    log_d("SPIFFS Mount Failed");
    return;
  }

  // Pin neopixel logic to core 0
  xTaskCreatePinnedToCore(
      neopixelTask,
      "Neopixels",
      1000,
      NULL,
      1,
      &TaskNeopixel,
      0);
}

void loop() {
  // iotWebConf - doLoop should be called as frequently as possible.
  iotWebConf.doLoop();

  statemachine();
}
