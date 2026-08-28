#include "weather_service.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

#include "location_config.h"
#include "wifi_service.h"
#include "weather_response_reader.h"

namespace {
constexpr uint32_t kSuccessRefreshIntervalMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kFailureRetryIntervalMs = 60UL * 1000UL;
constexpr uint32_t kHttpTimeoutMs = 10000;
constexpr uint32_t kWorkerStackBytes = 8192;

constexpr int kErrorTaskStart = -1001;
constexpr int kErrorHttpBegin = -1002;
constexpr int kErrorResponseSize = -1003;
constexpr int kErrorJson = -1004;
constexpr int kErrorMissingField = -1005;
constexpr int kErrorInvalidValues = -1006;
constexpr int kErrorInvalidTime = -1007;
constexpr int kErrorInvalidLocation = -1008;

struct WorkerResult {
  bool success;
  int error_code;
  WeatherValues values;
  char observation_time[20];
};

portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
WeatherServiceSnapshot current_snapshot = {
    WeatherServiceState::kWaitingWifi,
    0,
    false,
    {0.0F, 0, 0, 0.0F, 0.0F},
    "",
};
WorkerResult pending_result = {};
bool pending_result_ready = false;
bool worker_running = false;
bool service_started = false;
bool has_attempted = false;
bool last_attempt_succeeded = false;
int last_attempt_error_code = 0;
uint32_t last_attempt_ms = 0;

const char* state_name(WeatherServiceState state) {
  switch (state) {
    case WeatherServiceState::kWaitingWifi:
      return "waiting_wifi";
    case WeatherServiceState::kFetching:
      return "fetching";
    case WeatherServiceState::kOnline:
      return "online";
    case WeatherServiceState::kFailed:
      return "failed";
    case WeatherServiceState::kOfflineCached:
      return "offline_cached";
  }
  return "failed";
}

void report_state(WeatherServiceState state, int error_code) {
  if (state == WeatherServiceState::kFailed) {
    Serial.printf("weather_status=%s reason=%d\n", state_name(state), error_code);
    return;
  }
  Serial.printf("weather_status=%s\n", state_name(state));
}

void set_state(WeatherServiceState state, int error_code = 0) {
  bool changed = false;
  portENTER_CRITICAL(&state_mux);
  if (current_snapshot.state != state || current_snapshot.error_code != error_code) {
    current_snapshot.state = state;
    current_snapshot.error_code = error_code;
    changed = true;
  }
  portEXIT_CRITICAL(&state_mux);
  if (changed) {
    report_state(state, error_code);
  }
}

void publish_success(const WorkerResult& result) {
  portENTER_CRITICAL(&state_mux);
  current_snapshot.state = WeatherServiceState::kOnline;
  current_snapshot.error_code = 0;
  current_snapshot.has_valid_data = true;
  current_snapshot.values = result.values;
  strlcpy(current_snapshot.observation_time,
          result.observation_time,
          sizeof(current_snapshot.observation_time));
  portEXIT_CRITICAL(&state_mux);
  report_state(WeatherServiceState::kOnline, 0);
}

template <typename TReader>
int deserialize_weather_document(JsonDocument& document, TReader& reader) {
  const DeserializationError json_error = deserializeJson(
      document,
      reader,
      DeserializationOption::NestingLimit(4));
  return json_error ? kErrorJson : 0;
}

WorkerResult fetch_weather() {
  WorkerResult result = {};
  result.error_code = kErrorInvalidLocation;

  char forecast_url[kOpenMeteoForecastUrlCapacity] = {};
  if (!location_build_open_meteo_url(active_location_profile(),
                                     forecast_url,
                                     sizeof(forecast_url))) {
    return result;
  }
  result.error_code = kErrorHttpBegin;

  WiFiClientSecure secure_client;
  // Prototype limitation: the pinned ESP32 core has no project-managed CA
  // bundle. The request contains no credential or private payload, so TLS is
  // encrypted but its server certificate is not authenticated.
  secure_client.setInsecure();

  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(kHttpTimeoutMs);
  http.setTimeout(kHttpTimeoutMs);
  if (!http.begin(secure_client, forecast_url)) {
    return result;
  }

  const char* response_header_keys[] = {"Transfer-Encoding"};
  http.collectHeaders(response_header_keys, 1);

  const int http_status = http.GET();
  if (http_status != HTTP_CODE_OK) {
    result.error_code = http_status;
    http.end();
    return result;
  }

  const int response_size = http.getSize();
  if (response_size > static_cast<int>(kMaximumWeatherResponseBytes)) {
    result.error_code = kErrorResponseSize;
    http.end();
    return result;
  }

  Stream* const response_stream = http.getStreamPtr();
  if (response_stream == nullptr) {
    result.error_code = kErrorJson;
    http.end();
    return result;
  }

  JsonDocument document;
  int response_error = 0;
  const String transfer_encoding = http.header("Transfer-Encoding");
  if (transfer_encoding.equalsIgnoreCase("chunked")) {
    ChunkedBodyReader<Stream> decoded_reader(*response_stream);
    BoundedStreamReader<ChunkedBodyReader<Stream>> response_reader(
        decoded_reader,
        kMaximumWeatherResponseBytes);
    response_error = deserialize_weather_document(document, response_reader);
    if (response_reader.finish() == BoundedReaderFinish::kLimitExceeded) {
      response_error = kErrorResponseSize;
    } else if (!decoded_reader.complete() || decoded_reader.failed()) {
      response_error = kErrorJson;
    }
  } else {
    const bool has_known_size = response_size >= 0;
    const size_t reader_limit = has_known_size
                                    ? static_cast<size_t>(response_size)
                                    : kMaximumWeatherResponseBytes;
    BoundedStreamReader<Stream> response_reader(*response_stream, reader_limit);
    response_error = deserialize_weather_document(document, response_reader);
    if (!has_known_size &&
        response_reader.finish() == BoundedReaderFinish::kLimitExceeded) {
      response_error = kErrorResponseSize;
    }
  }
  if (response_error != 0) {
    result.error_code = response_error;
    http.end();
    return result;
  }

  const JsonVariantConst current = document["current"];
  const JsonVariantConst temperature = current["temperature_2m"];
  const JsonVariantConst humidity = current["relative_humidity_2m"];
  const JsonVariantConst weather_code = current["weather_code"];
  const JsonVariantConst wind_speed = current["wind_speed_10m"];
  const JsonVariantConst pressure = current["surface_pressure"];
  const JsonVariantConst observation_time = current["time"];
  if (!current.is<JsonObjectConst>() || !temperature.is<float>() ||
      !humidity.is<int>() || !weather_code.is<int>() || !wind_speed.is<float>() ||
      !pressure.is<float>() || !observation_time.is<const char*>()) {
    result.error_code = kErrorMissingField;
    http.end();
    return result;
  }

  result.values = {
      temperature.as<float>(),
      humidity.as<int>(),
      weather_code.as<int>(),
      wind_speed.as<float>(),
      pressure.as<float>(),
  };
  if (!weather_values_are_plausible(result.values)) {
    result.error_code = kErrorInvalidValues;
    http.end();
    return result;
  }

  const char* time_text = observation_time.as<const char*>();
  char formatted_time[20] = {};
  if (strlen(time_text) >= sizeof(result.observation_time) ||
      !weather_format_observation_time(formatted_time,
                                       sizeof(formatted_time),
                                       time_text)) {
    result.error_code = kErrorInvalidTime;
    http.end();
    return result;
  }

  strlcpy(result.observation_time, time_text, sizeof(result.observation_time));
  result.success = true;
  result.error_code = 0;
  http.end();
  return result;
}

void weather_worker_task(void* parameter) {
  (void)parameter;
  const WorkerResult result = fetch_weather();
  portENTER_CRITICAL(&state_mux);
  pending_result = result;
  pending_result_ready = true;
  worker_running = false;
  portEXIT_CRITICAL(&state_mux);
  vTaskDelete(nullptr);
}

bool is_worker_running() {
  portENTER_CRITICAL(&state_mux);
  const bool running = worker_running;
  portEXIT_CRITICAL(&state_mux);
  return running;
}

bool take_pending_result(WorkerResult* result) {
  portENTER_CRITICAL(&state_mux);
  const bool ready = pending_result_ready;
  if (ready) {
    *result = pending_result;
    pending_result_ready = false;
  }
  portEXIT_CRITICAL(&state_mux);
  return ready;
}

bool start_worker() {
  portENTER_CRITICAL(&state_mux);
  worker_running = true;
  portEXIT_CRITICAL(&state_mux);

  const BaseType_t task_result = xTaskCreate(weather_worker_task,
                                             "jarvis_weather",
                                             kWorkerStackBytes,
                                             nullptr,
                                             1,
                                             nullptr);
  if (task_result == pdPASS) {
    return true;
  }

  portENTER_CRITICAL(&state_mux);
  worker_running = false;
  portEXIT_CRITICAL(&state_mux);
  return false;
}
}  // namespace

