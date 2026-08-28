# Project Jarvis

Incremental firmware for the Waveshare **ESP32-S3-Touch-LCD-1.85** (touch
version). The current milestone establishes a reproducible toolchain and a
USB-powered, touch-scrollable six-tile LCD shell with a shared-bus RTC clock,
a real tappable Wi-Fi station connection, and live current weather for
the selected location profile—Chandler, Arizona by default. After Wi-Fi comes
online, non-blocking services synchronize the RTC from SNTP and fetch
Open-Meteo weather without blocking the display loop. The Weather city,
forecast coordinates, observation timezone, and RTC timezone all come from
that one profile. Deterministic tile selection supports framebuffer QA for
every screen. A battery and microSD card are not required.

> Hardware target: the build continues to use the
> `ESP32-S3-Touch-LCD-1.85` board definition. This unit's confirmed touch
> profile uses the shared I2C bus on GPIO11 (SDA) and GPIO10 (SCL).

## Hardware target

- ESP32-S3, dual core, up to 240 MHz
- 16 MB flash and 8 MB PSRAM
- 360 x 360 ST77916 QSPI LCD
- CST816 capacitive touch controller
- USB-C power and programming

The firmware initializes the LCD and CST816S touch controller, reuses their
shared I2C0 bus to read the PCF85063 RTC, then displays a horizontal six-tile
Project Jarvis shell designed for the panel's circular viewport. Finger swipes
move one tile at a time through Clock, Weather, Bins, Alarm, Radio, and MP3.
Clock's dark Wi-Fi card starts or retries a non-blocking station connection,
and Clock/Weather reflect its current state. Once connected, SNTP uses the
selected profile's POSIX timezone and initializes the PCF85063 once per powered
boot; the Clock continues to read the RTC once per second. Weather uses the
same profile's city, coordinates, and IANA observation timezone. Chandler,
Arizona is selected by default. Bins, Alarm, Radio, and MP3 remain
placeholders. The firmware also reports the detected
chip, flash, PSRAM, heap, display, touch, RTC, Wi-Fi, and time-sync state plus
weather state and an uptime heartbeat over USB serial. Battery, audio,
microphone, IMU, and microSD support remain outside this milestone.

## Prerequisites

On macOS:

```sh
brew install arduino-cli
```

## Setup and build

```sh
make bootstrap
make build
```

Dependencies are pinned and installed into the ignored `.arduino/` directory:

- Espressif Arduino core `3.0.7`
- LVGL `8.3.10`
- ESP32 Display Panel `0.1.8`
- ESP32 IO Expander `0.0.4`
- ArduinoJson `7.4.3`

The build enables USB CDC serial output and uses Waveshare's documented
non-speech partition layout (`3 MB app / 9.9 MB FATFS`).

## Connect the board

List detected ports:

```sh
make board-list
```

If no USB serial port appears, hold **BOOT**, reconnect USB-C, then release
**BOOT**. Waveshare documents this as the download-mode recovery procedure.
Also verify that the USB-C cable carries data, not power only.

After a port such as `/dev/cu.usbmodem1101` appears:

```sh
make upload PORT=/dev/cu.usbmodem1101
make monitor PORT=/dev/cu.usbmodem1101
```

Press **RESET** after upload if the application does not start automatically.

## Local Wi-Fi credentials

The firmware builds without credentials and shows `WiFi setup`; it never tries
an empty connection. For a local station connection, copy the tracked template:

```sh
cp firmware/project_jarvis/secrets.example.h firmware/project_jarvis/secrets.h
```

Edit the two macros in `secrets.h` locally. That file is ignored by Git. Never
commit it or put credential values in logs, screenshots, documentation, or
support messages. Wi-Fi starts one station attempt automatically after the
display, touch, RTC, and UI are initialized. The Clock card can be tapped to
retry a failed or disconnected attempt; a horizontal drag begun on the card
still bubbles to tile navigation.

## Changing location

