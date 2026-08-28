#include "rtc_datetime.h"

namespace {
bool decode_bcd(uint8_t raw,
                uint8_t mask,
                uint8_t minimum,
                uint8_t maximum,
                uint8_t* decoded) {
  const uint8_t value = raw & mask;
  const uint8_t ones = value & 0x0F;
  const uint8_t tens = value >> 4;
  if (ones > 9 || tens > 9) {
    return false;
  }

  const uint8_t result = tens * 10 + ones;
  if (result < minimum || result > maximum) {
    return false;
  }
  *decoded = result;
  return true;
}

uint8_t encode_bcd(uint8_t value) {
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

bool is_leap_year(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint8_t days_in_month(uint16_t year, uint8_t month) {
  static const uint8_t kMonthLengths[] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
  };
  if (month == 2 && is_leap_year(year)) {
    return 29;
  }
  return kMonthLengths[month - 1];
}

bool date_times_equal(const RtcDateTime& left, const RtcDateTime& right) {
  return left.year == right.year && left.month == right.month &&
         left.day == right.day && left.weekday == right.weekday &&
         left.hour == right.hour && left.minute == right.minute &&
         left.second == right.second;
}

RtcDateTime next_second(RtcDateTime value) {
  if (++value.second <= 59) {
    return value;
  }
  value.second = 0;
  if (++value.minute <= 59) {
    return value;
  }
  value.minute = 0;
  if (++value.hour <= 23) {
    return value;
  }
  value.hour = 0;
  value.weekday = static_cast<uint8_t>((value.weekday + 1) % 7);
  if (++value.day <= days_in_month(value.year, value.month)) {
    return value;
  }
  value.day = 1;
  if (++value.month <= 12) {
    return value;
  }
  value.month = 1;
  ++value.year;
  return value;
}
}  // namespace

bool rtc_datetime_is_valid(const RtcDateTime& date_time) {
  if (date_time.year < 2000 || date_time.year > 2099 ||
      date_time.month < 1 || date_time.month > 12 ||
      date_time.weekday > 6 || date_time.hour > 23 ||
      date_time.minute > 59 || date_time.second > 59) {
    return false;
  }
  return date_time.day >= 1 &&
         date_time.day <= days_in_month(date_time.year, date_time.month);
}

bool rtc_datetime_decode_registers(
    const uint8_t registers[kRtcTimeRegisterCount],
    RtcDateTime* date_time) {
  if (registers == nullptr || date_time == nullptr ||
      (registers[0] & 0x80) != 0) {
    return false;
  }

  uint8_t year_offset = 0;
  RtcDateTime candidate = {};
  if (!decode_bcd(registers[0], 0x7F, 0, 59, &candidate.second) ||
      !decode_bcd(registers[1], 0x7F, 0, 59, &candidate.minute) ||
      !decode_bcd(registers[2], 0x3F, 0, 23, &candidate.hour) ||
      !decode_bcd(registers[3], 0x3F, 1, 31, &candidate.day) ||
      !decode_bcd(registers[4], 0x07, 0, 6, &candidate.weekday) ||
      !decode_bcd(registers[5], 0x1F, 1, 12, &candidate.month) ||
      !decode_bcd(registers[6], 0xFF, 0, 99, &year_offset)) {
    return false;
  }
  candidate.year = static_cast<uint16_t>(2000 + year_offset);
  if (!rtc_datetime_is_valid(candidate)) {
    return false;
  }

  *date_time = candidate;
  return true;
}

bool rtc_datetime_encode_registers(
    const RtcDateTime& date_time,
    uint8_t registers[kRtcTimeRegisterCount]) {
  if (registers == nullptr || !rtc_datetime_is_valid(date_time)) {
    return false;
  }

  registers[0] = encode_bcd(date_time.second);
  registers[1] = encode_bcd(date_time.minute);
  registers[2] = encode_bcd(date_time.hour);
  registers[3] = encode_bcd(date_time.day);
  registers[4] = encode_bcd(date_time.weekday);
  registers[5] = encode_bcd(date_time.month);
  registers[6] = encode_bcd(static_cast<uint8_t>(date_time.year - 2000));
  return true;
}

bool rtc_datetime_matches_requested_or_next(const RtcDateTime& requested,
                                            const RtcDateTime& actual) {
  if (!rtc_datetime_is_valid(requested) || !rtc_datetime_is_valid(actual)) {
    return false;
  }
  return date_times_equal(requested, actual) ||
         date_times_equal(next_second(requested), actual);
}
