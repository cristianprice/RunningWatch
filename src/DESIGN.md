# RunningWatch — Design

Firmware for the Waveshare ESP32-S3-Touch-AMOLED-2.06, a watch-form-factor board used
here as an interval/running timer.

## Hardware

| Item | Value |
| --- | --- |
| MCU | ESP32-S3R8, 8 MB octal PSRAM, 32 MB flash |
| Display | 2.06" AMOLED, CO5300 driver, QSPI, **410 × 502**, RGB565 |
| Touch | FT3168 capacitive, I2C |
| PMU | AXP2101 (charging, PWR button) |
| RTC | PCF85063 |
| IMU | QMI8658C (6-axis) |
| Audio | ES8311 codec + ES7210 mic ADC + NS4150B amp |
| Storage | TF card slot (SPI) |

Peripherals share one I2C bus on `GPIO14` (SCL) / `GPIO15` (SDA). The display has a
dedicated QSPI bus. USB is native (`GPIO19/20`), so logging goes over USB Serial/JTAG.

## Software stack

```
            app_main  (src/main.c)
                 |
      theme (src/theme.c) + main_screen (src/main_screen.c)
                 |            + clock_widget (src/clock_widget.c)
                 |            + battery_widget (src/battery_widget.c)
                 |          + settings_screen (src/settings_screen.c)
                 |          + timer_screen (src/timer_screen.c)
                 |
    watch_rtc (src/watch_rtc.c)   <- PCF85063 + system clock
    watch_power (src/watch_power.c) <- AXP2101 fuel gauge
    watch_audio (src/watch_audio.c) <- ES8311 speaker + embedded gong
    watch_timer_config (src/watch_timer_config.c) <- session/break durations in NVS
                 |
               LVGL 9.5
                 |
   waveshare/esp32_s3_touch_amoled_2_06   <- board support package
                 |
   esp_lvgl_port / esp_lcd_sh8601 / esp_lcd_touch_ft5x06
                 |
              ESP-IDF 5.5
```

No display, touch or PMU drivers are written by hand. The Waveshare **BSP component**
owns panel, touch, PMU and the LVGL port; it is pulled in by the ESP-IDF Component
Manager via `src/idf_component.yml`, pinned in `dependencies.lock`, and unpacked into the
generated `managed_components/` directory. The RTC and the PMU's fuel gauge are the
exceptions — the BSP exposes neither, so `watch_rtc.c` and `watch_power.c` drive the
PCF85063 and the AXP2101 directly on the BSP's I2C bus.

## Runtime model

`bsp_display_start()` brings up the panel, touch and LVGL, and spawns the **LVGL task**
that owns rendering, timers and input polling from then on.

LVGL is not thread-safe, so every LVGL call made outside an LVGL event callback must hold
the port mutex:

```c
bsp_display_lock(0);   /* 0 = block indefinitely */
/* ... build or mutate LVGL objects ... */
bsp_display_unlock();
```

`app_main` is deliberately thin: it starts the BSP, restores the clock from the RTC,
turns on battery metering and the speaker, loads the saved timer configuration, installs
the theme and builds the first screen under the lock, loads it, and returns. All later
work happens in the LVGL task via event callbacks and LVGL timers.

## Timekeeping

The PCF85063 RTC is battery-backed, so it is the **source of truth for the wall clock**.
`src/watch_rtc.c` / `watch_rtc.h` owns it:

| Function | Role |
| --- | --- |
| `watch_rtc_init()` | installs the timezone, attaches the chip to the BSP I2C bus, copies the stored time into the system clock |
| `watch_rtc_get_time()` | reads the chip directly |
| `watch_rtc_set_time()` | writes the chip **and** the system clock, so a new time survives a reboot |

The chip stores **UTC**; local time comes from the `WATCH_RTC_TIMEZONE` POSIX TZ string in
`watch_rtc.h`, which is the only place the watch's timezone is configured. The PCF85063
raises an oscillator-stop flag when it loses its backup supply; `watch_rtc_init()` treats
that as "never set" and seeds the chip with the firmware build timestamp, so a fresh board
never shows 1970.