Wi-Fi connectivity does not determine the city. The compile-time location
profile in `firmware/project_jarvis/location_config.cpp` controls the Weather
title, Open-Meteo coordinates and observation timezone, and the POSIX timezone
passed to `configTzTime()` for RTC local time.

The active profile is selected by this deliberately isolated line:

```cpp
const LocationProfile& kActiveLocationProfile = kChandlerLocationProfile;
```

Change only that line to select the included `kAshfordLocationProfile`. To add
another city, define one more `LocationProfile` beside the Chandler and Ashford
profiles with its display name, latitude, longitude, IANA timezone, and
ESP32-compatible POSIX timezone, then point the selection line at it. Open-Meteo's
[Geocoding API](https://open-meteo.com/en/docs/geocoding-api) returns the city
latitude, longitude, and IANA `timezone`; for example, search its endpoint with
`https://geocoding-api.open-meteo.com/v1/search?name=Chandler&count=10&language=en&format=json`
and verify the intended region before copying the result.

The POSIX timezone is separate from the IANA name and must also be correct: it
controls RTC local time and daylight-saving transitions. Chandler uses
`America/Phoenix` plus `MST7` because Arizona does not observe daylight saving;
Ashford uses `Europe/London` plus `GMT0BST,M3.5.0/1,M10.5.0`.

## Capture the LCD framebuffer

The firmware mirrors every successful LVGL display flush into PSRAM. Before a
USB capture it performs a settled full redraw and freezes the mirror into a
separate immutable PSRAM transmit buffer. Capture that exact 360 x 360 RGB565
frame and convert it to a PNG with:

```sh
make screenshot PORT=/dev/cu.usbmodem1101
```

The default PNG is written under the ignored `artifacts/` directory. To choose
another destination:

```sh
make screenshot PORT=/dev/cu.usbmodem1101 OUTPUT=/path/to/jarvis-screen.png
```

Select and capture any tile deterministically with `TILE=0..5`:

```sh
make screenshot PORT=/dev/cu.usbmodem1101 TILE=0 OUTPUT=/path/to/clock.png
make screenshot PORT=/dev/cu.usbmodem1101 TILE=1 OUTPUT=/path/to/weather.png
```

The indices are Clock `0`, Weather `1`, Bins `2`, Alarm `3`, Radio `4`, and
MP3 `5`. The host waits for `tile_selected=N` and at least one display-loop
refresh before requesting the screenshot. Invalid indices fail without moving
the active tile. The equivalent direct option is
`python3 scripts/capture_screen.py --port <port> --tile N`.

The capture utility uses only Python's standard library. Its encoder can be
checked without connected hardware or a generated artifact:

```sh
python3 scripts/capture_screen.py --self-test
```

Compare the PNG with the physical LCD to isolate display faults:

- A correct PNG with a corrupted physical LCD indicates an LCD transport,
  controller, or panel-configuration problem.
- A corrupted PNG indicates a problem in LVGL rendering or framebuffer
  generation before the LCD transport.

### Confirmed panel revision

This board's display ID is `00:02:7F:7F`, which Waveshare's current official
example maps to the newer ST77916 vendor initialization profile. The firmware
pins that profile as `st77916_revision_02` and runs the final QSPI display
transfer clock at 80 MHz, matching the current official example. This clock is
the controlled comparison for the faint residual vertical seam seen at the
previous 40 MHz setting; the earlier 5 MHz setting was used only to obtain a
reliable diagnostic ID read.

The revision-02 panel reset is also pinned to the current Waveshare wiring:
the TCA9554 drives EXIO2 (zero-based expander pin 1) low for 10 ms and then
high for 50 ms before LCD initialization. This project-local override replaces
the older library preset's pin-0 LCD-reset pulse. EXIO1 is still used later,
separately, to reset the CST816S touch controller.

At boot, confirm that serial includes these diagnostic lines:

```text
display_reset=expander_io2
display_clock_hz=80000000
display_id_confirmed=00:02:7F:7F
display_id_raw_80mhz=...
display_profile=st77916_revision_02
display_transport=rgb565_byte_swap
touch_stage=controller_ready
touch_bus=i2c0_sda11_scl10_shared
touch_status=online
rtc_bus=i2c0_sda11_scl10_shared
rtc_status=online|invalid|offline
ui_heap_internal_free=...
wifi_status=unconfigured|connecting|online|failed|disconnected
time_sync_status=waiting_wifi|requested timezone=America/Phoenix|online
rtc_set_status=online|invalid|offline|mismatch
weather_status=waiting_wifi|fetching|online|failed|offline_cached
display_status=online
boot_status=ready
```

The confirmed ID is the trusted result measured at the diagnostic 5 MHz
clock. The raw 80 MHz value is timing-sensitive diagnostic output and may vary;
it does not replace the confirmed panel identity.

The transport diagnostic confirms that the blocking LVGL flush temporarily
swaps each RGB565 pixel's two bytes for the LCD. The original byte order is
restored immediately after each transfer so LVGL buffers and USB screenshots
retain their existing RGB565 little-endian representation.

If the framebuffer PNG remains correct but the physical LCD is corrupted,
continue troubleshooting the LCD transport and physical panel separately.

## Milestone 4.2 UI, Wi-Fi, time, and weather state

The LCD uses a square 360 x 360 framebuffer behind a circular visible panel.
All six horizontally addressed tiles therefore keep meaningful content inside
the circular safe area. The tileview scrolls horizontally only, snaps one tile
at a time, and has no visible scrollbar.

- Clock shows three concentric seconds/minutes/hours arcs, `HH:MM`, the date,
  and a tappable Wi-Fi connect/retry card. RTC state refreshes once per second;
  Wi-Fi state refreshes from a main-thread LVGL timer.
- Weather shows live rounded Celsius, a concise WMO condition, humidity, wind,
  surface pressure, and the provider's local `Updated HH:MM` time for Chandler
  by default.
- Bins, Alarm, Radio, and MP3 are title-only placeholders for later services.

The Wi-Fi service disables persistent flash writes, uses station mode and
automatic reconnect support, and polls connection status without wait loops or
long delays. A connection attempt times out after 15 seconds. Serial reports
only state, numeric status/reason, and local IP—never the SSID or password.
Wi-Fi failure does not degrade the already-working display/touch boot status.

The weather service uses Open-Meteo's public Forecast API with the active
profile—Chandler coordinates `33.30616, -111.84125` and timezone
`America/Phoenix` by default—and only the current temperature, relative
humidity, WMO code, 10 m wind speed, and surface pressure fields. It fetches
immediately when Wi-Fi first becomes online,
refreshes no sooner than 15 minutes after success, and retries no sooner than
60 seconds after failure. A bounded FreeRTOS worker owns DNS, TLS, HTTP, and
streaming ArduinoJson parsing; it never calls LVGL. Responses with a known
`Content-Length` are rejected above 4096 bytes. Unknown-length responses are
accepted, and `Transfer-Encoding: chunked` bodies are decoded as a pull stream,
but the decoded JSON body is still capped at exactly 4096 bytes: the reader
accepts EOF at the boundary and rejects the first byte beyond it. The main loop
publishes a fixed-size synchronized snapshot, and a normal LVGL timer performs
only change-aware label updates. A later failure preserves the last valid
reading. The UI distinguishes waiting for Wi-Fi, fetching, online, failed, and
offline-cached states.

This prototype calls `WiFiClientSecure::setInsecure()` because the pinned core
has no project-managed CA bundle. HTTPS still encrypts the credential-free,
public weather request, but the server certificate is not authenticated. This
must be replaced with a maintained trust anchor before treating the network
path as production-secure; the firmware never falls back to plaintext HTTP.

The time-sync service is also polled from the Arduino main loop. It waits for
the Wi-Fi snapshot to become online, calls `configTzTime()` with the active
profile's POSIX timezone and two public NTP servers, and checks
`time()`/`localtime_r()` without a blocking `getLocalTime()` timeout. RTC
initialization proceeds only after SNTP reports a completed synchronization.
The default `MST7` value represents America/Phoenix at UTC-7 with no
daylight-saving transition. Years before 2024 are rejected. A Wi-Fi disconnect
before completion returns the service to a retryable waiting state; after one
verified RTC update it performs no further synchronization that boot.

The previous Milestone 2 two-page vertical UI and its upward-swipe validation
are historical evidence; Milestone 3 replaces that composition rather than
adding pages to it.

## Shared-bus RTC behavior

The project-local PCF85063 service reads and writes address `0x51` through the
already installed 400 kHz I2C0 driver on GPIO11 SDA / GPIO10 SCL. It does not
use `Wire.begin`, install or delete an I2C driver, or reconfigure bus pins. RTC
access starts only after `panel->begin()` has initialized the shared peripheral
bus. Writes encode seconds, minutes, hours, day, weekday, month, and year into
registers `0x04..0x0A` only after full range, month-length, and leap-year
validation. The firmware reads the RTC back and accepts either the requested
time or exactly one second later, including calendar rollover.

Valid BCD time/date values render on the Clock tile. Transport errors, invalid
ranges, or the oscillator-stop flag produce `--:--` and `0000-00-00`; the
firmware does not invent a time. After verified NTP initialization, the
existing one-second LVGL callback automatically replaces the fallback and
advances all three Clock arcs from RTC reads. This board has no RTC backup
battery in the current setup, so synchronization is expected once per powered
boot and retention across power loss is not expected. RTC or synchronization
failure does not degrade the working display/touch boot.

The hardware-free validation commands for Milestone 4.2 are:

```sh
make test
make build NO_SECRETS=1
python3 scripts/capture_screen.py --self-test
python3 -m py_compile scripts/capture_screen.py
```

`NO_SECRETS=1` explicitly excludes the ignored local credential header from
the compiler. It is the safe build mode for hardware-free review. The normal
credential-bearing build remains local and never prints the credential values.

Milestone 4 is uploaded and reset-tested on the board. It boots `ready` with
display and touch online, automatically transitions from
`wifi_status=connecting` to `wifi_status=online`, and stabilizes near 204.9 KB
free internal heap with Wi-Fi active. Before Milestone 4.1 synchronization, the
unset RTC correctly remained invalid in the no-backup-battery setup. All six
360 x 360 framebuffers were selected and reviewed: Clock and Weather show the
live connected state; Bins, Alarm, Radio, and MP3 render their specified
title-only placeholders. A physical tap on the Clock card and horizontal
finger swiping still require direct touch confirmation.

Milestone 4.1 is uploaded and hardware-verified. After reset, the board reported
`boot_status=ready`, connected to Wi-Fi, requested Phoenix SNTP, then reported
`rtc_set_status=online` and `time_sync_status=online`. Display and touch stayed
online while free heap stabilized at 203,576 bytes through 26 seconds. The
final personalized binary is 1,518,225 bytes with SHA-256
`44f8457fc3a7bdd603f31f5eec29ce895b926d5807854ba6dcae09fabf6f5e94`.
Three final 360 x 360 Clock captures show `15:48`, `2026-08-27`, and identical
time/date, Wi-Fi, hour, and minute layers; only the blue seconds arc changes.
Their distinct hashes independently confirm that the RTC-driven Clock advances.

An initial Milestone 4.2 hardware run reached Open-Meteo with HTTP 200 but
reported `weather_status=failed reason=-1003`. Runtime inspection showed
`Transfer-Encoding: chunked` and `HTTPClient::getSize() == -1` even with
`useHTTP10(true)`. The corrected pull decoder was then rebuilt, uploaded, and
hardware-verified: weather progressed from `waiting_wifi` through `fetching`
to `online`; RTC/time sync, display, and touch remained online; and free heap
stabilized near 202.9 KB through 32 seconds. The final Weather capture renders
live Ashford values inside the circular safe area, and a subsequent Clock
capture confirms the clock composition remains healthy. Host tests cover the
4096-byte exact limit, byte-4097 rejection, chunk boundaries,
extensions/trailers, and invalid framing.

The final personalized binary is 1,643,984 bytes with SHA-256
`d2c4374080ba323af1bdeddb07d512fd8913b3ae9c213c4895e783aae282a97e`.
The Weather framebuffer is `artifacts/jarvis-m4-2-weather-live.png` (SHA-256
`7902cf5a4ef426552e15ef9d701af978bf063f3684e5748097410da2dbd7d444`);
the post-weather Clock framebuffer is `artifacts/jarvis-m4-2-clock-live.png`
(SHA-256
`7c63c81a717a30bc6bc707e0dd3633867eda7a6ec95b01eefdde7990205a637d`).

## Confirmed touch profile

Waveshare documents two touch-bus wirings for its 1.85-inch boards. This unit's
confirmed hardware profile attaches CST816S address `0x15` to I2C host 0,
sharing the TCA9554 peripheral bus on GPIO11 (SDA) and GPIO10 (SCL). The
expander initializes that host once at 400 kHz; touch explicitly skips host
initialization so the firmware does not install two I2C drivers on the same
wires. CST816S INT remains GPIO4 and the preset orientation is unchanged.

The factory image used the older, incompatible touch wiring on GPIO1 (SDA) and
GPIO3 (SCL) for this unit. Both the factory image and the initial project
configuration therefore failed the CST816 chip-ID read until the shared-bus
profile was selected.

Immediately before touch initialization, the project-local touch-start hook
drives TCA9554 EXIO1 (zero-based expander pin 0) low for 10 ms and then high
for 50 ms. This CST816S reset is independent of the earlier EXIO2 LCD reset.

Successful LVGL pointer registration reports `touch_status=online`.
Initialization or registration problems report a specific `touch_status`
error and leave boot status degraded. When diagnosing such an error, verify
the EXIO1 touch-reset pulse and touch I2C path; `display_reset=expander_io2`
confirms only the separate LCD reset.

The confirmed physical boot reports `CST816S: IC id: 181`,
`touch_stage=controller_ready`, `touch_bus=i2c0_sda11_scl10_shared`,
`touch_status=online`, and `boot_status=ready`. Dependency-level panel logging
is disabled during normal operation to avoid repeated idle touch-timeout
messages; the concise project-owned `touch_stage`, touch status, display
status, and boot status diagnostics remain enabled.

Physical Milestone 2 validation confirmed an upward swipe from its first page
to its second page. A separate downward-swipe capture was not recorded. Those
results confirm the touch path. Milestone 4 now has ready-boot, online
station-state, stable-heap, and six-framebuffer proof. The new card tap and
horizontal gestures still need direct physical touch confirmation.

## Incremental roadmap

1. USB serial diagnostic (complete)
2. Round-safe touch scrolling (complete)
3. RTC clock and horizontal six-tile UI shell (hardware boot and initial Clock
   framebuffer verified)
4. Tappable Wi-Fi station connectivity and six-tile capture selection (online
   connection and all six framebuffers verified; direct card tap/swipe pending)
5. NTP-backed America/Phoenix RTC initialization (hardware verified)
6. Live Ashford weather (hardware verified with live values, stable heap, and
   circular-safe Weather and regression Clock captures)
7. Bins, alarm, radio, and MP3 application services
8. Optional audio, microphone, IMU, microSD storage, and battery/power
   management

## References

- [Waveshare board documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.85)
- [Waveshare Arduino instructions](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.85/Arduino)
- [ESP32 Arduino documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
- [LVGL 8 documentation](https://docs.lvgl.io/8/)
- [Open-Meteo Forecast API](https://open-meteo.com/en/docs)
- [ArduinoJson 7 documentation](https://arduinojson.org/v7/)
