# USB Screenshot Diagnostic Specification

## Objective

Add a deterministic, dependency-free USB screenshot path for the current
Waveshare ESP32-S3-Touch-LCD-1.85 firmware. The captured image must represent
the exact LVGL pixels submitted to the LCD, so it can distinguish an LVGL/UI
problem from an LCD transport/controller problem.

## Constraints

- Keep the existing Arduino CLI, ESP32 core, LVGL, and display-library versions.
- Do not require a battery, microSD card, Wi-Fi, Python packages, or Homebrew
  packages beyond what the project already uses.
- Preserve the current display initialization and Project Jarvis boot screen.
- Store the mirror framebuffer in PSRAM, never in scarce internal DMA RAM.
- The screenshot command and response must not interleave heartbeat messages.
- Do not touch `backups/` or `.arduino/`.
- Do not commit, push, or alter unrelated user files.

## Firmware protocol

1. Mirror each successful LVGL flush into a 360 x 360 RGB565 little-endian
   framebuffer in PSRAM. Copy by row and respect the flushed area's coordinates.
2. Accept newline-terminated USB serial commands without blocking the display
   loop. At minimum support `help` and `screenshot`.
3. For `screenshot`, synchronously emit:

   - one ASCII header line beginning `SCREENSHOT_BEGIN` and containing
     `width=360 height=360 format=rgb565le bytes=259200`;
   - exactly 259200 raw framebuffer bytes;
   - a newline followed by the ASCII line `SCREENSHOT_END`.

   The synchronous response naturally pauses the main-loop heartbeat until the
   frame has been transmitted.
4. If the mirror framebuffer is unavailable, return a clear single-line error
   rather than sending a partial frame.

## Host capture utility

Create `scripts/capture_screen.py` using only Python's standard library.

- Required/accepted arguments: `--port`; optional `--output`.
- Open the macOS `/dev/cu.usbmodem*` tty in raw mode at 115200 without external
  serial libraries.
- Discard normal serial log lines until a valid `SCREENSHOT_BEGIN` header.
- Read exactly the declared byte count, verify the ending marker, and reject
  truncated or malformed transfers with a nonzero exit.
- Convert RGB565 little-endian pixels to an RGB PNG using standard-library
  `zlib`, `struct`, and CRC support.
- Default output belongs under ignored `artifacts/` and must print its final
  path and dimensions.
- Provide `--self-test` that exercises PNG encoding without hardware.

## Project integration

- Add `make screenshot PORT=/dev/cu.usbmodem...` and allow optional
  `OUTPUT=/path/file.png`.
- Ignore generated `artifacts/`.
- Update the README so the milestone description reflects the active LCD boot
  screen and documents screenshot capture and diagnostic interpretation:
  correct PNG plus corrupted physical LCD means LCD transport/configuration;
  corrupted PNG means LVGL/framebuffer generation.

## Acceptance criteria

- `make build` succeeds.
- `python3 scripts/capture_screen.py --self-test` succeeds without third-party
  modules and does not leave a repository artifact.
- `python3 -m py_compile scripts/capture_screen.py` succeeds.
- After upload to connected hardware, `make screenshot PORT=<detected-port>`
  creates a valid 360 x 360 PNG containing the Project Jarvis UI.

## Confirmed panel revision correction

Hardware evidence gathered after the screenshot diagnostic:

- The captured LVGL PNG is correct, while the photographed LCD output is
  striped, locating the fault after framebuffer generation.
- At the normal 80 MHz QSPI rate, display-ID register `0x04` returned zeros.
- At Waveshare's documented 5 MHz identification rate, the same register
  returned exactly `00:02:7F:7F`.
- Waveshare's current official example maps that value to
  `vendor_specific_init_new`, while ESP32_Display_Panel 0.1.8 contains only the
  older default sequence.

Implement the confirmed revision fix as follows:

- Reproduce the exact 184-command `vendor_specific_init_new` sequence from
  `/tmp/waveshare-st77916-revision-02-init.txt` in a clearly named project-local
  header. Preserve command order, parameters, parameter counts, and delays.
- Wire it through the supported `ESP_PANEL_LCD_VENDOR_INIT_CMD()` configuration
  hook; do not patch `.arduino/` library sources.
- Set the final QSPI transfer clock to 40 MHz for the first stable milestone.
  The 5 MHz override was diagnostic-only and must not remain.
- Keep the read-only display-ID logging, but document that the profile is pinned
  to the confirmed `00:02:7F:7F` panel revision.
- Add provenance comments identifying the sequence as sourced from Waveshare's
  `ESP32-S3-Touch-LCD-1.85-Demo.zip`, file
  `Arduino/examples/LVGL_Arduino/Display_ST77916.cpp`, downloaded from the
  official documentation, archive SHA-256
  `fa6cd4c47923d559e15571a20ec0d7029febee76e14e6afbd99760f9ad5d6990`.
- Update README troubleshooting with the confirmed revision and profile.

Additional acceptance criteria:

- `make build` succeeds without modifying `.arduino/` source files.
- After upload, serial reports `display_id_confirmed=00:02:7F:7F`,
  `display_profile=st77916_revision_02`, and stable heartbeats.
- `make screenshot` still creates a correct 360 x 360 PNG.
- Physical LCD validation remains a separate final check.

### Final serial diagnostic semantics

