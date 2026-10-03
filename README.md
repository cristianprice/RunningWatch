# RunningWatch

Interval-timer firmware for the **Waveshare ESP32-S3-Touch-AMOLED-2.06**, a watch-form-factor
board with a 2.06" round AMOLED, capacitive touch, PMU, RTC and an onboard speaker.

The watch shows the time and battery on its home screen; a play button starts a
**break / session** interval that loops forever, marking every switch with three gongs.

## Features

- Wall clock and date, kept by the battery-backed PCF85063 RTC.
- Battery state from the AXP2101 fuel gauge.
- On-screen editor for the clock and for the session/break durations, persisted in NVS.
- Interval timer: counts the break down first, sounds three fast gongs, then the session,
  and repeats until it is stopped.
- Audio cues through the ES8311 codec, from a WAV linked into the app image.
- PWR button: short press sleeps the display, long press powers the board off.

## Hardware

| Item | Value |
| --- | --- |
| MCU | ESP32-S3R8, 8 MB octal PSRAM, 32 MB flash |
| Display | 2.06" AMOLED, CO5300, QSPI, 410 × 502, RGB565 |
| Touch | FT3168 (I2C) |
| PMU | AXP2101 |
| RTC | PCF85063 |
| IMU | QMI8658C (unused so far) |
| Audio | ES8311 codec + NS4150B amplifier |

## Toolchain

- **PlatformIO** with the `espidf` framework (**ESP-IDF 5.5**)
- **LVGL 9.5**
- Waveshare's official ESP-IDF **BSP component**, which owns the panel, touch, PMU and the
  LVGL port — no display or touch drivers are written by hand.

Components are resolved by the ESP-IDF Component Manager from `src/idf_component.yml`,
pinned in `dependencies.lock` and unpacked into the generated, git-ignored
`managed_components/`.

## Build and flash

```bash
pio run                 # build
pio run -t upload       # flash over native USB
pio device monitor      # serial monitor, 115200 baud
pio run -t clean        # clean build artifacts
```

If a build fails with `The downloaded component "<name>" is corrupted`, delete that one
directory under `managed_components/` and rebuild to re-fetch it.

`sdkconfig.defaults` is the committed board configuration (32 MB QIO flash, octal PSRAM at
80 MHz, 240 MHz CPU, trimmed LVGL feature set). **After editing it, delete the generated
`sdkconfig.waveshare_esp32_s3_touch_amoled_2_06`** — ESP-IDF gives the generated file
precedence, so the change is silently ignored otherwise.

## Using the watch

| Screen | Controls |
| --- | --- |
| Main | gear → settings, play → start the interval timer |
| Settings | `−` / `+` steppers for hour, minute, date and both durations; ✗ discards, ✓ saves |
| Timer | shows the running phase and the countdown; ✗ stops and returns home |

Holding a stepper runs through its range, and values wrap at their limits. The timer
always starts with the break.

## Layout

```
assets/            gong sample (WAV) and its source recording
src/               application sources
  main.c           thin entry point: BSP, RTC, power, audio, config, first screen
  main_screen.c    home screen
  settings_screen.c  clock and duration editor
  timer_screen.c   interval countdown
  clock_widget.c   HH:MM:SS + date widget
  battery_widget.c battery glyph widget
  theme.c          black/orange palette
  watch_rtc.c      PCF85063
  watch_power.c    AXP2101 fuel gauge
  watch_sleep.c    PWR button, sleep and power-off
  watch_audio.c    ES8311 speaker and the gong
  watch_timer_config.c  session/break durations in NVS
  DESIGN.md        detailed design document
partitions.csv     8 MB factory app + 23 MB SPIFFS
embed_assets.py    links the embedded assets into the image
```

`src/DESIGN.md` documents the hardware map, software stack, runtime model and UI structure
in detail — read it before making non-trivial changes.

## Tests

There are none yet; `test/` is the empty PlatformIO Test Runner scaffold. Once tests exist
they run with `pio test`.
