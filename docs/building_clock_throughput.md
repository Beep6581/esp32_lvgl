# Builders throughput measurements

## 2026-11-15: independent ropes and local logistics

Uncommitted refinement of `4ccb755` on `buildclock`. The committed baseline
was rebuilt and rerun before editing. Same 32 deterministic roots, generator
version 5, five scenarios and three crew policies: 480 runs per comparison.
No generation, navigation capacities, movement speeds, action durations,
pacing minima, hardware/display configuration or task settings changed.

### Behavior and scheduling

- Every builder operates their own harness ascender/descender and travelling
  roof clamp. Mandatory belay jobs, anticipatory tender dispatch and the
  half-crew builder cap are removed. Rope lane separation, crossing avoidance,
  physical retrieval, material pickup and structural dependencies remain.
- Optional winching is a local idle activity, not a job. An already nearby
  idle rigger may assist from the same actual platform, 14-64 units from the
  anchor, only when no compatible pending work exists. There is no extra trip,
  reservation or influence on the builder. Useful work preempts immediately.
- A semantic flag records actual ascent/descent displacement in the latest
  quantum. Only that movement enables a helper's cranking and rope connection.
  Stationary work, preparation, waiting, blocked lanes and horizontal shifts
  never crank. The viewer also shows a small harness control on the builder.
- A broad roof cache permits four concurrent courier jobs, bounded by actual
  node capacity, instead of two. Narrow work sites are not widened. This gets
  independent pier kits and later sections supplied without dedicating crew
  to winches. The existing paired-kit and suspended-front continuation remain.
- A worker with ready work at their current station stays for that work before
  idle egress. Clearing dispatch also considers nearby clearers about to
  finish. The trace exposed workers leaving after each rubble item and roof
  replacements descending against ascending couriers; this was avoidable
  scheduling, not slow climbing.

Removing mandatory tending alone was insufficient: adaptive 7 -> 8 averaged
55.92 s with 23/32 within a minute, versus 54.83 s and 27/32 before. The local
work and supply changes below are therefore part of this refinement, not a
claim that deleting tender jobs alone improves every scenario.

### Measured results

Times are simulated seconds to fully `STABLE`, including inspection, hold and
rope retrieval, not just appearance of the last brick. Adaptive six plus relief:

| Transition | Before mean | After mean | After p90 | After max | <=60 s before / after |
| --- | ---: | ---: | ---: | ---: | ---: |
| 7 -> 8 | 54.83 | 51.10 | 57.20 | 62.20 | 27/32 / 30/32 |
| 1 -> 2 | 52.31 | 47.14 | 53.40 | 57.75 | 27/32 / 32/32 |
| 8 -> 9 | 53.89 | 50.01 | 58.30 | 60.55 | 24/32 / 31/32 |
| Rollover | 120.64 | 92.58 | 102.05 | 105.35 | 0/32 / 0/32 |
| Midnight | 188.24 | 134.02 | 150.35 | 159.25 | 0/32 / 0/32 |

Single-digit completion within 60 seconds improves **78/96 (81.3%) -> 93/96
(96.9%)**. Fixed six also achieves 93/96 with identical single-digit timing;
this improvement does not require adding workers. Fixed nine achieves 96/96,
but is not selected as the default. All 480 after-runs complete with zero
timeouts or evacuation failures. Accepted-family counts remain 5/8/7/3/9.
The 32-root adaptive benchmark now asserts the aggregate 90% target.

Useful contributors average 2.86/2.11/2.89 in single-digit scenarios (peaks
6/4/6), and 5.33/5.63 for rollover/midnight (peaks 9). The old sampler counted
mandatory tending as productive, so its larger 4.67/3.77/4.51 single-digit
figures are not like-for-like useful-throughput comparisons. Optional tending
is now explicitly excluded and takes 4.4/3.6/3.8% of worker-time. Actual
simultaneous masonry work still peaks at two builders for single digits and
four across multi-digit projects. Independent ropes free logistics workers;
they do not eliminate masonry dependencies or occupied rope lanes.

