# Clock - Builders visual/gameplay status

Current implementation: uncommitted site-interaction pass after `80e70cb` on
`buildclock`; reviewed 2026-11-15.
The character/site overhaul was committed in `4ccb755`; independent ropes,
logistics and crane/date color corrections followed in `035220d`. This file
describes the current viewer, not the superseded mandatory-tender model.
See [the specification](building_clock_simulation.md) for architecture/design
and [throughput](building_clock_throughput.md) for measurements and diagnostics.
Hardware/display configuration, generator version 5, clock scale, traversal
speeds and construction action durations are unchanged by these refinements.

## What is different

The latest pass connects visible rubble clearing to actual site logistics:
workers bag and carry rubble, drop sacks into generated bays, hand-haul them
onto trolleys and push loaded carts to recovery chutes. Stable crews descend
to fetch the equipment instead of leaving useful ground work indefinitely.
No recovery cargo or inventory is invented for animation. Unpacking and kit
pickup use separate real platform positions, clear of ladder mouths; builders
can rig from a compatible roof pickup instead of retracing the graph anchor.
Opposing walkers briefly turn shoulders and use shallow passing depth lanes.
This improves readability, not general collision avoidance.

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

Trolleys now transport actual recovery sacks along generated flat routes.
Operators load up to four using a visible hand line, walk to the correct
handle side, push at unchanged walk speed, and unload at the depot-side chute.
Preemption preserves intermediate progress and cargo; individual 900 ms sack
transfers finish atomically. Stable-clock dispatch fetches equipment through
real routes, and a minute change recalls workers after their current traversal.
Structural-kit deliveries and worker rides are still deferred.

The stable viewer scenario now supplies two low-rate inspection/maintenance
jobs at real site locations instead of an eighteen-job patrol queue. Recovery,
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
access and loaded rubble recovery.

Temporary ladder/scaffold extent follows actual installation-job progress,
growing from the lower support. Predefined navigation activates only after
installation completes. The old full-height visual appeared even while its
job was pending. Scaffold assembly remains a compressed, section-scale action,
not a simulation of erecting every individual pole.

Falling masonry now separates brick by brick: an initial failure spreads to
nearby bricks while the unreached masonry stays in place. Each brick falls
under integer gravity and remains at its landing position until bagging. The
old rigid-section 45-degree rotation is removed. The crane is lighter muted
blue-purple, nearer the platform palette but still contrasting with hard hats.
Section groups remain internal cleanup units, not giant falling objects.
A fixed sixteen-entry semantic
milestone ring records brick seating, landing, clearing and digit completion.
The renderer derives brief dust/chip feedback from that history. These are
read-only effects, not particle entities, renderer callbacks or extra physics.
Old events can be overwritten without affecting simulation correctness.

## Inspection and artifacts

The latest collapse review is in ignored `build/builders_brick_collapse/`:
`brick-wave-strip.png` shows consecutive 50 ms samples, `single-contact.png`
and `midnight-contact.png` show failure through settlement, and
`single-collapse.mp4` exports the six-second sequence. Single, rollover and
midnight sequences were inspected as ordered frames; no real-time playback
is claimed. The `before/` sequence preserves the prior rigid-section behavior.

Latest artifacts are in ignored `build/builders_site/`: `before-after-20s.png`,
`recovery-and-build.png`, `families-30s.png`, `after-rebuild-recovery.png`,
`bagging-carry-tip-strip.png` and `trolley-loading-strip.png`. The strips cover
consecutive 200 ms samples, including the same sacks leaving the worker,
landing in the bay and being lifted onto the trolley. Complete single,
rollover, midnight, stable and unfinished-minute PNG sequences are under
`final/`, alongside single-digit sequences for the other four topology seeds.
MP4 exports are supplied for interactive playback. Review used ordered frames
and consecutive-frame strips correlated with CSV traces, not real-time movie
playback. Earlier artifact paths below describe the previous visual baseline.

Observed across all five single-digit families, plus full rollover/midnight
sequences on split/bridge, continuous-lower and asymmetric-loop worlds: workers
now approach separate unpacking spots and pick up the kits left there. The
bagging and loading strips make each material transfer traceable. Stable
recovery draws some crew downstairs instead of leaving every worker on the
roof. Empty stable controls keep genuinely empty trolleys parked. Broad passing
has readable depth and a courtesy pose, but roof arrival queues and long rows
of resting crew are still less lively than the intended final game.

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
- `date-colors-50s.png`: previous color state, including the dark purple crane
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

Remaining weaknesses: brief queues still bunch at roof/ladder arrivals.
Passing gestures do not solve all same-direction or stationary-worker overlap.
The parked crane/lift are not active material machinery. Rubble recovery is
section-scale and some bays lack trolley access; their stored sacks remain
visible. Stable crews now have recovery jobs, but the full ambient scheduler
and richer equipment interactions remain future work.

## Current throughput and resources

The latest comparison is 89/96 (92.7%) single-digit transitions stable within
60 s, versus 93/96 (96.9%). Visible handling adds some time; worker speeds
were not increased. Mean single-digit times are 52.33/52.54/50.09 s, with
rollover/midnight 99.38/145.18 s. Context grows by 64 bytes to 10,408, state
by 48 to 9,536, workers by four bytes to 304. Entity limits are unchanged.
See the throughput report for the full current table and recovery accounting.

Previous independent-rope baseline (historical):

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
