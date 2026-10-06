# Building Clock Simulation Specification

Status: approved design direction, renderer-independent specification.

This document defines the generated construction world, clock transition
simulation, workers, jobs, materials, machinery, validation, and deterministic
seed behavior for the building clock. It deliberately does not define a
graphics backend, sprite or tile format, framebuffer strategy, dirty-region
algorithm, color encoding, or display synchronization method.

The existing particle clock remains a separate mode. The building clock should
be introduced as a separate application mode so that it can be developed and
tested without changing the particle clock or the display path.

## 1. Design goals

The simulation should produce a small, coherent 1984-style platform and
construction game which happens to display the time.

The important properties are:

- The world is generated once at boot and remains fixed until reboot.
- Different boots should normally produce meaningfully different levels.
- The time occupies fixed, protected digit envelopes.
- Permanent generated geometry must not obstruct those envelopes.
- Workers, debris, loads, temporary ladders, and temporary scaffolding may
  intentionally enter digit envelopes while construction is in progress.
- Workers use the actual generated platforms and ladders rather than a separate
  invented navigation layout.
- Construction activity continues during stable minutes.
- Digit changes are physical collapse and reconstruction events.
- Simulation cost and entity counts remain bounded.
- A logged fixed seed reproduces a world and its behavior in both the simulator
  and firmware.

## 2. Existing project integration

The project already has useful high-level clock infrastructure:

- mutually exclusive application modes;
- a clock task driven by elapsed time;
- `time()` and `localtime_r()` based local-time handling;
- minute and second change detection;
- a separation between simulation state and presentation;
- deterministic pseudo-random behavior; and
- runtime performance logging.

The building clock should reuse those concepts. It should not reuse the
particle mask or extend the particle engine into an unrelated game simulation.

The intended project-owned modules are conceptually:

- world generation and validation;
- navigation graph construction;
- building-clock simulation;
- a renderer-neutral scene snapshot; and
- a future presentation adapter.

These are specific parts of the building clock, not a generic game framework.

## 3. Coordinate regions and invariants

The simulation uses the 480 by 480 display as its design space. Exact visual
dimensions remain tunable, but the initial reserved regions are:

| Region | Initial placement | Rule |
| --- | --- | --- |
| Hour tens | x 60-128, y 120-334 | Permanent generated geometry excluded |
| Hour units | x 136-204, y 120-334 | Permanent generated geometry excluded |
| Colon | x 216-240, y 120-334 | Permanent generated geometry excluded |
| Minute tens | x 252-320, y 120-334 | Permanent generated geometry excluded |
| Minute units | x 328-396, y 120-334 | Permanent generated geometry excluded |
| Date band | x 0-479, y 420-479 | All generated geometry excluded |

Each digit also has a work halo immediately outside its envelope. The halo may
contain permanent access nodes and attachment points, but permanent platforms
must not cross the numeral face.

The exclusion rule applies only to permanent generated level geometry.
Intentional construction activity may enter a digit envelope, including:

- workers;
- carried material;
- crane hooks and suspended loads;
- falling and settled debris;
- temporary platforms and scaffolding;
- temporary ladders; and
- carts or lifts where a generated route permits them.

Temporary objects must still be removed when their job is complete and must not
permanently obscure an unchanged digit.

## 4. Fixed-capacity world description

The generated world is immutable after startup. Use explicit fixed-capacity
arrays and counts rather than runtime allocation or generic containers.

Initial capacity targets are:

| Item | Maximum |
| --- | ---: |
| Permanent platform spans | 18 |
| Permanent ladders | 10 |
| Side towers or major supports | 4 |
| Cranes | 1 |
| Lifts | 1 |
| Cart routes | 2 |
| Material depots | 1 |
| Debris bays | 4 |
| Staging areas | 8 |
| Worker spawn points | 10 |
| Temporary scaffold attachment sets | 8 |
| Navigation nodes | 96 |
| Navigation edges | 160 |

These are safety bounds, not targets. A normal world should be substantially
sparser.

The world description contains semantic geometry and relationships:

- platform spans and their walkable surfaces;
- ladders and the surfaces they connect;
- machinery positions and working ranges;
- cart routes and stops;
- depot, debris, staging, and spawn areas;
- per-digit work-access locations;
- temporary-structure attachment locations;
- navigation nodes and edges derived from the geometry; and
- the accepted seed, attempt number, and generator version.

It contains no renderer-owned data.

## 5. Procedural generation grammar