The revision-02 firmware booted successfully at the final 40 MHz QSPI clock,
but a post-initialization register read returned `00:03:3F:BF`. This is a raw,
timing-sensitive value and must not replace the panel identity that was
reliably measured at the diagnostic 5 MHz clock as `00:02:7F:7F`.

- Report the trusted result as `display_id_confirmed=00:02:7F:7F`.
- If the 40 MHz post-initialization read is retained, label it
  `display_id_raw_40mhz=...` so it cannot be mistaken for identity evidence.
- Continue reporting `display_profile=st77916_revision_02`.
- Update README boot examples to use these truthful labels.
- Preserve all panel initialization and USB screenshot behavior.

## RGB565 LCD transport byte order correction

Physical evidence from `/Users/alexis/Downloads/IMG_8535.heic` shows the full
Project Jarvis geometry correctly positioned, but navy renders yellow/olive and
the dark center renders cyan/green with a repeating pixel pattern. The USB
framebuffer remains pixel-perfect. This isolates the remaining fault to RGB565
byte order on the LCD transport.

Waveshare's official
`Arduino/examples/LVGL_Arduino/Display_ST77916.cpp` keeps
`LV_COLOR_16_SWAP=0` but explicitly swaps the two bytes of every `uint16_t`
pixel in `LCD_addWindow()` before calling `esp_lcd_panel_draw_bitmap()`.

Implement the same behavior without patching dependencies:

- In the project LVGL flush path, byte-swap every RGB565 pixel immediately
  before `drawBitmapWaitUntilFinish()`.
- Because the transfer waits for completion, restore the original byte order
  immediately afterward so LVGL buffers retain their expected representation.
- Mirror the original-order pixels into the screenshot framebuffer, preserving
  the existing 360x360 USB PNG byte-for-byte.
- Restore the buffer even when the LCD transfer reports failure.
- Add a boot diagnostic `display_transport=rgb565_byte_swap` and document the
  reason in README.
- Do not change colors, layout, panel initialization, dependencies, touch, or
  screenshot protocol.

Acceptance criteria:

- `make build` succeeds and the screenshot self-test passes.
- After upload, boot reports `display_transport=rgb565_byte_swap`, display
  online, and stable heartbeats.
- A new USB screenshot remains SHA-256
  `1e5050ab0495b550239319569e6c67fd24cdae3a86ef7ef90dceaac269765925`.
- Physical LCD validation is performed separately from a new phone photo.

## Milestone 2: round-safe touch scrolling

Status: implemented and hardware-verified for an upward swipe. The physical
vertical-band defect was resolved to the user's satisfaction by the EXIO2
reset correction and the official 80 MHz final QSPI clock.

The physical panel is a circular 360x360 viewport even though its framebuffer
is a square 360x360 raster. The Milestone 1 composition placed long footer text
near the square's bottom edge, so the circular mask clips it. Touch was also
explicitly disabled during LCD bring-up, therefore LVGL has no input device and
cannot scroll.

Implement the first interactive milestone:

- Re-enable the board preset's CST816S touch controller; retain its official
  Waveshare pins, I2C settings, and orientation.
- Before touch initialization, pulse Waveshare `EXIO1` using zero-based
  TCA9554 pin `0`: configure it as output, drive low for 10 ms, then high for
  50 ms. This is the touch reset and is separate from the LCD's EXIO2/pin-1
  reset. Wire it through the project-local
  `ESP_PANEL_BEGIN_TOUCH_START_FUNCTION` hook.
- After `panel->begin()`, require a non-null touch device and register it as an
  LVGL pointer input using polling through `readPoints()`.
- Report `touch_status=online` after successful registration and expose a
  truthful degraded/error state if touch initialization or registration fails.
- Replace the single square-oriented composition with a vertical, two-page
  scrollable viewport. Each page is 360x360, and the viewport must visibly
  scroll by a finger swipe.
- Page 1 retains Project Jarvis identity, core ring, and online status, but all
  meaningful content must fit inside the circular safe area. Replace the long
  footer with compact wording and include a visible `SWIPE UP` affordance.
- Page 2 is a circular-safe `DEVICE STATUS` page showing USB power, no SD card,
  no battery, and touch online. Include a `SWIPE DOWN` affordance.
- Use vertical-only scrolling, scroll snapping between pages, and an active
  scrollbar/position affordance. Do not allow horizontal scrolling.
- Preserve the confirmed ST77916 revision-02 initialization, 80 MHz QSPI clock,
  RGB565 transport byte swap, serial commands, and USB screenshot protocol.
- Update README with the round viewport and swipe behavior.

Acceptance criteria:

- `make build` and `python3 scripts/capture_screen.py --self-test` succeed.
- After upload, serial reports LCD online, touch online, the revision-02
  profile, the RGB565 byte-swap transport, and stable heartbeats.
- A fresh 360x360 USB screenshot shows Page 1 with no meaningful text outside
  the circular safe area.
- Physical swipe validation remains a separate user check.

### CST816S startup diagnostic

The first Milestone 2 hardware upload leaves the panel degraded because
`ESP_Panel::begin()` returns false after LCD initialization. Project-local
panel error logging is already enabled, but the native USB serial port
re-enumerates after upload and the failure is emitted before a monitor can
attach.

Add minimal, temporary-but-safe instrumentation so the exact touch failure can
be captured without modifying dependency sources:

