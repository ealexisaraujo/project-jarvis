#include "location_config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

// Open-Meteo Geocoding API result checked on 2026-08-27.
const LocationProfile kChandlerLocationProfile = {
    "Chandler",
    33.30616,
    -111.84125,
    "America/Phoenix",
    "MST7",
};

// Previous Open-Meteo location retained for one-line profile switching.
const LocationProfile kAshfordLocationProfile = {
    "Ashford",
    51.14648,
    0.87376,
    "Europe/London",
    "GMT0BST,M3.5.0/1,M10.5.0",
};

namespace {
// Change only this line to select another location profile.
const LocationProfile& kActiveLocationProfile = kChandlerLocationProfile;

constexpr size_t kMaximumDisplayNameLength = 64;
constexpr size_t kMaximumTimezoneLength = 128;
constexpr char kForecastUrlPrefix[] =
    "https://api.open-meteo.com/v1/forecast?latitude=";
constexpr char kForecastUrlBetweenCoordinates[] = "&longitude=";
constexpr char kForecastUrlBeforeTimezone[] =
    "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,"
    "surface_pressure&timezone=";
constexpr char kForecastUrlSuffix[] = "&forecast_days=1";

bool is_nonempty_bounded_text(const char* text, size_t maximum_length) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  return strnlen(text, maximum_length + 1) <= maximum_length;
}

bool is_safe_display_name(const char* text) {
  if (!is_nonempty_bounded_text(text, kMaximumDisplayNameLength)) {
    return false;
  }

  for (size_t index = 0; text[index] != '\0'; ++index) {
    const unsigned char character = static_cast<unsigned char>(text[index]);
    if (character < 0x20 || character > 0x7E) {
      return false;
    }
  }
  return true;
}

bool append_text(char* output,
                 size_t output_size,
                 size_t* output_length,
                 const char* text) {
  const size_t text_length = strlen(text);
  if (*output_length >= output_size ||
      text_length >= output_size - *output_length) {
    return false;
  }
  memcpy(output + *output_length, text, text_length);
  *output_length += text_length;
  output[*output_length] = '\0';
  return true;
}

bool append_coordinate(char* output,
                       size_t output_size,
                       size_t* output_length,
                       double coordinate) {
  char coordinate_text[32] = {};
  const int written =
      snprintf(coordinate_text, sizeof(coordinate_text), "%.5f", coordinate);
  return written > 0 && static_cast<size_t>(written) < sizeof(coordinate_text) &&
         append_text(output, output_size, output_length, coordinate_text);
}

bool is_unreserved_query_character(unsigned char character) {
  return (character >= 'A' && character <= 'Z') ||
         (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9') || character == '-' ||
         character == '.' || character == '_' || character == '~';
}

bool append_percent_encoded(char* output,
                            size_t output_size,
                            size_t* output_length,
                            const char* text) {
  constexpr char kHexDigits[] = "0123456789ABCDEF";
  for (size_t index = 0; text[index] != '\0'; ++index) {
    const unsigned char character = static_cast<unsigned char>(text[index]);
    if (is_unreserved_query_character(character)) {
      char unreserved[2] = {static_cast<char>(character), '\0'};
      if (!append_text(output, output_size, output_length, unreserved)) {
        return false;
      }
      continue;
    }

    char encoded[4] = {
        '%',
        kHexDigits[character >> 4],
        kHexDigits[character & 0x0F],
        '\0',
    };
    if (!append_text(output, output_size, output_length, encoded)) {
      return false;
    }
  }
  return true;
}
}  // namespace

const LocationProfile& active_location_profile() {
  return kActiveLocationProfile;
}

bool location_profile_is_valid(const LocationProfile& profile) {
  return is_safe_display_name(profile.display_name) &&
         isfinite(profile.latitude) && profile.latitude >= -90.0 &&
         profile.latitude <= 90.0 && isfinite(profile.longitude) &&
         profile.longitude >= -180.0 && profile.longitude <= 180.0 &&
         is_nonempty_bounded_text(profile.iana_timezone,
                                  kMaximumTimezoneLength) &&
         is_nonempty_bounded_text(profile.posix_timezone,
                                  kMaximumTimezoneLength);
}

bool location_build_open_meteo_url(const LocationProfile& profile,
                                   char* output,
                                   size_t output_size) {
  if (output == nullptr || output_size == 0) {
    return false;
  }
  output[0] = '\0';
  if (!location_profile_is_valid(profile)) {
    return false;
  }

  size_t output_length = 0;
  const bool success =
      append_text(output,
                  output_size,
                  &output_length,
                  kForecastUrlPrefix) &&
      append_coordinate(output,
                        output_size,
                        &output_length,
                        profile.latitude) &&
      append_text(output,
                  output_size,
                  &output_length,
                  kForecastUrlBetweenCoordinates) &&
      append_coordinate(output,
                        output_size,
                        &output_length,
                        profile.longitude) &&
      append_text(output,
                  output_size,
                  &output_length,
                  kForecastUrlBeforeTimezone) &&
      append_percent_encoded(output,
                             output_size,
                             &output_length,
                             profile.iana_timezone) &&
      append_text(output, output_size, &output_length, kForecastUrlSuffix);
  if (!success) {
    output[0] = '\0';
  }
  return success;
}
