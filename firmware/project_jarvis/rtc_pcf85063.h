#pragma once

#include <stdint.h>

#include "rtc_datetime.h"

enum class RtcStatus : uint8_t {
  kOnline,
  kInvalid,
  kOffline,
};

enum class RtcSetStatus : uint8_t {
  kOnline,
  kInvalid,
  kOffline,
  kMismatch,
};

RtcStatus rtc_pcf85063_begin();
RtcStatus rtc_pcf85063_read(RtcDateTime* date_time);
RtcSetStatus rtc_pcf85063_write_and_verify(const RtcDateTime& date_time);