- Keep `ESP_PANEL_ENABLE_LOG` enabled.
- Extend the existing startup delay after `Serial.begin()` to 4000 ms so a
  monitor opened immediately after upload can capture the full panel log.
- Print `touch_stage=reset_start` immediately before the EXIO1 reset pulse and
  `touch_stage=reset_complete` immediately after it.
- Define the supported touch-end hook to print
  `touch_stage=controller_ready` only after the touch bus, touch object, and
  orientation operations all succeed.
- Preserve the EXIO1 touch reset timing, EXIO2 LCD reset, 80 MHz clock, panel
  initialization, UI, and screenshot behavior exactly.
- Do not edit `.arduino/`, dependencies, backups, or unrelated files.

Acceptance criteria:

- `make build` succeeds.
- After upload and immediate monitor attachment, the log identifies whether
  failure occurs before reset completion, during touch bus begin, touch init,
  touch controller begin, or orientation setup.

### Touch-bus hardware revision A/B test

The complete factory flash backup was temporarily restored and its official
ESP-IDF application failed at the same CST816 chip-ID read, then rebooted. This
proves the failure is not introduced by the Jarvis LVGL integration or the
ESP32_Display_Panel C++ wrapper. Waveshare currently documents two distinct
1.85-inch board wirings: the original board routes touch I2C to GPIO1/GPIO3,
while the newer circular 1.85C routes touch through the shared peripheral I2C
bus on GPIO11/GPIO10. Both use address 0x15, INT GPIO4, and EXIO1 reset.

Perform one controlled hardware-revision test in the project-local board
configuration:

- Override the touch I2C host to host 0, the same host already used by the
  TCA9554 expander on GPIO11 (SDA) and GPIO10 (SCL).
- Set `ESP_PANEL_TOUCH_BUS_SKIP_INIT_HOST` to 1 so that bus is initialized only
  once by the expander configuration; do not install two I2C drivers on the
  same physical wires.
- Retain CST816 address 0x15, INT GPIO4, EXIO1 reset pin 0 and timing, LCD EXIO2
  reset, 80 MHz QSPI, revision-02 init, RGB565 swap, UI, screenshots, and the
  startup diagnostic markers.
- Add a successful boot diagnostic `touch_bus=i2c0_sda11_scl10_shared` and
  document that this is the confirmed hardware profile only if the physical
  upload reaches `touch_status=online`.
- Do not edit `.arduino/`, dependencies, backups, or unrelated files.

Acceptance criteria:

- `make build` succeeds.
- After upload, the CST816 chip ID read succeeds, serial reports
  `touch_stage=controller_ready`, `touch_status=online`, and `boot_status=ready`.
- The LCD remains online with the accepted 80 MHz revision-02 profile.
- If the CST816 still does not acknowledge, report the failed experiment and
  do not claim this bus profile is confirmed.

### Confirmed touch profile cleanup

The physical A/B upload succeeded on the shared I2C bus. Serial evidence:

- `CST816S: IC id: 181`
- `touch_stage=controller_ready`
- `touch_bus=i2c0_sda11_scl10_shared`
- `touch_status=online`
- `display_status=online`
- `boot_status=ready`

A fresh USB capture at
`artifacts/jarvis-screen-touch-page1.png` is a valid 360x360 PNG and shows all
meaningful Page 1 content inside the circular safe area.

Convert the successful experiment into the normal project configuration:

- Keep the confirmed touch host 0 / shared GPIO11-GPIO10 bus profile, CST816
  address, interrupt, reset timing, orientation, and success diagnostic.
- Restore the normal 500 ms startup delay now that early boot capture is no
  longer required.
- Disable verbose ESP32_Display_Panel dependency logging to eliminate the
  repeated idle touch timeout messages. Keep the concise project-owned
  `touch_stage` and status lines.
- Update README language from provisional A/B test to confirmed hardware
  profile, and explain that the factory image used the older incompatible
  GPIO1/GPIO3 touch wiring on this unit.
- Preserve the accepted LCD profile, UI, screenshot protocol, and all other
  behavior.
- Do not edit `.arduino/`, dependencies, backups, or unrelated files.

Acceptance criteria:

- `make build` and `python3 scripts/capture_screen.py --self-test` succeed.
- After upload, boot reaches `touch_stage=controller_ready`,
  `touch_bus=i2c0_sda11_scl10_shared`, `touch_status=online`,
  `display_status=online`, and `boot_status=ready` without idle timeout log
  spam.
- A physical upward swipe moved Page 1 to Page 2. The captured Page 2 PNG has
  SHA-256 `5f910418433f766813c2d62e2ab24ed1b8a5583e61c42f0524c8e9b9f869bcc3`.
- A software reset returned the device to Page 1, whose final capture matches
  the earlier Page 1 SHA-256
  `ba01c76ed775efe5340b8f842ddbcbbf8e719eeb5530b365694583a8d25693b8`.
- A physical downward swipe was not independently captured, so do not report
  that direction as separately verified.

## Revision-02 hardware reset-pin correction

The vertical bands in `/Users/alexis/Downloads/IMG_8536.heic` are visible to
the user directly and must be treated as a real LCD defect, not camera moire.

The ESP32_Display_Panel 0.1.8 Waveshare preset toggles zero-based TCA9554 pin
`0` during LCD reset. Waveshare's current official 1.85 example instead calls
`Set_EXIO(EXIO_PIN2, ...)`; its implementation maps `EXIO_PIN2` to TCA9554 bit
`EXIO_PIN2 - 1`, which is zero-based pin `1`. The new revision-02 LCD therefore
has not been receiving the official hardware reset pulse.

