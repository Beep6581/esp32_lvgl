# Display configuration decisions

This document records why the display, memory, buffering, and relevant LVGL
settings have their present values. It is an engineering record, not a list of
settings that must never change. Results should be re-tested when the display
module, ESP-IDF, LVGL, `esp_lvgl_port`, memory layout, or UI workload changes.

The snapshot reviewed for this record was `master` at `a900b39`, with the
experimental `triple_buffer` branch at `ee7bfa6`. The installed dependency lock
selected:

- ESP-IDF 6.1.0
- LVGL 9.6.0
- `esp_lvgl_port` 2.9.0
- `esp_lcd_gc9503` 3.0.1

Most controlled display experiments were performed immediately before or
during the migration from ESP-IDF 6.0.2 to 6.1.0, using LVGL 9.6.0 and
`esp_lvgl_port` 2.9.0. The representative benchmark was essentially unchanged
after the ESP-IDF 6.1 migration; the upgrade itself did not improve display
performance. Where an observation depends on a particular implementation, that
dependency is called out below.

## Sources of truth

The effective configuration is spread across several layers:

- `components/display/display.c` owns panel initialization, RGB timing, RGB
  driver buffers, and the LVGL display-port settings.
- `components/board/Kconfig` owns display dimensions and pins.
- `sdkconfig.defaults` is intended to reproduce repository defaults.
- `sdkconfig` is the generated configuration for the current local build.
- `main/idf_component.yml` and `dependencies.lock` select the component
  versions.
- `components/ui/screen_diagnostics.c` contains the repeatable diagnostic
  workloads and custom timing counters.

`CONFIG_LV_CONF_SKIP=y` is selected by the managed LVGL component. The current
LVGL configuration therefore comes from ESP-IDF Kconfig/`sdkconfig`, not from
the old custom `lv_conf.h` that was removed in commit `be33187`.

## Hardware and data path

The target is an ESP32-S3 board in the WT32-S3-WROVER-N16R8 class:

- 16 MB flash
- 8 MB Octal/OPI PSRAM
- 480 x 480 GC9503V-controlled LCD
- 16-bit RGB/DPI pixel bus carrying RGB565 pixels
- separate 3-wire SPI command interface used during panel initialization

One full RGB565 framebuffer occupies:

```text
480 * 480 * 2 = 460800 bytes, approximately 450 KiB
```

Two framebuffers therefore use about 900 KiB. That is much larger than the
practical internal-SRAM budget, so the RGB driver allocates the full
framebuffers in external PSRAM. LVGL DIRECT mode renders into those same
screen-sized buffers.

The ESP-IDF RGB driver also allocates two smaller DMA-capable bounce buffers in
internal SRAM. Each current 40-line RGB565 bounce buffer is:

```text
480 * 40 * 2 = 38400 bytes
```

The RGB peripheral consumes one bounce buffer while the driver refills the
other from the selected PSRAM framebuffer. This isolates the continuous LCD
DMA stream from much of the latency of direct external-memory access, but the
driver must still refill each buffer before its deadline. The CPU data cache
services CPU access to PSRAM and participates in these refills. Consequently,
PSRAM bandwidth, cache geometry, bounce-buffer size, and pixel clock interact;
none should be evaluated in isolation.

In this document:

- **Internal SRAM** is scarce, low-latency on-chip memory used for the two
  bounce buffers, DMA data, stacks, and other internal-only allocations.
- **External PSRAM** is the 8 MB Octal memory used for large full-screen
  framebuffers and general allocations.
- **D-cache** is the on-chip cache through which the CPUs access PSRAM. Cache
  size and line length affect both rendering and framebuffer refill behavior.
- **Full framebuffer** means one complete 480 x 480 RGB565 image in PSRAM.
- **Bounce buffer** means one 40-line internal-SRAM staging buffer used to feed
  RGB DMA; ESP-IDF allocates two of them.

## Current master baseline

The following is the current two-framebuffer baseline on `master`. Values in
this table come from current source or generated `sdkconfig`, not from an older
design target.