At the reference root `0x6a09e667f3bcc909`, 7 -> 8 improves 69.55 -> 57.35 s.
In the right roof region (y=73, x>=350), sampled mean occupancy falls 3.74 ->
1.70 workers, peak 6 -> 4, and time with at least three workers falls 51.20 ->
15.65 s. This is one seed's crowding diagnostic, not a claim about all worlds.
Early construction is not universally earlier: its first brick moves from
23.75 to 26.65 s because initial clearance and courier traffic compete. The
second pier's supply advances 39.45 -> 21.35 s, making the overall project
substantially faster. Remaining bottlenecks are real transport, opposing
ladder traffic, shared rope strips, dependencies and retrieval, not tender
availability. The three remaining >60 s single-digit runs are retained.

### Visual review and verification

Ignored repository-local artifacts: `build/builders_independent_ropes/`.
`baseline.txt` and `final32.txt` contain comparable benchmark output;
`four-single-*.csv` contains the final reference-worker/section/job trace.
`before-after-50s.png`, full single/rollover/midnight/unfinished PNG sequences,
three MP4 exports, storyboards and `descent-placement-strip.png` support review.
Four additional single-digit sequences cover the other topology families:
`02ce5c58e42372d7`, `7bac433ee14d632b`, `af4a0846691a0e44`, `f9fd2ded6d5b25c6`.
Inspection used ordered complete-sequence storyboards and consecutive frames,
not claimed real-time video playback. Builders carry baskets, descend/climb
on actual ropes, place within arm reach and return without roof partners.
Helpers no longer continuously crank while a builder hammers or shifts.
Brief cache clustering remains visible; no continuous crowd collision system
or new machinery system was added. The C64 character design is unchanged.

All thirteen host suites and the SDL animation check pass. All thirteen suites
also pass ASan/UBSan (`detect_leaks=0` for this environment). Focused tests cover
solo full reconstruction, unassisted continuation/rollover, optional tending
with no effect on motion, useful-work preemption, actual-displacement gating,
blocked lanes and local station continuity/egress. Thirty consecutive minute
changes average 39.49 s, maximum 53.25 s; twelve scheduled updates still exercise
unfinished rollover without transition-resource leaks.

The ESP-IDF build passes with the installed 6.1 environment and existing
repository-local offline dependency adapter. No flashing was performed.
The build reordered sdkconfig sections without changing values; that generated
ordering-only edit was removed. Strict C11/conversion checks pass for the
host-compatible runtime and the new simulation/throughput/trace tests.
A wider extra-strict all-files check still finds pre-existing signed-conversion
warnings in viewer coordinate expressions and older generator/transition test
helpers; normal `-Wall -Wextra -Werror` builds pass. The ESP entropy adapter
requires ESP headers and is checked by the firmware build, not standalone host C.
Formatting, ASCII and whitespace checks pass for changed source files.

No heap allocation or capacity increase. Host sizes are unchanged: worker 300,
state 9,488, context 10,344 bytes. Removing belay jobs reduces peak adaptive
job usage from 13 to 7 for 7 -> 8, 26 to 24 for rollover and 34 to 31 at midnight;
load peaks remain bounded at seven for 7 -> 8 and eight globally. The optional
activity scans existing fixed worker/job arrays and uses one 16-bit local mask.

## 2026-11-15: visual/gameplay tradeoff

The subsequent [visual pass](building_clock_visual_overhaul.md) adds physical
winch stations, off-duty platform separation, interrupted trolley shunting,
animated tools/materials and semantic site feedback. Generator version 5 and
all movement/action speeds remain unchanged. In the same 480-run batch,
adaptive single-digit means become 54.83/52.31/53.89 seconds and <=60-second
completion becomes 78/96 (81.3%), versus 83/96 (86.5%) below. Rollover/midnight
means are 120.64/188.24 seconds. No timeouts or evacuation failures.

This modest regression is retained explicitly in exchange for physical
separation/setup and more legible activity; it is not claimed as optimization.
Context storage is now 10,344 bytes (+336), worker 300 (+8), state 9,488 (+336).
All prior capacity limits remain unchanged. The linked report contains the
motion-review artifacts, verification and remaining visual weaknesses.

