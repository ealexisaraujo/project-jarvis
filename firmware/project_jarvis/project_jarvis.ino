#include <Arduino.h>
#include <string.h>

#include "display.h"
#include "time_sync_service.h"
#include "ui_shell.h"
#include "wifi_service.h"

namespace {
constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kHeartbeatIntervalMs = 2000;
constexpr size_t kCommandBufferSize = 64;
constexpr size_t kSerialBytesPerLoop = 64;
uint32_t last_heartbeat_ms = 0;
bool display_online = false;
char command_buffer[kCommandBufferSize];
size_t command_length = 0;
bool discarding_command = false;

void run_command(const char* command) {
  if (strcmp(command, "help") == 0) {
    Serial.println("commands=help,screenshot,tile N (0..5)");
    return;
  }

  if (strcmp(command, "screenshot") == 0) {
    if (!display_send_screenshot(Serial)) {
      Serial.println("SCREENSHOT_ERROR framebuffer_unavailable");
    }
    return;
  }

  if (strncmp(command, "tile", 4) == 0) {
    if (strlen(command) != 6 || command[4] != ' ' || command[5] < '0' ||
        command[5] > '5') {
      Serial.println("tile_error=invalid_index expected=0..5");
      return;
    }

    const uint8_t tile_index = static_cast<uint8_t>(command[5] - '0');
    if (!ui_shell_select_tile(tile_index)) {
      Serial.printf("tile_error=unavailable index=%u\n", tile_index);
      return;
    }
    Serial.printf("tile_selected=%u\n", tile_index);
    return;
  }

  if (command[0] != '\0') {
    Serial.printf("command_error=unknown command=%s\n", command);
  }
}

void process_serial_commands() {
  for (size_t processed = 0;
       processed < kSerialBytesPerLoop && Serial.available() > 0;
       ++processed) {
    const int value = Serial.read();
    if (value < 0) {
      break;
    }

    const char character = static_cast<char>(value);
    if (character == '\r') {
      continue;
    }

    if (character == '\n') {
      if (discarding_command) {
        Serial.println("command_error=line_too_long");
      } else {
        command_buffer[command_length] = '\0';
        run_command(command_buffer);
      }
      command_length = 0;
      discarding_command = false;
      continue;
    }

    if (discarding_command) {
      continue;
    }

    if (command_length + 1 >= kCommandBufferSize) {
      command_length = 0;
      discarding_command = true;
      continue;
    }
    command_buffer[command_length++] = character;
  }
}
}  // namespace

void setup() {
  Serial.begin(kSerialBaud);
  delay(500);

  Serial.println();
  Serial.println("Project Jarvis booting");
  Serial.printf("Chip: %s, revision: %d, cores: %d\n",
                ESP.getChipModel(),
                ESP.getChipRevision(),
                ESP.getChipCores());
  Serial.printf("Flash: %u bytes, PSRAM: %u bytes\n",
                ESP.getFlashChipSize(),
                ESP.getPsramSize());
  Serial.println("Milestone 4.1: initializing display, touch, RTC, UI, WiFi, and NTP");
  display_online = display_begin();
  if (display_online) {
    wifi_service_begin();
    time_sync_service_begin();
  }
  const bool boot_ready = display_online && display_touch_online();
  Serial.printf("boot_status=%s\n", boot_ready ? "ready" : "degraded");
}

void loop() {
  process_serial_commands();
  wifi_service_loop();
  time_sync_service_loop();

  const uint32_t now = millis();
  if (now - last_heartbeat_ms >= kHeartbeatIntervalMs) {
    last_heartbeat_ms = now;
    Serial.printf("heartbeat uptime_ms=%u free_heap=%u display=%s touch=%s detail=%s\n",
                  now,
                  ESP.getFreeHeap(),
                  display_online ? "online" : "offline",
                  display_touch_status(),
                  display_status());
  }
  display_loop();
}
