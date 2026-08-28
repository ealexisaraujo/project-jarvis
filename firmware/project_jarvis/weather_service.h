#pragma once

#include <stdint.h>

#include "weather_format.h"

enum class WeatherServiceState : uint8_t {
  kWaitingWifi,
  kFetching,
  kOnline,
  kFailed,
  kOfflineCached,
};

struct WeatherServiceSnapshot {
  WeatherServiceState state;
  int error_code;
  bool has_valid_data;
  WeatherValues values;
  char observation_time[20];
};

void weather_service_begin();
void weather_service_loop();
WeatherServiceSnapshot weather_service_snapshot();
