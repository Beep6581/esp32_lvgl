# Clock - Builders visual/gameplay pass

2026-11-15, uncommitted on `buildclock`. This builds on the preceding uncommitted
throughput changes. Hardware/display configuration, generator version 5, clock
scale, traversal speeds and construction action durations are unchanged.

## What is different

Workers are now little hard-hat characters rather than pale markers: three
overall colors, a white-hatted foreman, shaded faces, noses, occasional
moustaches, braces, pockets, gloves and boots. Discrete animation is driven by
semantic simulation time and actual movement, never SDL wall time or random
calls. Pausing freezes poses as well as the world.

- Walks have alternating legs, boots and swinging arms. Climbing alternates
  hands and feet with ladder/rope displacement, so a blocked climber does not
  continue running in place.
- Carriers hold brick baskets; climbers carry them at their back. This avoids
  the old giant brick bundle covering the carrier's head and body.
- Placement reaches from the basket to the actual masonry joint with a held
  brick and trowel. The simulation's brick-seated milestone then makes that
  masonry part of the structure. Securing has a separate overhead hammer swing.
- Unpacking bends the worker over the basket. Clearing uses a swinging shovel;
  inspectors hold a clipboard. Waiting has a look/tap pose, distinct from
  sitting, drinking from a mug, smoking, watching and briefly celebrating a
  completed digit.
- Tenders visibly operate a hand-cranked drum connected to the builder's real
  moving beam clamp/sheave. Ropes terminate at the harness. Anticipatory tenders
  have visible equipment even before their builder attaches a rope.

The uncluttered game scene is now the default; TAB toggles diagnostic status
and lifecycle text. Existing navigation, reservation, hazard, route and location
overlays remain available. The date uses a coarse `FRI 26 OCT 1984` treatment.

## Actual coordination and equipment behavior

The shared simulation now supports small local activities on broad continuous
platforms. This is not a second navigation graph: the platform must actually
contain the movement. Quiet/winch positions are chosen deterministically with
22-unit clearance from colleagues and clearance from ladder entrances. They
are limited to 100 units from the navigation anchor on platforms at least 120
units long. No narrow ledge, ladder or abstract temporary connection is used
as a free-space walking area.

Spare workers walk away from a crowded cache before resting. Active tenders
walk to separate winch stations; resting workers yield those spots to riggers.
Workers physically return to their graph anchor before beginning another
route. They retain broad-area occupancy during the excursion, and cancellation
does not snap them back. Local return travel is included in evacuation lead
estimates; actual worker safety still gates collapse.

Off-duty workers can shunt an EMPTY trolley along its generated flat cart
route. They walk to its handle, push at the existing walk speed, and leave it
at a service endpoint. If useful work interrupts the push, the cart remains
at its actual intermediate progress with no owner; the worker walks back to
the navigation anchor. This is a small functional equipment task, not an
automatic delivery system. Trolleys do not create or transport construction
loads in this pass. It only starts outside active digit transitions.

The stable viewer scenario now supplies two low-rate inspection/maintenance
jobs at real site locations instead of an eighteen-job patrol queue. Shunting,
off-duty behavior and all actual job execution remain shared simulation code.
Those two maintenance inputs are viewer demo wiring, not a new firmware
ambient scheduler.

## Site and construction storytelling

Depots are little stocked racks, staging areas have timber skids, and debris
service bays are open bins rather than filled UI rectangles. The parked crane
has a trussed tower/jib and cab. Its short idle jib is folded/parked; this is
not a claim that crane delivery or lift behavior was implemented. Winches and
empty-trolley shunting are the functional machinery in this pass.

Temporary ladder/scaffold extent follows actual installation-job progress,
growing from the lower support. Predefined navigation activates only after
installation completes. The old full-height visual appeared even while its
job was pending. Scaffold assembly remains a compressed, section-scale action,
not a simulation of erecting every individual pole.

Falling masonry tumbles in coarse integer 45-degree steps around its existing
gravity-driven semantic trajectory; the old unexplained yellow slash is gone.
Settled rubble is drawn as small coarse piles. A fixed sixteen-entry semantic
milestone ring records brick seating, landing, clearing and digit completion.
The renderer derives brief dust/chip feedback from that history. These are
read-only effects, not particle entities, renderer callbacks or extra physics.
Old events can be overwritten without affecting simulation correctness.

## Inspection and artifacts

All files below are repository-local, ignored development artifacts under
`build/builders_visual/`. They are intentionally not committed, and a build
cleanup may remove them.

- `before/`: captures from the executable saved before this pass, same root,
  single/rollover/midnight at 1.9/10/30/50/70 seconds.
