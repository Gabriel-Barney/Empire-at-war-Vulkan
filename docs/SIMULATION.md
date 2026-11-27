# Simulation rules

The rules that define how the simulation runs and how its behaviour is checked: determinism, parity, the parallel tick,
the AI lane, AI and Lua scheduling, checksums and the outcome tests. Code cites a section as
`docs/SIMULATION.md "Services chain"`; heading texts are stable. Build and test commands are in README.md. Documents
cited as `legacy docs/...` live only in the older checkout; `docs/LEGACY_DOCS.md` links them.

Terms used throughout:

| Term | Meaning |
|---|---|
| Tick / frame | One fixed simulation step; 30 ticks per second (`jobs::kDefaultTicksPerSecond`). |
| Rule 3 | The determinism rule ("Determinism rules"). |
| Golden-neutral | A change that leaves every CRC log byte-identical. |
| `[port]` | A port choice that differs from retail; it names the parity tier that shows it is harmless. |

## Determinism rules

Saves, replays and every parity and regression check rely on the per-tick CRC. Rule 3: **every result game logic reads is
bit-identical across worker counts and runs.** Parity with retail does not relax it ("Parity
tiers"). `engine/jobs/include/alamo/jobs/deterministic.hpp` holds the helpers that give bit-identical results for any
worker count (0, 1, 4, 16, ...) and any scheduling.

### Fixed tick and lockstep commands

- The simulation advances in fixed ticks. Every order (player, AI, script) is a command queued for a set tick and applied
  at that tick, never earlier (`engine/commands`). This is the lockstep model even though multiplayer is out of scope; the
  network scheduler paths were removed.
- Randomness is synced: the session's `SimRng` / `GameRandom` (`alamo/core/sim_rng.hpp`, retail's 32-bit LCG) is drawn in
  a defined order. Streams that are not the synced one are seeded from the session seed and tick-derived values only
  ("Counter-based random streams").
- A whole-state checksum is captured per tick ("Checksums").

### Floating point and compilers

- MSVC is the only compiler (`docs/DECISIONS.md` "MSVC is the only compiler"). A golden written by MSVC is the reference.
- IEEE single precision, no FMA contraction: MSVC `/fp:precise` (`alamo_engine_flags` in
  `engine/CMakeLists.txt`, with `/W4 /permissive- /utf-8`). `sqrt` only, which is correctly rounded.
- Retail-shaped numeric code already in place (for example `fast_rsqrt_retail`) stays.

### Parallel work, reductions and containers

