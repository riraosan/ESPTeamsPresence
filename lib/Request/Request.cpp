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

#include <memory>
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
#include <esp32-hal-log.h>

#include <secrets.h>
#include <config.h>
#include <filter.h>
#include <Request.h>

extern DNSServer        dnsServer;
extern WebServer        server;
extern IotWebConf       iotWebConf;
extern HTTPUpdateServer httpUpdater;

String  user_code   = "";
String  device_code = "";
uint8_t interval    = 5;

extern unsigned int expires;
extern char         paramClientIdValue[];
extern char         paramTenantValue[];
extern char         paramPollIntervalValue[];
extern char         paramNumLedsValue[];

extern IotWebConfParameter paramClientId;
extern IotWebConfParameter paramTenant;
extern IotWebConfParameter paramPollInterval;
extern IotWebConfParameter paramNumLeds;

extern String access_token;
extern String refresh_token;
extern String id_token;

extern uint8_t       state;
extern uint8_t       laststate;
extern unsigned long tsPolling;

extern WS2812FX ws2812fx;
extern int      numberLeds;

extern StaticJsonDocument<200> loginFilter;         //初回ログインに使用
extern StaticJsonDocument<200> tokenFilter;         //トークン取得に使用
extern StaticJsonDocument<200> refleshtokenFilter;  //トークン再取得に使用
extern StaticJsonDocument<200> presenceFilter;      //在籍情報取得時に使用

String  availability = "";
String  activity     = "";
uint8_t retries      = 0;
byte    lastIotWebConfState;

int getTokenLifetime() {
  return (expires - millis()) / 1000;
}

void removeContext() {
  SPIFFS.remove(CONTEXT_FILE);
  log_d("removeContext() - Success");
}

/**
 * API request handler
 */
bool requestJsonApi(JsonDocument& doc, ARDUINOJSON_NAMESPACE::Filter filter, String url, String payload, String type, bool sendAuth) {
  std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure);

#ifndef DISABLECERTCHECK
  if (url.indexOf("graph.microsoft.com") > -1) {
    client->setCACert(rootCACertificateGraph);
  } else {
    client->setCACert(rootCACertificateLogin);
  }
#endif

  HTTPClient https;

  if (https.begin(*client, url)) {  // HTTPS
    https.setConnectTimeout(10000);
    https.setTimeout(10000);
    https.useHTTP10(true);

    // Send auth header?
    if (sendAuth) {
      String header = "Bearer " + access_token;
      https.addHeader("Authorization", header);
      log_i("[HTTPS] Auth token valid for %d s.", getTokenLifetime());
    }

    // Start connection and send HTTP header
    int httpCode = 0;
    if (type == "POST") {
      httpCode = https.POST(payload);
    } else {
      httpCode = https.GET();
    }

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      // log_i("[HTTPS] Method: %s, Response code: %d", type.c_str(), httpCode);

      // File found at server (HTTP 200, 301), or HTTP 400 with response payload
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_BAD_REQUEST) {
        // Parse JSON data
        DeserializationError error = deserializeJson(doc, https.getStream(), filter);

        // serializeJsonPretty(doc, Serial);
        // Serial.println();

        if (error) {
          log_e("deserializeJson() failed: %s", error.c_str());
          https.end();
          return false;
        } else {
          log_i("deserializeJson() Success: %s", error.c_str());
          https.end();
          return true;
        }
      } else {
        log_e("[HTTPS] Other HTTP code: %d", httpCode);
        https.end();
        return false;
      }
    } else {
      log_e("[HTTPS] Request failed: %s", https.errorToString(httpCode).c_str());
      https.end();
      return false;
    }
  } else {
    log_e("[HTTPS] can't begin().");
    return false;
  }

  return false;
}

/**
 * Handle web requests
 */

// Requests to /
void handleRoot() {
  log_d("handleRoot()");
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal()) {
    return;
  }

  // <link href = "https://fonts.googleapis.com/css?family=Press+Start+2P" rel = "stylesheet">
  //     <link href = "https://unpkg.com/nes.css@2.3.0/css/nes.min.css" rel = "stylesheet" />

  String response = R"(
<!DOCTYPE html>
<html lang="en">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no"/>
<link href="https://unpkg.com/nes.css@2.3.0/css/nes.min.css" rel="stylesheet" />
<style type="text/css">
  body {padding:3.5rem}
  .ml-s {margin-left:1.0rem}
  .mt-s {margin-top:1.0rem}
  .mt {margin-top:3.5rem}
  #dialog-devicelogin{max-width : 800px }
</style>
<script>

