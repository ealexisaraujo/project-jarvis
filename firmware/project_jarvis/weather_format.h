#pragma once

#include <stddef.h>

struct WeatherValues {
  float temperature_c;
  int humidity_percent;
  int weather_code;
  float wind_speed_kmh;
  float surface_pressure_hpa;
};

const char* weather_code_description(int weather_code);
bool weather_values_are_plausible(const WeatherValues& values);
bool weather_format_temperature(char* output, size_t output_size, float temperature_c);
bool weather_format_metrics(char* output,
                            size_t output_size,
                            int humidity_percent,
                            float wind_speed_kmh,
                            float surface_pressure_hpa);
bool weather_format_observation_time(char* output,
                                     size_t output_size,
                                     const char* iso_local_time);
