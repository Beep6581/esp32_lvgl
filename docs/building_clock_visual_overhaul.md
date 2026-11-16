# Clock - Builders visual/gameplay status

Current implementation: uncommitted living-site and functional-crane refinement after `3ead7c4`
on `buildclock`; reviewed 2026-11-16.
The character/site overhaul was committed in `4ccb755`; independent ropes,
logistics and crane/date color corrections followed in `035220d`. This file
describes the current viewer, not the superseded mandatory-tender model.
See [the specification](building_clock_simulation.md) for architecture/design
and [throughput](building_clock_throughput.md) for measurements and diagnostics.
Hardware/display configuration, clock scale, traversal speeds and construction
action durations are unchanged. Generator version 6 removes vertical supports.

## Functional crane: current visual review

The crane is now cyan-blue (47,148,211) with dark blue shadows, distinct from
terracotta masonry and periwinkle platforms. Its cable is always ladder-yellow,
drawn by one foreground path in every phase, including idle and retraction.
The unloaded hook retracts while roof crew unpack; the same crane can then take
another advantageous delivery. Standalone vertical girders are no longer
generated or drawn. Temporary decks retain horizontal beams and explicit
yellow climbing ladders, not blue vertical rails. The crane mast remains.

Its tower/jib extend
to the derived overhead clearance of the generated working range; this is
functional reach, not merely a decorative boom. A trolley, hook, taut outboard
cable and joined one/two-kit sling show the real state machine. Six bricks on
a paired pallet correspond to two three-brick kit records, not duplicate art
pretending to be cargo.

The loader physically collects stock and brings it to the adjacent control
stance, attaches the sling and operates a pendant. Cargo now lands on a supported
roof skid without reserving a receiving builder or a signalling/waiting pose.
Landing unlocks an ordinary unpack job. Stock can wait while the operator and
crane take other work; a builder later performs the unchanged unpacking action.
Builders then take these actual kits onto their
normal rope jobs. A cancelled airborne pallet returns along separate vertical,
horizontal and lowering legs; later couriers can collect its recovered stock.

Review used consecutive 100/200 ms SDL captures correlated with worker/hoist
traces, including complete single, three-digit, midnight and interrupted runs.
Early inspection exposed three semantic defects that were fixed: holding the
main path's control node, chasing the moving cargo horizontally, and moving an
empty hook before an operator arrived. A later review removed the unnecessary
operator hold through roof unpacking. The receiving hands now meet the cargo;
the hook no longer leads the receiver around the scene. Foreground rigging
expresses an outboard hoist, not a new climbable abstract connection.

Current review evidence is in `build/crane_refinement/`: `retract-contact.png`
shows successive loading, lowering and retracting frames; `scene-contact.png`
follows construction into quiet operation. `single/` is a full 65-second
100 ms sequence on v6 root `f9fd2ded6d5b25c6`. `interrupted/` changes the target
mid-flight at 9 seconds; `rollover/` follows a complete multi-digit rebuild on
root `72db14d36a85161a`. The ordered frames show no red/blue cable-color change,
no parked extended cable after delivery and no standalone vertical girders.
Version-6 seeds intentionally produce different layouts from version 5.

Earlier version-5 visual evidence was in `build/builders_crane/`:

- `crane-single.mp4` versus `manual-single.mp4`: same split/bridge root
  `084160217307451e`, 100 seconds, 100 ms frames. `final-crane-contact.png` and
  `final-landing-contact.png` show loaded lift/traverse/landing and six close
  consecutive landing poses; `final-build-contact.png` follows roof stock into
  actual reconstruction and back to quiet activity.
- `crane-interrupted.mp4`, `final-interruption-contact.png`: target change at
  9 seconds during the loaded traverse, safe return and completed new target.
- `crane-rollover.mp4`, `crane-midnight.mp4`: complete 220/240-second sequences;
  ordered overviews are `final-rollover-contact.png` and
  `final-midnight-contact.png`.
- `final-continuous/` (`a678d9daf251c133`), `final-loop/`
  (`44b05394719c3d48`), `final-towers/` (`02ce5c58e42372d7`) and
  `final-central/` (`7bac433ee14d632b`): 100-second sequences spanning the other
  four topology families. The last two correctly prefer manual transport.
  No topology or generator changes were made just to make the crane busier.

Remaining weaknesses: the long top jib is visually prominent; the deliberate
outboard plane allows the sling to pass in front of intervening girders in the
flat image. A future renderer can strengthen depth cues, not invent navigation
or teleport loads. Receiving a large early delivery still ties up one worker
for preparation/signalling, and some busy-roof scheduling choices regress.
Three/four-digit collapse produces the existing crowded temporary assembly
around retreat nodes. The crane is selective rather than continuously animated;
it is intentionally inactive when manual work wins. Lifts and kit-delivery carts
remain future work. See the throughput report for paired gains and regressions.

## Retained living-site improvement

