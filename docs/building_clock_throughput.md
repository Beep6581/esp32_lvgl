# Builders throughput measurements

Current implementation: uncommitted living-site and functional-crane refinement after `3ead7c4`
on `buildclock`; reviewed 2026-11-16.
This file owns measured performance, diagnostic reproduction and remaining
bottlenecks. The [specification](building_clock_simulation.md) owns architecture
and planned behavior; the [visual overview](building_clock_visual_overhaul.md)
owns presentation status. Older numbers below are explicitly historical.

## Current check: independent landing (generator v6)

Continuous-mode regression check: seed `f9fd2ded6d5b25c6`, 14:38 through 15:17,
one new target every 60 simulated seconds without resetting worker state.
Previously the crane performed one lift and then parked at the delivery end;
later estimates included its unperformed empty return and rejected further use.
Automatic empty return now retracts vertically and parks over the loading
position. The same run performs 45 lifts in 39/40 minutes, including three
at 15:00. A continuous replay/ownership/safety test covers the full run.
Artifacts: `build/crane_continuous/before.txt`, `after.txt`, `tests-and-batch.txt`.
The viewer now documents keyboard 4/M and has proper --help/-h plus the
--scenario continuous alias; invalid scenario names no longer silently run single.
The 32-root rerun still meets 90.6% fresh/after-quiet single-digit completion
within 60 seconds. The independent-landing timing table below predates the
empty-return correction; the latest raw batch is `tests-and-batch.txt` above.

No roof builder is reserved or required for a delivery. Its unpack job stays
BLOCKED during flight and becomes PENDING only after safe landing. The ordinary
four-second unpack action can then happen later without tying up the crane.
Receiving-crew checks, clearing/project penalties and the special single-kit
four-second saving threshold are removed. Actual route/motor costs, a small
operating allowance and a common 1.2-second benefit margin remain.

Same 32 roots, 512 runs, adaptive six plus relief:

| Scenario | Previous auto mean | Independent landing mean | Crane lifts, before -> after |
| --- | ---: | ---: | ---: |
| 7 -> 8 | 50.730 s | 51.845 s | 6 -> 6 |
| 1 -> 2 | 51.031 s | 51.395 s | 6 -> 6 |
| 8 -> 9 | 48.964 s | 49.252 s | 0 -> 6 |
| 14:59 -> 15:00 | 97.214 s | 94.998 s | 5 -> 25 |
| Midnight | 143.138 s | 141.297 s | 1 -> 33 |

Seven worlds in each multi-digit batch now have multiple crane operations.
Root `f9fd2ded6d5b25c6` improves 105.55 -> 102.30 s on rollover and
143.40 -> 136.70 s at midnight. Single `7 -> 8` loses the pre-positioned receiver:
41.30 -> 46.10 s, still faster than manual-only 47.90 s. This tradeoff lowers
fresh and after-quiet single-digit <=60 s counts from 89/96 to 87/96 (90.6%).
No worker speeds, existing actions or capacities change.

Focused checks: crane cancellation/replay/reuse tests, an in-flight assertion
that no receiver is reserved, the 32-root A/B and an SDL rollover sequence.
Artifacts: `build/crane_independent/after32.txt`, `focused-tests.txt`, `sequence/`.
The older full-suite results below were not rerun for this focused change.

## Previous check: crane retraction and horizontal girders (generator v6)

The crane now retracts its empty hook automatically after delivery or safe
abort/offload. Crew/job/cargo claims are already released during retraction;
dispatch becomes available again when the hook reaches overhead clearance.
The motor speed, worker speeds and construction action durations are unchanged.
An explicit replay test uses the same crane for two deliveries/four kits across
five target changes without reinitializing the world or simulation.

Permanent vertical-support generation and drawing are removed, rather than
merely hidden. Temporary access uses yellow ladders with horizontal blue decks.
This changes generated geometry and random consumption, so the generator and
validated fallback advance to version 6 (fallback root 2, attempt 0).
Root/version-specific rejection and five-family test fixtures were re-established.
Version-5 timing tables below remain historical, not identical-world comparisons
with version 6.

