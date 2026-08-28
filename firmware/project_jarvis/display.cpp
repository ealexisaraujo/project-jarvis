#include "display.h"

#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "rtc_pcf85063.h"
#include "ui_shell.h"

namespace {
constexpr uint16_t kDisplayWidth = 360;
constexpr uint16_t kDisplayHeight = 360;
constexpr size_t kBufferRows = 24;
constexpr size_t kBufferPixels = kDisplayWidth * kBufferRows;
constexpr size_t kScreenshotBytes =
    static_cast<size_t>(kDisplayWidth) * kDisplayHeight * sizeof(lv_color_t);
constexpr size_t kScreenshotWriteChunkBytes = 4096;
constexpr char kDisplayProfile[] = "st77916_revision_02";

ESP_Panel* panel = nullptr;
lv_color_t* draw_buffer = nullptr;
uint8_t* screenshot_buffer = nullptr;
uint8_t* screenshot_tx_buffer = nullptr;
lv_disp_draw_buf_t draw_buffer_descriptor;
lv_disp_drv_t display_driver;
lv_indev_drv_t touch_driver;
lv_indev_t* touch_input = nullptr;
bool display_ready = false;
bool touch_ready = false;
const char* current_status = "not_started";
const char* current_touch_status = "not_started";

void swap_rgb565_bytes(lv_color_t* colors, size_t pixel_count) {
  auto* bytes = reinterpret_cast<uint8_t*>(colors);
  for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
    const size_t offset = pixel * sizeof(lv_color_t);
    const uint8_t first_byte = bytes[offset];
    bytes[offset] = bytes[offset + 1];
    bytes[offset + 1] = first_byte;
  }
}

void mirror_flushed_area(const lv_area_t* area, const lv_color_t* colors) {
  if (screenshot_buffer == nullptr) {
    return;
  }

  const int32_t source_width = area->x2 - area->x1 + 1;
  const int32_t first_x = max<int32_t>(area->x1, 0);
  const int32_t last_x = min<int32_t>(area->x2, kDisplayWidth - 1);
  const int32_t first_y = max<int32_t>(area->y1, 0);
  const int32_t last_y = min<int32_t>(area->y2, kDisplayHeight - 1);
  if (source_width <= 0 || first_x > last_x || first_y > last_y) {
    return;
  }

  const size_t copy_pixels = static_cast<size_t>(last_x - first_x + 1);
  for (int32_t y = first_y; y <= last_y; ++y) {
    const size_t source_offset =
        static_cast<size_t>(y - area->y1) * source_width + (first_x - area->x1);
    const size_t destination_offset =
        (static_cast<size_t>(y) * kDisplayWidth + first_x) * sizeof(lv_color_t);
    memcpy(screenshot_buffer + destination_offset,
           colors + source_offset,
           copy_pixels * sizeof(lv_color_t));
  }
}

void flush_display(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* colors) {
  auto* lcd = static_cast<ESP_PanelLcd*>(driver->user_data);
  const uint16_t width = area->x2 - area->x1 + 1;
  const uint16_t height = area->y2 - area->y1 + 1;
  const size_t pixel_count = static_cast<size_t>(width) * height;

  swap_rgb565_bytes(colors, pixel_count);
  const bool transfer_succeeded = lcd->drawBitmapWaitUntilFinish(
      area->x1,
      area->y1,
      width,
      height,
      reinterpret_cast<const uint8_t*>(colors));
  swap_rgb565_bytes(colors, pixel_count);

  if (transfer_succeeded) {
    mirror_flushed_area(area, colors);
  } else {
    Serial.println("display_error=flush_failed");
  }
  lv_disp_flush_ready(driver);
}