| Area | Current value | Scope and reason |
| --- | --- | --- |
| CPU | 240 MHz | Active and repository default. Keeps the software renderer at the ESP32-S3's selected maximum application frequency. No separate lower-frequency A/B result was preserved. |
| Flash | 16 MB, DIO, 80 MHz | Active build. The size matches the module. No display-specific QIO/DIO or flash-frequency benchmark was preserved. Only 16 MB is explicitly retained in `sdkconfig.defaults`. |
| PSRAM | Octal, 80 MHz | Active and repository default. 40 MHz caused periodic RGB shift/glitch behavior; 80 MHz fixed it. Experimental 120 MHz was faster but was not retained for reliability reasons. |
| PSRAM XIP | Enabled, including instruction fetch and read-only data | Active and repository default. Retained, but no clear display FPS improvement was measured in the tested workload. |
| D-cache | 64 KiB, 8-way, 64-byte lines | Active and repository default. Both capacity and especially 64-byte lines improved the tested PSRAM/RGB workload. |
| FreeRTOS tick | 1000 Hz | Active and repository default. Gives 1 ms timing granularity used by the UI/diagnostic workload; no alternative tick-rate benchmark was preserved. |
| LCD format | 480 x 480, RGB565, 16 data lines | Source setting and board Kconfig. Matches the physical RGB bus and keeps each pixel at two bytes. |
| RGB framebuffers | 2, in PSRAM | Source setting. Provides synchronized double buffering at about 900 KiB total while retaining the simpler supported port path. |
| RGB bounce buffers | 2 buffers, 40 lines each, internal DMA SRAM | Source setting. 40 lines retained stability; smaller tested values did not improve FPS and eventually glitched. |
| DMA burst | 64 bytes | Source setting. 32 was neutral; 128 was rejected by the tested ESP-IDF/GDMA external-memory path. |
| RGB scanout | Continuous (`refresh_on_demand=0`) | Source setting. The panel is continuously refreshed. |
| RGB cache behavior | `bb_invalidate_cache=1` | Source setting. It allows bounce-buffer mode to invalidate framebuffer data after reads and reduce cache occupation. Its individual contribution was not isolated; ESP-IDF warns that concurrent writes from another core require care. |
| LVGL render mode | DIRECT | Source setting. Preserves dirty-region rendering while drawing directly into full RGB framebuffers. |
| Full refresh | Disabled | Source setting. Avoids redrawing all 480 x 480 pixels when only small dashboard regions change. |
| Tearing avoidance | Enabled | Source setting. Makes `esp_lvgl_port` use the RGB driver's two framebuffers and wait for the RGB frame-complete event before reuse. |
| Bounce-buffer port mode | Enabled | Source setting. Selects the RGB frame-buffer-complete callback appropriate to ESP-IDF bounce-buffer mode. |
| LVGL refresh period | 15 ms | Active and repository default. A 10 ms test did not improve the representative Dashboard result. |
| LVGL software draw units | 2 | Active and repository default. Improved the demanding test with 64 KiB D-cache; it did not help with 32 KiB. |
| LVGL private heap | 131072 bytes (128 KiB) | Active and repository default. 64 KiB was exhausted while drawing three charts with many points and caused a crash. |
| LVGL object style cache | Enabled | Active generated config. Neutral in the simple solid-swipe test, retained because a real UI performs more style lookup work. It is currently an LVGL default and is not explicit in `sdkconfig.defaults`. |
| LVGL Sysmon/performance overlay | Disabled | Active generated config. Removed because its label work perturbed measurements and its reported 100% CPU result was invalid without the required task/idle accounting. |
| LVGL profiler | Disabled | Active generated config. Profiling was used during investigation, but is not part of the normal configuration. |
| LVGL C library substitutions | Built-in malloc/string/sprintf; CLIB variants disabled | Active generated config. The standard C implementations produced no measurable gain and were reverted. These are current component defaults rather than explicit lines in `sdkconfig.defaults`. |
| LVGL fast functions in IRAM | Disabled | Active generated config. Enabling `LV_ATTRIBUTE_FAST_MEM_USE_IRAM` consumed enough internal SRAM that RGB bounce-buffer allocation failed. |
| Compiler optimization | Performance | Active and repository default. Appropriate for the measured display workload, although no controlled debug-versus-performance result is preserved. |