Implement one controlled correction:

- In the project-local custom board configuration, override
  `ESP_PANEL_BEGIN_EXPANDER_END_FUNCTION` after including the old preset.
- Configure zero-based expander pin `1` as output, drive it low for 10 ms, then
  high for 50 ms, matching Waveshare's current `ST7701_Reset()` timing and pin.
- Do not also pulse pin `0`.
- Report `display_reset=expander_io2` after successful panel start and document
  the revision-specific reset in README.
- Preserve the 40 MHz clock, revision-02 init sequence, RGB565 byte swap, UI,
  disabled touch state, and screenshot protocol exactly. Do not implement the
  deferred touch/scroll milestone in this change.

Acceptance criteria:

- `make build` and the screenshot self-test succeed.
- After upload, serial reports `display_reset=expander_io2`, display online,
  the existing revision profile and transport, and stable heartbeats.
- The USB screenshot remains SHA-256
  `1e5050ab0495b550239319569e6c67fd24cdae3a86ef7ef90dceaac269765925`.
- The user performs a separate physical check for the vertical bands.

## Residual vertical-seam clock test

After correcting the revision-02 EXIO2 reset, the broad vertical bands reduced
substantially, but the user still sees a faint fixed vertical seam crossing the
center `J`. This is an LCD-visible defect, not camera moire.

The remaining deliberate divergence from Waveshare's current example is the
final QSPI clock. The official driver uses 5 MHz only for identification, then
recreates the display bus at `ESP_PANEL_LCD_SPI_CLK_HZ`, defined as 80 MHz. The
project currently uses 40 MHz.

Run one controlled comparison:

- Change only the final project-local QSPI clock from 40 MHz to 80 MHz.
- Add `display_clock_hz=80000000` to the successful boot diagnostics and update
  README accordingly.
- Preserve EXIO2 reset, revision-02 initialization, RGB565 byte swap, UI,
  disabled touch, and screenshot behavior exactly.
- Do not implement the deferred touch/scroll milestone.

Acceptance criteria:

- `make build` and the screenshot self-test succeed.
- After upload, serial reports `display_clock_hz=80000000`, existing reset,
  profile and transport diagnostics, display online, and stable heartbeats.
- USB screenshot SHA-256 remains
  `1e5050ab0495b550239319569e6c67fd24cdae3a86ef7ef90dceaac269765925`.
- The user separately compares the residual LCD seam at 80 MHz.

## Context-engineering setup record

Create a durable, restartable context document at:

`.context/ctx-08-27-2026-esp32-waveshare-setup.md`

Follow the repository convention `ctx-mm-dd-yyyy-[topic].md`. The document is
not a conversational transcript; it is an evidence-backed resume layer for a
future engineer or agent with no prior chat history.

Use context-engineering best practices:

- Lead with purpose, scope, freshness date, current operational state, and the
  next recommended action.
- Separate confirmed facts, runtime evidence, implementation decisions,
  assumptions, rejected hypotheses, and unverified work.
- Record exact paths, dependency versions, FQBN, device port, build/upload/
  monitor/screenshot commands, boot invariants, screenshot evidence and hashes.
- Preserve the critical hardware-revision learning: display ID and init
  profile, EXIO2 LCD reset, RGB565 byte swap, 80 MHz QSPI, CST816 chip ID 181,
  and the confirmed shared I2C0 GPIO11/GPIO10 touch bus. Explain that both the
  factory image and initial project configuration failed on GPIO1/GPIO3.
- Record constraints: USB power only; no battery or microSD; do not treat real
  LCD bands as camera moire; do not patch `.arduino/`; preserve backups and
  generated artifacts; physical and framebuffer validation are distinct.
- Include recovery information for
  `backups/ESP32-S3-Touch-LCD-1.85.bin` and its SHA-256, without embedding
  binary content.
- Include completed milestones, remaining verification (downward swipe not
  independently captured), incremental roadmap, source-of-truth file map, and
  a concise handoff checklist.
- Keep the content concise enough to scan, but complete enough to resume work
  without reconstructing this conversation.
- Do not edit firmware, dependencies, backups, or unrelated files. Do not
  commit.

Acceptance criteria:

- The `.context` directory and exact convention-compliant file exist.
- Every claim is consistent with current README, SPEC, source, runtime logs,
  and captured artifacts.
- Markdown headings and commands are clear, with no secrets or unsupported
  claims.

## Milestone 3: RTC clock and six-tile UI shell

### Objective

Replace the current two-page vertical Project Jarvis UI with a round-safe,
horizontal six-tile LVGL 8 shell. The first tile is a live PCF85063 RTC clock,
the second is a weather placeholder, and the remaining tiles are placeholders
for Bins, Alarm, Radio, and MP3. Networking, audio, microphone, IMU, SD, and
battery support remain out of scope.

This repository is not Waveshare's stock `examples/LVGL_Arduino/` sketch. Adapt
the requested behavior to the existing `firmware/project_jarvis` architecture;
do not create or edit nonexistent stock-demo files.

### Non-negotiable hardware and toolchain constraints

- Preserve Arduino core 3.0.7, LVGL 8.3.10, ESP32 Display Panel 0.1.8, and
  ESP32 IO Expander 0.0.4. Do not upgrade dependencies or patch `.arduino/`.
