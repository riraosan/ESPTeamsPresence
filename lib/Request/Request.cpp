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
      log_i("[HTTPS] Method: %s, Response code: %d", type.c_str(), httpCode);

      // File found at server (HTTP 200, 301), or HTTP 400 with response payload
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY || httpCode == HTTP_CODE_BAD_REQUEST) {
        // Parse JSON data
        DeserializationError error = deserializeJson(doc, https.getStream(), filter);

        serializeJsonPretty(doc, Serial);
        Serial.println();

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

  String s = R"(
  <!DOCTYPE html>
  <html lang="en">
  <head>
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no"/>
  <link href="https://fonts.googleapis.com/css?family=Press+Start+2P" rel="stylesheet">
  <link href="https://unpkg.com/nes.css@2.3.0/css/nes.min.css" rel="stylesheet" />
  <style type="text/css">
    body {padding:3.5rem}
    .ml-s {margin-left:1.0rem}
    .mt-s {margin-top:1.0rem}
    .mt {margin-top:3.5rem}
    #dialog - devicelogin{max - width : 800px }
  </style>
  <script>

  function closeDeviceLoginModal() {
    document.getElementById('dialog-devicelogin').close();
  }

  function performClearSettings() {
    fetch('/api/clearSettings').then(r = > r.json()).then(data = > {
      console.log('clearSettings', data);
      document.getElementById('dialog-clearsettings').close();
      document.getElementById('dialog-clearsettings-result').showModal();
    });
  }

  function openDeviceLoginModal() {
    fetch('/api/startDevicelogin').then(r = > r.json()).then(data = > {
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
  <title>ESP32 teams presence</title></head>
)";

  s += R"(<body><h2>ESP32 teams presence - v __VERSION__ </h2>)";

  s.replace("__VERSION__", VERSION);

  s += R"(<section class="mt"><div class=" nes - balloon from - left ">)";

  s += R"(__MESSAGE1__)";
  s += R"(__MESSAGE2__)";

  if (strlen(paramTenantValue) == 0 || strlen(paramClientIdValue) == 0) {
    s.replace("__MESSAGE1__", R"(<p class=" note nes - text is - error ">Some settings are missing. Go to <a href="config">configuration page</a> to complete setup.</p></div>)");
  } else {
    if (access_token == "") {
      s.replace("__MESSAGE2__", R"(<p class=" note nes - text is - error ">No authentication info's found, start device login flow to complete widget setup!</p></div>)");
    } else {
      s.replace("__MESSAGE2__", R"(<p class=" note nes - text ">Device setup complete, but you can start the device login flow if you need to re-authenticate.</p></div>)");
    }

    s += R"(<div><button type="button" class=" nes - btn " onclick = openDeviceLoginModal() >Start device login</button></div>)";
  }

  s += R"(<dialog class=" nes - dialog is - rounded " id=" dialog - devicelogin ">)";
  s += R"(<p class=" title ">Start device login</p>)";
  s += R"(<p id=" lbl_message "></p>)";
  s += R"(<input type=" text " id=" code_field " class=" nes - input " disabled>)";
  s += R"(<menu class=" dialog - menu ">)";
  s += R"(<button id=" btn_close " class=" nes - btn " onclick=" closeDeviceLoginModal() ">Close</button>)";
  s += R"(<a class=" nes - btn is - primary ml - s " id=" btn_open " href=" https :  // microsoft.com/devicelogin" target="_blank">Open device login</a>)";
  s += R"(</menu>)";
  s += R"(</dialog>)";
  s += R"(</section>)";

  s += R"(<div class=" nes - balloon from - left mt ">)";
  s += R"(Go to <a href=" config ">configuration page</a> to change settings.)";
  s += R"(</div>)";
  s += R"(<section class=" nes - container with - title "><h3 class=" title ">Current settings</h3>)";
  s += R"(<div class=" nes - field         mt - s "><label for=" name_field ">Client-ID</label><input type=" text " id=" name_field " class=" nes - input " disabled value="
                                                                                                                                                        " + String(paramClientIdValue) + "
                                                                                                                                                        "></div>)";
  s += R"(<div class=" nes - field         mt - s "><label for=" name_field ">Tenant hostname / ID</label><input type=" text " id=" name_field " class=" nes - input " disabled value="
                                                                                                                                                                   " + String(paramTenantValue) + "
                                                                                                                                                                   "></div>)";
  s += R"(<div class=" nes - field         mt - s "><label for=" name_field ">Polling interval (sec)</label><input type=" text " id=" name_field " class=" nes - input " disabled value="
                                                                                                                                                                     " + String(paramPollIntervalValue) + "
                                                                                                                                                                     "></div>)";
  s += R"(<div class=" nes - field         mt - s "><label for=" name_field ">Number of LEDs</label><input type=" text " id=" name_field " class=" nes - input " disabled value="
                                                                                                                                                             " + String(paramNumLedsValue) + "
                                                                                                                                                             "></div>)";
  s += R"(</section>)";

  s += R"(<section class=" nes - container with - title mt "><h3 class=" title ">Memory usage</h3>)";
  s += R"(<div>Sketch: " + String(ESP.getFreeSketchSpace() - ESP.getSketchSize()) + " of " + String(ESP.getFreeSketchSpace()) + " bytes free</div>)";
  s += R"(<progress class=" nes - progress " value="
                                         " + String(ESP.getSketchSize()) + "
                                         " max="
                                         " + String(ESP.getFreeSketchSpace()) + "
                                         "></progress>)";
  s += R"(<div class=" mt - s ">RAM: " + String(ESP.getFreeHeap()) + " of 327680 bytes free</div>)";
  s += R"(<progress class=" nes - progress " value="
                                         " + String(327680 - ESP.getFreeHeap()) + "
                                         " max=" 327680 "></progress>)";
  s += R"(</section>)";

  s += R"(<section class=" nes - container with - title mt "><h3 class=" title ">Danger area</h3>)";
  s += R"(<dialog class=" nes - dialog                  is - rounded " id=" dialog - clearsettings ">)";
  s += R"(<p class=" title ">Really clear all settings?</p>)";
  s += R"(<button class=" nes - btn " onclick=" document.getElementById('dialog-clearsettings').close() ">Close</button>))";
  s += R"(<button class=" nes - btn is - error " onclick=" performClearSettings() ">Clear all settings</button>)";
  s += R"(</dialog>)";
  s += R"(<dialog class=" nes - dialog is - rounded " id=" dialog - clearsettings - result ">)";
  s += R"(<p class=" title ">All settings were cleared.</p>)";
  s += R"(</dialog>)";
  s += R"(<div><button type=" button " class=" nes - btn is - error " onclick=" document.getElementById('dialog-clearsettings').showModal();">Clear all settings</button></div>))";
  s += R"(</section>)";

  s += R"(<div class="mt"><i class=" nes - icon github "></i> Find the <a href=" https://github.com/toblum/ESPTeamsPresence" target="_blank">ESPTeamsPresence</a> project on GitHub.</i></div>))";

  s += R"(</body></html>)";

  server.send(200, "text/html", s);
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

// Config was saved
void onConfigSaved() {
  log_d("Configuration was updated.");
  ws2812fx.setLength(atoi(paramNumLedsValue));
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
