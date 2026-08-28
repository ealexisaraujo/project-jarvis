#include "weather_format.h"

#include <cmath>
#include <stdio.h>
#include <string.h>

namespace {
bool write_succeeded(int written, size_t output_size) {
  return written >= 0 && static_cast<size_t>(written) < output_size;
}

bool is_ascii_digit(char value) {
  return value >= '0' && value <= '9';
}
}  // namespace

const char* weather_code_description(int weather_code) {
  switch (weather_code) {
    case 0:
      return "Clear";
    case 1:
      return "Mainly clear";
    case 2:
      return "Partly cloudy";
    case 3:
      return "Overcast";
    case 45:
    case 48:
      return "Fog";
    case 51:
    case 53:
    case 55:
      return "Drizzle";
    case 56:
    case 57:
      return "Freezing drizzle";
    case 61:
    case 63:
    case 65:
      return "Rain";
    case 66:
    case 67:
      return "Freezing rain";
    case 71:
    case 73:
    case 75:
      return "Snow";
    case 77:
      return "Snow grains";
    case 80:
    case 81:
    case 82:
      return "Rain showers";
    case 85:
    case 86:
      return "Snow showers";
    case 95:
      return "Thunderstorm";
    case 96:
    case 99:
      return "Thunderstorm with hail";
    default:
      return "Unknown conditions";
  }
}

bool weather_values_are_plausible(const WeatherValues& values) {
  return std::isfinite(values.temperature_c) && values.temperature_c >= -90.0F &&
         values.temperature_c <= 60.0F && values.humidity_percent >= 0 &&
         values.humidity_percent <= 100 && values.weather_code >= 0 &&
         values.weather_code <= 99 && std::isfinite(values.wind_speed_kmh) &&
         values.wind_speed_kmh >= 0.0F && values.wind_speed_kmh <= 400.0F &&
         std::isfinite(values.surface_pressure_hpa) &&
         values.surface_pressure_hpa >= 800.0F &&
         values.surface_pressure_hpa <= 1100.0F;
}

bool weather_format_temperature(char* output, size_t output_size, float temperature_c) {
  if (output == nullptr || output_size == 0 || !std::isfinite(temperature_c)) {
    return false;
  }
  const int written = snprintf(output,
                               output_size,
                               "%ld C",
                               lroundf(temperature_c));
  return write_succeeded(written, output_size);
}

bool weather_format_metrics(char* output,
                            size_t output_size,
                            int humidity_percent,
                            float wind_speed_kmh,
                            float surface_pressure_hpa) {
  if (output == nullptr || output_size == 0 || humidity_percent < 0 ||
      humidity_percent > 100 || !std::isfinite(wind_speed_kmh) ||
      !std::isfinite(surface_pressure_hpa)) {
    return false;
  }
  const int written = snprintf(output,
                               output_size,
                               "H %d%%  W %ld km/h  P %ld hPa",
                               humidity_percent,
                               lroundf(wind_speed_kmh),
                               lroundf(surface_pressure_hpa));
  return write_succeeded(written, output_size);
}

bool weather_format_observation_time(char* output,
                                     size_t output_size,
                                     const char* iso_local_time) {
  if (output == nullptr || output_size == 0 || iso_local_time == nullptr) {
    return false;
  }

  const char* time_separator = strchr(iso_local_time, 'T');
  if (time_separator == nullptr || time_separator == iso_local_time ||
      strlen(time_separator) < 6 || !is_ascii_digit(time_separator[1]) ||
      !is_ascii_digit(time_separator[2]) || time_separator[3] != ':' ||
      !is_ascii_digit(time_separator[4]) || !is_ascii_digit(time_separator[5])) {
    return false;
  }

  const int hour = (time_separator[1] - '0') * 10 + (time_separator[2] - '0');
  const int minute = (time_separator[4] - '0') * 10 + (time_separator[5] - '0');
  if (hour > 23 || minute > 59) {
    return false;
  }

  const int written = snprintf(output,
                               output_size,
                               "Updated %c%c:%c%c",
                               time_separator[1],
                               time_separator[2],
                               time_separator[4],
                               time_separator[5]);
  return write_succeeded(written, output_size);
}