`watch_rtc_init()` runs in `app_main` after `bsp_display_start()` (which brings the shared
I2C bus up) and before the first screen is built, so the clock already reads correctly on
its first frame. The time must only ever be changed through `watch_rtc_set_time()` —
calling `settimeofday()` directly would drift the two clocks apart and lose the value on
reboot.

## Power

`src/watch_power.c` / `watch_power.h` talks to the AXP2101 PMU (I2C `0x34`).
`watch_power_init()` enables cell detection (`0x68`) and the battery-voltage ADC channel
(`0x30`), which the PMU's built-in fuel gauge needs; `watch_power_get_battery()` then
returns a `watch_power_battery_t` with presence (`STATUS1` bit 3), charging state
(`STATUS2` bits 6:5) and state of charge (`0xA4`). A gauge that has not settled yet
reports `percent = -1` rather than a bogus number.

The BSP already powers the board through the AXP2101, so this module only *reads* it —
rail configuration stays with the BSP. The one exception is the power-off path in
`watch_sleep.c`, which writes `REG 0x10`.

## Sleep and the PWR button

`src/watch_sleep.c` / `watch_sleep.h` owns the PWR button and the watch's power state:

| Press | Effect |
| --- | --- |
| short | toggles sleep — display off and LVGL paused, or back on |
| long (> ~1.5 s) | `watch_sleep_power_off()`: AXP2101 soft power-off, every rail drops |

The button is wired to the **AXP2101's PWRON pin only**; the PMU's `IRQ` line is not
routed to any ESP32 GPIO on this board (GPIO18 is the vibration motor, 21 the IMU, 38
touch, 39 the RTC). So there is no interrupt and no GPIO wake source for it — a small task
polls the PMU's IRQ status register (`0x49`, bits 3:0 are the PWRON events, write-1-to-
clear) every 50 ms over I2C. The PMU latches the press, so the poll period only decides
how fast the screen reacts, never whether a press is seen. `watch_sleep_init()` enables
the short- and long-press sources in `REG 0x41` and clears stale flags first, so a press
during boot does not blank the screen immediately.

Sleep is **display off, not MCU sleep**: `bsp_display_brightness_set(0)` followed by
`lvgl_port_stop()`, which parks the LVGL task so nothing renders. The CPU keeps running,
which is what lets the poll task see the wake press. The brightness in force before
sleeping is restored on wake, not a hardcoded 100.

Power-off is the real low-power state: `REG 0x10` bit 0 (*Soft PWROFF*) drops DCDC/LDO,
so the ESP32 loses RAM and **cold-boots** when PWR is next held past `ONLEVEL`
(`REG 0x27[1:0]`). The PCF85063 runs off its own backup rail, so the clock is still
correct after a power cycle. With USB attached, VBUS re-powers the PMU immediately; the
call detects that it is still alive after `POWER_OFF_SETTLE_MS`, logs it and restores the
previous display state, so power-off only behaves as intended on battery.

Holding PWR for 6 s (`OFFLEVEL`) is a hardware power-off inside the PMU that no firmware
can intercept — the long-press IRQ simply fires first.

## Audio

`src/watch_audio.c` / `watch_audio.h` owns the ES8311 speaker path.
`watch_audio_init()` calls `bsp_audio_codec_speaker_init()` (which brings up I2S and the
amplifier for us), parses the embedded sample and starts a small FreeRTOS **playback
task**. `watch_audio_play_gong()` only notifies that task and returns, because writing a
sample blocks for its whole duration and must never happen on the LVGL task. A request
that lands while the gong is still sounding is dropped rather than queued.

`watch_audio_play_gongs(count)` is the same call with a strike count, carried in the task
notification value. The strikes are written back to back through a **single codec open**:
re-opening between them would power-cycle the amplifier and add an audible gap. The
sample is short enough that repeating it in full already sounds like fast, separate hits,
so no truncation or fading is needed. `watch_audio_play_gong()` is `count = 1`.

The codec is opened and closed around each sound, so the power amplifier is only live
while something is playing.

### The sample

