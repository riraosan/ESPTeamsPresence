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

// library's on GitHub
#include <IotWebConf.h>
#include <ArduinoJson.h>




/**
 * Calculate token lifetime
 */
int getTokenLifetime(void);

/**
 * Remove context information file in SPIFFS
 */
void removeContext(void);

/**
 * API request handler
 */
bool requestJsonApi(JsonDocument& doc, ARDUINOJSON_NAMESPACE::Filter filter, String url, String payload = "", String type = "POST", boolean sendAuth = false);

/**
 * Handle web requests
 */

// Requests to
void handleRoot(void);

void handleGetSettings(void);

// Delete EEPROM by removing the trailing sequence, remove context file
void handleClearSettings(void);

bool formValidator(void);

// Config was saved
void onConfigSaved(void);

// Requests to /startDevicelogin
void handleStartDevicelogin(void);
