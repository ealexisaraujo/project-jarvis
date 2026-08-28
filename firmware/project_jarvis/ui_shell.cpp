#include "ui_shell.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "location_config.h"
#include "rtc_pcf85063.h"
#include "weather_format.h"
#include "weather_service.h"
#include "wifi_service.h"

namespace {
constexpr uint16_t kDisplaySize = 360;
constexpr uint32_t kBackground = 0x07131F;
constexpr uint32_t kSurface = 0x102638;
constexpr uint32_t kCyan = 0x55D8FF;
constexpr uint32_t kBlue = 0x287DA3;
constexpr uint32_t kWhite = 0xF2FAFF;
constexpr uint32_t kRed = 0xFF5C6C;
constexpr uint32_t kAmber = 0xFFB84D;
constexpr uint32_t kMuted = 0x83A5B8;
constexpr uint8_t kTileCount = 6;

struct ClockWidgets {
  lv_obj_t* seconds_arc;
  lv_obj_t* minutes_arc;
  lv_obj_t* hours_arc;
  lv_obj_t* time_label;
  lv_obj_t* date_label;
};

struct WifiWidgets {
  lv_obj_t* clock_label;
};

struct WeatherWidgets {
  lv_obj_t* temperature;
  lv_obj_t* weather_description;
  lv_obj_t* metrics;
  lv_obj_t* weather_status;
};

ClockWidgets clock_widgets = {};
WifiWidgets wifi_widgets = {};
WeatherWidgets weather_widgets = {};
lv_obj_t* tileview_widget = nullptr;
lv_obj_t* tile_widgets[kTileCount] = {};

void permit_tile_swipe(lv_obj_t* object) {
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

lv_obj_t* make_label(lv_obj_t* parent,
                     const char* text,
                     const lv_font_t* font,
                     uint32_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  permit_tile_swipe(label);
  return label;
}

void set_label_text_if_changed(lv_obj_t* label, const char* text) {
  if (strcmp(lv_label_get_text(label), text) != 0) {
    lv_label_set_text(label, text);
  }
}

void set_arc_value_if_changed(lv_obj_t* arc, int16_t value) {
  if (lv_arc_get_value(arc) != value) {
    lv_arc_set_value(arc, value);
  }
}

lv_obj_t* add_tile(lv_obj_t* tileview, uint8_t column, lv_dir_t directions) {
  lv_obj_t* tile = lv_tileview_add_tile(tileview, column, 0, directions);
  lv_obj_set_style_bg_color(tile, lv_color_hex(kBackground), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
  permit_tile_swipe(tile);
  return tile;
}

lv_obj_t* make_clock_arc(lv_obj_t* parent, uint16_t size, uint32_t color) {
  lv_obj_t* arc = lv_arc_create(parent);
  lv_obj_set_size(arc, size, size);
  lv_obj_center(arc);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc, 4, LV_PART_INDICATOR);
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB | LV_STATE_ANY);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
  permit_tile_swipe(arc);
  return arc;
}

void update_clock(lv_timer_t* timer) {
  (void)timer;
  RtcDateTime date_time = {};
  const RtcStatus status = rtc_pcf85063_read(&date_time);
  if (status != RtcStatus::kOnline) {
    set_label_text_if_changed(clock_widgets.time_label, "--:--");
    set_label_text_if_changed(clock_widgets.date_label, "0000-00-00");
    set_arc_value_if_changed(clock_widgets.seconds_arc, 0);
    set_arc_value_if_changed(clock_widgets.minutes_arc, 0);
    set_arc_value_if_changed(clock_widgets.hours_arc, 0);
    return;
  }

  char time_text[6] = {};
  char date_text[11] = {};
  snprintf(time_text,
           sizeof(time_text),
           "%02u:%02u",
           static_cast<unsigned int>(date_time.hour),
           static_cast<unsigned int>(date_time.minute));
  snprintf(date_text,
           sizeof(date_text),
           "%04u-%02u-%02u",
           static_cast<unsigned int>(date_time.year),
           static_cast<unsigned int>(date_time.month),
           static_cast<unsigned int>(date_time.day));
  set_label_text_if_changed(clock_widgets.time_label, time_text);
  set_label_text_if_changed(clock_widgets.date_label, date_text);
  set_arc_value_if_changed(clock_widgets.seconds_arc, date_time.second);
  set_arc_value_if_changed(clock_widgets.minutes_arc, date_time.minute);
  set_arc_value_if_changed(clock_widgets.hours_arc, date_time.hour % 12);
}

void update_wifi(lv_timer_t* timer) {
  (void)timer;
  const WifiServiceSnapshot snapshot = wifi_service_snapshot();
  switch (snapshot.state) {
    case WifiServiceState::kUnconfigured:
      set_label_text_if_changed(wifi_widgets.clock_label,
                                LV_SYMBOL_REFRESH "  WiFi setup");
      break;
    case WifiServiceState::kConnecting:
      set_label_text_if_changed(wifi_widgets.clock_label,
                                LV_SYMBOL_REFRESH "  Connecting...");
      break;
    case WifiServiceState::kOnline:
      set_label_text_if_changed(wifi_widgets.clock_label,
                                LV_SYMBOL_REFRESH "  WiFi online");
      break;
    case WifiServiceState::kFailed:
    case WifiServiceState::kDisconnected:
      set_label_text_if_changed(wifi_widgets.clock_label,
                                LV_SYMBOL_REFRESH "  Retry WiFi");
      break;
  }
}

void update_weather(lv_timer_t* timer) {
  (void)timer;
  const WeatherServiceSnapshot snapshot = weather_service_snapshot();
  if (snapshot.has_valid_data) {
    char temperature_text[12] = {};
    char metrics_text[48] = {};
    if (weather_format_temperature(temperature_text,
                                   sizeof(temperature_text),
                                   snapshot.values.temperature_c)) {
      set_label_text_if_changed(weather_widgets.temperature, temperature_text);
    }
    set_label_text_if_changed(weather_widgets.weather_description,
                              weather_code_description(snapshot.values.weather_code));
    if (weather_format_metrics(metrics_text,
                               sizeof(metrics_text),
                               snapshot.values.humidity_percent,
                               snapshot.values.wind_speed_kmh,
                               snapshot.values.surface_pressure_hpa)) {
      set_label_text_if_changed(weather_widgets.metrics, metrics_text);
    }
  } else {
    set_label_text_if_changed(weather_widgets.temperature, "-- C");
    set_label_text_if_changed(weather_widgets.metrics,
                              "H --%  W -- km/h  P -- hPa");
  }

  switch (snapshot.state) {
    case WeatherServiceState::kWaitingWifi:
      if (!snapshot.has_valid_data) {
        set_label_text_if_changed(weather_widgets.weather_description,
                                  "Waiting for WiFi");
      }
      set_label_text_if_changed(weather_widgets.weather_status,
                                "Waiting for WiFi");
      break;
    case WeatherServiceState::kFetching:
      if (!snapshot.has_valid_data) {
        set_label_text_if_changed(weather_widgets.weather_description,
                                  "Fetching weather");
      }
      set_label_text_if_changed(weather_widgets.weather_status, "Fetching...");
      break;
    case WeatherServiceState::kOnline: {
      char updated_text[20] = {};
      if (weather_format_observation_time(updated_text,
                                          sizeof(updated_text),
                                          snapshot.observation_time)) {
        set_label_text_if_changed(weather_widgets.weather_status, updated_text);
      }
      break;
    }
    case WeatherServiceState::kFailed:
      if (!snapshot.has_valid_data) {
        set_label_text_if_changed(weather_widgets.weather_description,
                                  "Weather unavailable");
      }
      set_label_text_if_changed(weather_widgets.weather_status, "Update failed");
      break;
    case WeatherServiceState::kOfflineCached:
      set_label_text_if_changed(weather_widgets.weather_status,
                                "Offline - cached");
      break;
  }
}

void handle_wifi_card_event(lv_event_t* event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    wifi_service_request_connect();
  }
}

