# Building Clock host viewer

This SDL2/libpng program renders the same generated worlds and headless
simulation used by firmware. It is a semantic design/debug viewer, not the
ESP32 renderer.

Build and run:

```sh
cmake -S components/building_clock/host -B build/building_clock_viewer
cmake --build build/building_clock_viewer
build/building_clock_viewer/building_clock_viewer --seed 0x6a09e667f3bcc909
```

The default scenario is `14:37 -> 14:38`. Command-line scenarios are `stable`,
`single`, `rollover`, `midnight`, and `unfinished`. The last scenario advances
the target every 60 simulated seconds even while construction is incomplete.
A deterministic screenshot can be made
without opening an interactive window:

```sh
SDL_VIDEODRIVER=dummy build/building_clock_viewer/building_clock_viewer \
    --seed 0x6a09e667f3bcc909 --scenario rollover \
    --duration-ms 1800 --screenshot /tmp/building-clock.png
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
- `4`: repeated minutes starting at `14:37 -> 14:38`
- `M`: advance the target minute immediately, without resetting the scene
- `V`: navigation graph and reservations
- `I`: navigation node IDs
- `L`: semantic depot, staging, and debris locations
- `H`: active hazard areas
- `W`: worker routes
- `D`: digit envelopes and work positions
- `S`: save `building_clock_YYYYMMDD_HHMMSS.png` using local time
- `+`/`-`: simulation speed
- `Esc`: quit

The viewer supplies only deterministic time, clock transitions, and simple
diagnostic jobs. Movement, routing, reservations, hazards, and collapse all
come from the shared Building Clock component.

Rope builders use brown ropes attached to real overhead platforms. A second
worker tends each rope from the roof. Supply workers bring bundles from the
depot to staging or a reachable roof cache. Tender-controlled beam clamps can
shift suspended builders along a real roof without repeated full retrievals.
Couriers can bring two compatible small kits on one trip. A builder may carry
the next dependent kit and continue on the same rope once its supports are
secured. Rope tenders are dispatched concurrently with their builders.
Busy six-worker crews may admit relief through generated spawns, up to the
existing nine-worker limit. Unassigned crew drink, smoke, rest, or watch; these poses are
interruptible when work becomes available. Permanent and temporary ladders are
still used where needed to reach the roof.

Host throughput measurements (fixed six, fixed nine, and six plus relief):

```sh
cmake -S components/building_clock/test -B build/building_clock_tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/building_clock_tests
build/building_clock_tests/building_clock_throughput_tests 32
```

The argument is the number of deterministic world roots (1-128). Each root
runs three single-digit changes, a multi-digit rollover, and midnight. An
optional second argument prints a five-second timeline for the first fixture.
Output includes activity-time percentages, navigation/tender/rope-lane waits,
paired deliveries, continued rope sections, relief use and peak job/load slots.
See `docs/building_clock_throughput.md` for measured limits and remaining issues.
