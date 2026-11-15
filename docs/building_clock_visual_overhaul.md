# Clock - Builders visual/gameplay status

Current implementation: `035220d` on `buildclock`; reviewed 2026-11-15.
The character/site overhaul was committed in `4ccb755`; independent ropes,
logistics and crane/date color corrections followed in `035220d`. This file
describes the current viewer, not the superseded mandatory-tender model.
See [the specification](building_clock_simulation.md) for architecture/design
and [throughput](building_clock_throughput.md) for measurements and diagnostics.
Hardware/display configuration, generator version 5, clock scale, traversal
speeds and construction action durations are unchanged by these refinements.

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
- Builders independently control the roof clamp and harness ascender/descender;
  ropes terminate at their harness. A nearby otherwise idle rigger may help at
  a hand-cranked drum, but never owns a prerequisite job or delays a builder.
  Cranking is enabled only by actual ascent/descent displacement in the latest
  simulation quantum, not stationary work, preparation, waiting or shifting.

The uncluttered game scene is now the default; TAB toggles diagnostic status
and lifecycle text. Existing navigation, reservation, hazard, route and location
overlays remain available. The date uses the coarse `DAY DD MON YYYY` format:
weekday/month are warm masonry orange, day/year cool steel blue. The fixed
`FRI 26 OCT 1984` is a viewer demo input, not live date/calendar integration.
The colon currently remains steady.

## Actual coordination and equipment behavior

The shared simulation now supports small local activities on broad continuous
platforms. This is not a second navigation graph: the platform must actually
contain the movement. Quiet positions are chosen deterministically with
22-unit clearance from colleagues and clearance from ladder entrances. They
are limited to 100 units from the navigation anchor on platforms at least 120
units long. No narrow ledge, ladder or abstract temporary connection is used
as a free-space walking area.

Spare workers walk away from a crowded cache before resting. Optional tending
uses an already nearby idle worker on the real roof, creates no new trip or
reservation, and immediately yields to useful work. Mandatory winch-station
excursions and the old half-crew builder cap no longer exist.
Workers on break/trolley excursions physically return to their graph anchor
before beginning another route. They retain broad-area occupancy during the
excursion, and cancellation
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
has a trussed tower/jib and cab, now dark purple with deeper purple shadows
rather than yellow, so pale workers/hard hats remain readable against it.
Ladders remain yellow/cream. Its short idle jib is folded/parked; this is
scenery/layout preparation, not crane delivery or lift behavior. Functional
equipment is limited to independent ropes, optional idle winching, temporary
access and empty-trolley shunting.

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

Artifacts are repository-local ignored build output, not committed assets.
A build cleanup may remove them. Current motion review is under
`build/builders_independent_ropes/`:

- `single.mp4`: complete 80-second single-digit sequence.
- `rollover.mp4`: 140 seconds of `14:59 -> 15:00`.
- `midnight.mp4`: 180 seconds of collapse/reconstruction.
- `single/`, `rollover/`, `midnight/`, `unfinished/`: full PNG sequences;
  100 ms per single frame, 200 ms for the others.
- `*-storyboard.png` and `descent-placement-strip.png`: ordered sequence and
  consecutive-frame review, including four other topology families.
- `before-after-50s.png`: committed visual baseline versus independent ropes.
- `date-colors-50s.png`: latest color state, including the dark purple crane
  and alternating orange/blue date. Earlier motion movies/previews precede
  these final color-only corrections; do not use their yellow crane or all-
  orange date as the current intended palette.

Historical `4ccb755` visual-pass artifacts are under `build/builders_visual/`:

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
roles instead of only identical stationary pale figures. Independent ropes
remove dedicated roof partners; helpers no longer crank without vertical
motion. The darker crane improves worker contrast without recoloring ladders.
The trolley provides
a readable activity on the lower route. Construction is more intelligible with
labels disabled, especially when watching a placement hand meet its joint.

Remaining weaknesses: queues still bunch during cache handoffs; resting and
working crew can pass through one another on broad platforms (there is no
continuous collision system). Actual masonry work still peaks at two builders
for the measured single-digit scenarios, although logistics can use more crew.
The parked crane/lift are not yet active
material machinery. Rubble clearing/recovery remains section-scale rather than
a complete visible transport-to-bin lifecycle. These are next refinements,
not features hidden behind decorative animation.

## Current throughput and resources

The 32-root/five-scenario/three-policy benchmark completes all 480 runs without
timeouts or evacuation failures. With six plus relief, single-digit means are
51.10/47.14/50.01 seconds; rollover/midnight means are 92.58/134.02 seconds.
Single-digit fully stable within 60 seconds is **93/96 (96.9%)**; fixed six
achieves the same rate. The original character pass briefly regressed this
metric to 81.3%; it is a historical comparison, not the current result.
No worker/action speed was increased. Optional tending is excluded from useful
contributor counts; old mandatory-tender-inclusive counts are not equivalent.
Use the throughput report for distributions, definitions and remaining stalls.

Host ABI sizes remain worker 300, mutable state 9,488, context 10,344 bytes;
jobs are 64 and loads 32 bytes. The initial visual pass added 336 context bytes
for worker activities and bounded event history; independent ropes and the
latest palette fixes add no further storage. No simulation heap allocation or
capacity increase. The SDL viewer does not implement the firmware renderer;
no ESP32 rendering frame-rate result is claimed.

## Recorded verification and next review

During implementation, the thirteen host suites and SDL character-animation
test passed, as did all thirteen suites under ASan/UBSan with leak detection
disabled for this environment. The final crane/date edits also passed the
viewer build/animation check and screenshot inspection. These are recorded
results, not tests rerun for a docs-only update. Checks
cover real platform separation, graph-anchor ownership during local movement,
cart push/preemption/position retention, deterministic replay, partial access
erection, event subjects/timestamps and read-only capture. The renderer test
checks that movement/tool animations change software-rendered frame hashes;
it does not replace human visual evaluation. Existing unfinished-rollover and
long-run reconstruction tests remain in place.

Thirty consecutive completed minute changes average 39.49 seconds, maximum
53.25. Twelve scheduled 60-second updates include one unfinished rollover and
finish with no transition-resource leaks. Coverage includes solo rope work,
optional-tender motion/preemption, local station continuity and cancellation.

ESP-IDF builds use the installed 6.1 toolchain, existing managed components and
the repository-local offline dependency adapter from the preceding pass.
Component-manager writes outside the repository are disabled. No dependency,
LCD, LVGL, cache, PSRAM or scheduling configuration was changed, and no flashing
was performed. Strict C11/conversion checks pass for host-compatible runtime
and the new diagnostic tests; broader pre-existing extra-strict viewer/test
warnings are documented in the throughput report. Formatting, ASCII and
whitespace checks pass for changed sources.

Next interactive review should judge the exported sequences at normal speed,
especially cache handoffs and the distinction between placing and securing.
Remaining possible refinements are local supply/work-front placement and useful
material-cart work, not faster workers or decorative machinery presented as
functional. These are observations for review, not an authorization to expand
the feature set. Build/run commands and controls live in
[the host README](../components/building_clock/host/README.md).
