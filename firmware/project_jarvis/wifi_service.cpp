#include "wifi_service.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#ifndef PROJECT_JARVIS_DISABLE_LOCAL_SECRETS
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#endif

#ifndef PROJECT_JARVIS_WIFI_SSID
#define PROJECT_JARVIS_WIFI_SSID ""
#endif

#ifndef PROJECT_JARVIS_WIFI_PASSWORD
#define PROJECT_JARVIS_WIFI_PASSWORD ""
#endif

namespace {
constexpr uint32_t kConnectionTimeoutMs = 15000;

WifiServiceSnapshot current_snapshot = {
    WifiServiceState::kUnconfigured,
    WL_NO_SHIELD,
    "",
};
uint32_t attempt_started_ms = 0;
bool credentials_available = false;
bool retry_requested = false;

void set_snapshot(WifiServiceState state, int status_code, const char* ipv4 = "") {
  const bool changed = current_snapshot.state != state ||
                       current_snapshot.status_code != status_code ||
                       strncmp(current_snapshot.ipv4, ipv4, sizeof(current_snapshot.ipv4)) != 0;
  if (!changed) {
    return;
  }

  current_snapshot.state = state;
  current_snapshot.status_code = status_code;
  strlcpy(current_snapshot.ipv4, ipv4, sizeof(current_snapshot.ipv4));

  switch (state) {
    case WifiServiceState::kUnconfigured:
      Serial.println("wifi_status=unconfigured");
      break;
    case WifiServiceState::kConnecting:
      Serial.println("wifi_status=connecting");
      break;
    case WifiServiceState::kOnline:
      Serial.printf("wifi_status=online ip=%s\n", current_snapshot.ipv4);
      break;
    case WifiServiceState::kFailed:
      Serial.printf("wifi_status=failed reason=%d\n", status_code);
      break;
    case WifiServiceState::kDisconnected:
      Serial.printf("wifi_status=disconnected reason=%d\n", status_code);
      break;
  }
}

void start_connection() {
  retry_requested = false;
  attempt_started_ms = millis();
  set_snapshot(WifiServiceState::kConnecting, WL_IDLE_STATUS);
  WiFi.disconnect(false, false);
  WiFi.begin(PROJECT_JARVIS_WIFI_SSID, PROJECT_JARVIS_WIFI_PASSWORD);
}

void set_online_snapshot() {
  const IPAddress address = WiFi.localIP();
  char ipv4[16] = {};
  snprintf(ipv4,
           sizeof(ipv4),
           "%u.%u.%u.%u",
           static_cast<unsigned int>(address[0]),
           static_cast<unsigned int>(address[1]),
           static_cast<unsigned int>(address[2]),
           static_cast<unsigned int>(address[3]));
  set_snapshot(WifiServiceState::kOnline, WL_CONNECTED, ipv4);
}
}  // namespace

void wifi_service_begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  credentials_available = PROJECT_JARVIS_WIFI_SSID[0] != '\0' &&
                          PROJECT_JARVIS_WIFI_PASSWORD[0] != '\0';
  if (!credentials_available) {
    set_snapshot(WifiServiceState::kUnconfigured, WL_NO_SSID_AVAIL);
    return;
  }

  start_connection();
}

void wifi_service_loop() {
  if (!credentials_available) {
    return;
  }

  if (retry_requested) {
    start_connection();
  }

  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    set_online_snapshot();
    return;
  }

  if (current_snapshot.state == WifiServiceState::kOnline) {
    set_snapshot(WifiServiceState::kDisconnected, status);
    return;
  }

  if (current_snapshot.state != WifiServiceState::kConnecting) {
    return;
  }

  const bool terminal_failure =
      status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL;
  const bool timed_out = millis() - attempt_started_ms >= kConnectionTimeoutMs;
  if (terminal_failure || timed_out) {
    WiFi.disconnect(false, false);
    set_snapshot(WifiServiceState::kFailed, status);
  }
}

void wifi_service_request_connect() {
  if (!credentials_available) {
    return;
  }
  retry_requested = true;
}

WifiServiceSnapshot wifi_service_snapshot() {
  return current_snapshot;
}
