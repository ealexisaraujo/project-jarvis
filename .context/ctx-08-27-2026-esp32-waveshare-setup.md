# ESP32 Waveshare setup handoff

## Purpose and freshness

- **Purpose:** restartable engineering record for the Waveshare ESP32-S3-Touch-LCD-1.85 Project Jarvis firmware and its USB screenshot diagnostic.
- **Scope:** the accepted LCD/touch hardware profile, reproducible toolchain, runtime and screenshot evidence, recovery guardrails, and next work.
- **Fresh through:** 2026-08-27 (America/Phoenix).
- **Current state:** Milestone 4.2 live Ashford weather is implemented and hardware-verified. The first device run exposed chunked-response rejection (`weather_status=failed reason=-1003`); the corrected path lazily decodes chunk framing, accepts unknown decoded length, preserves the exact 4096-byte ceiling, and feeds ArduinoJson directly from a bounded stream. After rebuild/upload, the board progressed `waiting_wifi -> fetching -> online`, kept RTC/time sync, display, and touch online, stabilized near 202.9 KB free heap through 32 seconds, and produced circular-safe live Weather plus regression Clock framebuffers.
- **Next recommended action:** physically tap the Clock Wi-Fi card and verify horizontal swiping, including a drag begun on the clickable card. Then choose the next service among Bins, Alarm, Radio, and MP3. Exercise Weather failure/retry/offline-cache behavior only when useful. Do not reopen the confirmed display, touch, RTC, or weather transport paths unless new evidence contradicts them.

This file distinguishes historical Milestone 2/3 evidence from Milestone 4
and 4.1 hardware proof, the initial Milestone 4.2 failure evidence, and the
corrected Milestone 4.2 hardware proof. Milestones 4, 4.1, and the final 4.2
correction were built with the ignored local secret, uploaded, reset,
monitored, and framebuffer-validated on 2026-08-27. The delegated correction
pass did not access the secret or hardware; the main validation pass did.
Credentials are intentionally absent from this document.

## Confirmed operational profile

| Area | Confirmed value |
| --- | --- |
| Board | Waveshare ESP32-S3-Touch-LCD-1.85, touch version |
| MCU/memory | ESP32-S3 revision 2, 16 MB flash, 8 MB PSRAM |
| LCD | 360 x 360 ST77916 QSPI, circular visible viewport over a square framebuffer |
| Display identity | `00:02:7F:7F`, reliably read at the diagnostic 5 MHz clock |
| Display init | Project-local `st77916_revision_02`, Waveshare's 184-command `vendor_specific_init_new` sequence |
| LCD reset | TCA9554 EXIO2, zero-based pin 1: low 10 ms, high 50 ms |
| Final LCD clock | 80 MHz QSPI (`display_clock_hz=80000000`) |
| Pixel transport | Temporarily byte-swap RGB565 immediately around the blocking LCD transfer; restore before mirroring |
| Touch | CST816S, runtime chip ID `181`, address `0x15`, INT GPIO4 |
| Touch bus | I2C host 0 shared with TCA9554 on GPIO11 SDA / GPIO10 SCL; touch skips duplicate host initialization |
| Touch reset | TCA9554 EXIO1, zero-based pin 0: low 10 ms, high 50 ms |
| Serial | USB CDC, 115200 baud; heartbeat every 2000 ms |
| Last observed device port | `/dev/cu.usbmodem1101` (also present during the read-only 2026-08-27 inspection) |
| Power/storage | USB power only; no battery and no microSD card |

The 5 MHz display-ID read is identity evidence. A post-init read at the final clock is timing-sensitive and must remain labeled `display_id_raw_80mhz`; it must not replace `display_id_confirmed`. The earlier 40 MHz raw value `00:03:3F:BF` was likewise not identity evidence.

## Historical runtime evidence already captured

The accepted hardware run recorded these invariants:

```text
CST816S: IC id: 181
touch_stage=controller_ready
touch_bus=i2c0_sda11_scl10_shared
touch_status=online
display_reset=expander_io2
display_clock_hz=80000000
display_id_confirmed=00:02:7F:7F
display_id_raw_80mhz=...
display_profile=st77916_revision_02
display_transport=rgb565_byte_swap
display_status=online
boot_status=ready
heartbeat ... display=online touch=online detail=online
```