`CONFIG_LCD_RGB_RESTART_IN_VSYNC` is currently disabled. It was briefly added
to repository defaults in commit `81eb2b7`, then removed when the 80 MHz PSRAM
fix was recorded in `34bc6e1`. The history does not establish whether disabling
restart-on-VSYNC was independently tested or was only a configuration cleanup,
so no stronger rationale is claimed.

### Why DIRECT plus tearing avoidance is a pair here

With `avoid_tearing=1`, `esp_lvgl_port` 2.9.0 obtains the RGB driver's two
full-screen buffers using `esp_lcd_rgb_panel_get_frame_buffer()` and gives them
to LVGL. DIRECT mode lets LVGL update invalid regions at their absolute
coordinates in those complete images. On the final flush, the port submits the
rendered framebuffer and waits on the semaphore signaled by ESP-IDF's RGB
frame-complete callback before releasing it to LVGL.

Some fields in `lvgl_port_display_cfg_t` can look more influential than they
are in this path. Because tearing avoidance supplies the RGB driver's existing
framebuffers, `buff_dma`, `buff_spiram`, and `double_buffer` do not allocate
separate LVGL buffers in `esp_lvgl_port` 2.9.0. The RGB driver's
`num_fbs=2`/`fb_in_psram=1` settings determine their count and placement.
Likewise, `trans_size=64` is not consumed by this display path in the installed
port. These fields may still be useful as descriptive intent or for another
port mode, but their present rationale is not independently established.

Disabling tearing avoidance while leaving DIRECT mode enabled is not a safe
test with `esp_lvgl_port` 2.9.0. In that version the non-tearing path does not
create the transfer semaphore, while the DIRECT flush path still waits on it.
The result observed at startup was a FreeRTOS assertion on a NULL
queue/semaphore. This is a port-version implementation limitation, not a
fundamental LVGL restriction, and should be rechecked after a port upgrade.

## Panel initialization and signal choices

### Command interface and shared pins

The panel is configured first over 3-wire SPI using:

- CS: GPIO38
- SCL: GPIO45
- SDA/MOSI: GPIO48

GPIO45 and GPIO48 are later RGB data D0 and D1 respectively. The GC9503 vendor
configuration therefore uses `auto_del_panel_io=1`: after panel command
initialization, the command IO is deleted and releases the pins for the RGB
peripheral. `mirror_by_cmd=0` is consistent with deleting that command IO.

The custom `gc9503v_480_init` table is based on Espressif's GC9503 component/BSP
sequence and includes the panel power, gate, gamma, brightness, interface,
sleep-out, and display-on commands. The final interface command is `B0h=0x00`,
selecting DE mode, rising edge, and active-high DE in the configuration used by
this panel. Sleep-out waits 120 ms and display-on waits 20 ms.

### Pixel and color configuration

The host framebuffers, LVGL display, RGB input, and RGB output all use RGB565.
The RGB bus is 16 bits wide, the element order is RGB, the panel command data
endian is big-endian, and LVGL byte swapping is disabled.

The panel driver's `bits_per_pixel` field is deliberately 18 even though the
host framebuffer and physical bus are RGB565/16-bit. In this GC9503 command
setup the field programs command `3Ah`; the 16-bit command-interface selection
was observed to ignore the red and blue most-significant bits. Selecting the
18-bit panel-interface mode produced correct colors while the RGB host path
continues to send RGB565 over 16 lines. The current RGB order replaced an older
BGR setting during the color-correction work.

### Signal polarities

The timing structure starts from
`GC9503_480_480_PANEL_60HZ_RGB_TIMING()`. The current code explicitly keeps:

- `pclk_active_neg=0`: data is clocked out on the non-negative/rising edge
- `de_idle_high=0`: DE is low while idle and therefore active high
- `hsync_idle_low=0` and `vsync_idle_low=0`: sync signals are high while idle
- `pclk_idle_high=0`: PCLK is low while idle

Only `de_idle_high` is reassigned in project code; the other zero values come
from the zero-initialized vendor timing macro. `de_idle_high=0` was required to
match `B0h DEP=0`; the opposite setting produced a powered backlight with a
black image. The panel runs in DE mode, where the GC9503 command comment notes
that sync/pulse control bytes are ignored, but the host still supplies HSYNC
and VSYNC timing.

