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
#include "config.h"

// basic
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <EEPROM.h>
#include <FS.h>
#include <SPIFFS.h>

#include <IotWebConf.h>
#include <ArduinoJson.h>

// public
/// API request handler
bool requestJsonApi(JsonDocument& doc, ARDUINOJSON_NAMESPACE::Filter filter, String url, String payload = "", String type = "POST", bool sendAuth = false);

/// Calculate token lifetime
int getTokenLifetime(void);

/// Remove context information file in SPIFFS
void removeContext(void);

/// Requests to /startDevicelogin
void handleStartDevicelogin(void);

// private
/// Handle web requests
bool exists(String path);

/// Config was saved
void onConfigSaved(void);

bool formValidator(void);

String getContentType(String filename);

/// Requests to
void handleRoot(void);

void handleGetSettings(void);

/// Delete EEPROM by removing the trailing sequence, remove context file
void handleClearSettings(void);

void handleMinimalUpload(void);

void handleFileUpload(void);

void handleFileDelete(void);

void handleFileList(void);

bool handleFileRead(String path);

void pollForToken(void);

bool refreshToken(void);

void statemachine(void);

bool loadContext(void);

void saveContext(void);

// user method
void pollPresence(void);

void setAnimation(uint8_t segment, uint8_t mode = 0, uint32_t color = 0, uint16_t speed = 3000, bool reverse = false);

void setPresenceAnimation(void);