Generation uses hidden topology families as constraints, not visibly fixed
templates. A topology family defines only relationships such as "there is a
lower route connecting both clock halves" or "the crane side has upper access."
It does not prescribe exact coordinates or lengths.

Useful topology families include:

- a continuous lower service route with two asymmetric upper branches;
- two lower routes connected by a raised bridge;
- an asymmetric loop with one tall side tower;
- two side towers joined by staggered interior ledges; and
- a low central route with machinery access from one upper side.

For each candidate world, the generator performs these steps:

1. Choose a topology family.
2. Choose compatible vertical levels within safe bands.
3. Generate platform spans with randomized endpoints and gaps.
4. Assign left/right machinery, depot, and rubble responsibilities.
5. Place ladders or a lift so adjacent route levels are connected.
6. Place staging areas and digit work-access points.
7. Generate cart routes on suitable continuous spans.
8. Place worker spawn points on safe, connected surfaces.
9. Add a bounded amount of optional scaffolding and support structure.
10. Derive the navigation graph from the resulting geometry.
11. Validate the complete candidate.

Compatible choices should be randomized independently where possible:

- platform height within a permitted band;
- span length and side inset;
- ladder position along a span;
- left or right crane placement;
- crane reach and staging assignment;
- depot and rubble-bay side assignments;
- one or two cart routes;
- staging-area ordering;
- support style and spacing;
- spawn distribution; and
- optional ledges and scaffold anchors.

The generator should avoid merely selecting one of a few recognizable screens.
Repeated boots should change the silhouette, major routes, vertical rhythm, and
machinery balance while still looking intentionally designed.

## 6. Seed model

Use one root seed for a boot and derive independent deterministic streams for:

- world layout;
- construction scheduling;
- worker behavior; and
- cosmetic incidental events.

Separating the streams prevents an added worker gesture from unexpectedly
changing the generated level.

Normal operation uses a fresh boot seed. Debug operation accepts an explicit
seed. The following values must be logged:

- root seed;
- generator version;
- accepted generation attempt; and
- derived behavior seed, if it is independently configurable.

A seed is reproducible only with the same generator version. When generation
rules change incompatibly, increment that version.

Candidate attempts are derived deterministically from the root seed and attempt
number. Rejecting a candidate therefore remains reproducible.

## 7. World validation

Validation is a separate read-only pass over the finished candidate. It returns
an explicit failure reason. The generator must discard a failed candidate and
try another derived seed; it must not patch arbitrary broken geometry.

### 7.1 Geometry checks

- Every object lies inside the screen design space.
- Permanent geometry does not intersect digit envelopes or the date band.
- Platform spans have useful length and valid support relationships.
- Ladders terminate on real walkable surfaces.
- Machinery does not overlap another permanent object incompatibly.
- Debris bays have free falling space above them.
- Temporary attachment points connect to valid permanent structure.
- All item counts remain below their fixed capacities.

### 7.2 Readability and composition checks

- The four digit envelopes and colon remain visually separated.
- No permanent route crosses a numeral face.
- No dense wall of supports fills the central negative space.
- The upper skyline contains at least one substantial open region.
- Optional decoration stays subordinate to clock masonry.
- The date band remains completely clear.
- Generated geometry leaves a configured minimum proportion of the available
  construction area as black negative space.

The negative-space check measures permanent world geometry only. Temporary
construction may make an active area busier for a limited time.

### 7.3 Navigation checks

- All worker spawns belong to the main connected navigation component.
- The depot, staging areas, debris bays, and machinery controls are reachable.
- Every digit has reachable lower, side, and upper work positions.
- Every debris bay has a route to the depot or a material exit.
- Every cart stop connects to a worker route.
- Narrow ladders and platforms have valid waiting or turn-around nodes.
- A worker can retreat from each digit hazard zone to a safe node.

### 7.4 Construction checks

- Each digit can receive material from at least one staging area.
- Each upper digit section is reachable by ladder, lift, crane, or a valid
  temporary scaffold chain.
- Crane-dependent jobs lie within the generated crane working range.
- Every temporary scaffold sequence has a valid build and removal route.
- Debris from one digit cannot settle in another digit's required foundation.

### 7.5 Retry and fallback

Generation gets a small fixed number of attempts. If all attempts fail, use a
known-valid grammar family with a known-valid seed and log the failure. This is
still an instance of the generator grammar, not a separately hand-authored
level.

## 8. Navigation model

The navigation graph is built from the accepted world description.