void weather_service_begin() {
  service_started = true;
  has_attempted = false;
  last_attempt_succeeded = false;
  last_attempt_error_code = 0;
  last_attempt_ms = 0;
  portENTER_CRITICAL(&state_mux);
  current_snapshot = {
      WeatherServiceState::kWaitingWifi,
      0,
      false,
      {0.0F, 0, 0, 0.0F, 0.0F},
      "",
  };
  pending_result_ready = false;
  worker_running = false;
  portEXIT_CRITICAL(&state_mux);
  report_state(WeatherServiceState::kWaitingWifi, 0);
}

void weather_service_loop() {
  if (!service_started) {
    return;
  }

  WorkerResult result = {};
  if (take_pending_result(&result)) {
    last_attempt_succeeded = result.success;
    last_attempt_error_code = result.error_code;
    if (result.success) {
      publish_success(result);
    } else {
      set_state(WeatherServiceState::kFailed, result.error_code);
    }
  }

  const WifiServiceSnapshot wifi = wifi_service_snapshot();
  if (wifi.state != WifiServiceState::kOnline) {
    const WeatherServiceSnapshot snapshot = weather_service_snapshot();
    set_state(snapshot.has_valid_data ? WeatherServiceState::kOfflineCached
                                      : WeatherServiceState::kWaitingWifi);
    return;
  }

  if (is_worker_running()) {
    set_state(WeatherServiceState::kFetching);
    return;
  }

  const uint32_t now_ms = millis();
  const uint32_t required_interval =
      last_attempt_succeeded ? kSuccessRefreshIntervalMs : kFailureRetryIntervalMs;
  const bool fetch_due = !has_attempted || now_ms - last_attempt_ms >= required_interval;
  if (!fetch_due) {
    const WeatherServiceSnapshot snapshot = weather_service_snapshot();
    if (has_attempted && !last_attempt_succeeded) {
      set_state(WeatherServiceState::kFailed, last_attempt_error_code);
    } else if (snapshot.has_valid_data &&
               snapshot.state == WeatherServiceState::kOfflineCached) {
      set_state(WeatherServiceState::kOnline);
    }
    return;
  }

  has_attempted = true;
  last_attempt_ms = now_ms;
  set_state(WeatherServiceState::kFetching);
  if (!start_worker()) {
    last_attempt_succeeded = false;
    last_attempt_error_code = kErrorTaskStart;
    set_state(WeatherServiceState::kFailed, kErrorTaskStart);
  }
}

WeatherServiceSnapshot weather_service_snapshot() {
  portENTER_CRITICAL(&state_mux);
  const WeatherServiceSnapshot snapshot = current_snapshot;
  portEXIT_CRITICAL(&state_mux);
  return snapshot;
}