function closeDeviceLoginModal() {
  document.getElementById('dialog-devicelogin').close();
}

function performClearSettings() {
  fetch('/api/clearSettings').then(r => r.json()).then(data => {
    console.log('clearSettings', data);
    document.getElementById('dialog-clearsettings').close();
    document.getElementById('dialog-clearsettings-result').showModal();
  });
}

function openDeviceLoginModal() {
  fetch('/api/startDevicelogin').then(r => r.json()).then(data => {
    console.log('startDevicelogin', data);
    if (data && data.user_code) {
      document.getElementById('btn_open').href         = data.verification_uri;
      document.getElementById('lbl_message').innerText = data.message;
      document.getElementById('code_field').value      = data.user_code;
    }
    document.getElementById('dialog-devicelogin').showModal();
  });
}

</script>
<title>ESP32 teams presence</title>
</head>
<body>
<h2>ESP32 Teams Presence - Ver.__VERSION__ </h2>

<section class="mt"><div class="nes-balloon from-left">
__MESSAGE__
__BUTTON__

<dialog class="nes-dialog is-rounded" id="dialog-devicelogin">
<p class="title">Start device login</p>
<p id="lbl_message"></p>
<input type="text" id="code_field"class="nes-input" disabled >
<menu class="dialog-menu">
<button id="btn_close" class="nes-btn" onclick="closeDeviceLoginModal();">Close</button>
<a class="nes-btn is-primary ml-s" id="btn_open" href="https://microsoft.com/devicelogin" target="_blank">Open device login</a>
</menu>
</dialog>

</section>
<div class="nes-balloon from-left mt">
Go to <a href="config">configuration page</a> to change settings.
</div>
<section class="nes-container with-title"><h3 class="title">Current settings</h3>
<div class=" nes-field mt-s "><label for=" name_field ">Client-ID</label><input type=" text " id=" name_field " class=" nes-input " disabled value=__CLIENTID__ ></div>
<div class=" nes-field mt-s "><label for=" name_field ">Tenant hostname / ID</label><input type=" text " id=" name_field " class=" nes-input " disabled value=__TENANTVALUE__ ></div>
<div class=" nes-field mt-s "><label for=" name_field ">Polling interval (sec)</label><input type=" text " id=" name_field " class=" nes-input " disabled value=__POLLVALUE__ ></div>
<div class=" nes-field mt-s "><label for=" name_field ">Number of LEDs</label><input type=" text " id=" name_field " class=" nes-input " disabled value=__LEDSVALUE__ ></div>
</section>

<section class="nes-container with-title mt"><h3 class="title">Memory usage</h3>
<div>Sketch: __SKETCHSIZE__ of __FREESKETCHSPACE__ bytes free</div>
<progress class="nes-progress" value="__SKETCHSIZE__" max="__FREESKETCHSPACE__"></progress>
<div class="mt-s">RAM: __FREEHEAP__ of 327680 bytes free</div>
<progress class="nes-progress" value="__USEDHEAP__" max="327680"></progress>
</section>

<section class="nes-container with-title mt"><h3 class="title">Danger area</h3>
<dialog class="nes-dialog is-rounded" id="dialog-clearsettings">
<p class="title">Really clear all settings?</p>
<button class="nes-btn" onclick="document.getElementById('dialog-clearsettings').close();">Close</button>
<button class="nes-btn is-error" onclick="performClearSettings();">Clear all settings</button>
</dialog>
<dialog class="nes-dialog is-rounded" id="dialog-clearsettings-result">
<p class="title">All settings were cleared.</p>
</dialog>
<div><button type="button" class="nes-btn is-error" onclick="document.getElementById('dialog-clearsettings').showModal();">Clear all settings</button></div>
</section>