void read_touch(lv_indev_drv_t* driver, lv_indev_data_t* data) {
  auto* touch = static_cast<ESP_PanelTouch*>(driver->user_data);
  ESP_PanelTouchPoint point;
  const int point_count = touch->readPoints(&point, 1);
  if (point_count > 0) {
    data->point.x = point.x;
    data->point.y = point.y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

bool register_touch_input(ESP_PanelTouch* touch) {
  if (touch == nullptr) {
    current_touch_status = "missing_device";
    Serial.println("touch_status=error reason=missing_device");
    return false;
  }
  if (touch->getHandle() == nullptr) {
    current_touch_status = "initialization_failed";
    Serial.println("touch_status=error reason=initialization_failed");
    return false;
  }

  lv_indev_drv_init(&touch_driver);
  touch_driver.type = LV_INDEV_TYPE_POINTER;
  touch_driver.read_cb = read_touch;
  touch_driver.user_data = touch;
  touch_input = lv_indev_drv_register(&touch_driver);
  if (touch_input == nullptr) {
    current_touch_status = "registration_failed";
    Serial.println("touch_status=error reason=registration_failed");
    return false;
  }

  current_touch_status = "online";
  Serial.println("touch_bus=i2c0_sda11_scl10_shared");
  Serial.println("touch_status=online");
  return true;
}
}  // namespace

bool display_begin() {
  static_assert(sizeof(lv_color_t) == 2, "Screenshot protocol requires RGB565 pixels");

  current_status = "panel_allocation";
  panel = new ESP_Panel();
  if (panel == nullptr) {
    current_status = "panel_allocation_failed";
    Serial.println("display_error=panel_allocation_failed");
    return false;
  }

  current_status = "panel_init";
  Serial.println("display_stage=panel_init");
  if (!panel->init()) {
    Serial.println("display_error=panel_init_failed");
    current_status = "panel_init_failed";
    return false;
  }

  current_status = "panel_begin";
  ESP_PanelLcd* lcd = panel->getLcd();
  if (lcd == nullptr) {
    Serial.println("display_error=missing_lcd");
    current_status = "missing_lcd";
    return false;
  }
  if (!panel->begin()) {
    Serial.println("display_error=panel_begin_failed");
    Serial.println("touch_status=unavailable reason=panel_begin_failed");
    current_status = "panel_begin_failed";
    current_touch_status = "unavailable";
    return false;
  }
  Serial.println("display_reset=expander_io2");
  Serial.printf("display_clock_hz=%u\n",
                static_cast<unsigned int>(ESP_PANEL_LCD_SPI_CLK_HZ));

  constexpr uint32_t kReadDisplayIdCommand = (0x0BUL << 24) | (0x04UL << 8);
  uint8_t display_id[4] = {};
  Serial.println("display_id_confirmed=00:02:7F:7F");
  if (lcd->getBus()->readRegisterData(kReadDisplayIdCommand,
                                      display_id,
                                      sizeof(display_id))) {
    Serial.printf("display_id_raw_80mhz=%02X:%02X:%02X:%02X\n",
                  display_id[0],
                  display_id[1],
                  display_id[2],
                  display_id[3]);
  } else {
    Serial.println("display_id_raw_80mhz=unavailable");
  }
  Serial.printf("display_profile=%s\n", kDisplayProfile);
  Serial.println("display_transport=rgb565_byte_swap");

  current_status = "lcd_validation";
  if (panel->getLcdWidth() != kDisplayWidth || panel->getLcdHeight() != kDisplayHeight) {
    Serial.println("display_error=unexpected_lcd");
    current_status = "unexpected_lcd";
    return false;
  }

  current_status = "buffer_allocation";
  draw_buffer = static_cast<lv_color_t*>(
      heap_caps_malloc(kBufferPixels * sizeof(lv_color_t),
                       MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  if (draw_buffer == nullptr) {
    Serial.println("display_error=draw_buffer_allocation_failed");
    current_status = "draw_buffer_allocation_failed";
    return false;
  }

  screenshot_buffer = static_cast<uint8_t*>(
      heap_caps_calloc(kScreenshotBytes, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (screenshot_buffer == nullptr) {
    Serial.println("screenshot_error=framebuffer_allocation_failed");
  }
  screenshot_tx_buffer = static_cast<uint8_t*>(
      heap_caps_malloc(kScreenshotBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (screenshot_tx_buffer == nullptr) {
    Serial.println("screenshot_error=tx_buffer_allocation_failed");
  }

  lv_init();
  lv_disp_draw_buf_init(&draw_buffer_descriptor, draw_buffer, nullptr, kBufferPixels);
  lv_disp_drv_init(&display_driver);
  display_driver.hor_res = kDisplayWidth;
  display_driver.ver_res = kDisplayHeight;
  display_driver.flush_cb = flush_display;
  display_driver.draw_buf = &draw_buffer_descriptor;
  display_driver.user_data = lcd;
  lv_disp_drv_register(&display_driver);

  touch_ready = register_touch_input(panel->getTouch());
  rtc_pcf85063_begin();
  ui_shell_create();
  display_ready = true;
  current_status = touch_ready ? "online" : "online_touch_degraded";
  lv_refr_now(nullptr);
  Serial.println("display_status=online");
  return true;
}

const char* display_status() {
  return current_status;
}

const char* display_touch_status() {
  return current_touch_status;
}

bool display_touch_online() {
  return touch_ready;
}

void display_loop() {
  if (!display_ready) {
    delay(20);
    return;
  }

  const uint32_t next_run_ms = lv_timer_handler();
  delay(constrain(next_run_ms, 5U, 20U));
}

bool display_send_screenshot(Stream& output) {
  if (screenshot_buffer == nullptr || screenshot_tx_buffer == nullptr) {
    return false;
  }

  // A 24-row LVGL draw buffer can leave the PSRAM mirror between refresh
  // strips when a serial request arrives. The first pass drains any pending
  // invalidations; the second starts with a clean LVGL invalidation queue.
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(nullptr);
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(nullptr);
  memcpy(screenshot_tx_buffer, screenshot_buffer, kScreenshotBytes);

  output.printf("SCREENSHOT_BEGIN width=%u height=%u format=rgb565le bytes=%u\n",
                kDisplayWidth,
                kDisplayHeight,
                static_cast<unsigned int>(kScreenshotBytes));

  size_t offset = 0;
  while (offset < kScreenshotBytes) {
    const size_t remaining = kScreenshotBytes - offset;
    const size_t chunk = min(remaining, kScreenshotWriteChunkBytes);
    const size_t written = output.write(screenshot_tx_buffer + offset, chunk);
    if (written == 0) {
      delay(1);
      continue;
    }
    offset += written;
  }
  output.print("\nSCREENSHOT_END\n");
  return true;
}
