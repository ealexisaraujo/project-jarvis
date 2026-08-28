#pragma once

#include <Stream.h>

bool display_begin();
void display_loop();
const char* display_status();
const char* display_touch_status();
bool display_touch_online();
bool display_send_screenshot(Stream& output);