The same 32-root, eight-scenario, two-mode test (512 runs) passes with no timeout.
Current fresh single-digit results are manual **87/96**, crane-enabled **89/96
(92.7%)** within 60 s. After quiet activities they are **88/96 -> 89/96 (92.7%)**.
Within-version mean times in seconds:

| Transition | Manual | Crane enabled |
| --- | ---: | ---: |
| 14:37 -> 14:38 | 51.561 | 50.730 |
| 14:31 -> 14:32 | 51.455 | 51.031 |
| 14:38 -> 14:39 | 48.964 | 48.964 |
| 14:59 -> 15:00 | 97.423 | 97.214 |
| 23:59 -> 00:00 | 142.902 | 143.138 |

The crane remains selective: repeat-capable does not mean every next delivery
should use it. Six `7 -> 8` and six `1 -> 2` worlds use it; `8 -> 9` prefers
manual delivery throughout this batch. One repeated-target test explicitly proves
two actual operations rather than requiring decorative trips.

Artifacts are in `build/crane_refinement/`: `before32.txt` (version 5),
`after32.txt` (version 6 manual/auto pairs), `crane-trace.txt`, `tests-final.txt`,
`sanitized-tests.txt`, `retract-contact.png`, and complete PNG sequences.
Host tests and strict native/ESP32-S3 component compilation pass. A full firmware
build was attempted but is blocked by DNS fetching the existing locked GitHub
touch dependency. Disabling the component manager also omits its transitive
header dependencies, so that is not a valid replacement build. No dependency,
LCD, timing or SDK configuration changes are retained.

## Previous check: functional crane versus manual transport (generator v5)

The control is the living-site implementation below with new crane dispatch
disabled, not the older pre-upkeep commit. Generator v5, world, six-plus-relief
crew policy, time inputs, worker speeds and existing action durations are
identical within each pair. Thirty-two roots use
`0x6a09e667f3bcc909 + root * 0x9e3779b97f4a7c15`, with unsigned 64-bit wrap.
Eight scenarios and two modes give 512 complete runs; none timed out or failed
evacuation. Automatic transport remains mostly manual by design.

| Scenario | Manual mean | Auto mean | Worlds using crane | Kits unpacked |
| --- | ---: | ---: | ---: | ---: |
| 14:37 -> 14:38 | 52.595 s | 51.402 s | 10/32 | 20 |
| 14:31 -> 14:32 | 52.467 s | 52.227 s | 8/32 | 16 |
| 14:38 -> 14:39 | 50.109 s | 50.033 s | 1/32 | 2 |
| 14:59 -> 15:00 | 98.931 s | 99.111 s | 6/32 | 13 |
| 23:59 -> 00:00 | 145.706 s | 145.584 s | 1/32 | 1 |
| Quiet -> 14:37 -> 14:38 | 52.758 s | 51.695 s | 10/32 | 20 |
| Quiet -> 14:31 -> 14:32 | 52.012 s | 52.008 s | 8/32 | 16 |
| Quiet -> 14:38 -> 14:39 | 50.409 s | 50.395 s | 1/32 | 2 |

Fresh single-digit completion within 60 seconds remains **88/96 (91.7%)**.
After 15-22.75 seconds of quiet activities it improves from **87/96 (90.6%) to
89/96 (92.7%)**. Fresh aggregate mean improves 51.724 -> 51.220 s; after quiet,
51.727 -> 51.366 s. Fresh p90 is 58.55 -> 58.50 s, maximum 63.50 -> 65.40 s;
after-quiet p90 is 59.40 -> 58.20 s, maximum remains 64.65 s.

For the ten fresh `7 -> 8` worlds actually using the crane, complete-transition
mean improves **53.125 -> 49.305 s (7.2%)**, with no regressions in that subset.
This is the clearest demonstrated benefit, not a claim of a 7.2% improvement
everywhere. The other two used single-digit subsets improve 54.538 -> 53.575 s
and 49.750 -> 47.300 s. Quiet used `7 -> 8` improves 53.670 -> 50.270 s.