`assets/gong.wav` is **16 kHz, 16-bit, mono, 1 s (≈32 KB)**, which matches the BSP's I2S
slot configuration (`I2S_DATA_BIT_WIDTH_16BIT`, `I2S_SLOT_MODE_MONO`) so no conversion is
needed at runtime. It is derived from the 44.1 kHz stereo 24-bit `assets/*.aiff` original:

```bash
ffmpeg -i assets/<source>.aiff -t 1 -af "afade=t=out:st=0.85:d=0.15" \
       -ac 1 -ar 16000 -acodec pcm_s16le assets/gong.wav
```

The WAV is **linked into the app image**, not stored on SPIFFS, so flashing the firmware
is the only step needed. `watch_audio.c` walks the RIFF chunk list instead of assuming a
44-byte header — ffmpeg writes a `LIST`/`INFO` chunk before `data`, which a fixed offset
would land in the middle of.

### Embedding assets

`board_build.embed_files` in `platformio.ini` points at the asset. PlatformIO generates
`$BUILD_DIR/gong.wav.S` from it with the standard `_binary_gong_wav_start` / `_end`
symbols, but on the `espidf` framework it only declares a *dependency* on that assembly
and never assembles it, so the symbols never reach the linker. `embed_assets.py`
(`extra_scripts`) assembles it and puts the object on the link line.

Adding another asset means listing it under `board_build.embed_files`; the script picks
it up automatically. Do **not** use ESP-IDF's `EMBED_FILES` in `src/CMakeLists.txt` —
PlatformIO mis-resolves the path of the assembly CMake generates and the build fails
before compiling.

## Interval timer configuration

`src/watch_timer_config.c` / `watch_timer_config.h` owns the two durations the interval
timer runs on — a **session** and a **break**, each stored as a whole number of seconds in
a `watch_timer_config_t`. `watch_timer_config_get()` / `watch_timer_config_set()` read and
write an in-memory copy; the setter also persists it.

The store is **NVS**, not the RTC. The PCF85063 exposes a single byte of user RAM, which
cannot hold two durations, and NVS survives a power-off just as well — the `nvs` partition
is already in `partitions.csv`. `watch_timer_config_init()` therefore owns
`nvs_flash_init()` for the whole firmware; a partition that is full or written by an older
NVS version is erased and re-created rather than failing the boot, because the timer
configuration is the only thing in it and it falls back to its defaults (5 min session,
1 min break).

Both durations are clamped to `WATCH_TIMER_MIN_SECONDS` … `WATCH_TIMER_MAX_SECONDS`
(1 s … 99:59) on load and on save, so neither the editor nor the future timer state
machine has to defend against a nonsense value.

## UI structure

### Theme

The palette lives in its own compilation unit, `src/theme.c` / `src/theme.h`, so no screen
hardcodes colours. It is a **black/orange** scheme:

| Macro | Value | Use |
| --- | --- | --- |
| `THEME_COLOR_BACKGROUND` | `0x000000` | screen background (true black, pixels off on AMOLED) |
| `THEME_COLOR_PRIMARY` | `0xFF7A00` | interactive outlines, icons |
| `THEME_COLOR_PRIMARY_DARK` | `0xB34D00` | pressed / secondary surfaces |
| `THEME_COLOR_ON_BACKGROUND` | `0xFFA94D` | text and icons on black |

`theme_apply()` re-initializes LVGL's default theme in dark mode with the orange palette
and attaches it to the active display. It must run **with the LVGL lock held, after
`bsp_display_start()` and before the first screen is created**, so every object inherits
it. Screens still set their own background and on-colour from the macros above.

Changing the look means editing `theme.h` only; adding a colour means adding a macro
there rather than an `lv_color_hex()` call in a screen.

One screen per compilation unit. A screen module exposes a single
`<name>_screen_create()` that returns a **detached** screen object; the caller decides
when to show it with `lv_screen_load()`. This keeps screens independently testable and
makes future screen-to-screen navigation a matter of creating and loading.

### `main_screen`