`display_id_raw_80mhz` may vary or be unavailable. A ready boot requires both display and touch online. Normal dependency-level panel logging is disabled; concise project-owned stage/status lines remain.

Physical validation of the LCD/touch foundation and Milestone 2 established:

- The initial correct USB framebuffer plus striped physical LCD isolated the first fault after LVGL rendering.
- The visible vertical bands were real LCD defects, not camera moire.
- EXIO2 reset substantially reduced the broad bands; the final official 80 MHz clock resolved the remaining seam to the user's satisfaction.
- A physical upward swipe moved Page 1 to Page 2.
- A software reset returned the device to Page 1.
- A physical downward swipe has **not** been independently captured or verified.

Milestone 3 subsequently ran on the same confirmed profile. A reset produced:

```text
rtc_bus=i2c0_sda11_scl10_shared
rtc_status=invalid
ui_heap_internal_free=275440
display_status=online
boot_status=ready
heartbeat uptime_ms=2001 free_heap=275440 display=online touch=online detail=online
heartbeat uptime_ms=4006 free_heap=275440 display=online touch=online detail=online
heartbeat uptime_ms=6008 free_heap=275440 display=online touch=online detail=online
```

The invalid RTC status is an expected safe state for an unset/non-retained RTC;
it renders the documented fallback rather than degrading display or touch.

The Milestone 4 reset and bounded monitor produced:

```text
ui_heap_internal_free=244624
display_status=online
wifi_status=connecting
boot_status=ready
wifi_status=online ip=192.168.0.192
heartbeat uptime_ms=6023 free_heap=204960 display=online touch=online detail=online
heartbeat uptime_ms=22057 free_heap=204908 display=online touch=online detail=online
```

The IP is runtime evidence, not a credential. Wi-Fi connection did not degrade
display/touch, and free heap remained stable near 204.9 KB after connection.

The final Milestone 4.1 personalized build produced a 1,518,225-byte firmware
binary with SHA-256
`44f8457fc3a7bdd603f31f5eec29ce895b926d5807854ba6dcae09fabf6f5e94`.
After upload and reset, the bounded monitor produced:

```text
rtc_status=online
wifi_status=connecting
time_sync_status=waiting_wifi
boot_status=ready
wifi_status=online ip=192.168.0.192
time_sync_status=requested timezone=America_Phoenix
rtc_set_status=online
time_sync_status=online
heartbeat uptime_ms=26031 free_heap=203576 display=online touch=online detail=online
```

The RTC was already valid at boot from the prior powered session, then was
freshly synchronized and read back successfully after SNTP completed. Heap
stabilized at 203,576 bytes from 8 through 26 seconds with display and touch
remaining online.

A subsequent Milestone 4.2 hardware run supplied this failure evidence:

```text
weather_status=fetching
weather_status=failed reason=-1003
```

The request reached Open-Meteo and received HTTP 200 with
`Transfer-Encoding: chunked`; `HTTPClient::getSize()` returned `-1` even after
`useHTTP10(true)`. Error `-1003` therefore came from rejecting an unknown
response length before parsing, not from a server error or invalid weather
payload. This run proves the original failure path only. It does not prove a
successful parse, live values, corrected runtime behavior, heap stability, or
a live Weather framebuffer.

The corrected build was then uploaded and produced:

```text
weather_status=waiting_wifi
boot_status=ready
wifi_status=online ip=192.168.0.192
weather_status=fetching
weather_status=online
rtc_set_status=online
time_sync_status=online
heartbeat uptime_ms=32104 free_heap=202920 display=online touch=online detail=online
```

Free heap stayed between 202,868 and 202,920 bytes from 8 through 32 seconds.
The final personalized binary is 1,643,984 bytes with SHA-256
`d2c4374080ba323af1bdeddb07d512fd8913b3ae9c213c4895e783aae282a97e`.
This is successful runtime evidence for the corrected chunked path and the
existing RTC/display/touch invariants.

## Screenshot evidence