## 2026-11-14: ladder following, local crews and connected roofs

Worktree refinement of `d89c6e7`, including the host-only bottleneck probes.
No movement speed, brick-placement duration, hammer duration, pacing deadline,
display configuration or capacity was increased to obtain these results.
The adaptive crew reaches **83/96 (86.5%)** single-digit completions within
60 seconds, versus 14/96 (14.6%) before this pass. The requested 90% is not yet
met; this is a substantial improvement, not a completed throughput target.

### Comparable batch and distributions

The same 32 root inputs, five transitions and three crew policies were measured
before and after: 480 completed runs per revision, no timeouts or evacuation
failures. Roots are `0x6a09e667f3bcc909 + i * 0x9e3779b97f4a7c15` modulo 2^64.
Completion includes COMPLETE -> STABLE and required rope retrieval/cleanup,
not merely the last visible brick. Values below are simulated seconds.

| Adaptive six + relief | Before mean | After mean | Before p90 | After p90 | After maximum | <=60s before -> after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 7 -> 8 | 67.44 | 53.24 | 76.15 | 60.40 | 68.10 | 3 -> 27 / 32 |
| 1 -> 2 | 63.37 | 52.01 | 71.55 | 60.10 | 67.60 | 11 -> 28 / 32 |
| 8 -> 9 | 71.29 | 51.86 | 79.55 | 60.85 | 66.40 | 0 -> 28 / 32 |
| 14:59 -> 15:00 | 153.39 | 118.70 | 187.05 | 133.45 | 135.60 | 0 -> 0 / 32 |
| 23:59 -> 00:00 | 229.82 | 184.33 | 272.25 | 209.25 | 247.25 | 0 -> 0 / 32 |

This is a whole-system comparison, not a controlled scheduling-only A/B:
the grammar is now version 5. The same root can produce a different accepted
world. Final topology counts change from 4/9/6/9/4 to 5/8/7/3/9.
All old generator versions were already inputs to one current grammar, not
separately retained historical implementations; historical replay needs its
matching code revision as well as seed/version/attempt.

Fixed nine-worker results are 29/32, 32/32 and 30/32 single-digit completions
within a minute: **91/96 (94.8%)**, means 48.63/44.93/51.49 seconds.
This is diagnostic evidence for improving adaptive relief/local availability,
not a reason to force every scene to nine workers. Current adaptive admission
still requires almost everyone busy plus two pending jobs, and can miss a
useful remote helper when some workers are unavailable or on the wrong roof.

### Implemented changes and ownership

- Permanent ladders admit same-direction followers with at least 20 design
  units of separation for a 17-unit worker. Opposing traffic waits until empty.
  Fixed masks remain the reservation representation; runtime admission and
  progress enforce spacing. Synthetic two-climber four-second climbs finish
  in 4-5 seconds together rather than eight. Short temporary-access segments
  retain exclusive landings and capacity-one edges; they are not promoted to
  unrestricted multi-occupant abstract vertical links.
- Dispatch compares executable route duration, not Manhattan distance. A
  nearby worker already finishing unpacking/clearing or retrieving a rope can
  win over an inspector/tender making an avoidable cross-roof trip. Predictions
  cover only short committed local releases; actual motion still takes place.
- A support-ready delivery can dispatch its rope tender before the builder
  arrives. Anticipatory rigs are bounded to crew/3 and remain below clearing
  priority. Supply/rigging starts during PREPARING when its route is safe;
  placement still waits for clearing and real section dependencies.
- A suspended worker reaches masonry within the existing 12-unit arm reach
  instead of moving its whole body to every adjacent brick. Bodies remain
  under their rope anchor and the viewer draws the hand at the actual brick.
  Material ownership, per-brick positioning and hammer time are preserved.
- Stable-ID retrieval breaks mutually blocked suspended rope shifts. Idle
  occupants of cancelled exclusive scaffold landings now yield, including
  outside the hazard, so they cannot indefinitely block an evacuation route.