- Do not change the confirmed revision-02 ST77916 init, EXIO2 LCD reset,
  80 MHz QSPI clock, RGB565 transfer byte swap, display-ID diagnostics, draw
  buffer sizes/capabilities, PSRAM screenshot mirror/protocol, or LCD flush.
- Do not change CST816 address/interrupt/orientation, EXIO1 touch reset, shared
  I2C0 GPIO11 SDA/GPIO10 SCL wiring, host-skip behavior, or touch registration.
- The TCA9554, CST816S, PCF85063, and future QMI8658 share the already-installed
  I2C0 driver. RTC code must reuse I2C_NUM_0 at 400 kHz and must never call
  `Wire.begin`, `i2c_driver_install`, `i2c_driver_delete`, reconfigure pins, or
  create another I2C host.
- No Wi-Fi scan or microphone/speech initialization exists in this project;
  do not add it. Preserve contiguous internal RAM for the later Wi-Fi milestone.
- Do not modify backups, generated artifacts, or unrelated files. Do not commit.

### RTC service

- Add small project-local `rtc_pcf85063.h/.cpp` files rather than adding an
  unpinned third-party library.
- Use the ESP-IDF legacy I2C master API already used by the installed panel and
  expander libraries to read PCF85063 address `0x51` from I2C_NUM_0 after
  `panel->begin()` has initialized the shared bus.
- Read the PCF85063 time/date register block starting at seconds register 0x04.
  Decode BCD, mask flag bits, validate seconds/minutes/hours/day/month/year, and
  expose a compact value type plus status. Treat the oscillator-stop flag,
  transport errors, and invalid ranges as unavailable; do not silently set the
  RTC or invent a time.
- Emit concise one-time project-owned diagnostics such as
  `rtc_bus=i2c0_sda11_scl10_shared` and `rtc_status=online`,
  `rtc_status=invalid`, or `rtc_status=offline`. RTC failure must not make the
  already-working display/touch boot degraded.
- With no RTC backup battery, the UI must still render. Use `--:--` and
  `0000-00-00` when RTC data is unavailable.

### UI architecture and design

- Add `ui_shell.h/.cpp` with a slim entry point called only after LVGL display,
  touch, and RTC service setup. Keep display transport and UI concerns separate.
- Replace only `create_round_safe_screen()` and its old page helpers in
  `display.cpp` with the UI-shell call. Preserve display/touch/screenshot code.
- Create exactly one full-screen `lv_tileview` with horizontal scrolling only,
  no visible scrollbar, one-tile-at-a-time snapping, and six tiles at x=0..5,
  y=0 in this order: Clock, Weather, Bins, Alarm, Radio, MP3.
- All children must permit swipe gestures to reach the tileview. Clear child
  scrollability/clickability where needed and use gesture bubbling if required.
- Use the established Jarvis palette: background `0x07131F`, surface
  `0x102638`, cyan/blue accents, white text, red hour ring, amber temperature,
  muted secondary text, and round-safe alignment. Meaningful content must remain
  inside the circular visible area.

Clock tile:

- Three concentric perimeter arcs begin at 12 o'clock and advance clockwise:
  outer 356 px blue seconds 0..59, middle 326 px white minutes 0..59, inner
  296 px red hours 0..11. Use thin strokes that remain visible without obscuring
  the centre content.
- Hide every grey track with
  `lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN)`, hide/remove the
  knob, and prevent the arcs from acting as interactive controls.
- Centre stack: `HH:MM` in Montserrat 48 white; `YYYY-MM-DD` in Montserrat 22
  white; then a compact dark rounded card with `LV_SYMBOL_REFRESH` and `WiFi...`.
- A 1000 ms LVGL timer reads the RTC service and updates all three arcs plus the
  time/date labels. Invoke the callback once after creating the Clock tile.

Weather tile:

- Round-safe centred content: city `Ashford` in Montserrat 22, temperature `--`
  in Montserrat 48 amber, description `Connecting WiFi...`, metrics line
  `Humidity --  Wind --  Pressure --`, and status `No WiFi`.
- No networking is added in this milestone.

Placeholder tiles:

- Bins, Alarm, Radio, and MP3 each contain only a centred Montserrat 22 white
  title.

### LVGL and memory safety

- Enable `LV_FONT_MONTSERRAT_22` in project-local `lv_conf.h`; preserve existing
  enabled fonts and LVGL configuration.
- All literal label text must be ASCII except LVGL-provided `LV_SYMBOL_*`
  constants. Do not use a degree sign or any other literal non-ASCII glyph.
- Never use `%f`/`%.1f` in `lv_label_set_text_fmt`. Future float display must be
  formatted with libc `snprintf` into a buffer and passed as `%s` or set with
  `lv_label_set_text`.
- Keep the existing 64 KiB LVGL heap initially. Log one concise post-UI internal
  heap observation; increase LVGL memory only if build/runtime evidence proves
  it necessary. Do not move UI objects into internal DMA buffers.
- Do not introduce asynchronous LVGL calls. The 1 Hz callback runs through the
  existing `lv_timer_handler()` loop.

### Documentation

- Update README current-milestone/UI/RTC sections and roadmap to describe the
  six horizontal tiles, no-network placeholders, shared-bus RTC behavior, lack
  of retention without an RTC backup battery, and exact validation commands.
