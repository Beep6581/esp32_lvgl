# Builders throughput measurements

Current implementation: uncommitted site-interaction pass after `80e70cb` on
`buildclock`; reviewed 2026-11-15.
This file owns measured performance, diagnostic reproduction and remaining
bottlenecks. The [specification](building_clock_simulation.md) owns architecture
and planned behavior; the [visual overview](building_clock_visual_overhaul.md)
owns presentation status. Older numbers below are explicitly historical.

## Current check: individual-brick collapse

The same 480-run benchmark was rerun after replacing rigid section rotation
with spatially spreading individual-brick falls. Adaptive six plus relief:

| Scenario | Mean seconds | p90 | Maximum | <=60 seconds |
| --- | ---: | ---: | ---: | ---: |
| 7 -> 8 | 52.60 | 58.95 | 63.50 | 29/32 |
| 1 -> 2 | 52.47 | 60.30 | 63.35 | 28/32 |
| 8 -> 9 | 50.11 | 57.30 | 61.60 | 31/32 |
| Rollover | 98.93 | 108.60 | 114.45 | 0/32 |
| Midnight | 145.71 | 162.45 | 172.70 | 0/32 |

Single-digit completion is **88/96 (91.7%)**, versus 89/96 (92.7%) immediately
before this refinement. All 480 runs complete without timeout or evacuation
failure. Worker speeds, action times and fall-duration limits are unchanged.
Choosing a failure-origin brick replaces the old rotation random draws, so
per-seed collapse durations and later contention can differ from the prior
build. Replay within the new build remains deterministic.

Context is 10,792 bytes and mutable state 9,920: 384 additional bytes retain
the masonry recipe in bounded rubble records, so settlement keeps the same
bricks until bagging. Workers remain 304 bytes. No per-brick runtime slots or
heap allocations were added. Results: `build/builders_brick_collapse/benchmark.txt`.

## Previous benchmark: physical handling and site recovery

The same 32 roots, five scenarios and three crew policies were rerun before
editing and after the changes (480 runs each). Generator version 5, capacities,
worker speeds, existing construction action durations and pacing are unchanged.
Times below include inspection and retrieval through fully STABLE.

| Adaptive six plus relief | Before mean | Site-pass mean | Site-pass p90 | Site-pass max | <=60 s before/site pass |
| --- | ---: | ---: | ---: | ---: | ---: |
| 7 -> 8 | 51.10 | 52.33 | 58.25 | 62.50 | 30/32 / 30/32 |
| 1 -> 2 | 47.14 | 52.54 | 60.25 | 63.40 | 32/32 / 28/32 |
| 8 -> 9 | 50.01 | 50.09 | 57.25 | 61.60 | 31/32 / 31/32 |
| Rollover | 92.58 | 99.38 | 111.40 | 114.35 | 0/32 / 0/32 |
| Midnight | 134.02 | 145.18 | 157.85 | 167.15 | 0/32 / 0/32 |

Single-digit completion is **89/96 (92.7%)**, versus 93/96 (96.9%). The 90%
aggregate target is retained. All 480 runs complete without timeouts or
evacuation failures. This is a visual/physical-coherence tradeoff, not a claim
of throughput improvement: clearing now includes actual bagging and carrying,
unpacking uses separate platform work spots, and builders walk to real kits.
A second kit must be within reach, not just share a graph node. Builders rig
directly from a compatible roof pickup spot instead of retracing to its anchor.
The `1 -> 2` scenario alone is below 90% (28/32, 87.5%); the aggregate must
not be mistaken for a per-transition guarantee.

At the reference root the single-digit transition is 58.25 s (before 57.35).
Foundation clearance is 14.85 s. The first carried sack appears at 12.20 s,
the first trolley loading completes at 19.40 s, unloading begins at 23.00 s,
and recovered inventory first increases at 23.90 s. No material is credited
to the depot while it is still in a worker's hands, a service bay or a trolley.

Useful contributors average 3.30/2.48/3.82 in single-digit scenarios and
5.65/5.86 in rollover/midnight. These now include actual recovery work;
the sampler reports it separately, and still excludes optional winch tending.
This is not an increase in simultaneous masonry builders.

The new site tests run 24 generated single/rollover/midnight sequences for
240 simulated seconds each, plus cancellation and solo-builder recall cases.
They account for all 312 rubble records: 289 recovered through 105 trolley
deliveries, 21 stored in service bays, two aboard carts at the sampling endpoint.
All sampled steps conserve material and reproduce the unsampled replay.
Stored sacks without compatible flat trolley access are deliberately retained.
Stable-clock jobs fetch carts via normal navigation; useful work recalls a
worker after their current edge/ladder or single sack transfer, without snapping.

Artifacts: `build/builders_site/before/benchmark.txt`, `benchmark-final.txt`,
`final-single-{workers,sections,jobs,site}.csv`, and equivalent rollover traces.
The new site CSV records cargo, operator, transfer progress and bay inventories.
Frame strips and complete exported sequences are described in the visual report.
Context is 10,408 bytes (was 10,344), state 9,536 (was 9,488), worker 304
(was 300). No heap allocation or entity-capacity increase was added.

Verification: 14 host tests pass normally and under AddressSanitizer/UBSan;
the SDL character test passes. Full ESP-IDF build, strict runtime/new-test
compilation, clang-format, ASCII-source and `git diff --check` pass. Display
configuration is unchanged; no flash/hardware behavior is claimed. Recovery
credits loose masonry; converting it back into new prefab kits is still future
work, not a closed recycling loop hidden in the current logistics.

## Previous benchmark: independent ropes and local logistics

Implemented in `035220d`, refining `4ccb755`. The committed baseline
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