- Upper bridges connect both main roofs through real ladders. Roof and highest
  platform spawn locations give useful upper access without teleporting crew
  there during work. Initial spawn locations are intentionally different from
  version 4. Permanent platforms must all be reachable at complete validation.
- Ladder columns are chosen from compatible real surfaces with permanent and
  predefined temporary-access clearance. Redundant overlapping side ladders are
  omitted when a real segmented side route exists. Failed candidates are still
  rejected, never repaired by validation. The version-5 fallback is root 2,
  attempt 0, through the unchanged complete pipeline.

### Utilization and remaining critical path

| Scenario | Mean active before -> after | Peak active after | Time with >=5 active before -> after |
| --- | ---: | ---: | ---: |
| 7 -> 8 | 3.39 -> 4.68 | 8 | 21.3% -> 60.3% |
| 1 -> 2 | 2.52 -> 3.75 | 6 | 2.4% -> 32.0% |
| 8 -> 9 | 3.04 -> 4.51 | 8 | 18.3% -> 57.4% |
| Rollover | 5.37 -> 6.89 | 9 | 67.4% -> 82.1% |
| Midnight | 5.68 -> 6.97 | 9 | 75.1% -> 83.8% |

Active includes carrying, travel, clearing and tending, not only brick work.
For 7 -> 8, navigation waits fall from 4.5% to 0.8% of worker-time and tender
waits from 6.0% to 1.3%. Rope-lane waits rise from 1.7% to 2.4% as more fronts
overlap. Total waiting falls from 12.2% to 4.5%. Crew-time is now build 2.4%,
supply action 3.9%, carrying 9.8%, other travel 9.3%, rope travel 10.6%, tending
23.6%, clearing 0.5%, waiting 4.5%, ambient 35.5%. Some late roof spectators
remain; five productive workers is not consistently achieved for every digit.

The original root now selects split/bridge (not the historical twin towers).
Its single transition is 68.10 seconds, rollover 125.55. Single lifecycle:
collapse 1.00, clearing 3.00, foundation 9.25, structure 26.50, recognizable
56.80, finishing 65.25, complete 66.20, stable 68.10. Long depot-to-roof
supplies, a two-builder/tender work front, dependent final courses and retrieval
still determine outliers. The first brick is at 24.40 seconds: this long-route
example is not a startup-latency success. The old version-4 root completed the
single scenario in 63.75 seconds; changing geometry is not a per-seed speedup
guarantee. Midnight reaches all eight load slots and spends
21.0% of worker-time travelling, 13.2% carrying and 24.2% tending. Next work
should focus on useful local supply/relief and final dependent work, not speed
increases or declaring completion while rope crew is still committed.

### Generated worlds, memory and verification

4,096 requested roots produce 4,124 attempts: 12 preliminary clearance rejects,
16 complete spawn-connectivity rejects, no derivation failures, no fallbacks
or hard failures. Final family counts: 864/833/790/802/807. Navigation use is
71-106 nodes, average 91.92 of 112; limits were not raised.
The separate raw 4,096-candidate pipeline test observes 70-106 nodes and
74-117 edges (averages 91.84/98.27); complete validation rejects 26 unreachable
spawn candidates rather than adding invented connections.

Host ABI sizes are unchanged: context 10,008 bytes, mutable state 9,152,
worker 292, load 32. Limits remain nine workers, 48 jobs, eight loads,
20 falling sections, 32 debris, 112 nodes and 192 edges. The throughput batch
reaches 34 job slots and all eight loads. Extra dispatch route queries are
bounded but their ESP32 CPU cost has not been measured on hardware.

All 13 host suites pass, including deterministic control replay, spaced and
opposing ladder traffic, clearance rejection, suspended-lane contention,
cancellation and successive unfinished rollovers. Thirty completed consecutive
minutes average 42.01 seconds, maximum 66.60; twelve scheduled minute changes
include three unfinished rollovers and clean up without leaked resources.
The 128-world evacuation-lead check observes a worst 8,386 ms requirement.
Strict C11/conversion compilation, formatting and whitespace checks pass.
All 13 suites also pass AddressSanitizer/UndefinedBehaviorSanitizer; leak
detection is disabled for this environment. This caught and fixed an invalid
platform-ID lookup in the new generator helper before handoff.