### Reset and display-enable behavior

On this board GPIO41 is both `BOARD_LCD_RST` and RGB VSYNC. The LCD reset net is
coupled to VSYNC through the board's RC/diode network. Treating GPIO41 as an
ordinary reset and pulsing it broke panel bring-up, so the code passes the pin
to the panel description but intentionally does **not** call
`esp_lcd_panel_reset()` or generate a manual pulse.

There is no separate display-enable GPIO (`disp_gpio_num=GPIO_NUM_NC`). The
backlight on GPIO5 is initialized off, panel and RGB initialization are
completed, and then the backlight is enabled. This avoids displaying
uninitialized panel activity during startup.

## LCD timing

The timing selector stores a mode in NVS under `display/timing_mode` and
restarts the device when a new diagnostic selection is made. This means the
actual timing on any one unit can differ from the source fallback.

The normal/default timing selected by the display investigation is
`DISPLAY_TIMING_MIN_HBP_54HZ`:

| Parameter | Value |
| --- | ---: |
| Pixel clock | 16,600,000 Hz |
| Active area | 480 x 480 |
| HSYNC pulse / HBP / HFP | 1 / 72 / 2 clocks |
| VSYNC pulse / VBP / VFP | 1 / 1 / 71 lines |
| Calculated frame rate | about 54.09 Hz |

The 16.6 MHz clock is the documented lower end of the GC9503V DCLK range. The
75-clock total horizontal blanking satisfies the documented minimum 4.5 us
blanking interval at that clock. Testing favored putting the otherwise
unspecified blanking in HBP. HFP-heavy variants produced a blank white screen
and were removed from the selector.

The ~54 Hz mode extends the vertical front porch relative to the otherwise
similar `DISPLAY_TIMING_MIN_HBP` mode, which runs at about 60.06 Hz. Both worked,
but the lower frame rate reduced PSRAM/bounce-buffer refill pressure in
demanding tests. Across the diagnostic timing families, low-clock MIN modes
were clean after the cache changes, MID showed slight corruption, and MAX was
more susceptible. Increasing PCLK shortens refill deadlines even when nominal
panel timing remains valid.

All timing modes currently present in source are listed below. H values are
HSYNC/HBP/HFP clocks; V values are VSYNC/VBP/VFP lines.

| Mode | PCLK | H values | V values | Calculated rate |
| --- | ---: | ---: | ---: | ---: |
| WT | 20 MHz | 48 / 40 / 8 | 100 / 48 / 8 | about 54.59 Hz |
| BS | 10 MHz | 10 / 40 / 8 | 10 / 40 / 8 | about 34.55 Hz |
| MIN HBP | 16.6 MHz | 1 / 72 / 2 | 1 / 1 / 16 | about 60.06 Hz |
| MIN HBP 54Hz | 16.6 MHz | 1 / 72 / 2 | 1 / 1 / 71 | about 54.09 Hz |
| MIN HSYNC | 16.6 MHz | 71 / 2 / 2 | 1 / 1 / 1 | about 61.93 Hz |
| MID HBP | 26.407680 MHz | 1 / 115 / 2 | 64 / 64 / 128 | 60.00 Hz |
| MID BAL | 26.407680 MHz | 1 / 58 / 59 | 64 / 64 / 128 | 60.00 Hz |
| MID HSYNC | 26.407680 MHz | 114 / 2 / 2 | 64 / 64 / 128 | 60.00 Hz |
| MAX HBP | 35.7 MHz | 33 / 126 / 2 | 126 / 126 / 255 | about 56.42 Hz |
| MAX BAL | 35.7 MHz | 1 / 80 / 80 | 126 / 126 / 255 | about 56.42 Hz |
| MAX HSYNC | 35.7 MHz | 157 / 2 / 2 | 126 / 126 / 255 | about 56.42 Hz |

The 10 MHz Bootstrap (`BS`) mode looked smooth in some tests, but it is below
the documented 16.6 MHz DCLK and 54 fps lower limits and was more susceptible
to a separate panel artifact. It remains a diagnostic option, not the normal
timing.

Two visual problems encountered during this work should not be conflated:

1. A horizontal/every-fourth-row artifact changed with panel timing and panel
   behavior.
2. Large duplicated or shifted vertical blocks under heavy motion were a host
   PSRAM/cache/bounce-refill problem. The 80 MHz PSRAM and especially 64-byte
   D-cache lines addressed this second class.

### Default selection behavior

`DISPLAY_TIMING_MIN_HBP_54HZ` is the first-boot, missing-NVS, NVS-error, and
invalid-selection fallback in `display_get_saved_timing_mode()`. It is the
normal timing for Air Quality, Screen Diagnostics, and future application
modes unless a mode explicitly selects something else. A valid timing already
stored in NVS remains authoritative, so the diagnostic timing selector and all
alternative timing modes continue to work.

Also, `CONFIG_BOARD_LCD_PCLK_HZ` defaults to 16 MHz but no longer controls the
runtime pixel clock: every timing mode in `display.c` assigns its own
`timing.pclk_hz` value. The board option is therefore presently misleading or
redundant.

## Memory and cache experiments

### PSRAM frequency

40 MHz PSRAM was associated with periodic RGB shift/glitch behavior. Moving to
80 MHz fixed that behavior and is preserved in both active and repository
configuration.

120 MHz Octal PSRAM was later tested as a temporary experiment. In the
intentionally demanding full-screen rendering workload, software render time
fell from roughly 29 ms to 17-18 ms. This demonstrated that PSRAM/cache
bandwidth was a dominant cost, but the tested ESP-IDF configuration exposed
120 MHz as experimental and it was not considered reliable enough for the
application. The selected value remains 80 MHz. A later ESP-IDF release or
hardware revision may make 120 MHz worth retesting.

PSRAM XIP remains enabled. No obvious display-performance improvement was
measured from it, so its retention should not be cited as a measured FPS
optimization.

### D-cache line length and capacity

Changing the ESP32-S3 D-cache line length from 32 to 64 bytes was one of the
most important stability findings. Before the change, heavy screen motion
could show duplicated/shifted vertical blocks. With 64-byte lines, all 16.6 MHz
MIN modes became clean, MID retained only slight corruption, and MAX remained
more susceptible. This is an observed result for the tested ESP32-S3 and
ESP-IDF cache/RGB implementation, not a universal rule.

Increasing D-cache capacity also produced a repeatable but smaller performance
gain in the pathological full-screen benchmark with one draw unit:

| D-cache | FPS | Total refresh time |
| --- | ---: | ---: |
| 32 KiB | about 19.8 | about 46.7 ms |
| 64 KiB | about 21 | about 43.4 ms |

The 64 KiB / 64-byte-line configuration was therefore selected. Cache capacity
reduces internal SRAM available for other purposes, which is one reason bounce
buffer and IRAM experiments must be repeated if the memory layout changes.

## LVGL rendering experiments

### Refresh period

The representative Dashboard compared 15 ms and 10 ms LVGL refresh periods:

| Period | Dashboard result |
| --- | --- |
| 15 ms | about 46-47 FPS, 16-17 ms total refresh |
| 10 ms | about 47 FPS, 16-17 ms total refresh |

The negligible difference showed that the LVGL timer period was not the
bottleneck for that workload. Fifteen milliseconds was retained and a 5 ms
test was not pursued.

### Software draw units

One versus two software draw units interacted with D-cache capacity. With a
32 KiB cache there was almost no improvement. With 64 KiB cache, the demanding
swipe improved from about 21 FPS with one draw unit to about 22.7 FPS with two,
mainly through lower synchronization/flush time. Two draw units are therefore
selected for the current cache configuration. This is workload- and
memory-layout-dependent.

### Neutral or rejected LVGL changes

- Object style caching showed no measurable benefit in the solid-color swipe,
  but remains enabled as an upstream-supported optimization likely to be more
  relevant to a widget-heavy UI.
- Switching LVGL malloc, string, and sprintf handling to the standard C
  library produced no measurable gain and was reverted to LVGL's built-ins.
- Placing LVGL fast functions in IRAM exhausted enough internal memory to make
  RGB bounce-buffer allocation fail. It was reverted; a different internal
  memory layout could change this result.
