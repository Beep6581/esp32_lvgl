# Clock - Builders construction bottlenecks

Diagnosis at `d89c6e7`, 2026-11-14. No gameplay changes. The historical batch in
`building_clock_throughput.md` remains the distribution baseline; this report
examines complete individual sequences and their actual blocking conditions.

This is the historical version-4 investigation, not a description of the
current version-5 implementation. The subsequent fixes, before/after batch,
remaining limitations and current replay caveats are recorded in
`building_clock_throughput.md` (2026-11-14). Current traces recognize spaced
ladder admission as edge contention even when the owner mask is below capacity.

## Reproduction and evidence

Root `0x6a09e667f3bcc909`, generator version 4, attempt 0, twin towers,
101 navigation nodes / 109 edges. Six initial workers; normal relief policy.
Both viewer and trace advance in 50 ms quanta. Times below are simulated seconds
since the new target was supplied, not seconds since collapse finished.

```sh
cmake -S components/building_clock/host -B build/building_clock_viewer
cmake --build build/building_clock_viewer
./build/building_clock_viewer/building_clock_viewer --seed 0x6a09e667f3bcc909
# Press 2 for a freshly reset 14:59 -> 15:00 scenario.

cmake -S components/building_clock/test -B build/building_clock_tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/building_clock_tests
build/building_clock_tests/building_clock_bottleneck_trace single build/builders-diag-single
build/building_clock_tests/building_clock_bottleneck_trace rollover build/builders-diag-rollover
```

Each trace emits `PREFIX-workers.csv` (every worker every 50 ms),
`PREFIX-sections.csv` (section changes), and `PREFIX-jobs.csv` (job changes).
These include position, job/subject, carried load/companion IDs, rope phase,
blocking resource/owner mask, and pending-job dispatch gates. Stdout contains
geometry, lifecycle times, section milestones and per-worker wait totals.
Optional final argument overrides the root seed. Job slots are recycled: use
timestamp plus subject, not job ID alone. Worker and digit IDs are zero-based.

The analysis used these full traces plus 32 SDL captures through completion:
single every 5 seconds and rollover every 10 seconds, including endpoints.
Captures are in `/tmp/builders-diag-captures/`. For example:

```sh
SDL_VIDEODRIVER=dummy build/building_clock_viewer/building_clock_viewer \
  --seed 0x6a09e667f3bcc909 --scenario rollover --duration-ms 168750 \
  --screenshot /tmp/builders-diag-captures/rollover-end.png
```

## 14:37 -> 14:38: why two roof workers wait

Collapse begins at 1.00, clearing begins at 3.00, clearing finishes at 11.80.
First brick: **27.45** (24.45 after collapse/settling ends). Final section secured:
61.35; stable: **63.75**. No relief workers enter; no route failures occur.

| Worker | Initial useful work and arrival | Confirmed wait / subsequent role |
| --- | --- | --- |
| W0 | Remains at ground spawn x=52 until 21.60 | Dispatched as lower-right tender; reaches roof at 32.75, then tends |
| W1 | Clears rubble at 10.25-10.95; takes top kit at 16.80 | Ladder queue 17.80-22.55; roof at 27.75; stages 28.30-32.30; becomes left tender at 32.35 |
| W2 | Carries right-pier kits; ladder queue 8.20-12.20 | Roof at 17.40; stages 17.50-21.50; builder waits for W0 at 21.55-32.75, then rope lane until 34.75 |
| W3 | Carries left-pier kits | Ladder queue 7.35-17.40; roof at 22.55; stages 23.10-27.10; builder waits for W1 at 27.15-32.35 |
| W4 | First courier; roof at 12.20 | Stages 12.75-16.75; foundation builder waits for W5 at 16.80-22.65; first placement at 27.45 |
| W5 | Clears rubble, then waits for dispatch | Becomes foundation tender at 16.80; roof at 22.20, ready at 22.65; tends until 34.90 |