The SDL viewer builds. Ordered 15-second captures through complete single,
rollover and midnight sequences, plus repeated unfinished minutes and four
other family examples, were inspected. Builders visibly remain on ropes while
placing adjacent bricks; carriers and tenders work concurrently. Connected
upper routes form more useful silhouettes. Roof clusters of idle crew remain
visible, especially near completion; this is not a claim of finished game feel.
These are software-rendered sequence captures, not an interactive playback or
hardware rendering test. Captures and raw measurements live in the ignored
`build/builders_refinement/` directory, not source control.

Firmware builds with the installed ESP-IDF 6.1 toolchain and existing managed
components. To respect this task's no-outside-repository-write boundary, the
component manager was disabled and an ignored CMake adapter supplied the two
existing manifest-only dependency edges for panel-IO additions and FT6336U.
No installed dependency, source configuration, display setting or Git reference
was changed. This verifies compilation/linking, not flashing or hardware behavior.

## Historical checkpoints

The following results describe the previous committed implementations.

## 2026-10-19: paired supplies and persistent rope work

The same 32 roots and five scenarios were run with three crew policies: 480
runs, zero timeouts, zero evacuation failures, and no end-of-project load/debris
leaks. Root sequence is `0x6a09e667f3bcc909 + i * 0x9e3779b97f4a7c15` modulo
2^64. Topology counts remain 4/9/6/9/4; generation was not changed.

| Change | Original six-worker baseline | Oct 18 six + relief | Oct 19 six + relief | <=60s / 32 |
| --- | ---: | ---: | ---: | ---: |
| 7 -> 8 | 203.6 s | 84.1 s | 67.4 s | 3 |
| 1 -> 2 | 172.8 s | 74.6 s | 63.4 s | 11 |
| 8 -> 9 | 189.9 s | 79.6 s | 71.3 s | 0 |
| 14:59 -> 15:00 | 324.6 s | 171.5 s | 153.4 s | 0 |
| 23:59 -> 00:00 | 431.1 s | 235.6 s | 229.8 s | 0 |

Original baseline means include timeout caps, as explained below. Current
single-digit p90 values are 76.2/71.6/79.6 seconds, maxima 79.1/81.9/88.1.
Only 14/96 (14.6%) meet a minute, not 90%. Individual cases can regress even
when averages improve: the standalone midnight fixture is 240.6 seconds versus
227.8 before early-tender dispatch; the 32-root midnight mean improves.

The added logistics remain bounded and use actual worker motion:

- Compatible kits share a courier trip and four-second staging action. Two
  kits retain two load slots, not a synthetic merged material object.
- A rope builder can take the next dependent kit from the same cache. After
  securing the first section, a separate new placement job continues from the
  current physical position with the same tender. Dependencies remain enforced.
- Rope support is dispatched when construction is assigned, overlapping travel
  instead of requesting support only after the builder reaches the roof.
- No worker, rope or action speed changed; no capacity increased.

In 7 -> 8, paired deliveries average 2.9, continued sections 1.9, and ascents
4.9 (previously 6.8; original 13.1). Average active contributors are 3.39,
peak seven. Five or more are active for only 21.3% of elapsed time: the goal
of five normally contributing is also not met. Multi-digit/midnight averages
are 5.37/5.68, peak nine, with five active for 67.4%/75.1% of elapsed time.
Active includes travel and tending, not only productive hammer strokes.

7 -> 8 crew-time split: build 3.0%, supply action 3.8%, carrying 8.6%, other
travel 8.1%, rope travel 12.4%, tending 16.8%, clearing 0.5%, waiting 12.2%,
ambient 34.6%. Waiting comprises navigation 4.5%, tender arrival 6.0%, rope
lanes 1.7%, and less than 0.1% turns. These percentages use total worker-time;
the five-active percentage uses elapsed simulation time. Sampling is host-only.

