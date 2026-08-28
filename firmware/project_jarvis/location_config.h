#pragma once

#include <stddef.h>

struct LocationProfile {
  const char* display_name;
  double latitude;
  double longitude;
  const char* iana_timezone;
  const char* posix_timezone;
};

constexpr size_t kOpenMeteoForecastUrlCapacity = 320;

extern const LocationProfile kChandlerLocationProfile;
extern const LocationProfile kAshfordLocationProfile;

const LocationProfile& active_location_profile();
bool location_profile_is_valid(const LocationProfile& profile);
bool location_build_open_meteo_url(const LocationProfile& profile,
                                   char* output,
                                   size_t output_size);
