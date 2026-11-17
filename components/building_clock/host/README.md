# Building Clock host viewer

This SDL2/libpng program renders the same generated worlds and headless
simulation used by firmware. It is a semantic design/debug viewer, not the
ESP32 renderer.

The default view is uncluttered. Workers have hard hats, colored overalls and
discrete walk/climb/tool animations; TAB restores seed, timing and lifecycle
labels. Builders control their own ropes, spare crew walk to quiet
spots on broad platforms, and crews bag, carry and recover actual rubble using
loaded site trolleys. Stable-clock recovery jobs fetch equipment through the
normal navigation graph and yield safely to useful construction.
Pallets, baskets, bottom-up access erection and bounded site-event feedback
make construction readable without debug text. A cyan-blue crane now carries
real kit pallets when route/crew estimates favor it over manual delivery. Its
loader operates the pendant, cargo lands without a receiving builder, and cancelled
flights return cargo safely. Lifts remain parked; independent builder ropes
and loaded recovery trolleys remain functional.
Quiet crews now fetch shared maintenance kits, sweep actual girder patches,
tighten joints, check tools/beams, meet over a drawing and occasionally take tea.
These are shared simulation activities, not injected viewer jobs. Tools remain
where interrupted workers put them down, and construction/recovery takes priority.

Build and run:

```sh
cmake -S components/building_clock/host -B build/building_clock_viewer
cmake --build build/building_clock_viewer
build/building_clock_viewer/building_clock_viewer --seed 0x6a09e667f3bcc909
```

For a version-6 world where `7 -> 8` uses the crane, run seed `0xf9fd2ded6d5b25c6`.
Add `--no-crane` for identical-world manual delivery. A diagnostic
`--interrupt-at-ms 9000` advances the target once during that seed's loaded
flight; it uses the normal simulation time-input API. Values must be multiples
of 50 ms. `R` repeats the same input sequence, including the interruption.

The cable stays yellow in every phase. After offload it automatically retracts
to the jib and returns horizontally to the loading position, with no crew claim.
Parking there prevents the empty return cost from starving later clock minutes.
Permanent girders are horizontal; vertical access is shown as yellow ladders.
The generator/fallback are version 6, so older seeds have different layouts.

The default scenario is `14:37 -> 14:38`. Command-line scenarios are `stable`,
`single`, `rollover`, `midnight`, and `continuous` (`unfinished` is an alias). The last scenario advances
the target every 60 simulated seconds even while construction is incomplete.
Use `--help` or `-h` for all options and interactive controls. Mode `4` selects
continuous time interactively; `M` advances the target minute immediately.

```sh
build/building_clock_viewer/building_clock_viewer --seed 0xf9fd2ded6d5b25c6 --scenario continuous
```
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

The viewer supplies only deterministic time and clock transitions.
Movement, routing, upkeep, reservations, hazards, and collapse all
come from the shared Building Clock component.

Trace quiet activity without PNG compression/display (CSV every 250 ms):

```sh
cmake --build build/building_clock_viewer --target building_clock_living_trace
build/building_clock_viewer/building_clock_living_trace \
    0x6a09e667f3bcc909 stable 180000 build/living-workers.csv
```

CSV columns are elapsed milliseconds, worker ID, semantic x/y, worker state,
job slot (255 means none), upkeep kind, phase and partner (255 means none).
Kinds/phases are the `building_clock_upkeep_*` enums in the semantic model.
The summary counts explicit jobs/rounds, movement, close pairs and stationary
close pairs. It does not treat a tea round as construction productivity.

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

Collapse-only CPU and drawing benchmarks (host measurements, not ESP32 FPS):

```sh
cmake --build build/building_clock_tests --target building_clock_collapse_benchmark
build/building_clock_tests/building_clock_collapse_benchmark
cmake --build build/building_clock_viewer --target building_clock_render_benchmark
SDL_VIDEODRIVER=dummy build/building_clock_viewer/building_clock_render_benchmark
```

Both optional targets use `-O2` and print three process-CPU-time repetitions.
The geometry benchmark samples all numeral/section fixtures, including partial
construction; the draw benchmark saturates falling/pile capacities on an SDL
software canvas. They do not save PNGs or time file compression. The default
10,000 geometry rounds / 5,000 draw frames can be overridden by one argument.
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

Smoking now has two hand-to-mouth draws, a red ember and six rising/fading smoke
puffs. Bottle breaks raise and tilt the bottle, tip the head back, show a short
pour, then throw the bottle over the shoulder. Normal-size arms gesture without
stretching to meet the bottle. Its body retains its thickness through eight
rotation poses. The bottle and tip are dark green, the label blue and the four
moving liquid pixels red. It
follows a gravity arc, smashes on the first platform it reaches (possibly a
lower one), and separates into five pixels which roll four to eight units in
the throw direction before disappearing after 750 ms.
The shared tea activity still uses a reusable cup, not a discarded bottle.
Useful work can interrupt any pose; an already released bottle finishes its
fall/ground roll independently. The simulation records one fixed 16-byte
release origin/time/direction per worker (144 bytes total), with no particle
objects or physics updates. Replay/reset clears that history.

Native 480x480 synthetic sequences and a draw-cost measurement can be reproduced
with the test executable (the four-worker fixture includes both facing directions
and a bottle falling from an edge to a lower platform):

```sh
mkdir -p build/builders_breaks
SDL_VIDEODRIVER=dummy build/building_clock_viewer/building_clock_break_animation_tests build/builders_breaks
SDL_VIDEODRIVER=dummy build/building_clock_viewer/building_clock_break_animation_tests --benchmark
```

It exports one PNG every 50 ms across a complete four-second gesture. With no
argument it runs timing, landing, deterministic rendering, non-mutation and
useful-work interruption checks, also included in the viewer's CTest suite.

Detailed read-only construction traces, using the same host test build:

```sh
build/building_clock_tests/building_clock_bottleneck_trace single build/builders-single
build/building_clock_tests/building_clock_bottleneck_trace rollover build/builders-rollover
```

These write worker samples, section changes and job changes to three CSV files
per prefix plus a site/cargo CSV, and print milestone/wait summaries to stdout. A final optional root
seed overrides `0x6a09e667f3bcc909`. Diagnostics verify byte-identical replay
against an unsampled control. See
[the current throughput report](../../../docs/building_clock_throughput.md)
for timing definitions, diagnostic limits and remaining bottlenecks.

`building_clock_site_tests` checks whole-sequence sack accounting, loaded
trolley deliveries, separate work spots, cancellation and safe recovery-worker
recall during a generated-world ladder traversal. Recovered loose masonry is
recorded in depot inventory; conversion into replacement kits is not implemented.
