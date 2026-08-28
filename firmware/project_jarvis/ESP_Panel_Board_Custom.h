/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Reuse the Waveshare board preset from ESP32_Display_Panel 0.1.8, then
 * override the CST816S bus for this unit's confirmed shared-I2C hardware
 * revision.
 */

#pragma once

#include <Arduino.h>

#define ESP_PANEL_USE_CUSTOM_BOARD (1)

#include <board/waveshare/ESP32_S3_Touch_LCD_1_85.h>
#include "st77916_revision_02_init.h"

// Use the confirmed wiring by sharing the expander's I2C0 bus on SDA11/SCL10.
// The expander initializes this host; touch must not install a second driver.
#undef ESP_PANEL_TOUCH_BUS_HOST_ID
#define ESP_PANEL_TOUCH_BUS_HOST_ID (0)
#undef ESP_PANEL_TOUCH_BUS_SKIP_INIT_HOST
#define ESP_PANEL_TOUCH_BUS_SKIP_INIT_HOST (1)

#undef ESP_PANEL_BEGIN_EXPANDER_END_FUNCTION
#define ESP_PANEL_BEGIN_EXPANDER_END_FUNCTION(panel) \
  {                                                   \
    _expander_ptr->pinMode(1, OUTPUT);                \
    _expander_ptr->digitalWrite(1, LOW);              \
    vTaskDelay(pdMS_TO_TICKS(10));                    \
    _expander_ptr->digitalWrite(1, HIGH);             \
    vTaskDelay(pdMS_TO_TICKS(50));                    \
  }

#undef ESP_PANEL_BEGIN_TOUCH_START_FUNCTION
#define ESP_PANEL_BEGIN_TOUCH_START_FUNCTION(panel) \
  {                                                  \
    Serial.println("touch_stage=reset_start");       \
    _expander_ptr->pinMode(0, OUTPUT);               \
    _expander_ptr->digitalWrite(0, LOW);             \
    vTaskDelay(pdMS_TO_TICKS(10));                   \
    _expander_ptr->digitalWrite(0, HIGH);            \
    vTaskDelay(pdMS_TO_TICKS(50));                   \
    Serial.println("touch_stage=reset_complete");    \
  }

#undef ESP_PANEL_BEGIN_TOUCH_END_FUNCTION
#define ESP_PANEL_BEGIN_TOUCH_END_FUNCTION(panel) \
  {                                                \
    Serial.println("touch_stage=controller_ready"); \
  }

#undef ESP_PANEL_LCD_SPI_CLK_HZ
#define ESP_PANEL_LCD_SPI_CLK_HZ (80 * 1000 * 1000)

#define ESP_PANEL_LCD_VENDOR_INIT_CMD() \
  ST77916_REVISION_02_VENDOR_INIT_COMMANDS()