Extra crew do not solve the single-digit critical path. Fixed-six 7 -> 8 mean
is 67.45 seconds, fixed-nine 69.48, adaptive 67.44; adaptive adds only 0.4 workers
on average for this scenario. For multi-digit/midnight, adaptive reaches nine
and reduces fixed-six means of 191.5/287.9 to 153.4/229.8 seconds. Supplies,
travel, roof access and dependent work remain important; simply adding workers
creates more idle roof occupants. The eight shared load slots also limit kit
pairing in multi-digit work (only 0.1 paired deliveries per midnight on average).
Do not raise the cap without measuring whether additional staging truly helps.

### Verification and visual observations

All 11 host suites pass. Added tests cover paired ownership, carrying, dependent
placement, continuous rope handoff, delivery cancellation, suspended rollover,
early tender dispatch/cancellation, cleanup and safe descent. Existing byte
replay tests pass. Thirty consecutive completed minutes average 77.6 seconds,
maximum 135.8; twelve 60-second updates include eleven unfinished rollovers and
finish without leaked transition resources. AddressSanitizer/UBSan transition
tests pass; leak detection was disabled because LeakSanitizer cannot run in this
environment. Simulation code itself does not allocate dynamically.

SDL and ESP-IDF builds pass. Strict C11 conversion/pedantic compilation, format,
ASCII and whitespace checks pass. No firmware configuration changes or flashing.

Ordered SDL captures at 20/30/35/40/45/50/55/65 seconds show the twin-tower
7 -> 8 build progressing from delivered roof bundles into two distinct suspended
piers and then the middle/top masonry. Builders retain their next kit instead of
retrieving each section separately. Central-machinery rollover/midnight captures
show distinct work fronts across the clock and unfinished-minute continuation.
These are deterministic sequence captures, not an interactive motion acceptance
test. Roof caches/tenders still visually cluster; unused crew and long initial
supply journeys remain apparent. This is not yet consistently a busy five-worker
single-digit construction scene. Art and permanent geometry are unchanged.

Host sizes: simulation context 10008 bytes (+80 from `f6e7a16`, +8 from the
previous checkpoint), mutable state 9152 (+48 from baseline), worker 292
(unchanged), load 32 (+4 from baseline; companion ID fits existing padding).
Caps remain nine workers, 48 jobs, eight loads, 20 falling sections, 32 debris.
The acceptance batch reached 30 job slots and all eight load slots.

Next review should inspect the viewer before further behavioral changes. The
remaining throughput work needs better localized supply/work-front organization
and less serialized roof/ladder travel, not faster workers, arbitrary extra crew,
or remote placement. There is no evidence yet that the 90% target is attainable
with just another small dispatch tweak. No changes have been committed.

## Historical checkpoint: 2026-10-18 measurements

The host-only `building_clock_throughput_tests` samples the shared simulation at
50 ms intervals. It runs fixed-six, fixed-nine, and six-plus-relief crews over
three single-digit changes, 14:59 -> 15:00, and midnight. The final batch used
32 deterministic roots: 480 complete runs, zero timeouts or evacuation failures.
Accepted topology counts were 4/9/6/9/4 in enum order. The same roots were used
for the baseline. No generator or movement-speed changes were made.

Mean elapsed seconds, starting with the target-change input:

| Change | Baseline, six workers | Current, six plus relief |
| --- | ---: | ---: |
| 14:37 -> 14:38 | 203.6 | 84.1 |
| 14:31 -> 14:32 | 172.8 | 74.6 |
| 14:38 -> 14:39 | 189.9 | 79.6 |
| 14:59 -> 15:00 | 324.6 | 171.5 |
| 23:59 -> 00:00 | 431.1 | 235.6 |

Baseline means include 600-second timeout caps: respectively 1, 1, 1, 5 and 7
of 32 runs timed out. They are capped observations, not completed-run means.
For 7 -> 8, median fell from 199.2 to 81.8 seconds; current p90 is 100.8 and
maximum is 121.2 seconds. Only 1/32 meets 60 seconds. Across all three
single-digit scenarios, 4/96 meet 60 seconds: the requested 90% is NOT met.
Individual seeds are not all faster.

