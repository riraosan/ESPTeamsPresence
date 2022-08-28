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

#pragma once

#include <Arduino.h>

constexpr char _loginFilter[] PROGMEM = R"(
{
  "user_code" : true,
  "device_code" : true,
  "verification_uri" : true,
  "expires_in" : true,
  "interval": true,
  "message" : true
}
)";

// constexpr char _tokenFilter[] PROGMEM = R"(
// {
//   "expires_in": true
//   "access_token": true,
//   "refresh_token": true,
//   "id_token": true,
// }
// )";

constexpr char _refleshtokenFilter[] PROGMEM = R"(
{
  "token_type" : true,
  "scope" : true,
  "expires_in" : true,
  "ext_expires_in" : true,
  "access_token" : true,
  "refresh_token" : true,
  "id_token" : true
}
)";

constexpr char _presenceFilter[] PROGMEM = R"(
{
  "id": true,
  "availability": true,
  "activity": true"
}
)";
