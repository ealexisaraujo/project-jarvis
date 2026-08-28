#include <assert.h>
#include <stdint.h>

#include "rtc_datetime.h"

namespace {
void test_validation_and_bcd_round_trip() {
  const RtcDateTime leap_day = {2024, 2, 29, 4, 12, 34, 56};
  assert(rtc_datetime_is_valid(leap_day));

  uint8_t registers[kRtcTimeRegisterCount] = {};
  assert(rtc_datetime_encode_registers(leap_day, registers));
  const uint8_t expected[] = {0x56, 0x34, 0x12, 0x29, 0x04, 0x02, 0x24};
  for (size_t index = 0; index < kRtcTimeRegisterCount; ++index) {
    assert(registers[index] == expected[index]);
  }

  RtcDateTime decoded = {};
  assert(rtc_datetime_decode_registers(registers, &decoded));
  assert(rtc_datetime_matches_requested_or_next(leap_day, decoded));

  registers[0] |= 0x80;
  assert(!rtc_datetime_decode_registers(registers, &decoded));

  const uint8_t invalid_bcd[] = {0x6A, 0x34, 0x12, 0x29, 0x04, 0x02, 0x24};
  assert(!rtc_datetime_decode_registers(invalid_bcd, &decoded));

  const uint8_t invalid_april_date[] = {
      0x00, 0x00, 0x00, 0x31, 0x03, 0x04, 0x24,
  };
  assert(!rtc_datetime_decode_registers(invalid_april_date, &decoded));
}

void test_invalid_ranges() {
  assert(!rtc_datetime_is_valid({1999, 12, 31, 5, 23, 59, 59}));
  assert(!rtc_datetime_is_valid({2100, 1, 1, 5, 0, 0, 0}));
  assert(!rtc_datetime_is_valid({2023, 2, 29, 3, 0, 0, 0}));
  assert(!rtc_datetime_is_valid({2024, 4, 31, 3, 0, 0, 0}));
  assert(!rtc_datetime_is_valid({2024, 1, 1, 7, 0, 0, 0}));
  assert(!rtc_datetime_is_valid({2024, 1, 1, 1, 24, 0, 0}));
  assert(!rtc_datetime_is_valid({2024, 1, 1, 1, 0, 60, 0}));
  assert(!rtc_datetime_is_valid({2024, 1, 1, 1, 0, 0, 60}));
}

void test_one_second_rollover() {
  const RtcDateTime requested = {2024, 12, 31, 2, 23, 59, 59};
  const RtcDateTime same = requested;
  const RtcDateTime next = {2025, 1, 1, 3, 0, 0, 0};
  const RtcDateTime too_late = {2025, 1, 1, 3, 0, 0, 1};
  assert(rtc_datetime_matches_requested_or_next(requested, same));
  assert(rtc_datetime_matches_requested_or_next(requested, next));
  assert(!rtc_datetime_matches_requested_or_next(requested, too_late));
}
}  // namespace

int main() {
  test_validation_and_bcd_round_trip();
  test_invalid_ranges();
  test_one_second_rollover();
  return 0;
}
