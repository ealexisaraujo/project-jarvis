#include <assert.h>
#include <limits>
#include <string.h>

#include "location_config.h"

namespace {
void test_verified_profile_values() {
  assert(location_profile_is_valid(kChandlerLocationProfile));
  assert(location_profile_is_valid(kAshfordLocationProfile));

  assert(strcmp(kChandlerLocationProfile.display_name, "Chandler") == 0);
  assert(kChandlerLocationProfile.latitude == 33.30616);
  assert(kChandlerLocationProfile.longitude == -111.84125);
  assert(strcmp(kChandlerLocationProfile.iana_timezone,
                "America/Phoenix") == 0);
  assert(strcmp(kChandlerLocationProfile.posix_timezone, "MST7") == 0);

  assert(strcmp(kAshfordLocationProfile.display_name, "Ashford") == 0);
  assert(kAshfordLocationProfile.latitude == 51.14648);
  assert(kAshfordLocationProfile.longitude == 0.87376);
  assert(strcmp(kAshfordLocationProfile.iana_timezone, "Europe/London") == 0);
  assert(strcmp(kAshfordLocationProfile.posix_timezone,
                "GMT0BST,M3.5.0/1,M10.5.0") == 0);
}

void test_selected_profile_generically() {
  const LocationProfile& active = active_location_profile();
  assert(location_profile_is_valid(active));

  char output[kOpenMeteoForecastUrlCapacity] = {};
  assert(location_build_open_meteo_url(active, output, sizeof(output)));
  assert(output[0] != '\0');
}

void test_invalid_profiles_are_rejected() {
  LocationProfile invalid = kChandlerLocationProfile;
  invalid.display_name = "";
  assert(!location_profile_is_valid(invalid));

  invalid = kChandlerLocationProfile;
  invalid.display_name = "Chand\xC3\xA9ler";
  assert(!location_profile_is_valid(invalid));

  invalid = kChandlerLocationProfile;
  invalid.display_name = "Chandler\nArizona";
  assert(!location_profile_is_valid(invalid));

  invalid = kChandlerLocationProfile;
  invalid.latitude = 90.01;
  assert(!location_profile_is_valid(invalid));

  invalid = kChandlerLocationProfile;
  invalid.longitude = -180.01;
  assert(!location_profile_is_valid(invalid));

  invalid = kChandlerLocationProfile;
  invalid.latitude = std::numeric_limits<double>::quiet_NaN();
  assert(!location_profile_is_valid(invalid));

  invalid = kChandlerLocationProfile;
  invalid.iana_timezone = nullptr;
  assert(!location_profile_is_valid(invalid));

  invalid = kChandlerLocationProfile;
  invalid.posix_timezone = "";
  assert(!location_profile_is_valid(invalid));

  char output[kOpenMeteoForecastUrlCapacity] = "unchanged";
  assert(!location_build_open_meteo_url(invalid, output, sizeof(output)));
  assert(output[0] == '\0');
}

void test_chandler_url_creation() {
  char output[kOpenMeteoForecastUrlCapacity] = {};
  assert(location_build_open_meteo_url(kChandlerLocationProfile,
                                       output,
                                       sizeof(output)));
  assert(strcmp(
             output,
             "https://api.open-meteo.com/v1/forecast?latitude=33.30616&"
             "longitude=-111.84125&current=temperature_2m,relative_humidity_2m,"
             "weather_code,wind_speed_10m,surface_pressure&timezone="
             "America%2FPhoenix&forecast_days=1") == 0);
}

void test_timezone_percent_encoding() {
  const LocationProfile encoded_location = {
      "Encoding test",
      1.25,
      -2.5,
      "Area/City Name+Test",
      "UTC0",
  };
  char output[kOpenMeteoForecastUrlCapacity] = {};
  assert(location_build_open_meteo_url(encoded_location,
                                       output,
                                       sizeof(output)));
  assert(strstr(output, "latitude=1.25000&longitude=-2.50000") != nullptr);
  assert(strstr(output, "timezone=Area%2FCity%20Name%2BTest&") != nullptr);
}

void test_small_buffer_rejection() {
  char too_small[32] = "unchanged";
  assert(!location_build_open_meteo_url(kChandlerLocationProfile,
                                        too_small,
                                        sizeof(too_small)));
  assert(too_small[0] == '\0');
  assert(!location_build_open_meteo_url(kChandlerLocationProfile, nullptr, 0));
}
}  // namespace

int main() {
  test_verified_profile_values();
  test_selected_profile_generically();
  test_invalid_profiles_are_rejected();
  test_chandler_url_creation();
  test_timezone_percent_encoding();
  test_small_buffer_rejection();
  return 0;
}