The heuristic is not a future-critical-path oracle. Two fresh `1 -> 2` worlds
regress by 0.60/5.10 s. Busy three-digit rollovers average 0.18 s slower overall;
four of its six crane-using worlds regress by 2.55-6.40 s. Five after-quiet used
cases regress by 0.75-6.05 s. Receiving-crew assignment changes later clearing,
rope and inspection order. The policy already forecasts outgoing clearing work
and charges crew opportunity, but those are estimates, not a guarantee of a
positive end-to-end saving for every selected lift. Increasing crew or speeding
workers was not used to hide this limitation. Multi-digit scheduling remains
the most useful next performance refinement; safe unfinished rollover remains.

### Cost and verification

Native and installed ESP32-S3 compiler layouts agree:

| Type | Living-site baseline | With crane | Change |
| --- | ---: | ---: | ---: |
| Worker | 308 B | 308 B | 0 |
| Mutable state | 9,992 B | 10,016 B | +24 B |
| Simulation context | 10,864 B | 10,912 B | +48 B |
| Scene snapshot | 10,768 B | 10,808 B | +40 B |

State growth is included in context growth, not additive. A separately retained
snapshot adds 40 B. There are no new heap allocations or capacity increases.
The earlier living-site changes were already uncommitted when crane work began;
the combined worktree versus `3ead7c4` adds 376 B to the context and 368 B to a
retained snapshot. The table isolates the crane's additional cost.
An active lift needs one extra job slot and uses existing one/two kit records;
the established 480-case test peaks at 31/48 jobs and 8/8 load slots. Full queues
fall back/backpressure instead of dropping cargo. The target `-O2` compiler
reports a 1,216 B individual `assign_job` stack frame with inlined planning;
this is not the full call-chain/task-stack requirement. A single route scratch
buffer is reused for the bounded cost queries.

One optimized native 512-run comparison uses 11.734 s process CPU for all manual
runs versus 11.870 s automatic, about +1.2%; normalized by simulated duration,
661 versus 671 microseconds per simulated second (+1.6%). These include host
ownership assertions, omit SDL/file rendering, and are noisy host measurements,
not ESP32 performance or a hardware stack measurement.

Six in-flight stages are interrupted by delivery cancellation, operator
cancellation and new-minute input. Tests also cover landed cancellation/reuse,
recovered loading-area stock, contention, insufficient reach/manual fallback,
bounded axis movement and byte-identical replay. Four worlds additionally
receive seven unfinished target changes each, then finish without resetting.
The 16-test normal and ASan/UBSan suites, 480-case existing throughput test,
viewer animation check, strict compilation and firmware build pass. No flashing
or real-device runtime measurement was performed.

Artifacts: `build/builders_crane/benchmark.txt`, `benchmark-errors.txt`,
`throughput.txt`, `final-trace.txt`, `crane-final.txt`, `tests-final.txt`,
`sanitized-final.txt`, and `idf-build-final.log`.

```sh
cmake -S components/building_clock/test -B build/building_clock_tests
cmake --build build/building_clock_tests
build/building_clock_tests/building_clock_crane_tests 32
build/building_clock_tests/building_clock_throughput_tests 32
```

The crane test's optional `trace ROOT SCENARIO` arguments produce timestamped
worker/rigging traces on stderr without changing simulation behavior.

## Baseline retained: living stable-minute site

The established 480-run benchmark (32 roots, five transitions, three crew
policies) retains the committed completion times: adaptive six plus relief is
**88/96 single-digit cases within 60 seconds (91.7%)**, with no timeout or
evacuation failure. Traversal speeds, construction durations, generator v5,
clock geometry and capacities are unchanged. See `throughput-final.txt` under
`build/builders_living_site/` for the full result.

A second 96-case diagnostic starts the same three single-digit changes after
15-22.75 seconds of real quiet-site activity, rather than from the reset state.
It exercises tool drop, meeting cancellation, actual return walking and routing.

| Transition | Reset baseline <=60 s | After quiet work <=60 s |
| --- | ---: | ---: |
| 14:37 -> 14:38 | 29/32 | 28/32 |
| 14:31 -> 14:32 | 28/32 | 29/32 |
| 14:38 -> 14:39 | 31/32 | 30/32 |
| Total | 88/96 (91.7%) | 87/96 (90.6%) |

The after-quiet mean is 51,726 ms, maximum 64,650 ms. There are no timeouts or
evacuation failures. One scenario alone is below 90%; the overall measured
single-digit goal is met, not guaranteed for every seed/starting position.
No artificial worker speedup or material teleport was used to recover time.