- Pinning the LVGL task to a core and raising the draw-thread priority produced
  no measurable improvement and were reverted.
- Updating LVGL 9.5 to 9.6 produced neither a measurable gain nor regression
  in the synthetic benchmark. Version 9.6 was retained as the newer component.
- LVGL Sysmon's CPU value read 100% because the required FreeRTOS task/idle
  accounting was not wired into that configuration. The overlay also added
  label/glyph work, so Sysmon/performance-monitor output was removed.
- The LVGL profiler is disabled in the normal config. Temporary profiling
  showed that roughly 86-90% of the demanding swipe refresh was software
  rendering, dominated by repeated RGB565 rectangle blending/filling; normal
  LCD flush was only about 4-5 ms.

Custom `CONFIG_UI_METRICS` diagnostics remain enabled in both committed config
files. They use LVGL display events to log animation callbacks per second, FPS,
render time, and flush time without the Sysmon overlay. The Kconfig option
itself defaults off, but `sdkconfig.defaults` explicitly turns it on. It only
has practical effect when Screen Diagnostics is run.

## RGB bounce-buffer and transfer experiments

Forty lines is the selected bounce-buffer size. A 20-line buffer did not
improve FPS, while a 6-line buffer showed slight corruption/glitches and less
refill margin. Each extra configured line costs 1,920 bytes across the pair
(480 pixels x 2 bytes x two buffers), so 40 lines consumes 76,800 bytes of
internal DMA-capable SRAM in total. It was the tested stability/performance
compromise.

DMA burst size 64 was retained. A 32-byte burst performed essentially the
same. A 128-byte burst failed during RGB panel construction under the tested
ESP-IDF path because GDMA limited external-memory transfers to a maximum burst
of 64 bytes. That is a driver/hardware-path constraint observed with the tested
version and should be rechecked if ESP-IDF changes.

`bb_invalidate_cache=1` came in with the cache/bounce-buffer stability changes.
Current ESP-IDF describes it as invalidating data read during bounce-buffer
refill to free cache space and warns about simultaneous writes from another
core. Its independent effect was not isolated in the recorded A/B results, so
it should be treated as part of the validated combination rather than credited
alone.

### Supported PARTIAL path

A supported non-tearing configuration was tested with:

```text
direct_mode = 0
full_refresh = 0
avoid_tearing = 0
```

It was substantially slower on the demanding red/green swipe:

| Path | FPS | Render | Flush |
| --- | ---: | ---: | ---: |
| Normal two-buffer DIRECT | about 27 | about 29 ms | about 5 ms |
| PARTIAL, no tearing avoidance | about 18 | about 29 ms | about 23 ms |

Rendering itself was unchanged; the regression came from copying/flushing
large dirty areas from separate LVGL draw buffers into the RGB framebuffer.

An internal-SRAM version of that PARTIAL path used two DMA-capable LVGL draw
buffers. Before allocation, about 83,671 bytes of DMA-capable internal RAM were
free and the largest block was 49,152 bytes. Allocation results were:

| Buffer height, each | Allocation | Performance when tested |
| --- | --- | --- |
| 48 lines | Failed | - |
| 40 lines | Failed | - |
| 36 lines | Failed | - |
| 32 lines | Succeeded | about 24 FPS; 29.4 ms render; 8.3 ms flush; 37.8 ms total |
| 24 lines | Succeeded | about 20 FPS; 36.9 ms render; 8.6 ms flush; 45.5 ms total |

The failures were driven by available contiguous internal memory, not by an
LVGL line-count limit. During this test `esp_lvgl_port` 2.9.0 could leak the
first allocation if the second failed, so each failed size was tested after a
reboot. This path did not outperform direct framebuffer rendering. Changes in
application memory use or a newer port allocator could change the result.

## Triple-buffer experiment

The `triple_buffer` branch contains a validated project-local, zero-copy
three-framebuffer implementation. It is not the current `master` baseline.

ESP-IDF supports `num_fbs=3`, but `esp_lvgl_port` 2.9.0 obtains only two RGB
framebuffer addresses. Merely setting `num_fbs=3` through the normal port would
therefore allocate a third framebuffer without proving that LVGL ever rendered
to it. The branch instead:

1. Sets the ESP-IDF RGB driver to three framebuffers and obtains all three
   addresses with `esp_lcd_rgb_panel_get_frame_buffer()`.
2. Wraps all three in public LVGL draw-buffer objects and registers the third
   with `lv_display_set_3rd_draw_buffer()`.
3. Tracks explicit scanout and pending ownership so LVGL never renders into a
   buffer being scanned or awaiting the next scanout.
4. Uses ESP-IDF's `on_frame_buf_complete` callback and a FreeRTOS semaphore to
   recycle a buffer only after safe frame completion.
5. Submits the RGB driver's framebuffer itself; it performs no full-screen
   framebuffer copy.
6. Logs per-buffer render, submit, scanout, completion, and recycle counts.

Runtime counters showed fb0, fb1, and fb2 all rotating through the pipeline.
The implementation used public ESP-IDF and LVGL APIs, but required substantial
project-local ownership code because the port did not expose a native
three-buffer RGB integration.

### FULL versus DIRECT triple buffering

The first true triple-buffer comparison used LVGL FULL render mode. It improved
the pathological 960 x 480 red/green swipe from roughly 27 FPS to 30-31 FPS by
reducing buffer-handoff waiting. On the representative Dashboard it fell to
about 18 FPS, about 50 ms render, and near-zero measured flush because FULL mode
redrew the complete 480 x 480 display on every refresh.

LVGL 9.6 source and tests were then inspected. They supported third-buffer
rotation and dirty-region synchronization in DIRECT mode. The same ownership
pipeline was switched to DIRECT and tested on hardware without visible stale
regions, corruption, flicker, or tearing:

| Configuration | Dashboard FPS | Render | Flush |
| --- | ---: | ---: | ---: |
| Normal two-buffer DIRECT | about 46-49 | about 8-9 ms | about 7-8 ms |
| Triple-buffer DIRECT | about 44-50, typically 46-47 | about 8-10 ms | about 5-7 ms |

Triple buffering reduced some handoff latency but did not deliver a meaningful
general FPS improvement. It also consumes one additional 460,800-byte
framebuffer in PSRAM and adds synchronization/ownership code. The simpler
supported two-buffer implementation therefore remains on `master`, while the
validated experiment is preserved on `triple_buffer`. A future
`esp_lvgl_port` with native three-buffer support could change this tradeoff.

## Diagnostic workloads and how to interpret them

The screen-diagnostics application includes several visual tests. The two most
important performance workloads are deliberately different:

- The 960 x 480 solid red/green sliding track continuously invalidates very
  large areas and stresses RGB565 fill/blend bandwidth. Its roughly 27 FPS
  two-buffer result is intentionally pathological and is useful for controlled
  A/B tests, not as an estimate of normal application speed.
- The 480 x 480 Dashboard uses ordinary labels, buttons, an arc, bar, slider,
  chart, and simultaneous deterministic animations. It is the representative
  general-UI benchmark and creates no object larger than the display.

Typical Dashboard results on the normal two-buffer DIRECT baseline were:

```text
46-49 FPS
100-110 animation callbacks/s
8-9 ms software rendering
7-8 ms flush/wait
15-17 ms total refresh
```

The normal panel timing is about 54 Hz, so this workload ran reasonably
close to the physical refresh rate. Do not compare these results without also
recording active timing mode, cache geometry, PSRAM speed, display mode, and
which diagnostic page was visible.

## Repository defaults versus active local configuration

The intended roles are:

```text
sdkconfig.defaults  reproducible repository defaults
sdkconfig           generated active/local build configuration
```

`main/Kconfig.projbuild` and `sdkconfig.defaults` select Air Quality as the
repository default. A fresh configuration generated from the committed
defaults therefore boots the normal Air Quality application. The current
generated `sdkconfig` selects Screen Diagnostics for local display testing;
this intentional local difference does not change the repository default.
Both files currently enable `CONFIG_UI_METRICS`, although those counters only
have practical effect in Screen Diagnostics.

Several deliberate values are explicit in both files: performance compiler
optimization, Octal PSRAM at 80 MHz with XIP, CPU at 240 MHz, 64 KiB/64-byte
D-cache, 1000 Hz FreeRTOS tick, 128 KiB LVGL heap, 15 ms LVGL refresh, and two
software draw units.