The screen stacks a **transparent flex column** in its center: the clock block on top, the
button row underneath, separated by `CLOCK_GAP`. Nesting the two groups keeps the whole
thing centered as a unit when either part changes size. The battery icon is deliberately
*not* part of that column — it is a direct child of the screen, aligned to the top-right
corner with `BATTERY_MARGIN` of inset so it clears the panel's rounded corner.

Two circular, icon-only buttons sit in the row. They are **outline-only**:
transparent fill, orange ring (`BUTTON_BORDER_WIDTH`) and orange icon, both dimming to
`THEME_COLOR_PRIMARY_DARK` while pressed.

| Icon | Constant | Action |
| --- | --- | --- |
| gear | `LV_SYMBOL_SETTINGS` | open `settings_screen` |
| play | `LV_SYMBOL_PLAY` | open `timer_screen` |

Centering is done by placing the buttons in a **transparent flex row that is itself
centered** inside the screen's column, rather than aligning each button individually. The
group then stays centered when button count or size changes.

Each button stores an action tag through `lv_obj_set_user_data()`. The play button swaps
in `timer_screen`, the gear button `settings_screen`.

Sizes come from the `BUTTON_SIZE` / `BUTTON_GAP` / `CLOCK_GAP` enum at the top of the
file — adjust there rather than at the call sites.

### `clock_widget`

`src/clock_widget.c` / `clock_widget.h` is the only widget module that is not a screen:
`clock_widget_create(parent)` returns a transparent, self-sizing flex column holding a
Montserrat 48 `HH:MM:SS` readout over a Montserrat 28 `Wkd dd Mon yyyy` date. Screens drop
it into their own layout; `main_screen` puts it above the buttons.

Each instance keeps its labels in a `clock_widget_t` stored in the container's user data,
and refreshes from `localtime_r()` on an LVGL timer. The timer ticks every
`CLOCK_REFRESH_PERIOD_MS` (200 ms) but only relabels when the second actually changes, so
the display never lags a visible tick behind while still redrawing once a second. An
`LV_EVENT_DELETE` handler deletes the timer and frees the state with the object, so the
widget can be created and destroyed with its screen.

### `battery_widget`

`src/battery_widget.c` / `battery_widget.h` is a second non-screen widget:
`battery_widget_create(parent)` returns a **single Montserrat 28 label** holding one
glyph and no text. It is unpositioned on purpose — the caller aligns it, which is what
lets `main_screen` pin it to the top-right corner.

| State | Glyph | Colour |
| --- | --- | --- |
| charging | `LV_SYMBOL_CHARGE` | `THEME_COLOR_PRIMARY` |
| 0–10 / 11–35 / 36–60 / 61–85 / 86–100 % | `LV_SYMBOL_BATTERY_EMPTY` … `_FULL` | `THEME_COLOR_ON_BACKGROUND` |
| no battery, or gauge not settled | `LV_SYMBOL_BATTERY_EMPTY` | `THEME_COLOR_PRIMARY_DARK` (dimmed) |

LVGL only ships five battery glyphs, so the percentage is bucketed by the
`BATTERY_LEVEL_*` thresholds at the top of the file. The PMU is polled every
`BATTERY_REFRESH_PERIOD_MS` (10 s) — a fuel gauge moves far too slowly to justify the
clock's cadence. Like the clock, the LVGL timer is torn down on `LV_EVENT_DELETE`.

### `timer_screen`

`src/timer_screen.c` / `timer_screen.h` runs the interval. Its layout mirrors the main
screen: a centered transparent flex column holding the phase caption (Montserrat 28,
orange) over the Montserrat 48 `MM:SS` countdown, with the icon-only back button
underneath and the battery icon pinned to the top-right corner.

The **break always runs first**, so a session is never entered cold. When a phase runs
out, `watch_audio_play_gongs(3)` sounds three fast strikes and the other phase starts
immediately; this alternates forever until the back button (`LV_SYMBOL_CLOSE`) loads a
fresh main screen. There is no pause — stopping is leaving the screen.

Durations are read from `watch_timer_config_get()` **once, when the screen is created**,
so a change made in the settings screen takes effect the next time the timer is started
rather than mid-interval.