### Stable-site observations

Each family was traced and captured for 180 simulated seconds, sampling worker
positions every 250 ms. The baseline binary uses the old viewer's exact two-job
demo; the revised viewer supplies only time/targets, and activity is autonomous.
"Task" means an explicit job or a local upkeep/social round, including its travel,
fetch/return and tea phases; this is not a construction-productivity percentage.

| Family / seed | Mean workers on task, before -> after | Longest unmoved, before -> after |
| --- | ---: | ---: |
| Split/bridge / 6a09e667f3bcc909 | 1.54 -> 3.97 | 52.75 -> 9.25 s |
| Twin towers / 02ce5c58e42372d7 | 1.35 -> 3.96 | 180 -> 8.75 s |
| Central machinery / 7bac433ee14d632b | 1.54 -> 4.03 | 56 -> 15.5 s |
| Continuous lower / af4a0846691a0e44 | 1.53 -> 3.93 | 142.25 -> 24.75 s |
| Asymmetric loop / f9fd2ded6d5b25c6 | 1.58 -> 4.01 | 71.5 -> 8 s |

Mean close worker pairs per sample (|dx|<12, |dy|<8) fall from 0.235 to 0.052
over the five worlds. No stationary close pair was sampled after the change;
brief walking crossings still occur and use broad-platform depth/shoulder poses.
The finer 50 ms replay tests average 3.98 workers on task, record 51 complete
dust-mask cleanups and 9,601 tool-motion quanta across five 180-second runs.
All activity kinds appear and replay is byte-identical. These sample intervals
measure different things; movement counts from 50 and 250 ms are not comparable.

### Fixed cost and verification

Both native and installed ESP32-S3 compiler layouts measure:

| Type | Before | After |
| --- | ---: | ---: |
| Worker | 304 B | 308 B |
| Mutable state | 9,664 B | 9,992 B |
| Simulation context | 10,536 B | 10,864 B |
| Scene snapshot | 10,440 B | 10,768 B |

State/context growth is the same 328 B, not additive. A separately retained
snapshot adds another 328 B. Eighteen 16-byte stations and nine worker extensions
fit existing limits; no extra job/load/debris/nav capacities or heap allocations.
The isolated target upkeep object at `-O2` is 4,303 text bytes and no global
data/BSS. Its largest compiler-reported individual stack frame is 128 B; this
is not a measurement of full task stack or firmware render/FPS cost.

Twenty paired `-O2` host trace runs (180 simulated seconds each, no rendering or
CSV file output) average approximately 17 ms process CPU before and 2 ms after.
This compares whole quiet-site policies: removing repeated cross-world demo-job
planning accounts for much of the reduction. It does not show an eightfold
improvement to construction or predict ESP32 FPS. Host summary precision is 1 ms.

The normal host suite now has 15 tests, including all upkeep phases, tool
ownership/movement, pair cleanup, interruption idempotence, valid snapshots,
real-platform bounds, deterministic replay and post-quiet rollovers. The viewer
animation check, strict runtime compilation and ESP-IDF build are also exercised.
Artifacts, worker CSV traces, sanitizer/build logs and full sequences are under
`build/builders_living_site/`. The trace reproduction command is in the host README.

## Previous committed check: collapse sampling and drawing

The committed approach was already correct architecturally: simulation updates
section groups, never individual bricks. This refinement retains that approach,
the same fall-duration limits, and the same random draws. Bricks remain transient
geometry; neither their identities nor physics are maintained between frames.

The full 480-run throughput log is identical to `274ab1a`, excluding the size
line: **88/96 (91.7%)** single-digit runs stable within 60 seconds, no timeouts or
evacuation failures. The following completion-time table remains current.

### Measured cost

Isolated host tests use GCC 16.2.1, `-O2`, no LTO and process CPU time. Values
are medians of three repetitions, not ESP32 frame timings. Geometry uses 49
occupied numeral/section fixtures, including partial masonry, 10,000 rounds
(490,000 calls per mode per repetition). Draw stress tests use SDL software
rendering at 480x480, 5,000 frames: either 20 falling groups or 32 settled piles.
They include clear/present and collapse drawing, not the rest of the game.

