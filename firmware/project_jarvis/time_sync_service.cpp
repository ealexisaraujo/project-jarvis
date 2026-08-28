#include "time_sync_service.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <time.h>

#include "location_config.h"
#include "rtc_pcf85063.h"
#include "wifi_service.h"

namespace {
constexpr char kPrimaryNtpServer[] = "pool.ntp.org";
constexpr char kSecondaryNtpServer[] = "time.nist.gov";
constexpr uint16_t kMinimumSynchronizedYear = 2024;
constexpr uint32_t kRtcSetRetryIntervalMs = 1000;

bool synchronized_once = false;
bool service_started = false;
bool ntp_requested = false;
bool waiting_wifi_reported = false;
uint32_t last_rtc_set_attempt_ms = 0;
RtcSetStatus last_set_status = RtcSetStatus::kOnline;
bool set_status_reported = false;

const char* rtc_set_status_name(RtcSetStatus status) {
  switch (status) {
    case RtcSetStatus::kOnline:
      return "online";
    case RtcSetStatus::kInvalid:
      return "invalid";
    case RtcSetStatus::kOffline:
      return "offline";
    case RtcSetStatus::kMismatch:
      return "mismatch";
  }
  return "offline";
}

void report_waiting_wifi() {
  if (!waiting_wifi_reported) {
    Serial.println("time_sync_status=waiting_wifi");
    waiting_wifi_reported = true;
  }
}

bool read_plausible_local_time(RtcDateTime* date_time) {
  const time_t now = time(nullptr);
  struct tm local_time = {};
  if (now < 0 || localtime_r(&now, &local_time) == nullptr) {
    return false;
  }

  const int year = local_time.tm_year + 1900;
  if (year < kMinimumSynchronizedYear || year > 2099) {
    return false;
  }

  const RtcDateTime candidate = {
      static_cast<uint16_t>(year),
      static_cast<uint8_t>(local_time.tm_mon + 1),
      static_cast<uint8_t>(local_time.tm_mday),
      static_cast<uint8_t>(local_time.tm_wday),
      static_cast<uint8_t>(local_time.tm_hour),
      static_cast<uint8_t>(local_time.tm_min),
      static_cast<uint8_t>(local_time.tm_sec),
  };
  if (!rtc_datetime_is_valid(candidate)) {
    return false;
  }
  *date_time = candidate;
  return true;
}
}  // namespace

void time_sync_service_begin() {
  service_started = true;
  synchronized_once = false;
  ntp_requested = false;
  waiting_wifi_reported = false;
  last_rtc_set_attempt_ms = 0;
  set_status_reported = false;
  report_waiting_wifi();
}

void time_sync_service_loop() {
  if (!service_started || synchronized_once) {
    return;
  }

  const WifiServiceSnapshot wifi = wifi_service_snapshot();
  if (wifi.state != WifiServiceState::kOnline) {
    ntp_requested = false;
    report_waiting_wifi();
    return;
  }

  waiting_wifi_reported = false;
  if (!ntp_requested) {
    const LocationProfile& location = active_location_profile();
    configTzTime(location.posix_timezone,
                 kPrimaryNtpServer,
                 kSecondaryNtpServer);
    ntp_requested = true;
    Serial.printf("time_sync_status=requested timezone=%s\n",
                  location.iana_timezone);
  }

  if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    return;
  }

  RtcDateTime synchronized_time = {};
  if (!read_plausible_local_time(&synchronized_time)) {
    return;
  }

  const uint32_t now_ms = millis();
  if (last_rtc_set_attempt_ms != 0 &&
      now_ms - last_rtc_set_attempt_ms < kRtcSetRetryIntervalMs) {
    return;
  }
  last_rtc_set_attempt_ms = now_ms;

  const RtcSetStatus set_status =
      rtc_pcf85063_write_and_verify(synchronized_time);
  if (!set_status_reported || set_status != last_set_status) {
    Serial.printf("rtc_set_status=%s\n", rtc_set_status_name(set_status));
    last_set_status = set_status;
    set_status_reported = true;
  }
  if (set_status != RtcSetStatus::kOnline) {
    return;
  }

  synchronized_once = true;
  Serial.println("time_sync_status=online");
}