All listed files were inspected as valid, non-interlaced 360 x 360 RGB PNGs. Framebuffer and physical-panel checks are separate: a correct PNG proves LVGL/mirroring output, not the physical LCD transport.

| Evidence | Path | SHA-256 |
| --- | --- | --- |
| Final Page 1 / home | `artifacts/jarvis-screen-touch-page1.png` | `ba01c76ed775efe5340b8f842ddbcbbf8e719eeb5530b365694583a8d25693b8` |
| Final Page 2 after upward swipe | `artifacts/jarvis-screen-touch-after-swipe.png` | `5f910418433f766813c2d62e2ab24ed1b8a5583e61c42f0524c8e9b9f869bcc3` |
| Final home after software reset | `artifacts/jarvis-screen-final-home.png` | `ba01c76ed775efe5340b8f842ddbcbbf8e719eeb5530b365694583a8d25693b8` |
| Accepted pre-touch LCD framebuffer | `artifacts/jarvis-screen-80mhz.png` | `1e5050ab0495b550239319569e6c67fd24cdae3a86ef7ef90dceaac269765925` |
| Milestone 3 Clock fallback after corrected reset | `artifacts/jarvis-screen-milestone3-clock-final.png` | `4fe014d2c109a9c8ef6e945751f658e401a06baf173c789f69dc35b266017bcd` |
| Milestone 4 Clock / Wi-Fi online | `artifacts/jarvis-m4-0-clock.png` | `6287c7591448808926f1531840c0c3f772ad9be304bfad1bd80b983306a34563` |
| Milestone 4 Weather / Wi-Fi connected | `artifacts/jarvis-m4-1-weather.png` | `f4703b568e99c35be379918e8b7dc3cccf5374cee69168cc1fa581980a89a8a1` |
| Milestone 4 Bins placeholder | `artifacts/jarvis-m4-2-bins.png` | `a3d3ec763b927f077e276a994789e6e6b4614e58e8348f081a4cdecb360be1bd` |
| Milestone 4 Alarm placeholder | `artifacts/jarvis-m4-3-alarm.png` | `2417f71956b44e26b5f038fb92ed76cfd5806fc914bdab64ddc21146734fd589` |
| Milestone 4 Radio placeholder | `artifacts/jarvis-m4-4-radio.png` | `2ff63176523f86da21d2ca0fd13fe946c9d133730a3ab50f3177d48c2b03077b` |
| Milestone 4 MP3 placeholder | `artifacts/jarvis-m4-5-mp3.png` | `6a92230eb939c370a268223035c8af4db25ba8d5eda539f1231fd4cf188a9e31` |
| Milestone 4.1 final Clock frame A | `artifacts/jarvis-clock-rtc-final-a.png` | `405f8568e6659e49ced8d7cf2f1309a850b948e2646a0c9827a1d9cdfdf6dd49` |
| Milestone 4.1 final Clock frame B | `artifacts/jarvis-clock-rtc-final-b.png` | `3ed93f918a57974c93472f25e7bc0318f5089972e990224f2ca5547b65584d72` |
| Milestone 4.1 final Clock frame C | `artifacts/jarvis-clock-rtc-final-c.png` | `5e0d0b301db627acd39233bdf6aaf5a2dfdd5dac198fbfeab673ac09d88dd39e` |
| Milestone 4.2 live Weather | `artifacts/jarvis-m4-2-weather-live.png` | `7902cf5a4ef426552e15ef9d701af978bf063f3684e5748097410da2dbd7d444` |
| Milestone 4.2 post-weather Clock regression | `artifacts/jarvis-m4-2-clock-live.png` | `7c63c81a717a30bc6bc707e0dd3633867eda7a6ec95b01eefdde7990205a637d` |

All three final Milestone 4.1 frames show `15:48` and `2026-08-27`, matching
the America/Phoenix host clock at capture time. Local pixel analysis reports
the same white-text count (`4764`) and red/hour-arc count (`928`) in every
frame. Consecutive frame differences are confined to the blue seconds arc
(`285` and `378` changed pixels), and all PNG hashes differ. This proves the
full Clock composition remains stable while successive RTC-driven seconds
renderings advance. When reviewing several images through an image tool, open
each independently: some viewers optimize consecutive images as visual deltas
even though the stored PNGs are complete.