| Measurement | Committed | Revised | Difference |
| --- | ---: | ---: | ---: |
| Simulation context | 10,792 B | 10,536 B | -256 B |
| Mutable state | 9,920 B | 9,664 B | -256 B |
| One semantic debris record | 32 B | 24 B | -8 B |
| One falling section | 36 B | 36 B | unchanged |
| One transient brick sample | 20 B | 20 B | unchanged |
| Active geometry, ns/section | 255.0 | 256.0 | essentially unchanged |
| End-of-fall geometry, ns/section | 225.9 | 237.5 | +5.1% |
| Static rubble geometry, ns/section | 236.0 | 198.0 | -16.1% |
| Active draw stress, us/frame | 164.2 | 169.9 | +3.5% |
| Static rubble draw stress, us/frame | 218.5 | 215.8 | -1.2% |

The context and state rows describe the same saving, not additive savings.
A separately retained scene snapshot also copies 256 fewer bytes. No heap or
entity-capacity changes were made. Dynamic samples still reuse one 24-entry
array; a saturated scene does not allocate one array per section.

Cross-compilation with the installed ESP32-S3 GCC 15.2.0 at `-O2` confirms the
same type sizes. Compiler-reported geometry stack frames shrink from 400 to
304 bytes for the falling sampler and 576 to 80 bytes for the settled sampler.
The latter no longer calls the falling sampler or allocates its 480-byte output.
These are individual function frames, not total task stack measurements.
The target assembly removes up to three `__udivdi3` calls per active brick,
replacing them with at most one per section before the brick loop. The isolated
geometry object text is 1,994 -> 1,922 bytes; this is not a whole-firmware size
comparison. No on-device cycle/FPS measurement is claimed.

The small draw regression buys independent subtle tilts, not a physics system.
The SDL implementation uses a 42-byte read-only span atlas and one batched
body draw per tilted brick; an initial unbatched version was about 70% slower
in the active draw stress and was discarded. Batching requires a bounded
14-rectangle (224-byte) temporary on the host. Host `-Os` frame inspection shows
the inlined draw function growing 592 -> 848 bytes; this SDL-specific tradeoff
is separate from the smaller target geometry frames and persistent-state saving.
Future firmware can blit the same tiny poses directly instead of adopting SDL
rectangles or renderer resources.

Normalized phase/Q8 arithmetic is deliberately not pixel-identical mid-flight.
576,640 old/new brick-position comparisons across all numerals, occupied
sections, full/40%-installed prefixes and every millisecond of a 900 ms fall
found at most 1 horizontal and 2 vertical design-pixel differences. Final pile
positions are identical. The maximum populated section in those shapes is 20
bricks, below the 24-entry safety capacity.

Verification includes all existing host tests plus full-duration angle/position
checks, full uint32 duration boundaries and 15 actual generated-world cases
where a new minute interrupts an active collapse. Outgoing geometry and timing
stay unchanged, and the latest target finishes with deterministic replay and
no leaked transition resources. All 14 tests pass normally and under ASan/UBSan;
the viewer test, strict runtime compilation and firmware build also pass.

Artifacts: `build/builders_collapse_review/` contains `before-cost-final.txt`,
`after-cost-final.txt`, `render-{before,after}-final.txt`, target assembly/stack
reports, `path-delta.txt`, `throughput.txt` and test/build logs. See the visual
overview for the ordered sequences and screenshots.

## Committed baseline: individual-brick collapse

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
| Site recovery, before brick collapse | 5 | 89/96 (92.7%) | Physical-handling tradeoff above |
| Individual bricks `274ab1a` | 5 | 88/96 (91.7%) | Current timing baseline; sampling review preserves it |

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
- The crane now delivers kits opportunistically; lifts remain parked. Trolleys recover rubble but do not yet
  deliver structural kits or carry workers. Bays without a compatible route
  retain stored cargo. Broad-platform passing is not general collision physics.
- This batch meets the single-digit target, not the multi-digit one-minute
  artistic goal. Keep safe unfinished rollover rather than forcing completion.
- ESP32 runtime cost and final renderer performance remain unmeasured on
  hardware. Host timing and screenshot inspection do not establish them.

For viewer commands and controls, use
[the host README](../components/building_clock/host/README.md).
