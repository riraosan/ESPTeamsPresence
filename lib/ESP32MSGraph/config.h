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
 * refactored by @riraosan.github.io
 * If I have seen further it is by standing on the shoulders of giants.
 */

#pragma once

// Global settings
//#define DISABLECERTCHECK                  1                // Uncomment to disable https certificate checks (if not set via build flags)
//#define STATUS_PIN                        LED_BUILTIN      // User builtin LED for status (if not set via build flags)
#define DATAPIN                           27               // GPIO pin used to drive the LED strip (20 == GPIO/D13) (if not set via build flags)
#define NUMLEDS                           25               // Number of LEDs on the strip (if not set via build flags)
#define DEFAULT_POLLING_PRESENCE_INTERVAL "30"             // Default interval to poll for presence info (seconds)
#define DEFAULT_ERROR_RETRY_INTERVAL      30               // Default interval to try again after errors
#define TOKEN_REFRESH_TIMEOUT             60               // Number of seconds until expiration before token gets refreshed
#define CONTEXT_FILE                      "/context.json"  // Filename of the context file
#define VERSION                           "0.18.1"         // Version of the software