The USB protocol is `SCREENSHOT_BEGIN width=360 height=360 format=rgb565le bytes=259200`, exactly 259200 raw bytes, then a newline and `SCREENSHOT_END`. The live mirror and immutable transmit copy live in PSRAM. A capture performs two synchronous full redraw passes before freezing the mirror. Heartbeats cannot interleave because screenshot transmission is synchronous in the main loop.

## Toolchain and commands

Pinned dependencies, installed under ignored `.arduino/`:

- Arduino CLI `1.5.1`
- Espressif Arduino core `3.0.7`
- LVGL `8.3.10`
- ESP32 Display Panel `0.1.8`
- ESP32 IO Expander `0.0.4`
- ArduinoJson `7.4.3`
- Bundled esptool `4.6`

Exact FQBN:

```text
esp32:esp32:waveshare_esp32_s3_touch_lcd_185:CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB
```

Normal commands from the repository root:

```sh
make bootstrap
make build
make board-list
make upload PORT=/dev/cu.usbmodem1101
make monitor PORT=/dev/cu.usbmodem1101
make screenshot PORT=/dev/cu.usbmodem1101
make screenshot PORT=/dev/cu.usbmodem1101 TILE=0
make screenshot PORT=/dev/cu.usbmodem1101 OUTPUT=/path/to/jarvis-screen.png
python3 scripts/capture_screen.py --self-test
python3 -m py_compile scripts/capture_screen.py
```

Milestone 4.2 hardware-free validation, which deliberately bypasses the
ignored credential header and does not touch a serial device, is:

```sh
make test
make build NO_SECRETS=1
python3 scripts/capture_screen.py --self-test
python3 -m py_compile scripts/capture_screen.py
```

All four commands passed on 2026-08-27. The corrected no-secrets build uses
1,643,585 bytes of program storage (52%) and 116,392 bytes of global RAM (35%),
leaving 211,288 bytes for local variables. These commands are software-only
evidence: the correction pass produced no upload, new runtime API response,
serial state, heap observation, screenshot, or physical LCD result. The
earlier `-1003` device evidence remains a failed pre-correction run.

Do not run upload and monitor concurrently. The native USB CDC port can
re-enumerate after upload; press RESET if the application does not start. The
final Milestone 3 pass ran the build, host self-tests, upload, bounded serial
monitoring, reset, and live USB framebuffer capture.

The corrected Milestone 3 build completed on 2026-08-27. Arduino CLI reported
932,677 bytes of sketch storage (29%) and 92,056 bytes of global RAM (28%),
leaving 235,624 bytes for local variables. The resulting
`build/project_jarvis.ino.bin` has SHA-256
`912f4b34638410a786b5d277027d211973bd2b5aa78234d52acc7501e50b8c92`.

The credential-bearing but ignored Milestone 4 build reports 1,507,605 bytes of
sketch storage (47%) and 114,424 bytes of global RAM (34%), leaving 213,256
bytes for local variables. `build/project_jarvis.ino.bin` has SHA-256
`45b76b5abc01ddd4b3a17128d9d58d47a11f31bbbe849254432e17be0968d844`.
`capture_screen.py --self-test` covers PNG encoding plus tile command, range,
acknowledgement, and rejection behavior; Python bytecode compilation also
passes. The binary hash should be treated as sensitive distribution metadata
because the ignored credentials are compiled into that local firmware image.

## Implementation decisions