The countdown is an LVGL timer ticking every `TICK_PERIOD_MS` (100 ms), but the remaining
time is derived from `lv_tick_elaps()` against the tick the phase started at, never
accumulated per callback — a late or coalesced timer therefore cannot make the interval
drift. The value is **rounded up**, which keeps the full duration on screen for the first
second and shows `00:00` only at the very end, and the label is only rewritten when the
second actually changes.

State lives in one allocation hung off the screen's user data; `LV_EVENT_DELETE` deletes
the LVGL timer and frees it, so navigating away stops the countdown. The screen also sets
the gong volume to 100 % on creation, loud enough to be heard while running.

### `settings_screen`

`src/settings_screen.c` / `settings_screen.h` is the second screen. It edits two things:
the **wall clock** (hour, minute, year, month, day) and the **interval timer** (session
and break, each as minutes + seconds). Nothing is applied while editing — the confirm
button writes the clock through `watch_rtc_set_time()` and the durations through
`watch_timer_config_set()`, and the back button discards everything.

Each value is a row: a caption on the left (abbreviated — `Hr`, `Min`, `Yr`, `Mon`, `Day`,
`Sec` — and clipped rather than wrapped, so a row is always one line high), then
`−  value  +` right-aligned, built by `create_field_row()`. The stepper pair was chosen
over a roller or a spinbox because it
needs no LVGL widget beyond button and label, so `sdkconfig.defaults` is unchanged. Both
buttons are wired to `LV_EVENT_SHORT_CLICKED` **and** `LV_EVENT_LONG_PRESSED_REPEAT`, so
a hold runs through a range; using `SHORT_CLICKED` rather than `CLICKED` keeps the release
at the end of a hold from adding one extra step. Values **wrap** at their limits, which
turns "59 from 0" into one tap.

Nine rows do not fit on a 410 × 502 panel, so the screen object itself is the scroll
container: a vertical flex column of section titles (`Time`, `Session`, `Break`), rows,
and the back/confirm pair at the bottom.

State lives in one `settings_screen_t` allocation hung off the screen's user data and
freed on `LV_EVENT_DELETE`. It holds a `field_t` per value (current value, limits,
zero-padding width, label) plus a `step_t` per button naming the field and the direction,
which is what lets a single `on_step_clicked()` serve all eighteen stepper buttons. The
day field's maximum is recomputed by `clamp_day()` whenever the month or the year changes,
so 31 January cannot survive a switch to February. Seconds are deliberately zeroed when
the clock is written — a time is set to the minute.

### Navigation

Screens replace each other with
`lv_screen_load_anim(next, LV_SCR_LOAD_ANIM_NONE, 0, 0, /* auto_del */ true)` from inside
the event callback, so the outgoing screen (and the LVGL timers its widgets own) is
destroyed once the load completes. Leaving the settings screen therefore builds a **fresh**
main screen, which is how a time the user has just changed shows up immediately.

## Configuration

`sdkconfig.defaults` is the source of truth and is committed; `sdkconfig.<env>` is
generated from it. Because ESP-IDF lets an existing generated `sdkconfig` win over the
defaults file, **delete the generated file after editing the defaults**, otherwise the
change silently does nothing.

Key settings:

- 32 MB QIO flash; custom `partitions.csv` (8 MB factory app + 23 MB SPIFFS).
- Octal PSRAM at 80 MHz — required to hold LVGL framebuffers for a 410 × 502 panel.
- 240 MHz CPU, `FREERTOS_HZ=1000`, LVGL refresh period 15 ms.

### LVGL is opt-in

LVGL enables most widgets, both themes, all color formats, and its examples/demos by
default. `sdkconfig.defaults` turns these **off** and re-enables only what the UI uses:
button, label, flex, the default theme, RGB565 + ARGB8888 blending, and two Montserrat
fonts — 48 (also the default, used for the time and button icons) and 28 (the date).

When a new widget or feature is used, flip its `CONFIG_LV_USE_*` back to `y` in the same
section. Leaving everything on costs significant flash and build time.

## Not yet implemented

- IMU use — available through the BSP.