Node types include:

- platform endpoint;
- platform junction;
- ladder top or bottom;
- cart stop;
- lift stop;
- digit work position;
- staging position;
- depot position;
- debris position;
- machinery control position;
- spawn position;
- safe waiting position; and
- temporary scaffold attachment position.

Edge types include walk, climb, ride, and temporary connection. Edges record
direction, traversal cost, capacity, and whether carrying a large load is
allowed.

Route finding occurs when a worker accepts or must replan a job, not on every
simulation update. The graph is small enough for a bounded simple search. Common
service routes may be cached after generation if profiling justifies it.

Narrow ladders and ledges have capacity one. Workers reserve them briefly. If a
reservation remains unavailable, a worker may wait, choose a different job, or
turn around. These conflicts create useful unscripted-looking behavior without
general collision avoidance.

Temporary structures add only predefined validated edges. Removing a temporary
structure first requires that no worker is using its nodes or edges.

## 9. Clock and digit state

The simulation stores four current digits, four target digits, and a colon
state. Each digit has an independent lifecycle:

1. `STABLE`
2. `PREPARING`
3. `EVACUATING`
4. `COLLAPSING`
5. `SETTLING`
6. `CLEARING`
7. `FOUNDATION`
8. `STRUCTURE`
9. `RECOGNIZABLE`
10. `FINISHING`
11. `COMPLETE`

Unchanged digits remain stable. A changed digit retains its old construction
plan through collapse and its new plan through reconstruction.

Digits are visually made from many bricks but simulated as a bounded set of
structural sections. Possible semantic sections are:

- foundation or base course;
- top beam;
- middle beam;
- upper-left and upper-right piers;
- lower-left and lower-right piers;
- corner or diagonal sections; and
- small finishing or infill sections.

Each numeral has a section dependency plan. The plan need not look like a
seven-segment display. Several ready sections may be built in different valid
orders, allowing variation between transitions.

A section can be absent, staged, in transit, placed, secured, failed, falling,
or debris. A few loose bricks may accompany a section for visual character, but
the simulation does not account for every brick.

## 10. Build pacing

Pacing is data, not scattered transition constants. A pacing profile defines:

- preparation start during the preceding minute;
- evacuation start;
- collapse duration;
- settling duration;
- debris-clearing target;
- foundation target;
- first-recognizable target;
- major-completion target; and
- finishing target.

An initial normal profile is:

| Milestone | Target |
| --- | --- |
| Begin staging next parts | second 40-45 of preceding minute |
| Begin evacuation | second 56-58 |
| Collapse | first 2-3 seconds after rollover |
| Settle and assess | through second 5-6 |
| Clear debris and establish base | through second 14-18 |
| First recognizable structure | near second 28-32 |
| Major construction complete | near second 42-46 |
| Finishing work complete | near second 52-56 |

The scheduler creates jobs toward these milestones. It does not directly place
sections merely because a timer expired.

If work is behind schedule, the simulation may:

- omit optional carrying trips;
- combine several loose pieces into a prefabricated load;
- source a section from an off-screen delivery point; or
- cancel decorative inspection jobs.

It should not teleport visible workers or run them at implausible speed. If work
is ahead, workers inspect, hammer, wait, or prepare future material.

Multi-digit transitions stagger collapses by a short configurable interval and
increase crews and equipment use. They do not multiply entity counts without a
fixed bound.

## 11. Job system

Jobs are small explicit records in a fixed-capacity queue. A job contains:

- type;
- priority;
- target navigation node;
- associated digit and section, if any;
- required material or equipment;
- earliest start and desired completion milestone;
- required worker capability;
- reservation owner; and
- current status.

Job categories include:

- stage material;
- carry material;
- push cart;
- operate crane or lift;
- install temporary ladder or platform;
- remove temporary structure;
- remove support;
- clear debris;
- place section;
- secure or hammer section;
- inspect;
- patrol; and
- celebrate.

Construction, evacuation, and hazard jobs override ambient jobs. Idle workers
select compatible work using priority, route cost, and a small deterministic
variation so that every worker does not make the same decision.

One job should usually create one clear visible action. Avoid long monolithic
jobs which secretly script an entire transition.

## 12. Worker state model

Each worker stores a role preference, current job, current and destination
navigation nodes, route progress, facing, carried item, pose/action phase, and
short behavior timers.

Worker states are:

- idle;
- choose job;
- walk;
- climb;
- ride;
- carry;
- push;
- wait;
- yield or turn around;
- inspect;
- hammer;
- operate machinery;
- clear debris;
- avoid danger;
- finish job;
- celebrate; and
- smoke during optional idle time.

State transitions are stepped and readable. Walking and action poses use short
loops, abrupt direction changes, and brief pauses. Construction jobs override
ambient personality events.

Low-rate incidental events make workers appear less mechanical:

- impatience while waiting for a ladder;
- watching a nearby worker or collapse;
- briefly taking a wrong branch and correcting course;
- yielding on a narrow platform;
- dropping a small non-critical piece;
- inspecting finished work twice; or
- giving a short satisfied gesture.

Incidental events have strict time limits and cannot block milestone-critical
work.

## 13. Hazards and debris

Each collapsing digit activates a predefined hazard volume and safe retreat
nodes from the generated world.

Collapse operates at structural-section scale:

- supports fail first;
- upper sections begin falling;
- a bounded number of loose fragments separates from them;
- sections bounce or rotate only enough to communicate weight;
- pieces settle into the digit's assigned debris bay; and
- settled debris becomes clearing jobs.

Workers inside a hazard volume immediately suspend non-safety work and route to
a safe node. Nearby workers may stop and watch after reaching safety.

Initial dynamic limits are:

| Item | Normal maximum | Midnight maximum |
| --- | ---: | ---: |
| Workers | 6 | 9 |
| Falling structural sections | 8 | 20 |
| Loose debris pieces | 16 | 32 |
| Carried or suspended loads | 4 | 8 |
| Carts | 1 | 2 |

The exact limits can be reduced after profiling without changing simulation
rules.

## 14. Material lifecycle

Material accounting is approximate but visually connected.

The simulation tracks small bounded quantities such as available loose bricks,
recoverable sections, staged loads, and depot stock. It does not require strict
conservation.

Typical flow is:

1. A collapsed section becomes debris.
2. A clearing worker marks part of it recoverable.
3. Recovered material moves to a cart, depot, or staging area.
4. A required section becomes available as a prefabricated load.
5. Workers or machinery move that load to the digit.
6. Placement and securing jobs complete the section.

If the required material would miss its pacing milestone, an off-screen supply
delivery may provide it. This is preferable to making workers visibly cheat.

## 15. Machinery

Machinery is generated as part of the level and represented by simple state
machines.

A crane can be idle, slewing, lowering, waiting for attachment, lifting,
carrying, lowering at destination, or returning. Its jobs are limited to its
validated working range.

A cart can be parked, loading, moving between route stops, unloading, or waiting
for a blocked route.

A lift can be idle, called, moving, loading, or unloading. Workers and loads
reserve it like a narrow navigation edge.

Only machinery present in the generated level may receive jobs. Construction
plans must always have a validated alternative for required work.

## 16. Normal-minute behavior

At least three clear activities should normally be visible:

- one worker transporting or staging material;
- one worker inspecting or maintaining structure; and
- one worker moving between useful areas or operating machinery.

Other workers may patrol, clear old material, build temporary access, prepare
the next known transition, or pause for a short personality action.

The simulation knows the next minute's digits, so it can prepare only relevant
parts. Preparation should become more obvious when several digits will change.

## 17. Transition scenarios

### 17.1 14:37 to 14:38

Only the minute-units crew becomes urgent. A lower-left or lower-loop section
for the future eight is staged during the preceding minute. Workers evacuate the
seven, remove a support, and let its upper/right structure fall into its
generated debris bay. One worker clears while another transports the new
section. The eight becomes recognizable near the middle of the minute. Other
workers continue ambient work elsewhere using that boot's generated routes.

### 17.2 14:59 to 15:00

Three digits change. The preceding minute stages two zero structures and parts
for the hour-unit five. The generated depot, crane, carts, and staging areas are
used according to their actual positions. The three collapses are slightly
staggered. Separate crews work in parallel, sharing machinery through jobs and
reservations. The site is visibly busier, but the unchanged hour-tens digit
remains intact.

### 17.3 23:59 to 00:00

All four digits change. Preparation begins early in the preceding minute. The
maximum worker crew appears, staging areas fill, and machinery remains active.
At midnight, collapses run in a rapid stagger rather than one simultaneous
screen-wide event. Two broad crews work from opposite sides of the generated
world and share central routes where required. All four zeros progress through
foundation, side, and top stages together so they become recognizable at roughly
the same point. The date line changes at the local date boundary and remains
clear throughout.