- Parallel jobs write only per-index outputs, or deferred records merged by a canonical key ("Deferred writes in
  canonical order").
- Reductions run in index order (order-stable `parallel_reduce`, `parallel_map_then_ordered_commit`).
- Sim code never iterates a pointer-keyed or seeded-hash container, so iteration order never depends on addresses or hash
  seeds.
- Lazy caches filled from parallel read-only loops must end with exactly the entries the serial code would have filled
  (for example `TacticalMode::model_ref`'s per-type cache), or later readers change.

### Scheduling by tick number

Work is scheduled by tick number only. Wall-clock values (profiler zones, cost estimates, CPU topology, process and thread
priority) may decide when the CPU does work, never what the sim sees. Never done, because each makes the outcome depend on
the machine: time-budgeted work ("run the AI until 2 ms are used"), skipping a late result, applying a result early because
it finished early, first-come ordering.

## Parity tiers

Parity with base Forces of Corruption is checked by behaviour within tolerance, not by matching retail's per-tick CRC or
hashes bit for bit. The port must reach retail's outcome but may get there its own way:

| Tier | What | How it is checked | A difference is |
|---|---|---|---|
| 1, exact | What a player or modder sees or relies on: formulas and constants (damage, costs, build and travel times, ranges, income), how XML and Lua are read and the Lua API, the visible states a unit goes through, order that is itself a rule (which target is picked first), feel (animation and sound timing, fire rhythm, order response, camera) within a frame or two | Spec-driven from the retail notes, unit tests; feel by a person in game | a bug |
| 2, statistical | What randomness drives: AI decisions, battle and auto-resolve outcomes, campaign flow | The parity harness (`tools/parity`) over many seeds: the port's distribution (means and spread) within tolerance of retail's | a failure only when the distribution fails; one seed moving is not |
| 3, free | Internals, as long as tiers 1 and 2 hold: data structures, threading, scheduling, memory layout, algorithms, random-stream layout and draw order, performance work, save format | Rule 3 and the tier 1 and 2 checks | allowed |

Always: rule 3 is unchanged; retail bugs that affect balance or that mods rely on are kept; crashes, hangs and save
corruption are fixed; every difference is a `[port]` entry saying why it differs and which tier shows it is harmless.
Retail draw order is chased only when a tier-2 metric fails. Until the harness has several retail runs per scenario, its
single-run metrics stay as they are.

### Flag-gated tier-2 changes

Every change that moves AI decisions or random draws (the AI lane, counter-based streams, any later AI parallelism) lands
behind a config flag that defaults to the old path, so landing it moves no CRC. The default is switched in its own commit
only after the parity harness in distribution mode shows the new path inside the old path's distribution (and retail's)
over at least 12 seeds per scenario. Reason: a 2-frame galactic AI timing shift once moved the Underworld's random-driven
outcomes out of retail's range. The harness's A/B mode is that check: `tools/parity/parity_harness.py run --scenario S
--seeds 12 --port-arg=--ai-lane --ab` runs every seed with and without the flag, grades each flag-on metric against the
flag-off runs' distribution and reports it against retail's beside that. Story and mission Lua stay on the critical
lane (tier 1).

## Parallel simulation design

### Lanes and the lane boundary rule

Each tick has three lanes, all scheduled by tick number:

- **Critical lane (the tick).** The sim thread, fanning out to workers inside each phase: apply inputs (AI command batches
  due this tick, then player commands), the phase graph (movement, combat, abilities, services), commit buffers in
  canonical order, publish the snapshot, checksum. Holds everything that must be current: player commands, physics-like
  systems, rules, story Lua, victory checks.
- **AI lane.** The tactical AI: perception, goals, plans, free store, AI Lua. It reads only a frozen view of tick N and
  produces a command batch applied at tick N + k_ai ("AI lane").
- **Background lane.** Fixed-latency jobs (`alamo/ai/latency_job.hpp`): path recomputes, galactic all-pairs, plan solves.
  Kick on N, apply on N + k, block if late ("AI scheduling").

The rule that makes this deterministic: **what crosses a lane boundary is a value copied at a tick and applied at a tick
fixed when it was produced, never earlier; the receiver blocks if it is late.** Thread timing decides only when the CPU does
the work. Sim / render overlap is not done.

### Job system and CPU topology

`alamo/jobs/job_system.hpp`, `topology.hpp`. Everything here is scheduling only and golden-neutral.

- **Pool:** one worker per physical core on the performance cores (highest `EfficiencyClass`) of the largest last-level
  cache domain (then the larger L3), minus the sim thread and a reserved AI-thread slot. Workers are kept on the pool's
  cores only when they fit; `ALAMO_JOBS_CPUSETS=0` turns that off.
- **Background-only workers (#127):** on a hybrid CPU, the automatic pool adds one worker per E-core that takes
  `Priority::Background` jobs only (never critical or AI work, also while it waits), outside the background cap.
- **Priorities, strict:** `Priority::Critical` (the tick's phase jobs) > `Ai` > `Background`. Only a loop's helpers run
  queued critical jobs between chunks, never its owner. A critical job never waits on an AI or background counter.
- **Fork only when it pays:** each `parallel_for` call site keeps a measured work-per-item estimate; a loop estimated below
  `kInlineWorkNs` (50 us) runs inline. The first call of a site always forks.
- **OS hints:** power-throttling opt-out, the sim thread one priority step up. Wall clock only.

### Snapshot (frozen read view)

At the AI slot of tick N the critical lane publishes what the AI reads: the host's per-object fill, the tick's event lists
and a copy of the battle's object store (same slots and generations, so held handles stay valid; hooks cleared, own id
counter, `create` refused). Rules:

- One writer per buffer, and no reader opens it before it is sealed at the fixed tick. Reading a half-written tick breaks
  rule 3, saves and replays.
- Static data (type tables, XML) is shared read-only and never copied.

### Deferred writes in canonical order

`engine/tactical/services/include/alamo/tactical/services/deferred.hpp`.

- Inside a parallel phase a job never writes shared state. It writes its own per-index output slot, or appends records
  (commands, damage, spawns, deaths, events) to its lane of a `DeferredBuffer`.
- `commit()` merges every lane by a canonical `Key` (a, b, seq), never by worker or arrival order, and applies records in
  key order. Keys are unique per record. Typical keys: (phase, source entity id, seq); (target id, source id, seq) for
  damage.

### Counter-based random streams

`engine/tactical/services/include/alamo/tactical/services/stream_rng.hpp`.

- A converted service draws from a `SimRng` seeded by `stream_seed(session seed, tick, StreamId, entity id)`, a splitmix64
  finaliser chain; the draw index is the LCG step count from that seed. Each entity's draws are independent of every other
  entity's, of the thread and of service order.
- Same LCG and the same range / percent formulas, so per-draw distributions are unchanged and only per-seed outcomes move
  (draw order is tier 3; tier 2 is checked by the harness).
- `StreamId` values are part of the outcome: never renumber.
- Each converted service keeps its synced path behind its own bit of `TacticalBattle::ServiceRunDefaults::streams`
  (headless `--svc-streams`), default 0 (every service on the synced RNG). A save and its load use the same setting. The
  default changes only per "Flag-gated tier-2 changes".
- Converted, with the entity each stream is keyed by (#128; `TacticalBattle::svc_rng` picks the stream or the synced RNG):
  - hangars (`hangar`): per carrier (entry and bay picks).
  - reinforce (`reinforce`): per participant (its deploy).
  - landing (`landing`): per landing object (an unload step's deploy and exit move).
  - garrisons (`garrison`): per control point (a capture's animation pick). The garrison moves (`s4_apply`: entering,
    leaving, holding, a squad's walk-out, corrective and reissued moves) still draw from the synced RNG.
  - pads (`pads`): per pad (the finished object's adoption draws: hardpoints, first-due ability and behaviour draws,
    a hangar). A build order's under-construction object joins the battle through the build command, on the synced RNG.
  - weapons (`weapons`): per special-weapon pack (a TSW's debris pick) and per player's bombardment record (the
    scattered shots' angle and radius; keyed above 2^32, clear of the pack ids). A bombardment's start (its
    distribution cursor) is a command, on the synced RNG.
  - queues (`queues`): per order group a queue starts, and the queue pass: the draws a queued move, attack or ability
    makes through the host (a standing squad container's move draws its members' start delays; an ability's dummy
    target spawn).
- Nothing to convert: fog and victory draw nothing.
- Still on the synced RNG while a converted service runs (tier 3; deterministic either way, the services run in graph
  order): the `Death_Explosions_End` pick of a death clone's deletion (#221: `expire` and `pushed_clones`, both
  exclusive steps, and a remove-at-once clone on its caller's RNG), the `Death_Explosions` pick of a death (#358:
  `process_death`, the debris expiry of `spawners`, each on its caller's RNG), a spin-away wreck's draws (#357: the
  `Spin_Away_On_Death_Explosion` pick at the death, the path's theta at its first service and its end's
  `Death_Explosions` pick, in `expire`), a combat listener's draws (the tick's RNG, `hooks_->set_tick`), the first-due draw of each behaviour an
  object gets when it is spawned (`ObjectStore::attach`, the host's bound synced RNG), and, under queues, the draws of
  the ability system and of combat's attack orders (their own bound RNG). A service is fully independent of
  the others only once those move too; until then the streams make a converted service's own draws independent.
- The same pattern backs the plan-pick and PotentialPlan solve streams (`pp_streams`: seeded by session seed, frame,
  player, goal source and target; on by default) and the AI lane's own stream.

### Services chain

`TacticalBattle::tick` runs as a services phase graph (`engine/tactical/services/.../service_graph.hpp`,
`engine/apps/sim/src/tactical_battle_services.cpp`).

- Every step of the tick is a service declared in the old serial order with read and write component sets (rng, store,
  state, movement, combat, abilities, garrison, hangar, fog, damage, trees, signals, hooks, host). A barrier reads and
  writes everything.
- A service goes into the first phase after every earlier-declared service it conflicts with (one writes what the other
  reads or writes) or that an explicit `order()` edge puts before it. Services of one phase touch disjoint data, so any
  order or concurrency gives the declared serial result. Phases depend on the declarations only.
- Retail-defined order (tier 1) that the sets do not imply is an explicit `order()` edge, kept even when sets are narrowed.
- Run modes (`RunMode`): `Parallel` (default), `Serial` (the reference), `Reverse` (check). Headless `--svc-mode`.
- Inside a service, per-unit loops compute per unit in parallel and write only the unit's own slot; shared state (fog
  grids, spatial trees, signal lists, engine loops) is then changed serially in unit order. Fog: re-reveal cell lists in
  parallel, then each player's grid applies its undos in unit order on its own worker. Trees: each tree gets its boxes in
  unit order, trees side by side. Per-unit loops fork from `ServiceRunDefaults::parallel_min` (256) units (headless
  `--svc-parallel-min`); results never depend on it.
- Each service is timed as a `tac.s.<step>` profiler zone. Profiler zones are wall clock only; sim code never reads them.

### Latency budgets

| Latency | Rule |
|---|---|
| Player order to effect | Applied in the tick it is scheduled for; player commands never go through the AI lane or a snapshot. |
| Player input to screen | Presentation reads the published state it already reads. |
| AI reaction | Fixed k_ai ticks (default 2), per world, saved. |
| Background results | +k per job (the `LatencyJob` contract). |
| Tick wall time | Budget p99 below 16.7 ms, no tick above 33.3 ms. |

Overrun rule: if the AI lane is not done when its batch is due, the critical lane waits; it never skips or reorders. The cost
shows as a tick-time spike, not as a different outcome.

## AI lane

Off by default (`TacticalAiConfig::ai_lane`, process default `sim::ai_lane_defaults()`; headless `--ai-lane`
`[--ai-lane-k K]`). Off is the pre-lane behaviour. With it on the AI acts on tick N and its commands apply at N + k_ai, so
it is a tier-2 change ("Flag-gated tier-2 changes"). `think()` runs on the AI lane's own thread (one per `BattleAi`, at the
job system's AI priority, so what it fans out is AI work) while the critical lane runs on. It runs inline with 0 workers
and in a battle whose `Evaluate_In_Galactic_Context` evaluates (a campaign battle with a conflict planet: its equations
read the live galaxy, which the battle writes between AI slots). The results are those of the inline lane (#636). The job is joined at its collect (the next AI slot, or the due tick when
k_ai = 1), by `BattleAi::lane_sync()` before a save, at a scheduled save's in-flight check, at a load and at the end of
the battle, and in the destructor. The world's Lua VMs follow the job (`TacticalAiWorld::claim_scripts`): the job's
thread claims every one (plan pools and pristine VMs included) before `think()`, the joining thread takes them back. A
host that reads the AI world between ticks either reads `BattleAi::world_counts()` (published at each join) or calls
`lane_sync()` first.

### AI lane cycle

1. AI slot of tick N (critical lane): capture the facts the AI reads, publish the snapshot, start `think()`.
2. `think()` reads only the snapshot and the capture. Every order goes into an AI command batch tagged
   `scheduled_tick = N + k_ai` instead of executing. AI-internal state lives only on the lane.
3. Start of tick N + k_ai: the batch is applied ahead of the players' commands in `drain_order_less` order, through the
   unchanged `execute_order`, which validates each command against the current state as for a player command (a unit that
   died meanwhile is rejected).
4. Bindings that read back an order just given answer from the lane's own record and predictions until a snapshot holds
   the effect.
5. Signals (deaths, production done, landings, ability ends) reach the AI with the snapshot, so one snapshot plus k_ai
   later than with the lane off.

k_ai = 2 (`kDefaultAiLaneLatency`) gives the AI one full tick of wall time; 1 leaves no slack, more adds delay for no gain.
Two ticks (67 ms) is far below retail's AI cadences (goal cycle sleeps, the 90-frame path cadence).

### AI lane boundary rules

| Crossing | Rule with the lane on |
|---|---|
| Object store reads (AI world and AI Lua bindings) | The snapshot store. |
| Host queries and perception hooks | Answer from the capture (`BattleFacts`: `FactTable` families per player / object / (player, type), `FactQuery` families with a snapshot function). Live mode only on the critical lane. |
| Orders, ability and host writes, GlobalValue sets, releases | Commands in the batch; their same-call answer is a prediction from the capture (`BattleAi::predict_order`, dry runs). A predicted rejection is not queued. The job stamps them itself (`BattleAi::lane_command`): issue tick = the job's frame N, scheduled N + k_ai, sequence = the batch's base plus its place in the batch; the shared sequence counter moves on only when the job is collected (#632). |
| Objects a command will create | Provisional handles that resolve when the batch applies. |
| Random draws on the AI side | An AI-owned `GameRandom` seeded by `ai_lane_seeds(session seed)`, hashed and saved with the AI world. Host draws inside AI-issued orders stay on the synced stream, made at apply time. |
| Pushes into the AI (`order_replaced`, fog refog, Lua `Prevent_AI_Usage`, `BattleAi::set_difficulty`) | Queued in order (`BattleAi::LanePush`) and delivered at the next AI slot, before its snapshot. `Prevent_AI_Usage` answers at once from the per-player answer published at collect (no AI player, not initialised, done); a delivered push that answers otherwise counts as a mismatch. The queue is saved and hashed (#633). `set_difficulty` answers from the published AI flags and the difficulties set so far; the build-time multipliers follow at the delivery (#634). |
| Movement reading AI state (threat grid, script locks, Divert) | The version of the last collected job: the threat mode reads the danger layers copied when the job is collected (`TacticalAiWorld::danger_layers`, `point_danger_from`; #633), the formation follow the attack-positioning flags copied then (#634). |
| Presentation reading the AI bridge (Hide_Sub_Object) | The copy published at collect (`BattleAi::hidden_sub_objects`; #634). |
| "In lane code" | A per-thread fact (#634): the thread running `think()` (and the lane start scripts) holds the AI's lane context, which is the thinking flag, the view (while valid), the facts' snapshot mode and the refused-order record; the AI's fan-outs (goal candidates, slot-10 solves) run with the caller's context (`ai_lane_context_pf`), and the critical lane reads live at the same time. |
| Story / mission Lua and the AI | Story stays on the critical lane with the live tables; it reaches the AI through the snapshot, and AI GlobalValue sets reach story k_ai ticks later. |
| Galactic state during a battle | Read-only copy; HIJACK_UNIT fails with the lane on. `Evaluate_In_Galactic_Context` reads the live galaxy, so a battle where it evaluates keeps `think()` on the sim thread (#636). |

Lane code must never read the live battle: `BattleAi::lane_live_read` counts every such read, and a lane write stamped
outside an open job counts too (debug builds assert on both). `alamo_headless` exits 3 when the count is not 0, so every
`--ai-lane` test checks it. The AI VMs' remaining battle hooks answer from the capture (the finished flag, the map hints,
the ship names, the entry facings, `Are_On_Opposite_Sides_Of_Shield`'s shields) or are batch writes (Set_Garrison_Spawn,
Prevent_Opportunity_Fire, Release_Credits_For_Tactical, Face_Immediate); #634, #636. After a lane run `alamo_headless` prints the lane's mismatches (applies whose result differed from the prediction)
and the Spawn_Unit calls it refused.

### AI lane save and checksum rules

- The `battle.ai` CRC hashes the digest of the last collected job, computed inside the job; a capture never reads the AI
  world while a job may run.
- A job's queued orders and digest become visible only when the job is collected (the next AI slot, or the due tick when
  k_ai = 1). A save keeps a finished, uncollected job and its queued writes; per-slot captures (ability records, facts) are
  not saved, because a load recaptures them from the same saved battle state. The queued pushes and the copies published
  at collect (danger layers, Prevent_AI_Usage answers, attack-positioning flags) are saved, because the saved world may
  already be a job further on (`battle.ai` lane version 9, #633; 10 with the attack-positioning flags, #634).
- A save made with the lane on records `ai_lane` / `k_ai`, and a load rebuilds the session that way. A world saved with the
  lane on loads only into a lane-on world; a lane-off save also loads into a lane-on world.
- In-flight work in general follows the `LatencyJob` save rule ("AI scheduling"): a save/load round trip equals the
  uninterrupted run.

## AI scheduling

When the AI thinks is a function of tick numbers only; what it computes is unchanged.

- **Fixed-latency background jobs.** `LatencyJob<T>` (`alamo/ai/latency_job.hpp`): on the kick tick the sim thread copies
  every input, the job runs off the sim thread (`BackgroundStart` hook; a serial host runs it inline with the same result),
  and the result is applied on the ready tick fixed at the kick, never earlier, blocking if late. Used for the galactic AI
  all-pairs path recomputes and the tactical zone reachability recompute (`path_latency`, default
  `galactic::kDefaultAllPairsLatency` = 15 frames; headless `--ai-latency`, 0 = original timing). The creation recompute
  stays synchronous. The CRC hashes a job's ready frame only while it is in flight.
- **Save rule.** An in-flight job is saved as its inputs plus kick and ready frames; a load re-runs it inline from those
  inputs and applies it on the saved ready frame.
- **Stagger is off.** Every AI player's clocks start on the same frames, as in retail (`GalacticAiConfig::stagger` and
  `TacticalAiConfig::stagger` default false). `--ai-stagger` restores the per-player phases as a benchmark knob only.

## Lua GC schedule

Lua 5.0.2's collector itself is unchanged; only when it runs is scheduled (`engine/lua/.../gc_schedule.hpp`).

- Each host collects its due VMs at one fixed point of its tick, before any of its scripts run: the galactic AI once per
  frame, the tactical AI at the start of `update`, the story session at the start of `service`.
- A VM is looked at every `period` (32) ticks at its own slot (a hash of its threshold, saved with the VM) and is due once
  its bytes reach `fill_percent` (67 %) of its automatic threshold; after a scheduled collection the threshold is
  `threshold_percent` (150 %) of Lua's own.
- Due VMs of one host are collected in parallel through its `ParallelFor`; `__gc` finalizers run afterwards on the VMs'
  owner thread (the sim thread; the AI lane's thread for the tactical AI with the lane on, #636) in list order.
  A VM is never collected during a call.
- Collection timing is invisible to shipped scripts (none uses weak tables, `__gc`, `newproxy` or `gcinfo`).

## Checksums

Every system registered with the CRC registry hashes its state per captured tick; the log's columns are the systems plus a
`parity` part and a `combined` value. Capture must not serialise the tick.

### CRC capture cadence

`SimHost::set_crc_cadence(every, ring)` / `set_default_crc_cadence` / `capture_crc_now()`. Only which ticks are hashed
changes, never the sim.

- `every` = 1 (headless, tests, `--crc-out`): every tick.
- `every` > 1: the initial state, ticks with `tick % every == 0`, and on-demand captures (save, a benchmark's end, before a
  compare); the log is a checkpoint log. `alamo_game` uses 64 with a 4096-record ring in live play, a benchmark scenario's
  fingerprint cadence when it has one, and 64 for other benchmark modes.

### Tree hash of per-item loops

`hash_blocks(h, count, fn[, cache])` in `alamo/core/sim_crc.hpp`: items split into fixed blocks of `kCrcBlockItems` (256),
each hashed by its own `StateHasher`, in parallel through the capture's `ParallelFor`; the system's hasher then receives
`u32(count)` and every block digest in block order. The digest is the same for any worker count. Block size and layout are
fixed forever. `CrcBlockCache` reuses the digest of a block nobody marked dirty; a missed mark can only hide a divergence,
never change the sim.

`hash_parts(h, fn...)` hashes independent parts of one system side by side: each part feeds its own `StateHasher` (with
the capture's `ParallelFor`, so it may use `hash_blocks` inside), then the system's hasher receives `u32(parts)` and every
part's digest in order. Users: the object store, `battle.movement` and `battle.combat` (`hash_blocks`), and the tactical
AI's perception in `battle.ai` (#631: the threat grid's per-cell lists, each player's per-cell layers and its objects, the
targets and the leaf cache, the three as `hash_parts`). No production code passes a `CrcBlockCache`.

### CRC log formats

Text, LF line ends, written byte-identically by every build (`CrcLog` in `sim_crc.hpp`; the Python twin is
`tools/goldens/convert_goldens.py`, checked byte for byte by a ctest).

```
ALAMO_CRC 2                        (ALAMO_CRC 1: the full log, one record per captured tick)
systems <count> <names...>
parity <count> <names...>
checkpoint <N>                     v2 only, N > 0
keep <tick> <tick> ...             v2 only, optional: extra stored ticks, increasing
# comment lines                    seed, ticks, workers...; ignored by compare
<tick> <combined> <parity> <crc0> <crc1> ...
```

A v2 checkpoint log stores the first record, the last record, every tick with `tick % N == 0` and every `keep` tick; it is a
subset of the full log of the same run. `--crc-out` writes v1 by default; `--crc-checkpoint N [--crc-keep T,...]` writes
v2; `alamo_headless --crc-convert IN OUT` rewrites a log as v2 (default N 64). Determinism checks compare full v1 logs.

### CRC log comparison

Used by `--golden F`, `alamo_headless --compare EXPECTED ACTUAL`, `first_divergence()` and
`engine/apps/game/compare_smoke.cmake`:

- Two full logs compare tick by tick.
- When either log is a checkpoint log, only its stored ticks are compared (the expected log's when both are); each must
  exist in the other with the same CRCs, and both logs must end at the same tick.
- A mismatch reports the first bad checkpoint, the window of ticks it lies in, the first differing system and every
  differing column. For the exact tick, write full logs of both sides and compare them.
- A difference that appears and disappears between two checkpoints is not seen; pin such ticks with `--crc-keep` or use a
  smaller N.

## Worker-count check

Rule 3 requires identical per-tick CRCs for every worker count, so every change to jobs, threading or parallel sim code is
checked by comparing each count's full CRC log with the 0-worker log (`first_divergence` names the first diverging tick and
system). Two forms:

- **Per process (long runs):** `alamo_add_worker_count_check(<name> COMMAND ...)` in `engine/CMakeLists.txt`. One ctest
  entry per count so counts run side by side: `<name>` runs `--workers 0 --crc-out`, and `<name>_w1`, `_w4`, `_w16` and
  `_wmax` (hardware threads - 1, named the same on every machine) run `--workers N --golden` against it. 16 is above
  the logical core count of most machines, so preemption inside a tick is covered. The entries are labelled `determinism`. A command must accept
  `--workers`, `--crc-out` and `--golden` (every `alamo_headless` mode does).
- **In process (unit tests, short runs):** `alamo::jobs::check_worker_counts(run, determinism_worker_counts())` in
  `alamo/jobs/crc_recorder.hpp` runs 0 / 1 / 4 / default and returns a `WorkerCountReport`.

Long determinism tests get one ctest entry per worker count, never a single `--check-workers` entry. There is no TSan
runtime on Windows, so data races are caught by these checks.

## Services graph checks

A service that touches a component outside its declared sets is a data race once its phase runs wide. Three checks prove
the declarations ("Services chain"):

- **Write check:** a serial run digests every checkable component around each service and reports a component changed
  outside the service's write set (headless `--svc-check`; on in every debug-build battle, where it also asserts).
- **Read check:** `RunMode::Reverse` runs each phase's services in reverse order; its CRC log must equal the default run's.
- **Forks:** forcing the per-unit loops wide (`--svc-parallel-min 1`) and running phases serially (`--svc-mode serial`)
  must give the same CRCs, at every worker count.

## Declared behaviour changes

A pull request that changes simulation code says what behaviour changes. The simulation paths are listed in
`tests/simulation_paths.txt`. The pull request's description carries one line:

- `Behaviour: none` when no observable outcome changes (a refactor, a speed-up that keeps every CRC log byte-identical);
- `Behaviour: <scenarios and systems>` otherwise, for example `Behaviour: simultaneous combat events tie-break
  (combat), skirmish_ai_a1`. The pull request's tests show the expected effect, and its evidence names the first
  affected tick and the scenario result.

`landing` fails a pull request that touches a listed path without that line (`tools/landing_gate.py classify`). The
line may be a list item or bold, in any case; a line inside an HTML comment or a code block, or an unfilled
`Behaviour: <...>`, does not count. A moved file counts at its old and its new path. The check reads the description when it runs, so after adding the line, re-run the
failed jobs. Every `landing` run also runs the synthetic `determinism` entries ("Worker-count check"). The entries that
need game data run in the local `dev.cmd suite retail` that the pull request records (`docs/DECISIONS.md` "Retail and
GPU evidence is a recorded local run"). The rule is `docs/DECISIONS.md` "Simulation changes are declared".

## Proving the same outcome

Every simulation change is judged against the tiers:

- **Rule 3:** the worker-count checks; an MSVC AddressSanitizer run for job-system changes.
- **Tier 1:** unit tests and retail spec checks; retail-defined order stays an explicit graph edge; story Lua stays on the
  critical lane.
- **Tier 2:** flag-gated changes judged by the parity harness over many seeds ("Flag-gated tier-2 changes").
- **Saves:** a save/load round trip, with in-flight work, equals the uninterrupted run (`--save-check`).
- **Speed:** the benchmark scenes at 0 workers and the default, plus a large-scene stress at four times the land forces
  (`--campaign foc-land-army4`); more cores must never make a tick slower.

## Outcome tests

### Outcome tests over seeds

AI-vs-AI outcome tests never pin a hand-picked seed. Each scenario of `tools/parity/outcomes.json` is one slow ctest,
`alamo_headless_outcome_<id>`, that runs seeds 1..N through `tools/parity/parity_harness.py outcome` and asserts the share
of seeds that end, the end-frame range of the ended ones, no Lua error outside retail's documented ones, and
`alamo_headless`'s own checks. Thresholds sit below the measured share and change only with a new measurement and its
reason in the scenario's `why`. A known port bug's Lua error may be listed under `known_lua` with its owning open work; it is
reported, not failed, and the entry goes when the fix lands. Determinism stays with the CRC runs (one fixed seed each).

### skai exit test

The skirmish AI exit test is a tier-2 distribution, not "the run does not end". Scenarios `skai_exit_rebel`,
`skai_exit_empire`, `skai_exit_underworld` (the AI against an idle human on Utapau, seeds 1-10, 36000 ticks; retail runs in
each scenario's `retail` list; `tools/parity/outcome_sweep.py`):

1. **Rate:** a two-sided Fisher exact test of port ends / runs against retail ends / runs passes while p >= 0.05.
2. **Frames,** only when both sides have ended games: the median end frame of the port's ended games is at least retail's
   earliest end minus 3000 frames. One-sided, because the tick cap cuts the late side and a port that ends too late ends
   too rarely, which the rate test sees.

More retail runs tighten the check: add them to the scenario's `retail` list.

### Retail Lua errors in outcome tests

AI-vs-AI games pass with Lua errors that retail raises too. `--fail-on-lua-errors` (always on for `foc-station`) reports an
error listed in `kRetailLuaErrors` (`engine/apps/headless/skirmish_run.cpp`, exact text) as "retail, documented" and fails
on any other. An error goes on the list only with evidence from the binary in the retail notes, so a port bug still fails.
First entry: `BobaFettFlameInfantry.lua` `MoveUntilDone` re-issues `Move_To` on a dead target after `PumpEvents` without
`TestValid`, and retail's `Move_To` raises "Target is already dead.".

### Land skirmish tests without a battle end

Pre-placed base structures (`DUMMY_GROUND_STRUCTURE`) are left out of player force sums and `Find_Nearest`, as in retail
traces. With that rule retail's AI does not finish an idle base, and FoC skirmish has no AI-vs-AI mode to give a reference
end, so AI-vs-AI land runs (`foc_land_full`, `foc_land_empire_underworld`) must play their whole run with no
non-retail Lua error but need no winner. Scenarios checked against retail outcomes (space, station, campaign-rules land
invasion) keep `--expect-battle-end`.

## Retail-order rules

### Land tick frame order

`MovementSystem::tick_land` runs retail's land frame as a cycle: collision driver (services only), detection (a new record
waits for the next driver), the movement plans (reading the tracker flags of the snapshot taken before the last walk),
the answer fold, the tracker snapshot, then the walk. It was adopted because the earlier order measurably changed records,
re-paths and arrivals against retail.

### Running-plan re-test

The tactical AI's running-plan contrast re-test runs at every commit, as in retail (`TacticalAiConfig::plan_retest`,
`TacticalAiSetup::plan_retest`, default true). `alamo_headless --no-plan-retest` gives the earlier port behaviour.