void create_clock_tile(lv_obj_t* tile) {
  clock_widgets.seconds_arc = make_clock_arc(tile, 356, kBlue);
  lv_arc_set_range(clock_widgets.seconds_arc, 0, 59);
  clock_widgets.minutes_arc = make_clock_arc(tile, 326, kWhite);
  lv_arc_set_range(clock_widgets.minutes_arc, 0, 59);
  clock_widgets.hours_arc = make_clock_arc(tile, 296, kRed);
  lv_arc_set_range(clock_widgets.hours_arc, 0, 11);

  clock_widgets.time_label = make_label(tile, "--:--", &lv_font_montserrat_48, kWhite);
  lv_obj_align(clock_widgets.time_label, LV_ALIGN_CENTER, 0, -51);

  clock_widgets.date_label =
      make_label(tile, "0000-00-00", &lv_font_montserrat_22, kWhite);
  lv_obj_align(clock_widgets.date_label, LV_ALIGN_CENTER, 0, 4);

  lv_obj_t* wifi_card = lv_obj_create(tile);
  lv_obj_remove_style_all(wifi_card);
  lv_obj_set_size(wifi_card, 174, 42);
  lv_obj_set_style_radius(wifi_card, 21, LV_PART_MAIN);
  lv_obj_set_style_bg_color(wifi_card, lv_color_hex(kSurface), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(wifi_card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(wifi_card, lv_color_hex(kBlue), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_align(wifi_card, LV_ALIGN_CENTER, 0, 64);
  lv_obj_clear_flag(wifi_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(wifi_card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(wifi_card, handle_wifi_card_event, LV_EVENT_CLICKED, nullptr);

  wifi_widgets.clock_label =
      make_label(wifi_card, LV_SYMBOL_REFRESH "  WiFi...", &lv_font_montserrat_14, kCyan);
  lv_obj_center(wifi_widgets.clock_label);

  lv_timer_create(update_clock, 1000, nullptr);
  update_clock(nullptr);
}

void create_weather_tile(lv_obj_t* tile) {
  const LocationProfile& location = active_location_profile();
  const char* city_name =
      location_profile_is_valid(location) ? location.display_name
                                          : "Invalid location";
  lv_obj_t* city =
      make_label(tile, city_name, &lv_font_montserrat_22, kWhite);
  lv_obj_align(city, LV_ALIGN_CENTER, 0, -92);

  weather_widgets.temperature =
      make_label(tile, "-- C", &lv_font_montserrat_48, kAmber);
  lv_obj_align(weather_widgets.temperature, LV_ALIGN_CENTER, 0, -34);

  weather_widgets.weather_description =
      make_label(tile, "Waiting for WiFi", &lv_font_montserrat_20, kWhite);
  lv_obj_align(weather_widgets.weather_description, LV_ALIGN_CENTER, 0, 24);

  weather_widgets.metrics = make_label(tile,
                                       "H --%  W -- km/h  P -- hPa",
                                       &lv_font_montserrat_14,
                                       kMuted);
  lv_obj_align(weather_widgets.metrics, LV_ALIGN_CENTER, 0, 65);

  weather_widgets.weather_status =
      make_label(tile, "Waiting for WiFi", &lv_font_montserrat_14, kCyan);
  lv_obj_align(weather_widgets.weather_status, LV_ALIGN_CENTER, 0, 101);
}

void create_placeholder_tile(lv_obj_t* tile, const char* title) {
  lv_obj_t* label = make_label(tile, title, &lv_font_montserrat_22, kWhite);
  lv_obj_center(label);
}
}  // namespace

void ui_shell_create() {
  lv_obj_t* screen = lv_scr_act();
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  tileview_widget = lv_tileview_create(screen);
  lv_obj_remove_style_all(tileview_widget);
  lv_obj_set_size(tileview_widget, kDisplaySize, kDisplaySize);
  lv_obj_set_pos(tileview_widget, 0, 0);
  lv_obj_set_scroll_dir(tileview_widget, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(tileview_widget, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scroll_snap_y(tileview_widget, LV_SCROLL_SNAP_NONE);
  lv_obj_set_scrollbar_mode(tileview_widget, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(tileview_widget, LV_OBJ_FLAG_SCROLL_ONE);

  tile_widgets[0] = add_tile(tileview_widget, 0, LV_DIR_RIGHT);
  tile_widgets[1] = add_tile(tileview_widget, 1, LV_DIR_LEFT | LV_DIR_RIGHT);
  tile_widgets[2] = add_tile(tileview_widget, 2, LV_DIR_LEFT | LV_DIR_RIGHT);
  tile_widgets[3] = add_tile(tileview_widget, 3, LV_DIR_LEFT | LV_DIR_RIGHT);
  tile_widgets[4] = add_tile(tileview_widget, 4, LV_DIR_LEFT | LV_DIR_RIGHT);
  tile_widgets[5] = add_tile(tileview_widget, 5, LV_DIR_LEFT);

  create_clock_tile(tile_widgets[0]);
  create_weather_tile(tile_widgets[1]);
  create_placeholder_tile(tile_widgets[2], "Bins");
  create_placeholder_tile(tile_widgets[3], "Alarm");
  create_placeholder_tile(tile_widgets[4], "Radio");
  create_placeholder_tile(tile_widgets[5], "MP3");

  lv_timer_create(update_wifi, 500, nullptr);
  update_wifi(nullptr);
  lv_timer_create(update_weather, 500, nullptr);
  update_weather(nullptr);
  lv_obj_update_layout(tileview_widget);
  lv_obj_set_tile(tileview_widget, tile_widgets[0], LV_ANIM_OFF);

  Serial.printf("ui_heap_internal_free=%u\n",
                static_cast<unsigned int>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
}

bool ui_shell_select_tile(uint8_t index) {
  if (index >= kTileCount || tileview_widget == nullptr || tile_widgets[index] == nullptr) {
    return false;
  }

  lv_obj_update_layout(tileview_widget);
  lv_obj_set_tile(tileview_widget, tile_widgets[index], LV_ANIM_OFF);
  lv_refr_now(nullptr);
  return true;
}
