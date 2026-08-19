# AGENTS.md

## Project purpose

This is an ESP32-S3 air-quality/display project using ESP-IDF components and LVGL.
Treat the current working hardware behavior as the baseline and make changes conservatively.

The code should stay easy for a human to read and learn C from. Prefer the smallest clear solution over a clever or highly abstract one.

## Toolchain and language

- C only. Do not introduce C++.
- ESP-IDF target: v6.0 for now.
- LVGL target: v9.5 for now.
- esp_lvgl_port: 2.7.2.
- esp_lcd_gc9503: 3.0.1.
- ESP32-S3 target.
- Keep the ESP-IDF component-style project structure.
- Use only APIs that exist in the project's installed versions.
- Before claiming an API/module/function is correct, verify it against the installed headers or current official documentation. Do not guess API names from older ESP-IDF/LVGL versions.

## Architecture boundaries

Keep these responsibilities separate:

- `main`: startup/wiring only. Keep `app_main()` thin.
- `display`: LCD/panel/LVGL display-port initialization and display hardware configuration.
- `touch`: touch-controller integration.
- `i2c_bus`: ownership and setup of the shared ESP-IDF I2C master bus.
- device components such as `scd4x` and `sht20`: device/protocol-specific communication.
- `air_quality`: sensor orchestration and the application-facing air-quality data model.
- `ui`: LVGL widgets, interaction, presentation, and plotting. UI code must not contain I2C transactions or sensor protocol logic.

Do not move sensor/I2C logic into the UI to make a feature easier.
Do not expose sensor register/protocol details to the UI.

## Known-good display configuration

The RGB display is currently stable. Do not change these settings unless the task specifically requires display work and there is evidence the change is needed:

- PSRAM speed: 80 MHz.
- LCD pixel clock: 16 MHz.
- RGB framebuffers: 2.
- RGB bounce buffer: enabled, currently `BOARD_LCD_HRES * 30` pixels.
- `bb_mode = 1`.
- `avoid_tearing = 0`.
- `full_refresh = 0`.
- `direct_mode = 0`.

The previous periodic RGB screen-shift/glitch was fixed by changing PSRAM from 40 MHz to 80 MHz. Do not undo this.

## I2C and sensor behavior

Known addresses:

- FT6336U touch: `0x38`.
- SHT20: `0x40`.
- SCD41: `0x62`.
- STCC4: `0x64` if legacy support is still present in the current revision.

Rules:

- The project does not support I2C hot-plugging.
- Probe supported sensor addresses once at startup and cache presence.
- Do not repeatedly probe absent devices during normal operation.
- Do not add a continuous/general address scan to the normal runtime path.
- SCD41 I2C speed is 400 kHz and is known to work.
- SCD41 normal periodic measurement mode produces a new sample about every 5 seconds.
- Use `get_data_ready_status`/the existing data-ready logic rather than blindly reading an empty measurement buffer.
- Preserve the existing tolerance for isolated SCD41 I2C failures; do not reset/reinitialize the sensor after one transient failure.

The previous long SCD41 readout gaps were caused by runtime probing of an absent sensor address interacting badly with the I2C path. Startup-only probing fixed the problem.

STCC4 is legacy. Do not add new STCC4 functionality or new dependencies on it. It may be removed as part of current work.

Future external sensor support may use Modbus. Do not implement Modbus unless the task explicitly asks for it, but avoid UI/data-model choices that unnecessarily assume all sources are I2C devices.

## UI and data-model direction

Keep UI concepts separate from device names/protocols where practical.

The intended long-term application is sensor comparison:

- SCD41 provides temperature, RH, and CO2.
- SHT20 provides temperature and RH.
- A future external source may provide some or all of the same metrics.

When refactoring toward multi-sensor support, prefer a small fixed C model using:

- enums for source and metric identifiers,
- fixed-size arrays,
- explicit validity/capability flags,
- timestamps / last-update information where useful.

Avoid inventing a generic sensor framework, virtual interfaces, function-pointer tables, generic containers, or plugin architecture for a three-source/three-metric problem unless there is a demonstrated need.

Keep stored/history data independent of LVGL widget pointers so later logging/export can reuse it. Missing data must be distinguishable from a numeric zero.

## C style

Optimize for clarity and learnability:

- Prefer explicit structs, enums, arrays, loops, and small helper functions.
- Prefer fixed-size data structures when the maximum size is small and known.
- Prefer straightforward control flow over abstraction for its own sake.
- Use meaningful names.
- Keep functions reasonably small when extracting an obvious helper improves readability.
- Avoid unnecessary dynamic allocation.
- Avoid unnecessary macros.
- Avoid function pointers/callback architectures unless required by an external API or clearly justified.
- Avoid opaque context structures that only hide simple state.
- Comments should explain why something is done, especially hardware/protocol constraints, rather than narrate obvious C syntax.
- Code and comments must use plain ASCII only.

## Change discipline

Before editing:

1. Inspect the relevant files and call sites first.
2. Understand the current data flow and ownership boundaries.
3. State any important uncertainty instead of guessing.

While editing:

- Make the smallest coherent change that solves the requested problem.
- Do not perform unrelated cleanup, renaming, formatting, or refactoring.
- Preserve known-good display, I2C, sensor, and touch behavior unless the task explicitly targets it.
- Do not recreate whole files when a small edit is sufficient.
- Do not modify vendored code unless there is no cleaner project-side solution and the task explicitly requires it, in which case you must state this clearly.
- Do not commit changes; the user reviews and commits manually unless explicitly asked otherwise.

For small requested changes, report them in a focused way. When explaining manual edits, prefer:

`Replace this ... with this ...`

rather than reproducing whole files.

## Verification and truthfulness

- Do not claim a build, test, runtime result, API behavior, or hardware behavior that was not actually verified.
- If something is uncertain, say so.
- Prefer primary/authoritative sources: installed headers, ESP-IDF/LVGL/Sensirion documentation, datasheets, and observed logs/measurements.
- Do not rely on retailer pages, blog posts, or forum claims when authoritative documentation is available.
- Seek evidence that could contradict the current hypothesis when debugging.
- Distinguish clearly between an observed fact, a documented fact, and an inference.

## Build and review workflow

After a meaningful change, if the ESP-IDF environment is available:

1. Run `idf.py build`.
2. Fix build errors caused by the change.
3. Inspect warnings relevant to the changed code.
4. Review `git diff` for accidental unrelated changes.
5. Report the build result and any hardware behavior that still needs real-device testing.

Do not use `idf.py fullclean` routinely; use it only when there is a reason to invalidate the build tree.
Do not change `sdkconfig`/`sdkconfig.defaults` casually. Preserve reproducible configuration changes in the appropriate project configuration mechanism.

## Repository hygiene

- Keep component dependencies accurate in each `CMakeLists.txt`.
- Remove dead code when it becomes dead because of the requested change, but do not roam through unrelated components looking for cleanup opportunities.
- Keep vendored third-party code separate from project-owned wrappers/adapters.
- Do not add generated build output to version control.
