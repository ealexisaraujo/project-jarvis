#include <assert.h>
#include <limits>
#include <string.h>

#include "weather_format.h"

namespace {
void assert_ascii(const char* text) {
  for (size_t index = 0; text[index] != '\0'; ++index) {
    assert(static_cast<unsigned char>(text[index]) < 128);
  }
}

void test_wmo_mapping_groups() {
  const struct {
    int code;
    const char* expected;
  } cases[] = {
      {0, "Clear"},
      {1, "Mainly clear"},
      {2, "Partly cloudy"},
      {3, "Overcast"},
      {45, "Fog"},
      {48, "Fog"},
      {51, "Drizzle"},
      {53, "Drizzle"},
      {55, "Drizzle"},
      {56, "Freezing drizzle"},
      {57, "Freezing drizzle"},
      {61, "Rain"},
      {63, "Rain"},
      {65, "Rain"},
      {66, "Freezing rain"},
      {67, "Freezing rain"},
      {71, "Snow"},
      {73, "Snow"},
      {75, "Snow"},
      {77, "Snow grains"},
      {80, "Rain showers"},
      {81, "Rain showers"},
      {82, "Rain showers"},
      {85, "Snow showers"},
      {86, "Snow showers"},
      {95, "Thunderstorm"},
      {96, "Thunderstorm with hail"},
      {99, "Thunderstorm with hail"},
  };

  for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    assert(strcmp(weather_code_description(cases[index].code), cases[index].expected) == 0);
    assert_ascii(weather_code_description(cases[index].code));
  }
  assert(strcmp(weather_code_description(-1), "Unknown conditions") == 0);
  assert(strcmp(weather_code_description(4), "Unknown conditions") == 0);
  assert(strcmp(weather_code_description(100), "Unknown conditions") == 0);
}

void test_rounding_and_metric_text() {
  char text[64] = {};
  assert(weather_format_temperature(text, sizeof(text), 16.5F));
  assert(strcmp(text, "17 C") == 0);
  assert(weather_format_temperature(text, sizeof(text), -2.5F));
  assert(strcmp(text, "-3 C") == 0);
  assert(weather_format_metrics(text, sizeof(text), 92, 10.5F, 1001.6F));
  assert(strcmp(text, "H 92%  W 11 km/h  P 1002 hPa") == 0);
  assert_ascii(text);

  char too_small[5] = {};
  assert(!weather_format_metrics(too_small, sizeof(too_small), 92, 11.0F, 1002.0F));
}

void test_observation_time_extraction() {
  char text[20] = {};
  assert(weather_format_observation_time(text,
                                         sizeof(text),
                                         "2026-08-27T09:04"));
  assert(strcmp(text, "Updated 09:04") == 0);
  assert(weather_format_observation_time(text,
                                         sizeof(text),
                                         "2026-08-27T23:59:00"));
  assert(strcmp(text, "Updated 23:59") == 0);
  assert(!weather_format_observation_time(text, sizeof(text), "2026-08-27 09:04"));
  assert(!weather_format_observation_time(text, sizeof(text), "2026-08-27T24:00"));
  assert(!weather_format_observation_time(text, sizeof(text), "2026-08-27T12:60"));
  assert(!weather_format_observation_time(text, sizeof(text), "2026-08-27T9:04"));
  assert(!weather_format_observation_time(text, sizeof(text), "2026-08-27T"));
  assert(!weather_format_observation_time(text, sizeof(text), "T09:04"));
}

void test_plausible_ranges() {
  const WeatherValues minimum = {-90.0F, 0, 0, 0.0F, 800.0F};
  const WeatherValues maximum = {60.0F, 100, 99, 400.0F, 1100.0F};
  assert(weather_values_are_plausible(minimum));
  assert(weather_values_are_plausible(maximum));

  WeatherValues invalid = minimum;
  invalid.temperature_c = -90.1F;
  assert(!weather_values_are_plausible(invalid));
  invalid = minimum;
  invalid.humidity_percent = 101;
  assert(!weather_values_are_plausible(invalid));
  invalid = minimum;
  invalid.weather_code = -1;
  assert(!weather_values_are_plausible(invalid));
  invalid = minimum;
  invalid.wind_speed_kmh = 400.1F;
  assert(!weather_values_are_plausible(invalid));
  invalid = minimum;
  invalid.surface_pressure_hpa = 799.9F;
  assert(!weather_values_are_plausible(invalid));
  invalid = minimum;
  invalid.temperature_c = std::numeric_limits<float>::quiet_NaN();
  assert(!weather_values_are_plausible(invalid));
  invalid = minimum;
  invalid.wind_speed_kmh = std::numeric_limits<float>::infinity();
  assert(!weather_values_are_plausible(invalid));
}
}  // namespace

int main() {
  test_wmo_mapping_groups();
  test_rounding_and_metric_text();
  test_observation_time_extraction();
  test_plausible_ranges();
  return 0;
}
