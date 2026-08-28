#include "rtc_pcf85063.h"

#include <Arduino.h>
#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>

namespace {
constexpr i2c_port_t kRtcI2cPort = I2C_NUM_0;
constexpr uint8_t kRtcAddress = 0x51;
constexpr uint8_t kSecondsRegister = 0x04;
constexpr TickType_t kTransactionTimeout = pdMS_TO_TICKS(50);

const char* status_name(RtcStatus status) {
  switch (status) {
    case RtcStatus::kOnline:
      return "online";
    case RtcStatus::kInvalid:
      return "invalid";
    case RtcStatus::kOffline:
      return "offline";
  }
  return "offline";
}
}  // namespace

RtcStatus rtc_pcf85063_read(RtcDateTime* date_time) {
  if (date_time == nullptr) {
    return RtcStatus::kInvalid;
  }

  uint8_t registers[kRtcTimeRegisterCount] = {};
  const esp_err_t result = i2c_master_write_read_device(kRtcI2cPort,
                                                        kRtcAddress,
                                                        &kSecondsRegister,
                                                        1,
                                                        registers,
                                                        sizeof(registers),
                                                        kTransactionTimeout);
  if (result != ESP_OK) {
    return RtcStatus::kOffline;
  }

  RtcDateTime candidate = {};
  if (!rtc_datetime_decode_registers(registers, &candidate)) {
    return RtcStatus::kInvalid;
  }

  *date_time = candidate;
  return RtcStatus::kOnline;
}

RtcSetStatus rtc_pcf85063_write_and_verify(const RtcDateTime& date_time) {
  uint8_t write_buffer[kRtcTimeRegisterCount + 1] = {kSecondsRegister};
  if (!rtc_datetime_encode_registers(date_time, write_buffer + 1)) {
    return RtcSetStatus::kInvalid;
  }

  const esp_err_t write_result = i2c_master_write_to_device(kRtcI2cPort,
                                                            kRtcAddress,
                                                            write_buffer,
                                                            sizeof(write_buffer),
                                                            kTransactionTimeout);
  if (write_result != ESP_OK) {
    return RtcSetStatus::kOffline;
  }

  RtcDateTime readback = {};
  const RtcStatus read_status = rtc_pcf85063_read(&readback);
  if (read_status == RtcStatus::kOffline) {
    return RtcSetStatus::kOffline;
  }
  if (read_status != RtcStatus::kOnline) {
    return RtcSetStatus::kInvalid;
  }
  if (!rtc_datetime_matches_requested_or_next(date_time, readback)) {
    return RtcSetStatus::kMismatch;
  }
  return RtcSetStatus::kOnline;
}

RtcStatus rtc_pcf85063_begin() {
  RtcDateTime date_time = {};
  const RtcStatus status = rtc_pcf85063_read(&date_time);
  Serial.println("rtc_bus=i2c0_sda11_scl10_shared");
  Serial.printf("rtc_status=%s\n", status_name(status));
  return status;
}