<div class="mt">
<i class=" nes-icon github "></i> Find the <a href="https://github.com/toblum/ESPTeamsPresence" target="_blank">ESPTeamsPresence</a> project on GitHub.</i>
</div>
</body>
</html>
)";

  if (strlen(paramTenantValue) == 0 || strlen(paramClientIdValue) == 0) {
    response.replace("__MESSAGE__", R"(<p class="note nes-text is-error">Some settings are missing. Go to <a href="config">configuration page</a> to complete setup.</p></div>)");
    response.replace("__BUTTON__", "");
  } else {
    if (access_token == "") {
      response.replace("__MESSAGE__", R"(<p class="note nes-text is-error">No authentication info's found, start device login flow to complete widget setup!</p></div>)");
    } else {
      response.replace("__MESSAGE__", R"(<p class="note nes-text">Device setup complete, but you can start the device login flow if you need to re-authenticate.</p></div>)");
    }

    response.replace("__BUTTON__", R"(<div><button type="button" class="nes-btn" onclick="openDeviceLoginModal();" >Start device login</button></div>)");
  }

  response.replace("__CLIENTID__", String(paramClientIdValue));
  response.replace("__TENANTVALUE__", String(paramTenantValue));
  response.replace("__POLLVALUE__", String(paramPollIntervalValue));
  response.replace("__LEDSVALUE__", String(paramNumLedsValue));

  response.replace("__VERSION__", VERSION);
  response.replace("__SKETCHSIZE__", String(ESP.getSketchSize()));
  response.replace("__FREESKETCHSPACE__", String(ESP.getFreeSketchSpace()));
  response.replace("__FREEHEAP__", String(ESP.getFreeHeap()));
  response.replace("__USEDHEAP__", String(327680 - ESP.getFreeHeap()));

  server.send(200, "text/html", response);
}

void handleGetSettings() {
  log_d("handleGetSettings()");

  const int                    capacity = JSON_OBJECT_SIZE(13);
  StaticJsonDocument<capacity> responseDoc;
  responseDoc["client_id"].set(paramClientIdValue);
  responseDoc["tenant"].set(paramTenantValue);
  responseDoc["poll_interval"].set(paramPollIntervalValue);
  responseDoc["num_leds"].set(paramNumLedsValue);

  responseDoc["heap"].set(ESP.getFreeHeap());
  responseDoc["min_heap"].set(ESP.getMinFreeHeap());
  responseDoc["sketch_size"].set(ESP.getSketchSize());
  responseDoc["free_sketch_space"].set(ESP.getFreeSketchSpace());
  responseDoc["flash_chip_size"].set(ESP.getFlashChipSize());
  responseDoc["flash_chip_speed"].set(ESP.getFlashChipSpeed());
  responseDoc["sdk_version"].set(ESP.getSdkVersion());
  responseDoc["cpu_freq"].set(ESP.getCpuFreqMHz());

  responseDoc["sketch_version"].set(VERSION);

  server.send(200, "application/json", responseDoc.as<String>());
}

// Delete EEPROM by removing the trailing sequence, remove context file
void handleClearSettings() {
  log_d("handleClearSettings()");

  for (int t = 0; t < 4; t++) {
    EEPROM.write(t, 0);
  }
  EEPROM.commit();
  removeContext();

  server.send(200, "application/json", "{\"action\": \"clear_settings\", \"error\": false}");
  ESP.restart();
}

bool formValidator() {
  log_d("Validating form.");
  boolean valid = true;

  int l1 = server.arg(paramClientId.getId()).length();
  if (l1 < 36) {
    paramClientId.errorMessage = "Please provide at least 36 characters for the client id!";
    valid                      = false;
  }

  int l2 = server.arg(paramTenant.getId()).length();
  if (l2 < 10) {
    paramTenant.errorMessage = "Please provide at least 10 characters for the tenant host / GUID!";
    valid                    = false;
  }

  int l3 = server.arg(paramPollInterval.getId()).length();
  if (l3 < 1) {
    paramPollInterval.errorMessage = "Please provide a value for the presence poll interval!";
    valid                          = false;
  }

  int l4 = server.arg(paramNumLeds.getId()).length();
  if (l4 < 1) {
    paramNumLeds.errorMessage = "Please provide a value for the number of LEDs!";
    valid                     = false;
  }

  return valid;
}

// Requests to /startDevicelogin
void handleStartDevicelogin() {
  // Only if not already started
  if (state != SMODEDEVICELOGINSTARTED) {
    log_d("handleStartDevicelogin()");

    // Request devicelogin context
    DynamicJsonDocument doc(JSON_OBJECT_SIZE(6) + 540);

    bool res = requestJsonApi(doc,
                              DeserializationOption::Filter(loginFilter),
                              "https://login.microsoftonline.com/" + String(paramTenantValue) + "/oauth2/v2.0/devicecode",
                              "client_id=" + String(paramClientIdValue) + "&scope=offline_access%20openid%20Presence.Read");

    if (res && doc.containsKey("device_code") && doc.containsKey("user_code") && doc.containsKey("interval") && doc.containsKey("verification_uri") && doc.containsKey("message")) {
      // Save device_code, user_code and interval
      device_code = doc["device_code"].as<String>();
      user_code   = doc["user_code"].as<String>();
      interval    = doc["interval"].as<unsigned int>();

      // Prepare response JSON
      DynamicJsonDocument responseDoc(JSON_OBJECT_SIZE(3));
      responseDoc["user_code"]        = doc["user_code"].as<const char*>();
      responseDoc["verification_uri"] = doc["verification_uri"].as<const char*>();
      responseDoc["message"]          = doc["message"].as<const char*>();

      // Set state, update polling timestamp
      state     = SMODEDEVICELOGINSTARTED;
      tsPolling = millis() + (interval * 1000);

      // Send JSON response
      server.send(200, "application/json", responseDoc.as<String>());
    } else {
      server.send(500, "application/json", "{\"error\": \"devicelogin_unknown_response\"}");
    }
  } else {
    server.send(409, "application/json", "{\"error\": \"devicelogin_already_running\"}");
  }
}