- Update `.context/ctx-08-27-2026-esp32-waveshare-setup.md` so the restartable
  handoff distinguishes the prior two-page milestone from this new milestone
  and records what remains hardware-unverified until the main session uploads
  and observes it.

### Acceptance criteria

- `make build` succeeds with the pinned toolchain.
- `python3 scripts/capture_screen.py --self-test` succeeds.
- Source inspection shows one `lv_tileview`, six horizontally addressed tiles,
  three arcs with transparent main tracks/no knobs, a 1000 ms timer with one
  immediate callback, ASCII-only literal UI text, and no `%f` passed to LVGL.
- Source inspection shows RTC reuse of I2C_NUM_0 without `Wire.begin`, driver
  install/delete, or pin reconfiguration.
- Existing ST77916/CST816/display/screenshot boot invariants remain present.
- Do not upload or monitor from the delegated implementation pass. The main
  session will review the diff, upload once, monitor boot/heartbeat/RTC status,
  capture framebuffer evidence for the Clock and reachable tiles, and perform
  visual validation.

### Visual validation correction: deterministic initial tile

The first hardware upload booted healthy (`boot_status=ready`, stable
heartbeats, `rtc_status=invalid`, and `ui_heap_internal_free=275440`), but two
independent captures separated by a software reset both produced an elastically
offset MP3 tile instead of the Clock tile. Both PNGs have SHA-256
`80974592dcf83fd92cc7a6630a4eca36dd228b8576aef767c6ba84053214813f`.
This proves a deterministic tileview construction/initial-position defect, not
a retained transient screen.

- After all six tiles and their children are created, force LVGL layout and
  explicitly select/align the Clock tile at column 0, row 0 with animation off.
- Store/use the actual Clock tile pointer; do not rely on the tileview's implicit
  initial active child while its snap/layout extents are still being built.
- Preserve horizontal navigation, all visual requirements, RTC behavior, and
  every display/touch/screenshot invariant.
- Update README/context only if necessary; do not yet claim the correction is
  hardware-verified.
- `make build` and `python3 scripts/capture_screen.py --self-test` must pass.
- Do not upload, monitor, capture hardware, or commit in the correction pass.

### Visual validation correction 2: preserve tile geometry

The first correction compiled but failed hardware validation: the framebuffer
remained byte-identical to the MP3 capture above. Inspection of pinned LVGL
8.3.10 `lv_tileview.c` proves the root cause. `lv_tileview_add_tile()` assigns
each tile's 100% size and column position through local styles in the tile
constructor. The project helper then calls `lv_obj_remove_style_all(tile)`,
which erases those geometry styles. All six tiles therefore collapse onto the
same position and the last-created MP3 child paints over the others.

- Remove the tile-level `lv_obj_remove_style_all(tile)` call. Preserve the
  geometry established by `lv_tileview_add_tile()`.
- Retain the explicit background color/opacity styling and the post-population
  `lv_obj_update_layout()` plus Clock selection with animation off.
- Do not add scroll-offset workarounds or manually duplicate LVGL's tile
  geometry. Correct the destructive style removal at its source.
- Preserve all six tiles, horizontal gestures, UI/RTC behavior, and every
  display/touch/screenshot invariant.
- `make build` and `python3 scripts/capture_screen.py --self-test` must pass.
- Do not upload, monitor, capture hardware, update documentation, or commit in
  this delegated correction pass. The main session performs hardware proof.

## Milestone 4: tappable Wi-Fi and six-tile visual QA

### Objective

Replace the Clock/Weather Wi-Fi placeholder with a real, non-blocking station
connection driven by project-local ignored credentials. The Clock card must be
a genuine touch target for connect/retry while horizontal tile gestures remain
available. Add a deterministic serial/host tile-selection diagnostic so the
main session can capture and visually review all six rendered tiles from the
actual board.

This increment activates Wi-Fi only. Weather data remains unavailable and
Bins, Alarm, Radio, and MP3 remain title-only placeholders until their product
behavior is separately specified.

### Secrets and safety

- `firmware/project_jarvis/secrets.h` is already ignored and will be created by
  the main session with the user-provided station SSID/password. Do not create,
  print, copy, infer, or document its values in the delegated pass.
- Add a tracked `secrets.example.h` with empty/example macros and document the
  local setup shape without real credentials.
- Production code must compile both with and without `secrets.h`, using
  `__has_include` or an equivalent compile-time fallback. Missing/empty
  credentials render a clear ASCII-only setup state and never attempt an empty
  connection.
- Never log the password. Avoid logging the SSID as well; diagnostics need only
  state, reason/status code, IP, and heap observations.

### Wi-Fi service

- Add a small `wifi_service.h/.cpp` using the pinned ESP32 Arduino core's
  `WiFi.h` station API. Do not add a dependency or change any pinned versions.
- Keep connection non-blocking: no loops waiting on `WL_CONNECTED`, no long
  delays, and no LVGL calls from Wi-Fi callbacks/tasks. Expose a small snapshot
  API for UI polling and a `wifi_service_loop()` called from the Arduino loop.
- Disable persistent flash writes, use station mode, enable reconnect where
  supported, and start one connection automatically at boot when credentials
  exist. A user tap must retry/restart connection after failure or disconnect.
- Use a bounded connection timeout and concise state transitions such as
  `wifi_status=connecting`, `wifi_status=online ip=...`,
  `wifi_status=failed reason=...`, and `wifi_status=unconfigured`. No credential
  content may appear.