- `review/characters.png`: synthetic eight-pose character sheet; not gameplay.
- `review/single.mp4`: complete 80-second single-digit sequence.
- `review/rollover.mp4`: 150 seconds of `14:59 -> 15:00`.
- `review/midnight.mp4`: 280 seconds of midnight collapse and reconstruction.
- `review/unfinished.mp4`: 180 seconds with new targets every 60 seconds.
- `review/stable.mp4`: trolley shunting, maintenance and off-duty activity.
- Each movie has its source PNG sequence at 100 ms intervals. Movies use
  nearest-neighbor scaling, no interpolation or smoothing.
- `review/before-after.png`: the same seed at 50 simulated seconds before/after.
- `review/seed-*-storyboard.png`: complete single-digit sequences from the four
  other topology families (source frames at 200 ms intervals).

The inspection method is ordered full-transition storyboards plus consecutive
frame strips for climbing, carrying, winching, placement and collapse. The
environment does not provide video playback to the agent; movies are exported
for interactive evaluation, not claimed as watched real-time playback.

Observed improvements: hard hats/overalls remain recognizable against masonry;
hands and feet change on ladders; baskets stay with carriers; ropes and winch
drums make support activity legible. The upper work front contains distinct
roles instead of only identical stationary pale figures. The trolley provides
a readable activity on the lower route. Construction is more intelligible with
labels disabled, especially when watching a placement hand meet its joint.

Remaining weaknesses: queues still bunch during cache handoffs; resting and
working crew can pass through one another on broad platforms (there is no
continuous collision system). Two rope-building fronts and separate tenders
still dominate a single-digit build. The parked crane/lift are not yet active
material machinery. Rubble clearing/recovery remains section-scale rather than
a complete visible transport-to-bin lifecycle. These are next refinements,
not features hidden behind decorative animation.

## Throughput and resource tradeoff

Same 32 root inputs, three single-digit scenarios, rollover and midnight, and
three crew policies: 480 completed runs, no timeouts or evacuation failures.
The generator/accepted-world distribution is unchanged from the preceding pass.
Below is adaptive six plus relief, in simulated seconds.

| Transition | Before mean | After mean | After p90 | After maximum |
| --- | ---: | ---: | ---: | ---: |
| 7 -> 8 | 53.24 | 54.83 | 61.00 | 73.30 |
| 1 -> 2 | 52.01 | 52.31 | 60.65 | 66.65 |
| 8 -> 9 | 51.86 | 53.89 | 63.35 | 72.80 |
| Rollover | 118.70 | 120.64 | 132.90 | 145.90 |
| Midnight | 184.33 | 188.24 | 213.90 | 261.65 |

Single-digit <=60-second counts change from 27/28/28 to 27/27/24:
**83/96 (86.5%) -> 78/96 (81.3%)**. This is an explicit visual/physical setup
tradeoff, not a throughput improvement. Means regress about 0.6-3.9%; workers
were not accelerated to conceal actual return/setup travel. Average active
contributors are approximately 4.67/3.77/4.51 for the single-digit scenarios,
6.81 for rollover and 6.88 at midnight. Idle strolling is not counted as
productive construction time by the host diagnostic.

Host ABI sizes: worker 292 -> 300 bytes; mutable state 9,152 -> 9,488;
context 10,008 -> 10,344. The 336-byte context increase is 72 bytes for nine
workers plus the bounded milestone history/index. Jobs remain 64 bytes and
loads 32 bytes. No simulation heap allocation; existing entity/navigation limits
are unchanged. SDL drawing cost increased, but hardware rendering is not wired
to this viewer and no ESP32 frame-rate result is claimed.

## Verification and next review

The thirteen host suites and the SDL character-animation test pass. New checks
cover real platform separation, graph-anchor ownership during local movement,
cart push/preemption/position retention, deterministic replay, partial access
erection, event subjects/timestamps and read-only capture. The renderer test
checks that movement/tool animations change software-rendered frame hashes;
it does not replace human visual evaluation. Existing unfinished-rollover and
long-run reconstruction tests remain in place.

Thirty consecutive completed minute changes average 42.27 seconds (previously
42.01), maximum 68.80 (previously 66.60). Twelve scheduled 60-second updates
still include three unfinished rollovers and finish with no transition-resource
leaks. All thirteen suites also pass ASan/UBSan with leak detection disabled
for this environment. A new occupancy assertion caught and fixed an early
anchor-release bug when reclaiming a trolley operator before handoff.

ESP-IDF builds use the installed 6.1 toolchain, existing managed components and
the repository-local offline dependency adapter from the preceding pass.
Component-manager writes outside the repository are disabled. No dependency,
LCD, LVGL, cache, PSRAM or scheduling configuration was changed, and no flashing
was performed. Strict C11/conversion, formatting, ASCII and whitespace checks
are part of verification.

Next interactive review should judge the exported sequences at normal speed,
especially cache handoffs and the distinction between placing and securing.
Then prefer better local supply/work-front placement and useful material-cart
jobs over additional decorative props or faster workers.