- `ESP_Panel_Board_Custom.h` reuses the 0.1.8 Waveshare preset, then overrides only this unit's confirmed revision behavior.
- The revision-02 vendor sequence is project-local and wired through `ESP_PANEL_LCD_VENDOR_INIT_CMD()`; `.arduino/` is never patched.
- Sequence provenance: Waveshare `ESP32-S3-Touch-LCD-1.85-Demo.zip`, `Arduino/examples/LVGL_Arduino/Display_ST77916.cpp`, archive SHA-256 `fa6cd4c47923d559e15571a20ec0d7029febee76e14e6afbd99760f9ad5d6990`.
- The flush callback swaps bytes only for `drawBitmapWaitUntilFinish()`, always restores the LVGL buffer, and mirrors only successful transfers in original RGB565 little-endian order.
- The 360 x 360 screenshot mirror is allocated with PSRAM capability; the smaller LVGL draw buffer remains internal DMA memory.
- The touch controller shares the expander's already-initialized 400 kHz I2C0 host. `ESP_PANEL_TOUCH_BUS_SKIP_INIT_HOST=1` prevents a second driver on the same wires.
- The PCF85063 service reads and writes address `0x51` with the ESP-IDF legacy master API on the existing I2C0 driver. It never installs, deletes, resets, or reconfigures the bus. Writes cover seconds through year at registers `0x04..0x0A` only after year, month length, leap year, weekday, and time validation.
- RTC write success requires a validated readback equal to the requested local value or exactly one second later, including date rollover. Transport failure, invalid BCD/ranges, oscillator-stop, or mismatch stays nonfatal and leaves display/touch operation intact.
- The time-sync service is main-loop polled and once-per-boot. It waits for `wifi_service_snapshot().state == kOnline`, requests `configTzTime("MST7", ...)` with two public servers, requires SNTP's completed status, and polls `time()` plus `localtime_r()` without `getLocalTime()` or delay loops. It rejects system years before 2024, returns to retryable waiting if Wi-Fi drops, and reports only state—not credentials.
- The Clock's existing 1000 ms LVGL timer remains the sole UI time updater. Once RTC read status becomes online, fallback labels and all three arcs update automatically from RTC values.
- The weather service requests only Open-Meteo current temperature, humidity, WMO code, 10 m wind, and surface pressure for fixed Ashford coordinates `51.14648,0.87376` in `Europe/London`. A bounded 8 KiB FreeRTOS worker performs DNS/TLS/HTTP and ArduinoJson stream parsing behind an exact 4096-byte decoded-body ceiling; it never calls LVGL or exposes heap-owned strings.
- Known `Content-Length` values above the ceiling fail before parsing. Unknown lengths are allowed. Because ESP32 core 3.0.7 exposes raw chunk framing through `HTTPClient::getStreamPtr()`, a pull-based decoder handles `Transfer-Encoding: chunked` before the bounded reader. ArduinoJson still reads directly from that adapter; no full response buffer is introduced. After JSON parsing stops, the reader drains only to decoded EOF or byte 4097 so exact-limit input succeeds and oversized input fails closed.
- Weather values are range-checked before a fixed-size snapshot is published under a short critical section. The main-loop scheduler fetches immediately after first online Wi-Fi, refreshes no sooner than 15 minutes after success, retries no sooner than 60 seconds after failure, handles `millis()` rollover by unsigned subtraction, and preserves valid cached data across later failure/offline states.
- The UI owns separate Clock Wi-Fi and Weather widget groups. A main-thread LVGL timer performs change-aware ASCII-only updates for temperature, WMO description, compact humidity/wind/pressure metrics, and provider-local `Updated HH:MM` state.
- Prototype TLS uses `WiFiClientSecure::setInsecure()`: the credential-free request remains encrypted but the server certificate is not authenticated. There is no plaintext fallback; production hardening requires a maintained CA trust anchor.
- Without an RTC backup battery, NTP-to-RTC initialization is expected once per powered boot and time retention across unplugging is not expected.
- LVGL exposes exactly one horizontal tileview with Clock, Weather, Bins, Alarm, Radio, and MP3 at columns 0 through 5. It snaps one tile at a time with no scrollbar and keeps meaningful content inside the circular safe area.
- The Clock tile reads RTC state from a 1000 ms LVGL timer. Separate main-thread LVGL timers poll fixed-size Wi-Fi and Weather snapshots; no network worker calls LVGL.
- Wi-Fi is station-only and non-blocking: credentials come from ignored `secrets.h`, missing/empty values remain unconfigured, persistent writes are disabled, one bounded boot attempt starts after the UI, and card taps enqueue retries. Logs exclude SSID/password. Bins, Alarm, Radio, and MP3 remain placeholders.
- The tileview and all six tiles are retained by the UI shell. Serial `tile N` selects indices 0 through 5 with animation off and refreshes on the main thread. Host `--tile` waits for acknowledgement and a display-loop interval before preserving the strict screenshot framing.
- `LV_MEM_SIZE` remains 64 KiB. The corrected hardware run reports `ui_heap_internal_free=275440`, which remains stable in subsequent heartbeats.
- Preserve tile geometry created by `lv_tileview_add_tile()`: removing all styles from a tile also erases its constructor-assigned size and column position, collapsing all six tiles and leaving the last-created MP3 content on top. The corrected helper styles the background without clearing tile styles, then explicitly selects Clock after layout.
- Startup delay is the normal 500 ms. Verbose dependency logging used for the early touch failure was removed after the profile was confirmed.

