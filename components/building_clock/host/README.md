# Building Clock host viewer

This SDL2/libpng program renders the same generated worlds and headless
simulation used by firmware. It is a semantic design/debug viewer, not the
ESP32 renderer.

The default view is uncluttered. Workers have hard hats, colored overalls and
discrete walk/climb/tool animations; TAB restores seed, timing and lifecycle
labels. Builders control their own ropes, spare crew walk to quiet
spots on broad platforms, and off-duty crew can shunt empty site trolleys.
Pallets, baskets, bottom-up access erection and bounded site-event feedback
make construction readable without debug text. Cranes and lifts remain parked;
the working machinery in this pass is the rope/winch system and empty trolleys.

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
    --duration-ms 1800 --screenshot build/building-clock.png
```

Run generator-selection diagnostics with:

```sh
build/building_clock_viewer/building_clock_viewer --diagnose 20000
```

Controls:

- `Tab`: status and lifecycle labels (off initially)
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

Rope builders use brown ropes attached to real overhead platforms and control
their own harness ascenders/descenders. Supply workers bring bundles from the
depot to staging or a reachable roof cache. Builder-controlled beam clamps can
shift suspended builders along a real roof without repeated full retrievals.
Couriers can bring two compatible small kits on one trip. A builder may carry
the next dependent kit and continue on the same rope once its supports are
secured. A nearby idle rigger may assist at a winch, but only while a builder
actually ascends/descends; assistance never owns a job or delays a builder.
Dispatch uses actual route duration and nearby returning/unpacking/clearing crew
rather than unnecessary trips across roofs. Permanent ladders
allow same-direction following at 20-unit spacing; opposing traffic queues.
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
Output includes activity-time percentages, navigation/rope-lane waits,
paired deliveries, continued rope sections, relief use and peak job/load slots.
Optional tending is reported separately and excluded from productive counts;
simultaneous masonry work and aggregate stable-within-60s results are included.
See `docs/building_clock_throughput.md` for measured limits and remaining issues.

## Motion review and character checks

Export every frame of a deterministic sequence (the output directory must
already exist). Frame spacing must be a positive multiple of 50 ms. Exporting
frames and pausing do not advance animation independently of simulation time.

```sh
mkdir -p build/builders_sequence
SDL_VIDEODRIVER=dummy build/building_clock_viewer/building_clock_viewer \
    --seed 0x6a09e667f3bcc909 --scenario single --duration-ms 80000 \
    --frame-ms 100 --sequence build/builders_sequence
ffmpeg -framerate 10 -i build/builders_sequence/%06d.png \
    -vf scale=960:960:flags=neighbor -c:v libx264 -crf 16 -pix_fmt yuv420p \
    build/builders_sequence.mp4
ctest --test-dir build/building_clock_viewer --output-on-failure
SDL_VIDEODRIVER=dummy build/building_clock_viewer/building_clock_viewer \
    --animation-sheet build/builders_characters.png
```

The character check compares software-rendered frame hashes for walk, climb,
carry, hammer, winch, shovel, inspection and unpacking poses. Its sheet is a
synthetic pose review, not a simulated gameplay sequence. See
`docs/building_clock_visual_overhaul.md` for captures, tradeoffs and remaining
limitations. `--animation-check` runs the same checks without writing a sheet.

Detailed read-only construction traces, using the same host test build:

```sh
build/building_clock_tests/building_clock_bottleneck_trace single build/builders-single
build/building_clock_tests/building_clock_bottleneck_trace rollover build/builders-rollover
```

These write worker samples, section changes and job changes to three CSV files
per prefix, and print milestone/wait summaries to stdout. A final optional root
seed overrides `0x6a09e667f3bcc909`. Diagnostics verify byte-identical replay
against an unsampled control. See `docs/building_clock_bottlenecks.md` for the
investigation, timing definitions, code references and remaining uncertainties.