/**
 * SPIFFS webserver
 */
bool exists(String path) {
  bool yes  = false;
  File file = SPIFFS.open(path, "r");
  if (!file.isDirectory()) {
    yes = true;
  }
  file.close();
  return yes;
}

void handleMinimalUpload() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", "<!DOCTYPE html>\
			<html>\
			<head>\
				<title>ESP8266 Upload</title>\
				<meta charset=\"utf-8\">\
				<meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">\
				<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
			</head>\
			<body>\
				<form action=\"/fs/upload\" method=\"post\" enctype=\"multipart/form-data\">\
				<input type=\"file\" name=\"data\">\
				<input type=\"text\" name=\"path\" value=\"/\">\
				<button>Upload</button>\
				</form>\
			</body>\
			</html>");
}

void handleFileUpload() {
  File        fsUploadFile;
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    log_d("handleFileUpload Name: %s", filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename     = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
    }
    log_d("handleFileUpload Size: %d", upload.totalSize);
  }
}

void handleFileDelete() {
  if (server.args() == 0) {
    return server.send(500, "text/plain", "BAD ARGS");
  }
  String path = server.arg(0);
  log_d("handleFileDelete: %s", path);
  if (path == "/") {
    return server.send(500, "text/plain", "BAD PATH");
  }
  if (!exists(path)) {
    return server.send(404, "text/plain", "FileNotFound");
  }
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if (!server.hasArg("dir")) {
    server.send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = server.arg("dir");
  log_d("handleFileList: %s", path);

  File root     = SPIFFS.open(path);
  path          = String();
  String output = "[";
  if (root.isDirectory()) {
    File file = root.openNextFile();
    while (file) {
      if (output != "[") {
        output += ',';
      }
      output += "{\"type\":\"";
      output += (file.isDirectory()) ? "dir" : "file";
      output += "\",\"name\":\"";
      output += String(file.name()).substring(1);
      output += "\"}";
      file = root.openNextFile();
    }
  }
  output += "]";
  server.send(200, "text/json", output);
}

String getContentType(String filename) {
  if (server.hasArg("download")) {
    return "application/octet-stream";
  } else if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}

bool handleFileRead(String path) {
  log_d("handleFileRead: %s", path);
  if (path.endsWith("/")) {
    path += "index.htm";
  }
  String contentType = getContentType(path);
  String pathWithGz  = path + ".gz";
  if (exists(pathWithGz) || exists(path)) {
    if (exists(pathWithGz)) {
      path += ".gz";
    }
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
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

/*
IOTWEBCONF_STATE_NOT_CONFIGURED || IOTWEBCONF_STATE_AP_MODE

SMODEWIFICONNECTING SMODEWIFICONNECTING
SMODEWIFICONNECTED SMODEWIFICONNECTED

SMODEDEVICELOGINSTARTED

SMODEPOLLPRESENCE

SMODEREFRESHTOKEN
*/
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
    // startMDNS();
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

bool loadContext(void) {
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

// Get presence information
// user method
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

// Neopixel control
// user method
void setAnimation(uint8_t segment, uint8_t mode, uint32_t color, uint16_t speed, bool reverse) {
  uint16_t startLed, endLed = 0;

  // Support only one segment for the moment
  if (segment == 0) {
    startLed = 0;
    endLed   = numberLeds;
  }

  log_i("setAnimation: %d, %d-%d, Mode: %d, Color: %d, Speed: %d", segment, startLed, endLed, mode, color, speed);

  ws2812fx.setSegment(segment, startLed, endLed, mode, color, speed, reverse);
}

// user method...
//  - Activity
//  Available,
//  Away,
//  BeRightBack,
//  Busy,
//  DoNotDisturb,
//  InACall,
//  InAConferenceCall,
//  Inactive,
//  InAMeeting,
//  Offline,
//  OffWork,
//  OutOfOffice,
//  PresenceUnknown,
//  Presenting,
//  UrgentInterruptionsOnly
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