## Rejected hypotheses and superseded experiments

- **LVGL/UI corruption:** rejected because the USB PNG was pixel-perfect while the physical LCD was striped.
- **Camera moire:** rejected; the user could directly see the fixed bands/seam.
- **Library-wrapper regression causing CST816 failure:** rejected because the restored factory ESP-IDF image failed the same chip-ID read on its older wiring.
- **GPIO1/GPIO3 touch bus for this unit:** rejected. Both the factory image and initial project configuration failed there; GPIO11/GPIO10 shared I2C0 succeeded with chip ID 181.
- **Old preset LCD reset on zero-based expander pin 0:** rejected for revision 02. EXIO2/pin 1 is the LCD reset; pin 0 is reserved for the independent touch reset.
- **5 MHz as an operating clock:** rejected; it was identification-only.
- **40 MHz as the final clock:** superseded by the accepted Waveshare-matching 80 MHz setting.
- **Post-init raw ID as the panel identity:** rejected because the high-speed read is timing-sensitive.

## Factory-image recovery

Preserve `backups/ESP32-S3-Touch-LCD-1.85.bin`; do not modify or regenerate it.

- Size: `6220298` bytes
- SHA-256: `e9b87dc42f0fb581d5e4236eccdd780495c937ef6298eaed4c43b161c1156595`
- Read-only `esptool 4.6 image_info` recognizes it as an ESP32-S3 image with valid checksum and validation hash.

Before any future recovery, verify the backup hash and current port:

```sh
shasum -a 256 backups/ESP32-S3-Touch-LCD-1.85.bin
make board-list
```

The backup was successfully restored during diagnosis with this exact command:

```sh
.arduino/data/packages/esp32/tools/esptool_py/4.6/esptool \
  --chip esp32s3 \
  --port /dev/cu.usbmodem1101 \
  --baud 921600 \
  write_flash 0x0 backups/ESP32-S3-Touch-LCD-1.85.bin
```

Factory recovery is destructive and outside routine development. Verify the target port and backup hash first, retain a current flash backup, and enter ROM download mode with BOOT if needed. On this hardware revision, the restored factory firmware uses the incompatible GPIO1/GPIO3 touch wiring, fails the CST816 chip-ID read, and reboots. Recovery therefore must be followed by rebuilding and uploading Project Jarvis to return the device to the confirmed GPIO11/GPIO10 profile. The 6,220,298-byte file is a proven restorable factory image, but it must not be described as a raw 16 MB flash dump.

## Source-of-truth map

