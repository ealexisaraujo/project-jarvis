#pragma once

#include <stdint.h>

enum class WifiServiceState : uint8_t {
  kUnconfigured,
  kConnecting,
  kOnline,
  kFailed,
  kDisconnected,
};

struct WifiServiceSnapshot {
  WifiServiceState state;
  int status_code;
  char ipv4[16];
};

void wifi_service_begin();
void wifi_service_loop();
void wifi_service_request_connect();
WifiServiceSnapshot wifi_service_snapshot();