- Initialize the service only after the existing display/touch/RTC/UI shell so
  the prior hardware startup order and scarce internal-RAM observations remain
  meaningful. Wi-Fi failure must not degrade display/touch `boot_status`.

### UI behavior

- Keep the existing six-tile geometry and initial Clock selection intact.
- Make only the dark Wi-Fi card on Clock clickable. Tapping it starts/retries
  the station connection. It must retain a pressed visual state and gesture
  bubbling/ancestor scrolling so a horizontal drag begun on the card can still
  move the tileview.
- Poll the Wi-Fi snapshot through an LVGL timer on the normal LVGL thread and
  update Clock and Weather labels only on that thread.
- Required ASCII-safe states:
  - no credentials: Clock `WiFi setup`; Weather description/status indicate
    setup/no Wi-Fi;
  - connecting: Clock `Connecting...`; Weather indicates connection progress;
  - connected: Clock `WiFi online`; Weather says Wi-Fi is connected and may
    show the local IPv4 address, but temperature and metrics remain `--`;
  - failed/disconnected: Clock offers `Retry WiFi`; Weather shows no Wi-Fi.
- LVGL-provided symbol constants are allowed. Continue to avoid literal
  non-ASCII text and every LVGL `%f` formatting path.

### Deterministic six-tile capture

- Store the tileview and six tile pointers in the UI shell and expose a bounded
  selection function for indices 0 through 5. Selection must run on the main
  LVGL/Arduino thread, use animation off, refresh/layout as needed, and reject
  invalid indices without disturbing the active tile.
- Extend the USB command parser with `tile N` for N=0..5 and advertise it in
  `help`. Emit concise success/error lines without changing screenshot framing.
- Extend `scripts/capture_screen.py` with optional `--tile 0..5`. When present,
  send the tile command, allow at least one display-loop refresh before sending
  `screenshot`, then use the existing strict screenshot protocol. Extend the
  Makefile screenshot target with optional `TILE=N` forwarding.
- Add hardware-free parsing/range/self-test coverage appropriate to the new
  host behavior.

### Constraints

- Preserve the confirmed ST77916 revision-02 init, EXIO2 reset, 80 MHz QSPI,
  RGB565 byte swap, buffers, PSRAM screenshot mirror/protocol, display ID,
  CST816 shared-I2C0 path, PCF85063 shared-bus service, and setup order through
  `display_begin()`.
- Do not call LVGL outside the main loop/timer context. Do not add microphone,
  audio, weather API, NTP/RTC setting, SD, IMU, battery, AP/captive portal, or an
  on-screen keyboard in this increment.
- Preserve `LV_MEM_SIZE=64 KiB`; report build/global-RAM evidence. Do not commit,
  upload, monitor, open serial hardware, or write generated artifacts in the
  delegated pass.

### Documentation

- Update README for the local ignored-secret workflow, tappable retry card,
  actual scope (connectivity active; weather and four services still
  placeholders), `TILE=` capture usage, and validation commands. Never include
  real credentials.
- Update the existing `.context/ctx-08-27-2026-esp32-waveshare-setup.md` with
  Milestone 4 design and what remains hardware-unverified until the main
  session uploads, monitors, and captures all six tiles. Never include real
  credentials.

### Acceptance criteria

- `make build` succeeds with the pinned toolchain.
- `python3 scripts/capture_screen.py --self-test` and
  `python3 -m py_compile scripts/capture_screen.py` succeed.
- A repository scan outside the ignored local secret contains no real password
  and no credential-bearing logs/docs.
- Source inspection proves non-blocking Wi-Fi state handling, automatic first
  attempt, clickable retry, main-thread LVGL updates, six bounded tile indices,
  and unchanged display/touch/RTC transport invariants.
- Delegated pass performs no hardware access, upload, monitor, artifact write,
  commit, or secret-file creation. Main session will review, create the ignored
  local secret, upload, monitor connection status/heap, capture all six tiles,
  and visually judge every image.

## Milestone 4.1: NTP-backed RTC initialization

### Objective

Make the existing Clock tile functional after Wi-Fi connects. The firmware
must obtain local America/Phoenix time from NTP, write that value to the
on-board PCF85063, verify the write by reading the RTC back, and then continue
using the RTC as the Clock tile's once-per-second source. With no battery
installed, this synchronization is expected once per powered boot.

### Behavior

- Add a small project-local non-blocking time synchronization service. It is
  started during the existing setup flow and polled from the Arduino main loop.
- Wait until `wifi_service_snapshot()` reports `kOnline`, then request SNTP
  with `configTzTime()` using POSIX timezone `MST7` (America/Phoenix, UTC-7,
  no daylight-saving transition) and at least two public NTP servers.
- Never use the blocking default `getLocalTime()` timeout. Poll the system
  clock without delaying the LVGL/display loop, and accept only a plausible
  synchronized year of 2024 or later.
- Convert the synchronized local `struct tm` to `RtcDateTime`, write it to the
  PCF85063 time registers, and read it back. Report success only after a valid
  readback matching the requested local date and time, allowing a one-second
  rollover during verification.
- Synchronize once per firmware boot. If Wi-Fi disconnects before the clock is
  synchronized, remain retryable and resume when Wi-Fi is online again.
- Emit concise status lines such as `time_sync_status=waiting_wifi`,
  `time_sync_status=requested timezone=America_Phoenix`,
  `rtc_set_status=online`, and `time_sync_status=online`. Never log the SSID,
  password, or other credential material.