| Path | Responsibility |
| --- | --- |
| `SPEC.md` | Required behavior, experiment history, accepted runtime evidence, and remaining gap |
| `README.md` | Operator setup, current hardware profile, capture interpretation, and roadmap |
| `Makefile` | Exact FQBN and bootstrap/build/upload/monitor/screenshot entry points |
| `arduino-cli.yaml` | Repository-local Arduino CLI data/download/user directories |
| `scripts/bootstrap.sh` | Pinned core and library versions |
| `scripts/capture_screen.py` | Dependency-free raw-tty protocol validation and RGB565LE-to-PNG encoder |
| `firmware/project_jarvis/project_jarvis.ino` | Boot, serial command parser, ready/degraded state, and heartbeat |
| `firmware/project_jarvis/display.cpp` | LCD/touch integration, UI-shell startup, byte swap, PSRAM mirror, and screenshot response |
| `firmware/project_jarvis/rtc_datetime.h/.cpp` | Hardware-free calendar validation, PCF85063 BCD conversion, and one-second rollover matching |
| `firmware/project_jarvis/rtc_pcf85063.h/.cpp` | Validated shared-I2C0 PCF85063 read plus write/readback service |
| `firmware/project_jarvis/time_sync_service.h/.cpp` | Non-blocking once-per-boot Phoenix SNTP-to-RTC orchestration |
| `firmware/project_jarvis/wifi_service.h/.cpp` | Non-blocking station state, bounded timeout, safe snapshot, and retry request |
| `firmware/project_jarvis/weather_service.h/.cpp` | Bounded worker fetch, validated Open-Meteo parsing, scheduling, cached state, and synchronized snapshot |
| `firmware/project_jarvis/weather_response_reader.h` | Host-testable decoded-body limiter and lazy HTTP chunk decoder used by ArduinoJson |
| `firmware/project_jarvis/weather_format.h/.cpp` | Hardware-free WMO mapping, range validation, rounding, metrics, and observation-time formatting |
| `firmware/project_jarvis/secrets.example.h` | Tracked empty credential shape; local `secrets.h` stays ignored |
| `firmware/project_jarvis/ui_shell.h/.cpp` | Horizontal six-tile shell, dynamic Clock/Weather state, tappable card, and bounded selection |
| `tests/rtc_datetime_test.cpp` | Host coverage for validation, BCD, oscillator-stop handling, and rollover acceptance |
| `tests/weather_format_test.cpp` | Host coverage for WMO groups/unknowns, rounding, ASCII metrics, time extraction, and invalid ranges |
| `tests/weather_response_reader_test.cpp` | Host coverage for under/exact/over-limit reads plus valid and invalid chunk framing |
| `firmware/project_jarvis/ESP_Panel_Board_Custom.h` | Confirmed EXIO resets, I2C0 touch profile, 80 MHz clock, and vendor-init hook |
| `firmware/project_jarvis/st77916_revision_02_init.h` | Provenanced revision-02 vendor command sequence |
| `firmware/project_jarvis/ESP_Panel_Conf.h` | Panel logging and library compatibility configuration |
| `artifacts/` | Ignored generated framebuffer evidence; preserve existing captures |
| `backups/` | Ignored recovery material; preserve unchanged |

Reference tracked-file hashes after Milestone 4 hardware validation (ignored
`secrets.h` deliberately excluded):

```text
README.md                                      df184bceb961bd66f427a68cfce313e0a112a4642ac2cecbba4f6702ce242cf9
SPEC.md                                        42771b975679faa20c995415d71a772d3bd8ce9ed5d9d31d7fc7990a61a5443a
Makefile                                       17f6afe056235d82b83b6d34c96d90d5ca54e4a3ba50f3dd8ebd95da08080cdd
scripts/capture_screen.py                      73e6162c36f109a1d878475f5af16616764d0d51ce077ac5890fc3b05b742b73
firmware/project_jarvis/project_jarvis.ino     bdd8442f495e9482009b272dd5a0c999f87c454362c188935bfd66e0aeeec0cc
firmware/project_jarvis/display.cpp            ee64d63f2a8230d66ff020a80ed2439bcdee7db4be9355ca296d4c763330f1ec
firmware/project_jarvis/ui_shell.cpp           042e37023caeb692e52e921f801a0b08ffadcfc78c0bc53d1019683d4c932a1d
firmware/project_jarvis/ui_shell.h             60973a9fa74e65033cf0cc6d462d3a45ba2dca969d8645089f299fd0c3bc7e49
firmware/project_jarvis/wifi_service.cpp       0fb9f30c61e76131f8ed36b67c40457a44e9d6f1c90acd51d18aaf83f68c31c7
firmware/project_jarvis/wifi_service.h         7977e501c13469303a31c82ff338d549fc498eab570607c18c75f50671ce9ca3
firmware/project_jarvis/secrets.example.h      12fb5b685179e73de553a47498f850bd6441a7cc4c6e2800db09a25f9a8717a2
firmware/project_jarvis/rtc_pcf85063.cpp       059e45f95b7df7db5fddcb23f947618ac3ca947bc54b90b24ca3936d73a99691
firmware/project_jarvis/rtc_pcf85063.h         5f23614baf0e8019bae4ce2521831b660e4c5ea0e36a9130c4deacf3c89dbb04
firmware/project_jarvis/ESP_Panel_Board_Custom.h c3a0d827865d085ba20c4c80f7eeb6c8dc82a52191c7b18d3967ba2619ea2be4
firmware/project_jarvis/ESP_Panel_Conf.h        3f05f1ad74587c4a733543c29ec6e39b8912ba41e68bf7ee287e88bf6ac44200
firmware/project_jarvis/st77916_revision_02_init.h bc0de29d553945072091e62cc888215d58febfb967559072ecd439c12199e10d
```

