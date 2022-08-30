/**
 * Copyright (C) 2022 @riraosan.github.io
 * MIT Licence
 */

#pragma once

#include <Arduino.h>

constexpr char loginFilter[] = R"(
{
  "user_code" : true,
  "device_code" : true,
  "verification_uri" : true,
  "expires_in" : true,
  "interval": true,
  "message" : true
}
)";

constexpr char refleshtokenFilter[] = R"(
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

constexpr char presenceFilter[] = R"(
{
  "id": true,
  "availability": true,
  "activity": true"
}
)";