- Once the RTC is valid, the existing one-second Clock callback must replace
  the fallback `--:--` / `0000-00-00` values automatically and animate the
  seconds, minutes, and hours arcs from RTC data.

### RTC transport constraints

- Extend the project-local RTC service with a validated write API; preserve
  its existing validated read API and boot status behavior.
- Use the already-installed shared ESP-IDF `I2C_NUM_0` controller, address
  `0x51`, SDA 11, and SCL 10. Do not install, delete, reset, or reconfigure the
  I2C driver and do not call `Wire.begin()`.
- Encode seconds, minutes, hours, day, weekday, month, and two-digit year as
  PCF85063 BCD values beginning at register `0x04`. Validate years 2000-2099,
  month lengths, leap years, and all time ranges before writing.
- A time-sync or RTC-write failure must not block or degrade display/touch/UI
  operation.

### Hard constraints

- Preserve the confirmed ST77916 revision-02 init, EXIO2 reset, 80 MHz QSPI,
  RGB565 byte swap, display buffers, screenshot protocol, CST816 path,
  `LV_MEM_SIZE=64 KiB`, and all display/touch pin assignments.
- Preserve the existing setup order through `display_begin()` and keep all
  LVGL work on the main loop/timer context.
- Do not add weather fetching, AP/captive portal, on-screen credential entry,
  SD, IMU, microphone, audio, battery logic, or unrelated UI redesign here.
- Do not expose, copy, modify, or log the ignored local `secrets.h` file in the
  delegated implementation pass. Do not upload, monitor hardware, capture
  generated artifacts, commit, or perform destructive repository operations in
  that pass.

### Documentation and acceptance

- Update README and the existing
  `.context/ctx-08-27-2026-esp32-waveshare-setup.md` with the software design,
  Phoenix timezone behavior, no-battery limitation, and commands/evidence
  boundaries. Never include real credentials.
- `make build` succeeds with the pinned toolchain.
- `python3 scripts/capture_screen.py --self-test` and
  `python3 -m py_compile scripts/capture_screen.py` succeed.
- Source inspection proves non-blocking synchronization, shared-I2C0 RTC
  writing, range validation, readback verification, once-per-boot behavior,
  and unchanged display/touch invariants.
- The main session reviews the exact diff, performs the credential-bearing
  build locally, uploads to the connected board, verifies serial status and
  stable heap, and captures two Clock frames that show a real local date/time
  and changing seconds arc.

### Clock redraw stability correction

Hardware framebuffer validation after the first Milestone 4.1 upload proved
that NTP and RTC synchronization succeed. It also identified unnecessary
full-object invalidation: the callback assigned the same `HH:MM`, date, minute
arc, and hour arc values every second even when only seconds changed.

- Preserve the existing Clock design, RTC source, 1 Hz timer, and all display
  transport behavior.
- In `update_clock()`, format the current time/date as before but call
  `lv_label_set_text()` only when the new text differs from the label's current
  text. Use LVGL's current-label text API and plain `strcmp`; do not cache
  pointers returned by LVGL across updates.
- Call `lv_arc_set_value()` only when the target value differs from the arc's
  current value. Seconds normally changes each tick; minutes and hours should
  invalidate only on their own boundaries.
- Apply the same change-aware assignment to the invalid/fallback state so an
  invalid RTC does not repeatedly invalidate unchanged fallback widgets.
- Do not change widget order, sizes, colors, positions, fonts, tile behavior,
  RTC/time synchronization, Wi-Fi behavior, or display/touch initialization.
- `make test`, `make build NO_SECRETS=1`, screenshot self-test, and Python
  compilation must pass. The main session will then rebuild with local
  credentials, upload, reset, and capture at least three settled Clock frames
  several seconds apart; every frame must retain time, date, Wi-Fi card, and
  all three arcs while the seconds arc advances.

### Settled screenshot capture correction

The change-aware Clock update passed build/runtime checks. The capture path is
also hardened against a request arriving between LVGL draw-buffer strips and
against USB CDC retaining a source buffer while queued chunks are transmitted.

- In `display_send_screenshot()`, before writing the protocol header, perform
  two active-screen invalidate plus synchronous `lv_refr_now(nullptr)` passes,
  then copy the live mirror into a dedicated immutable PSRAM transmit buffer.
  USB CDC may finish sending queued chunks after `write()` returns, so the
  protocol must transmit from the frozen copy rather than the live mirror that
  later LVGL refreshes continue to modify.
- This function already runs from the Arduino main loop; keep all LVGL calls on
  that thread and preserve the existing synchronous serial response.
- Keep both the live mirror and frozen transmit copy in PSRAM. Do not change
  RGB565 byte order, flush callback, dimensions, protocol header/marker, LCD
  transport, or UI design. If either buffer is unavailable, return the existing
  screenshot-unavailable error rather than sending a mutable or partial frame.
- After upload, three captures several seconds apart must all retain the full
  Clock composition and have distinct hashes as the seconds arc advances.

Final validation met this criterion. Three 360 x 360 PNGs had invariant white
text and red/hour-arc pixel counts, while consecutive differences were confined
to the advancing blue seconds arc. An image-viewer optimization initially made
some consecutively opened frames appear partial; direct PNG pixel comparison
proved the stored captures themselves were complete.