## Milestones and roadmap

Completed:

1. Reproducible pinned Arduino CLI toolchain and USB serial diagnostics.
2. Deterministic PSRAM-backed USB screenshot protocol and standard-library host capture utility.
3. Revision-02 ST77916 init, trustworthy ID semantics, EXIO2 LCD reset, RGB565 transport swap, and final 80 MHz QSPI clock.
4. Confirmed CST816S shared-I2C0 profile with EXIO1 reset and LVGL pointer input.
5. Circular-safe two-page vertical Milestone 2 UI; physical upward swipe verified.
6. Milestone 3 implementation and hardware boot: validated PCF85063 reader, Clock tile, Weather placeholder, and four title-only placeholders in a horizontal six-tile shell; pinned build, ready boot, stable heap, and initial Clock framebuffer pass.
7. Milestone 4 implementation and hardware proof: ignored local credentials, non-blocking station connection/retry, main-thread dynamic Clock/Weather labels, deterministic serial/host tile selection, ready boot, online Wi-Fi, stable heap, and six visually reviewed framebuffers.
8. Milestone 4.1 implementation and hardware proof: non-blocking completed-SNTP `MST7` orchestration, validated shared-I2C0 RTC write/readback, once-per-boot state, host calendar/BCD/rollover tests, personalized build/upload, successful runtime status, stable heap, and three advancing full Clock captures.
9. Milestone 4.2 implementation, failure diagnosis, correction, and hardware proof: fixed Ashford Open-Meteo request, bounded worker, observed HTTP-200 chunked-length failure `-1003`, direct de-chunked parsing behind the strict response cap, validated fixed-size cached snapshot, five UI states, ASCII formatting, pinned ArduinoJson 7.4.3, host helper/reader tests, personalized build/upload, `weather_status=online`, stable heap, and visually reviewed live Weather plus Clock framebuffers.

Remaining / unverified:

1. Physically tap the Clock card to prove its connect/retry handler and verify horizontal swiping, including a drag begun on the clickable card.
2. Exercise a controlled failure/retry or reconnect only when useful; automatic successful connection and stable active-Wi-Fi heap are already proven.
3. Confirm behavior after a full USB power removal if needed; without backup power, retention across unplugging is not expected and NTP should initialize the RTC again.
4. Exercise Weather failure, retry, and offline-cached behavior on hardware only if useful.
5. Specify and implement Bins, Alarm, Radio, and MP3 services.
6. Optionally add audio, microphone, IMU, microSD, and battery/power management.

## Handoff checklist

1. Read `SPEC.md`, then this file and `README.md`.
2. Run `git status --short --branch`; preserve all unrelated/untracked work. This repository currently has no commits, so do not mistake existing untracked files for disposable output.
3. Verify hashes before relying on a backup or named screenshot.
4. Keep `.arduino/`, `backups/`, and existing `artifacts/` unchanged; never patch dependency sources.
5. Milestone 4.2 is the last successful hardware-verified firmware. Preserve the initial `-1003` failure as diagnosis evidence and the final `weather_status=online`, stable-heartbeat, Weather framebuffer, and Clock regression capture as correction evidence.
6. Future Weather changes must retain `weather_status=waiting_wifi|fetching|online|failed|offline_cached`, the existing boot invariants, stable heartbeats, and the exact decoded-body cap.
7. Compare framebuffer PNG and physical LCD independently.
8. Historical six-tile images predate live weather. Keep them separate from the final Milestone 4.2 tile-1 capture and the still-pending physical card-tap/swipe proof.
