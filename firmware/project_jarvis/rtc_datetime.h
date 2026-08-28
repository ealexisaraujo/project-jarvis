#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t kRtcTimeRegisterCount = 7;

struct RtcDateTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t weekday;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

bool rtc_datetime_is_valid(const RtcDateTime& date_time);
bool rtc_datetime_decode_registers(
    const uint8_t registers[kRtcTimeRegisterCount],
    RtcDateTime* date_time);
bool rtc_datetime_encode_registers(
    const RtcDateTime& date_time,
    uint8_t registers[kRtcTimeRegisterCount]);
bool rtc_datetime_matches_requested_or_next(const RtcDateTime& requested,
                                            const RtcDateTime& actual);
