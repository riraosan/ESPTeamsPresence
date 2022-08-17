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

#include "driver/rmt.h"
#include <Arduino.h>

#define APB_CLK_MHZ 80                                  // default RMT CLK source (80MHz)
#define RMT_CLK_DIV 2                                   // RMT CLK divider
#define RMT_TICK    (RMT_CLK_DIV * 1000 / APB_CLK_MHZ)  // 25ns

// timing parameters for WS2812B LEDs. you may need to
// tweek these if you're using a different kind of LED
#define T1_TICKS    250 / RMT_TICK    // 250ns
#define T2_TICKS    625 / RMT_TICK    // 625ns
#define T3_TICKS    375 / RMT_TICK    // 375ns
#define RESET_TICKS 50000 / RMT_TICK  // 50us

/*
 * Convert uint8_t type of data to rmt format data.
 */
void IRAM_ATTR u8_to_rmt(const void* src, rmt_item32_t* dest, size_t src_size, size_t wanted_num, size_t* translated_size, size_t* item_num);
/*
 * Initialize the RMT Tx channel
 */
void rmt_tx_int(rmt_channel_t channel, uint8_t gpio);