For 7 -> 8, fixed-six current mean is 89.2 seconds, versus 84.1 with relief.
Relief helps, but cannot remove support dependencies and repeated roof access.
The existing nine-worker cap was sufficient; no entity capacity increased.

In 7 -> 8, average active contributors increased from 1.97 to 3.65, with peak
nine. Contributor samples include construction, supply actions, carrying,
travel, rope travel, tending and clearing; they exclude waiting and ambient
poses. Thus this is not a measure of hammering time alone. The current whole-
crew time split is 1.9% building, 4.2% supply actions, 9.5% carrying, 7.7% other
travel, 9.5% rope travel, 12.6% tending, 0.3% clearing, 12.5% waiting and 41.8%
ambient. Baseline ambient was 61.7%. Relief changes the denominator. Five useful
workers are not sustained throughout a single rebuild. Rope ascents dropped
from 13.1 to 6.8 per run. Multi-digit/midnight average contributors are 5.20/5.62.

## What changed and why

- Couriers deliver to usable roof pickup nodes. Builders no longer retrieve
  every section from lower staging. Loads retain an explicit actual pickup node.
- Delivery dispatch counts deliveries, not unrelated builders/tenders sharing
  a broad roof. The old rule withheld supplies while crew members were working.
- Nearby free crew are preferred only after confirming graph reachability.
  An unreachable nearer worker cannot starve all other applicants.
- Side piers own footing bricks and depend on local supports. Center footing
  and both side footings can proceed independently; silhouettes are unchanged.
- Tender-operated travelling beam clamps shift ropes along an actual roof.
  Materials remain with their builder. Occupied crossing lanes require retrieval.
- Belayers use broad real roof nodes, not exclusive temporary-access nodes.
  This fixes a baseline stall at seed `0x6496e29f64d8f6c2`.
- Secured sections release dependencies immediately, while final promotion
  waits for returning crews and their job references to be released.
- Pacing minima are cumulative reconstruction milestones rather than new
  idle holds following each late phase.
- Relief enters through generated spawns when the crew is busy and jobs wait,
  at most once per 1500 ms and never beyond the existing nine-worker capacity.
- Job subjects are assigned field-by-field after zeroing records. Copying local
  struct padding caused byte-replay failures exposed by the larger test suite.

## Verification and visual checkpoint

All 11 host suites pass, including consecutive unfinished minutes, deterministic
replay, reference cleanup, independent footings, relief spawns, roof caches and
pulley bounds. All numeral brick totals are checked. ESP-IDF and SDL builds pass.
The renderer-independent component sources pass C11 warnings-as-errors including
conversion/pedantic checks; the ESP-only entropy adapter is verified by IDF.

SDL snapshots of single-digit work at 35/60 seconds and midnight show separate
suspended fronts, supplied roof caches and visible rope clamps. Roof crew still
cluster, and late single-digit work still has too many ambient workers. This was
snapshot inspection, not a full interactive motion review. Further interactive
viewer acceptance remains necessary.

Host sizes: simulation context 10000 bytes (+72), mutable state 9152 (+48),
worker 292 (unchanged), load 32 (+4). Capacities remain nine workers, 48 jobs,
eight loads, 20 falling sections and 32 debris records. No hardware configuration
or application-mode changes; no flashing performed.

## Historical continuation point (addressed by the work above)

Review the uncluttered viewer first. The next throughput investigation should
measure opportunities for builders to retain a work front across adjacent
sections and for small supply batches to support those visits. Currently each
section still has its own courier trip and roof return. Do not hide that cost
with faster workers, remote placement, or a longer acceptance timeout. Also
break waiting samples down into ladder/roof congestion versus rope-tender waits;
the current sampler reports aggregate waiting only. Avoid increasing crew size:
the measurements show that more workers alone do not fix this bottleneck.

Reproduce with the commands in `components/building_clock/host/README.md`.
No commit was requested for this checkpoint; all implementation, tests and
documentation remain in the worktree.
