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

// Statemachine
#define SMODEINITIAL                      0   // Initial
#define SMODEWIFICONNECTING               1   // Wait for wifi connection
#define SMODEWIFICONNECTED                2   // Wifi connected
#define SMODEDEVICELOGINSTARTED           10  // Device login flow was started
#define SMODEDEVICELOGINFAILED            11  // Device login flow failed
#define SMODEAUTHREADY                    20  // Authentication successful
#define SMODEPOLLPRESENCE                 21  // Poll for presence
#define SMODEREFRESHTOKEN                 22  // Access token needs refresh
#define SMODEPRESENCEREQUESTERROR         23  // Access token needs refresh

// Global settings
#define NUMLEDS                           37  // Number of LEDs on the strip (if not set via build flags)
#define DATAPIN                           32  // GPIO pin used to drive the LED strip (20 == GPIO/D13) (if not set via build flags)
// #define DISABLECERTCHECK 1					        // Uncomment to disable https certificate checks (if not set via build flags)
// #define STATUS_PIN LED_BUILTIN				      // User builtin LED for status (if not set via build flags)
#define DEFAULT_POLLING_PRESENCE_INTERVAL "30"             // Default interval to poll for presence info (seconds)
#define DEFAULT_ERROR_RETRY_INTERVAL      30               // Default interval to try again after errors
#define TOKEN_REFRESH_TIMEOUT             60               // Number of seconds until expiration before token gets refreshed
#define CONTEXT_FILE                      "/context.json"  // Filename of the context file
#define VERSION                           "0.18.1"         // Version of the software