The first two roof arrivals are **not interchangeable idle crew**. They are
couriers finishing four-second staging actions, then become separately reserved
builders. Their tenders are committed elsewhere or travelling from ground level.
The third roof arrival is W5 at 22.20; it starts tending at 22.65. There is no
local handoff/re-pairing when another worker reaches the roof.
See [assignment](../components/building_clock/building_clock_simulation.c#L552),
[tender dispatch](../components/building_clock/building_clock_simulation.c#L814)
and the descent readiness check in
[rope movement](../components/building_clock/building_clock_simulation.c#L1324).
Staging uses `FINISH_JOB`, which has only the generic standing pose in
[the viewer](../components/building_clock/host/building_clock_viewer.c#L449).
Thus a stationary courier can be performing its four-second action rather than
waiting; the trace distinguishes these cases.

### Section timeline

"Ready" means supports secured; it does not bypass clearing. "Assigned" is the
first builder reservation (paired kits can reserve future work before it is
ready). All seven supply jobs are allocated at 3.05. Values <=0.05 mean ready at
the first sample. Installation is shown as first brick through fully placed.

| Section | Ready | Supplied | Assigned | Installing | Secured |
| --- | ---: | ---: | ---: | --- | ---: |
| Center footing | <=0.05 | 16.75 | 16.80 | 27.45-30.35 | 31.05 |
| Lower left | <=0.05 | 27.10 | 27.15 | 36.85-40.70 | 41.40 |
| Lower right | <=0.05 | 21.50 | 21.55 | 39.70-44.35 | 45.05 |
| Upper left | 41.40 | 27.10 | 27.15 | 41.85-44.30 | 45.00 |
| Upper right | 45.05 | 21.50 | 21.55 | 46.10-48.65 | 49.35 |
| Middle | 45.05 | 16.75 | 45.10 | 50.60-53.30 | 54.00 |
| Top | 49.35 | 32.30 | 49.40 | 53.35-60.65 | 61.35 |

The decisive branch is right-pier supply -> remote tender -> lower right ->
upper right -> top -> securing/retrieval -> promotion. The top's rope waits
another 1.45 seconds at 49.40 and 1.95 at 54.85 for occupied lanes. Finishing
inspection completes at 62.30; final rope retrieval keeps the project alive
until 63.75. Late pacing minima are already satisfied; they are not the delay.

There is **no hard two-builder limit**. Six workers permit three builder jobs,
and three are assigned at 27.15, but one is waiting for a tender and then a lane.
At most two builders are suspended simultaneously in this run. At 35.00 W4/W5
have no pending job; at 61.00 four workers have none. Already-owned sections and
support dependencies prevent them from joining another worker's section.
See [builder/dispatch limits](../components/building_clock/building_clock_simulation.c#L572)
and [section scheduling](../components/building_clock/building_clock_simulation.c#L2528).

## 14:59 -> 15:00: stationary loads and the completion tail

Three digits change. Relief enters at 4.55/6.05/7.55, reaching nine workers.
First brick: **32.90**; final masonry secured: **151.85**; stable: **168.75**.
All requested worker routes exist; route-failure count is zero.

| Interval | Worker / work | Confirmed cause |
| --- | --- | --- |
| 25.15-48.15 | W5, digit 1 footing, carrying | Tender W1 travels from the other roof via ground |
| 30.40-56.05 | W4, digit 1 lower pier, carrying | Tender W2 also crosses between roofs; lane adds 5.75 s afterward |
| 40.20-106.20 | Digit 2 upper-right supply | Job pending until 70.15; pickup at 76.25; W7 then queues 81.05-96.50 before staging completes |
| 81.80-101.65 | W8, carrying digit 3 upper-right kit | Capacity-one ladder 93; several preceding users, not a missing destination |
| 106.25-126.20 | W7, digit 2 upper-right kit | Tender W3 travelling between roofs |
| 110.95-136.15 | W8, digit 3 upper-right kit | Tender W1 travelling between roofs |
| 112.40-132.15 | W2, digit 1 top kit | Tender W5 travelling between roofs |
| 142.55-163.30 | W3, digit 1 inspection | Roof-to-roof trip; the left-roof crew becomes free shortly after dispatch |
| 145.55-168.20 | W2, digit 2 inspection | Opposite roof-to-roof trip; right-roof workers become idle while W2 continues |

The late critical branch runs through digit 3's delayed right-pier supply and
tender, but **the final completion is determined by digit 2's inspector**.
Digit 3 is already stable at 154.25; the last two inspections leave it waiting
another 14.50 seconds for the whole clock to become stable.

Assignment favors current free workers using Manhattan proximity, with route
cost choosing among jobs for each worker. It does not globally compare worker
arrival times, wait briefly for a soon-free local crew, or reassign a distant
reserved inspector/tender. A small gap between roofs therefore causes ~20-second
detours. This is confirmed travel, not failure to find a route.

## What dominates

Worker-seconds over each complete run (parallel waits must not be added to
elapsed completion time):

| Activity / blocking reason | Single | Rollover |
| --- | ---: | ---: |
| Graph travel | 59.85 | 431.85 |
| Rope travel | 53.20 | 167.15 |
| Waiting for tender | 22.25 | 136.65 |
| Waiting for edge | 18.75 | 135.70 |
| Waiting for rope lane | 7.20 | 5.75 |
| Waiting for destination node | 0.35 | 12.05 |
| Idle: no pending job | 84.40 | 270.85 |
| Idle: dispatch capacity gate | 31.65 | 25.35 |

Most "construction activity" is support/travel: actual placement plus securing
accounts for 11.80/29.75 worker-seconds, versus tending 72.50/203.60 and staging
16.00/64.05. Broad roof occupancy is not the dominant restriction. In rollover,
ladder 93 alone accounts for 112.15 worker-seconds of edge waits.

## Root causes and ranked corrections to consider

These are measured opportunities or explicitly conditional estimates, **not
speedups demonstrated by modified simulations**. They overlap and are not
additive. No corrections were implemented.

1. **Coordinate workers by actual route time and roof locality, including
   tender pairs and final inspection.** Individual tender stalls are 5.85-11.20 s
   in single and up to 25.65 s in rollover. Reusing nearby support could remove
   part of these stalls. Local inspection could plausibly eliminate much of the
   14.50 s rollover completion tail; exact recovery requires testing a policy.
2. **Permit spaced, same-direction ladder following.** Every permanent ladder
   becomes one capacity-one edge, reserved for its whole length. Ladder 93 is
   258 units long: 5.16 s per climber. W3 waits 10.05 s while later-arriving,
   lower-ID W2 gets the next slot. A hypothetical 12-unit spacing is 0.24 s at
   current climb speed, rather than a 5.16 s entry headway: potentially ~4.9 s
   per queued follower. Opposing traffic needs direction control. Raising the
   capacity alone would allow overlapping workers. See
   [ladder edges](../components/building_clock/building_clock_navigation.c#L376),
   [reservations](../components/building_clock/building_clock_runtime_navigation.c#L272),
   [entry](../components/building_clock/building_clock_simulation.c#L1055) and
   [release](../components/building_clock/building_clock_simulation.c#L1126).
3. **Reduce courier/builder/tender role churn and supply dispatch serialization.**
   The two-deliveries-per-target gate holds idle workers at ground level even
   while another kit is pending. First roof arrival then consumes four seconds
   staging before requesting support. Single's first brick is 10.70 s after
   its first kit is supplied. Multi's upper-right kit takes 65.35 s beyond its
   support-ready time to arrive. Crew capacity, route queues and the eight-load
   budget all contribute; extra load slots alone would not fix these delays.
   See [supply limits](../components/building_clock/building_clock_simulation.c#L2391).
4. **Offer distinct cooperative work within long owned sections.** Late idle
   crew usually have no unassigned job, not an inaccessible one. Explore useful
   bounded work groups/positions rather than weakening legitimate support
   dependencies. The top owns a 7.30 s installation interval while spare crew
   watch; achievable recovery is not yet quantified. Rope lanes are a secondary
   single-digit restriction, not the main multi-digit problem.

For this seed, only 3.75 s must be recovered to cross 60 s, but this says nothing
about the 90% batch target. The previous 32-world distribution must be rerun
after any chosen correction.

## Geometry findings deferred to generation work

Twin-tower ladders at x=29 and x=440 are split by intermediate ledges. Later
`add_side_ladders()` adds direct ladders at x=34 and x=444. The SDL ladder width
is nine units, so these records visibly overlap by several units; segmented
records also touch at their common landing. The
[generator order](../components/building_clock/building_clock_generator.c#L327)
and [ladder validator](../components/building_clock/building_clock_validation.c#L220)
contain no pairwise ladder-clearance rule. Future clearance checks must cover
permanent/permanent, permanent/temporary and temporary/temporary access. No
temporary ladder is instantiated in these two runs, so that case is not an
observed failure here. Removing the overlapping parallel route without a
concurrency policy may worsen queues.

Highest platforms 5 (x=25..132,y=64) and 6 (x=335..424,y=97) are reachable by
permanent worker routes but get **zero visits** in both scenarios. The nearer,
lower roof wins construction access selection. Thus "unused" is confirmed;
"inaccessible" is disproven for this seed. Record purpose/use of optional upper
girders as future world-generation work; no redesign was attempted.

## Verification and limits

All 13 host suites pass, including two new full-transition trace tests. Every
sample is checked against an unsampled byte-identical replay, and the world is
checked for mutation. Strict host compilation, formatting, ASCII and whitespace
checks pass. SDL builds and complete sequence captures pass. The ESP-IDF build
passes (the first attempt needed access to the existing external component
cache). Production simulation and viewer sources are unchanged; firmware
memory/capacities are unchanged.

Wait labels describe post-step resource state, so a resource released later in
that quantum can show `retry_next_quantum`. Section/job events have 50 ms
resolution; very short intra-quantum states can be missed. `dispatch_choice`
deliberately leaves preference/order uncertain instead of claiming a hard block.
No counterfactual policy was run. Net savings, starvation under long mixed ladder
traffic, and cross-seed prevalence of these exact critical paths remain unmeasured.