## 18. Date and colon

The date is formatted exactly as:

`DAY DD MON YYYY`

Weekday and month use the warm masonry color role. Day number and year use the
cool steel color role. The simulation snapshot exposes the separate date tokens
and their semantic color roles rather than a pre-rendered string.

The colon remains a stable spatial anchor. Its state may pulse once per second
using a restrained mechanical or lamp-like action. It does not participate in
minute collapse jobs.

## 19. Renderer-neutral scene snapshot

The simulation exposes a read-only snapshot containing semantic objects:

- accepted immutable world description;
- current and target time digits;
- per-digit phase and structural-section states;
- temporary structures;
- worker positions, facing, action, and carried items;
- debris positions and motion state;
- carts, crane, lift, and suspended loads;
- material quantities visible at depots and staging areas;
- colon state; and
- formatted date tokens with semantic color roles.

The snapshot does not contain pixels, draw callbacks, sprite formats, tile
indices, framebuffer pointers, or dirty regions. A later presentation layer may
interpret the same snapshot using whichever rendering design is selected.

## 20. Update model and time discontinuities

The simulation receives elapsed time and a current local calendar time. It owns
no network or RTC setup.

Updates are bounded so a long task delay cannot cause unbounded physics or job
processing. Character pose changes should retain an intentionally stepped
old-game cadence even if simulation updates occur more frequently.

At initial startup, the current digits begin complete. Workers then enter normal
activity. This avoids replaying a minute transition every boot.

A normal one-minute change uses the full transition choreography. If system time
jumps by more than one minute or moves backward, the simulation should:

1. cancel obsolete jobs;
2. move workers to safe valid nodes;
3. clear active hazards and temporary transition debris;
4. adopt the new target time; and
5. use a short commissioning sequence or establish the digits complete.

It must not attempt to replay every missed minute.

## 21. Performance properties

The generator and full validator run only at startup.

Runtime work should be proportional to bounded active entities:

- workers;
- jobs;
- active structural sections;
- debris pieces; and
- machinery.

Runtime rules:

- no per-frame dynamic allocation;
- no repeated procedural generation;
- no full-world path search every frame;
- no pairwise all-entity collision system;
- no strict per-brick material simulation; and
- no simulation behavior dependent on full-screen pixel count.

If profiling requires reduced complexity, lower optional debris, incidental
worker behavior, and ambient crew count before removing the core construction
choreography.

## 22. Test and acceptance plan

### Generator tests

- Generate a large sequence of seeds in the host simulator.
- Require every accepted world to pass the same firmware validator.
- Confirm failed candidates reproduce with seed, version, and attempt number.
- Confirm fixed seeds produce byte-for-byte equivalent semantic worlds.
- Check that valid worlds are normally found within the attempt budget.
- Measure variation in crane side, route topology, platform heights, ladder
  positions, depot side, staging order, and spawn distribution.
- Review batches of generated worlds to ensure topology families are not
  visually obvious templates.

### Simulation tests

- Run stable minutes for several simulated hours without deadlock.
- Exercise every single-digit transition.
- Exercise 14:37 to 14:38, 14:59 to 15:00, and 23:59 to 00:00.
- Confirm workers can evacuate every digit hazard zone.
- Confirm construction reaches each pacing milestone.
- Confirm temporary routes are never removed while occupied.
- Confirm large forward and backward time jumps recover safely.
- Replay identical seeds and time input and compare event logs.

### Visual acceptance

- Digits dominate the scene despite generated variation.
- Permanent geometry never obscures digit readability.
- Temporary activity may enter the digits but remains purposeful.
- Reboots produce meaningfully different silhouettes and routes.
- The world looks designed rather than uniformly random.
- Stable minutes remain alive without becoming noisy.
- Multi-digit changes scale visibly while remaining bounded.

## 23. Implementation sequence

1. Define the fixed-capacity semantic world and simulation data structures.
2. Implement deterministic seed streams and boot/debug seed selection.
3. Implement constrained generation and the read-only rejection validator.
4. Test generation, validation, reproducibility, and variation on the host.
5. Derive and test navigation graphs from accepted worlds.
6. Implement the headless simulation and transition state machines.
7. Verify deterministic transitions, rollovers, and time jumps without graphics.
8. Define the renderer-neutral scene snapshot.
9. Integrate the building clock as a separate application mode.
10. Implement and profile the renderer only after its direction is ready.

This sequence keeps procedural generation and game behavior testable without
disturbing the current display work.