Quiet periods now have shared maintenance kits, girder sweeping and tightening,
tool sorting, clipboard/chalk beam checks, brief two-person drawing reviews and
occasional tea at real platform locations. Kits are fetched, carried and returned;
an urgent interruption leaves a kit where it was put down. Sweepers clean actual
patches instead of making remote dirt disappear. A colleague can inspect a
separate connection while the kit is occupied, rather than waiting in a crowd.
Drawing reviews reserve two positions 24 units apart; a single folded drawing
passes between reaching hands on a shared timer. Meetings time out cleanly and
both workers disperse. Returning crew use nearby existing graph anchors instead
of commuting back across a whole platform. Walk cycles follow displacement;
tool actions follow their local phase rather than a synchronized global cycle.

The earlier living-site portion did not add machinery. Rope construction, individual-brick collapse,
local rubble and loaded trolley recovery remain functional and unchanged. Quiet
rounds stop when reconstruction starts; they are not competing priority-10 jobs.
The old viewer-fed inspection/hammer queue is removed, so the behavior also runs
in the shared headless simulation without a viewer.

### Visual review evidence

Artifacts are repository-local and untracked in `build/builders_living_site/`:

- `before.mp4` and `living-site.mp4`: 180-second before/after on root
  `6a09e667f3bcc909`; `before-contact.png` shows the former sparse activity.
- `stable-0x*/`: full 250 ms sequences for all five families, 180 seconds each;
  seeds are listed in the throughput report. `families-contact.png` compares them.
- `single.mp4`, `rollover.mp4`, `midnight.mp4`: complete 240-second sequences,
  including the return to quiet activity; `unfinished.mp4` advances targets every
  minute without waiting for completion.
- `sweep-sequence.png`: ordered 250 ms frames of a crouching sweeper brushing
  a real girder patch. `plan-sequence.png`: ordered 500 ms frames of a drawing
  review/handoff. `consecutive-50ms.png`, `closeup/` and `twin-closeup/` retain
  finer walking/action frames. Full worker timelines are `0x*-workers.csv`.

Review used ordered consecutive captures and traces, not an assumption that an
MP4 export alone proves the movement. Sweeping/toolbox trips now read clearly;
the folded drawing gives a readable small social moment, with both faces visible.
The lower routes and upper roofs remain active independently in quiet scenes.
Initial inspection found blocked fixed work spots, remote sweeping and an
unsynchronized paper handoff; all three were corrected before final captures.
Construction still visibly preempts rounds, and the site resumes upkeep after
single/multiple digits become stable. Heavy rollovers still have some roof
congestion from the existing logistics/construction system.

Remaining artistic limitations: rounds are deliberately local and can become
repetitive over long frozen-clock runs; a short/narrow platform offers fewer
activity choices. Occasional inactivity reaches 24.75 seconds in one sampled
world, although the other workers remain busy. Walking bodies can briefly
occlude during depth-lane passing. The chalk marks and tool-check gesture are
subtler than the sweeping/tea/drawing actions. Cross-level planned inspections
and richer contextual interactions would be useful later. Functional crane work
is described above; no display-path change is implied.

### Previously committed collapse refinement

The collapse review preserves section-authoritative simulation and adds only
transient per-brick tilts. Failure still spreads spatially from one brick per
section; unreleased bricks stay seated. Moving bricks tilt by at most 30 degrees
and return flat in the same local pile. A first 60-degree preview lost the tiny
brick silhouettes and was reduced. Three pre-rasterized poses, mirrored as
needed, avoid runtime trigonometry and keep the coarse C64 appearance. The
settled helper now draws the pile directly, without recomputing its flight.
There are no per-brick physics entities, collision tests, reservations or heap
allocations. Timing, recovery accounting and throughput are unchanged.

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

Historically, the stable viewer scenario supplied two low-rate inspection/maintenance
jobs at real site locations instead of an eighteen-job patrol queue. Recovery,
off-duty behavior and all actual job execution remain shared simulation code.
Those two maintenance inputs were viewer demo wiring. The current living-site
simulation replaces them, as described above.

## Historical site and construction storytelling

Depots are little stocked racks, staging areas have timber skids, and debris
service bays are open bins rather than filled UI rectangles. The parked crane
has a trussed tower/jib and cab, now muted blue-purple with darker shadows
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

Current review artifacts are in ignored `build/builders_collapse_review/`:
`before-after.png`, `final-brick-strip.png`, `midnight-collapse.mp4`, and five
`<seed>-story.png` sheets. Each sheet has single/rollover/midnight rows and
columns at 0, 1.5, 2, 2.5, 3.5 and 12 seconds. The `sequences/` directory holds
all 15 complete twelve-second sequences at 50 ms intervals, spanning all five
topology families. Inspection used ordered frames and consecutive-frame strips,
not real-time playback. The brief wave of tilted bricks reads as independent
falling masonry rather than rigid rotating strokes; multi-digit staggering
remains legible and settlement does not relocate the pieces. Piles remain
deliberately shallow/overlapping, and there is no collision or bounce simulation.
See the throughput report for measured savings and the small draw-cost tradeoff.

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

Weaknesses at that checkpoint: brief queues still bunch at roof/ladder arrivals.
Passing gestures do not solve all same-direction or stationary-worker overlap.
The parked crane/lift are not active material machinery. Rubble recovery is
section-scale and some bays lack trolley access; their stored sacks remain
visible. Stable crews now have recovery jobs, but the full ambient scheduler
and richer equipment interactions remain future work.

## Historical throughput and resources

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
