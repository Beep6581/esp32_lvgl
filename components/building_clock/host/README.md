# Building Clock host viewer

This SDL2 program renders the same generated worlds and headless simulation
used by firmware. It is a semantic design/debug viewer, not the ESP32 renderer.

Build and run:

```sh
cmake -S components/building_clock/host -B build/building_clock_viewer
cmake --build build/building_clock_viewer
build/building_clock_viewer/building_clock_viewer --seed 0x6a09e667f3bcc909
```

The default scenario is `14:37 -> 14:38`. Command-line scenarios are `stable`,
`single`, `rollover`, and `midnight`. A deterministic screenshot can be made
without opening an interactive window:

```sh
SDL_VIDEODRIVER=dummy build/building_clock_viewer/building_clock_viewer \
    --seed 0x6a09e667f3bcc909 --scenario rollover \
    --duration-ms 1800 --screenshot /tmp/building-clock.bmp
```

Run generator-selection diagnostics with:

```sh
build/building_clock_viewer/building_clock_viewer --diagnose 20000
```

Controls:

- `Space`: run/pause
- `.` or right arrow: advance 50 ms
- `R`: restart an identical replay
- `N`: select the next deterministic root seed
- `0`: stable `14:37`
- `1`: `14:37 -> 14:38`
- `2`: `14:59 -> 15:00`
- `3`: `23:59 -> 00:00`
- `V`: navigation graph and reservations
- `I`: navigation node IDs
- `L`: semantic depot, staging, and debris locations
- `H`: active hazard areas
- `W`: worker routes
- `D`: digit envelopes and work positions
- `S`: save `building_clock_view.bmp`
- `+`/`-`: simulation speed
- `Esc`: quit

The viewer supplies only deterministic time, clock transitions, and simple
diagnostic jobs. Movement, routing, reservations, hazards, and collapse all
come from the shared Building Clock component.