Other tested choices are effective in generated `sdkconfig` but absent from
the minimal `sdkconfig.defaults` because they match the current component
defaults: style cache enabled, Sysmon/profiler disabled, LVGL built-in
malloc/string/sprintf selected, CLIB replacements disabled, and LVGL fast IRAM
placement disabled. That means their desired values may silently change after
a component changes its Kconfig defaults. If preserving those decisions across
upgrades is more important than keeping `sdkconfig.defaults` minimal, they are
candidates to make explicit.

## What to reconsider after an upgrade or hardware change

The following are worthwhile re-tests rather than permanent prohibitions:

- 120 MHz Octal PSRAM if ESP-IDF no longer treats the relevant mode as
  experimental and the hardware passes long reliability testing.
- DIRECT mode with `avoid_tearing=0` after verifying that the installed
  `esp_lvgl_port` no longer waits on a missing semaphore.
- Native three-buffer support if a future port exposes all ESP-IDF RGB
  framebuffers and safe frame-complete ownership.
- Bounce-buffer height after changes to internal-SRAM use, RGB driver behavior,
  cache configuration, or pixel clock.
- Internal-SRAM PARTIAL buffers if substantially more contiguous internal
  memory becomes available or the port fixes allocation cleanup.
- One versus two draw units if cache size, renderer implementation, or UI
  complexity changes.
- A shorter LVGL refresh period only if the measured representative workload
  becomes timer-limited.
- CLIB and style-cache choices after a substantial LVGL/compiler change.
- `LV_ATTRIBUTE_FAST_MEM_USE_IRAM` only after checking the complete internal
  memory budget, especially both RGB bounce buffers.
- Higher panel timings after confirming PSRAM/cache refill margin under the
  most demanding animation, not just a static image.

## Known uncertainties and cleanup candidates

These items should be reviewed separately from this documentation change:

- Remove or reconnect `CONFIG_BOARD_LCD_PCLK_HZ`; it currently has no effect
  because every runtime timing mode overwrites the pixel clock.
- Consider explicitly recording style cache, built-in/CLIB choices, Sysmon,
  profiler, and fast-IRAM choices in `sdkconfig.defaults` so future component
  default changes do not silently reverse tested decisions.
- Confirm whether `CONFIG_LV_BUILD_EXAMPLES=y` in generated `sdkconfig` is
  intentional. It is not set in `sdkconfig.defaults` and no project code uses
  the LVGL examples.
- Clarify or remove `trans_size=64` and the LVGL buffer allocation flags if the
  display remains permanently on the tearing-avoidance path where
  `esp_lvgl_port` 2.9.0 does not use them.
- The independent contribution and concurrency assumptions of
  `bb_invalidate_cache=1` were not isolated; retain it as part of the tested
  combination until a controlled test says otherwise.
- The history does not preserve a clear reason for leaving
  `CONFIG_LCD_RGB_RESTART_IN_VSYNC` disabled after the PSRAM fix.
- Flash DIO/80 MHz and the 1000 Hz FreeRTOS tick are current values, but no
  controlled display-performance rationale for those particular choices was
  found.

## Useful history

These commits provide the shortest path back to the relevant implementation
changes and rationale:

- `81eb2b7` and `34bc6e1`: RGB glitch work and selection of 80 MHz PSRAM
- `699aad5`: LVGL heap increased to 128 KiB after three-chart exhaustion
- `df28bab`, `1393dfe`, `0f423ba`: timing families and naming
- `9dfeb94`: synchronized two-buffer DIRECT baseline
- `174d2b9`: 64-byte cache lines, 40-line bounce buffers, and low-clock timing
- `be33187` and `d8944de`: diagnostics and custom timing metrics
- `ee50715`: 64 KiB D-cache, 15 ms refresh, and two draw units
- `7ffeb90`: LVGL style cache
- `dbff939`: LVGL 9.6 migration
- `1175954` and `9492dc8`: ESP-IDF 6.1 defaults and current config consistency
- `a900b39`: representative Dashboard workload
- `ee7bfa6` on `triple_buffer`: zero-copy three-framebuffer DIRECT pipeline
