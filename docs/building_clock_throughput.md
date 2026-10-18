# Builders throughput measurements

Uncommitted refinement of `f6e7a16`. Latest verification: 2026-10-19.
This improves throughput substantially but does not meet the requested 90% of
single-digit rebuilds within 60 seconds. Viewer review is still needed before
expanding the feature set. Historical checkpoint results follow the new results.

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