### Visual review and recorded implementation verification

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
The later crane/date color fixes in the same commit do not affect these timing
results; their latest captures are listed in the visual overview.

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
These are recorded implementation checks, not tests rerun for a docs-only edit.

No heap allocation or capacity increase. Host sizes are unchanged: worker 300,
state 9,488, context 10,344 bytes. Removing belay jobs reduces peak adaptive
job usage from 13 to 7 for 7 -> 8, 26 to 24 for rollover and 34 to 31 at midnight;
load peaks remain bounded at seven for 7 -> 8 and eight globally. The optional
activity scans existing fixed worker/job arrays and uses one 16-bit local mask.

## Historical summary (superseded)

These checkpoints explain the path to the current implementation; they are
not current scheduling rules or an instruction to resume an older task.
Detailed former traces and reports remain recoverable from Git history.

| Checkpoint | Generator | Single-digit <=60 s | Meaning |
| --- | ---: | ---: | --- |
| October 18 throughput pass | 4 | 4/96 (4.2%) | Roof caches, local dependencies and relief |
| October 19 paired supplies | 4 | 14/96 (14.6%) | Paired kits and connected rope work |
| November 14 refinement, included in `4ccb755` | 5 | 83/96 (86.5%) | Spaced ladders, route-time dispatch and connected roofs |
| Visual overhaul `4ccb755` | 5 | 78/96 (81.3%) | Character/site improvements with physical winch setup |
| Independent ropes `035220d` | 5 | 93/96 (96.9%) | Previous benchmark and behavior above |
| Site recovery (worktree) | 5 | 89/96 (92.7%) | Current physical-handling tradeoff above |

The version-4 to version-5 comparison changed world grammar as well as
scheduling, so it was not a controlled scheduling-only A/B. The same root may
select a different world. A root/version/attempt needs its matching source
revision for historical replay.

Version 5 already corrected the old overlapping side-ladder mechanism:
generation chooses cleared permanent/predefined temporary-access columns,
omits redundant overlapping side routes, connects upper bridges to roofs, and
complete validation rejects unreachable permanent platforms. The old diagnosis
that every long ladder serializes the crew no longer applies: permanent
ladders support same-direction following at 20-unit spacing; opposing traffic
still waits. Mandatory-tender delays and the half-crew builder cap are removed.

The previous version-5 acceptance batch (unchanged generator in `035220d`)
tested 4,096 roots: 4,124 attempts, 12 preliminary clearance failures, 16 complete
spawn-connectivity failures, no navigation-derivation failures, fallbacks or
hard failures. Passing family counts were 864/833/790/802/807 in enum order.
Navigation used 71-106 of 112 nodes, averaging 91.92; the raw-candidate graph
batch used 74-117 of 192 edges. These are recorded measurements, not a new batch
run for this documentation update. Do not raise capacities merely for headroom.

## Current diagnostics and unresolved questions

The historical standalone `building_clock_bottlenecks.md` was removed because
its version-4 timelines, mandatory-tender analysis and line references are
superseded. The diagnostic executable remains; no instrumentation was deleted.

Build the assertions-enabled host targets and reproduce the current scenarios:

```sh
cmake -S components/building_clock/test -B build/building_clock_tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/building_clock_tests
ctest --test-dir build/building_clock_tests --output-on-failure
build/building_clock_tests/building_clock_throughput_tests 32

mkdir -p build/builders_diagnostics
build/building_clock_tests/building_clock_bottleneck_trace single build/builders_diagnostics/single
build/building_clock_tests/building_clock_bottleneck_trace rollover build/builders_diagnostics/rollover
```

A final trace argument overrides root `0x6a09e667f3bcc909`. Pass `-` instead of
an output prefix for summary-only output. Each prefix produces
`-workers.csv`, `-sections.csv` and `-jobs.csv`. The trace records worker
position/state, job subject, load/companion IDs, rope phase, blocking resource
and owner mask, lifecycle and section milestones, and pending-job gates.
Stdout includes geometry, completion time and per-worker reason totals.

Wait labels are read-only post-step observations at 50 ms resolution, not an
internal scheduler event stream. A resource released later in the same quantum
may appear as `retry_next_quantum`; `dispatch_choice` deliberately does not
invent a hard blocking cause. Job slots are reusable: identify work by timestamp
and semantic subject, not slot ID alone. Worker/digit IDs are zero-based. Each
sample is compared against an unsampled byte-identical control and the world
is checked for mutation.

Remaining questions for future tasks, not fixes authorized by this handoff:

- Real transport, opposing ladder traffic, rope-lane contention and dependent
  final sections still determine outliers. Early first-brick time can regress
  even while full completion improves; see the reference timeline above.
- Single-digit useful contributions remain limited across the full
  lifecycle. There are still spectators and temporary cache handoff clusters,
  despite the much lower roof occupancy in the measured reference seed.
- Broad-area crews can pass through one another; there is no continuous crowd
  collision system. Narrow work sites and temporary access remain exclusive.
- All permanent platforms must be reachable, but reachability is not proof of
  useful visits. Version-4 zero-visit platform IDs are not current-world
  evidence; new purpose/visit measurements would be needed before redesign.
- Cranes/lifts remain parked. Trolleys now recover rubble; they do not yet
  deliver structural kits or carry workers. Bays without a compatible route
  retain stored cargo. Broad-platform passing is not general collision physics.
- This batch meets the single-digit target, not the multi-digit one-minute
  artistic goal. Keep safe unfinished rollover rather than forcing completion.
- ESP32 runtime cost and final renderer performance remain unmeasured on
  hardware. Host timing and screenshot inspection do not establish them.

For viewer commands and controls, use
[the host README](../components/building_clock/host/README.md).
