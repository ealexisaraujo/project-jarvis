# Location profile configuration

## Objective

Make the Jarvis city, weather coordinates, weather observation timezone, and
RTC/NTP timezone come from one compile-time location profile. Select Chandler,
Arizona by default. A future move must require changing one clearly marked
profile-selection line rather than editing UI, networking, and clock services.

## Design and constraints

- Add a small hardware-independent `location_config` module under
  `firmware/project_jarvis/`.
- Store a display name, latitude, longitude, IANA timezone, and ESP32-compatible
  POSIX timezone in each profile.
- Include verified profiles for Chandler, Arizona and the previous Ashford,
  Kent location, plus clear instructions for adding another city.
- Keep the active profile selection in one obvious line.
- Build the Open-Meteo URL into a fixed-size caller-owned buffer. Percent-encode
  the timezone query component and reject invalid profiles or insufficient
  buffers. Do not introduce dynamic `String` ownership into persistent state.
- Use the same active profile for the Weather city label, Open-Meteo request,
  and `configTzTime()` call.
- Preserve weather scheduling, response limits, worker/LVGL separation, cached
  state, display/touch initialization, Wi-Fi behavior, RTC verification, and
  all existing UI layout.
- Do not alter local credentials, dependencies, generated files, artifacts, or
  unrelated user changes. Implementation workers do not flash; the reviewed
  build is uploaded only during the final hardware-validation step.

## Documentation

Update the README's current behavior and add a concise "Changing location"
section. Explain that Wi-Fi does not determine the city, identify the one-line
profile selection, document how to obtain latitude/longitude/IANA timezone via
Open-Meteo's geocoding endpoint, and state that the POSIX timezone must also be
correct for RTC local time and daylight-saving behavior.

Chandler profile source checked on 2026-08-27 using Open-Meteo Geocoding API:

- city: Chandler, Arizona, United States
- latitude: `33.30616`
- longitude: `-111.84125`
- IANA timezone: `America/Phoenix`
- POSIX timezone: `MST7` (Arizona does not observe DST)

## Acceptance criteria

- `make test` succeeds, including host tests for valid/invalid profiles, URL
  creation, timezone percent-encoding, and small-buffer rejection.
- Switching the active profile to another valid profile must not require
  editing the test suite; tests validate the selected profile generically while
  separately checking the bundled Chandler and Ashford values.
- Reject non-ASCII or control characters in `display_name` before that value can
  reach LVGL, and cover the rejection with a host test. This board's configured
  Montserrat subset is intentionally ASCII-only.
- `make build` succeeds with the pinned Arduino toolchain.
- Source search finds no live-service hard-coded Ashford city/coordinates or
  Europe/London timezone outside the reusable Ashford profile and historical
  documentation/evidence.
- The active Chandler profile drives city text, weather URL, and clock timezone.

## Implementation and verification

Status: complete and hardware-verified on 2026-08-27.

- `firmware/project_jarvis/location_config.cpp` is the single editable location
  source. Chandler and Ashford are bundled; line 27 selects the active profile.
- The Weather title, Open-Meteo request, and RTC/NTP timezone all consume that
  active profile. Wi-Fi remains connectivity only and does not infer a city.
- The URL builder validates ranges, uses a fixed 320-byte buffer, percent-encodes
  the IANA timezone, and returns an error for invalid data or insufficient space.
- Display names are restricted to printable ASCII before reaching LVGL. Invalid
  configuration renders the safe ASCII fallback `Invalid location`.
- `make test`, an Ashford-selection test without test edits, `make build`, and
  `git diff --check` passed. The final build used 1,644,729 bytes (52%) of flash
  and 116,392 bytes (35%) of static RAM.
- The firmware was uploaded through `/dev/cu.usbmodem1101`. Runtime reached
  `wifi_status=online`, `weather_status=online`, `rtc_set_status=online`, and
  `time_sync_status=online`, with the requested timezone `America/Phoenix` and
  stable free heap near 202.9 KB.
- `artifacts/jarvis-m4-3-weather-chandler.png` is a verified 360 x 360 capture
  showing Chandler live weather and Phoenix-local observation time. SHA-256:
  `b6e4965faf5ea3ec3768a7060c82fd5024b97ae1aef99154dac05add1c5b2196`.

## Moving to another city

1. Query Open-Meteo Geocoding for the intended city and verify the region.
2. Add a `LocationProfile` near the bundled profiles with an ASCII display
   name, latitude, longitude, IANA timezone, and correct POSIX timezone rule.
3. Change only `kActiveLocationProfile` to the new profile.
4. Run `make test` and `make build`; the tests do not need to be edited when the
   active profile changes.
5. Upload the new build. Weather and RTC local time change together after boot.
