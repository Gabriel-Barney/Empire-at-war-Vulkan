# The planet's trade-route list and 0x3f7b30 (#436)

Retail FoC x64 `StarWarsG.exe` (v1.121.859328), read in the Ghidra export (reference only, not copied) on 2026-11-24.
Tiers: **[H]** read directly in the code, **[M]** inferred from the code with a gap named, **[L]** a guess.
Answers #436 (old gai5/t07, b0514): which edges does 0x3f7b30, the "connected by a route" test of the galactic AI's
neighbour list and of IsConnectedTo's attenuation (legacy docs/re/AI_GALACTIC_TOKENS.md §4), count?

## The list [H]

The planet's data pack D (planet object +0xB8, a `PlanetaryDataPackClass`) keeps a vector of 32-byte records at
D+0x58 with its count at D+0x60. Three functions read it:

| Function | Reads |
|---|---|
| 0x3f95a0, `Find_Trade_Route_To` (PlanetaryBehaviorClass) | walks the records for the one whose +0x18 is the given planet object and returns the record's +8, the route object |
| 0x3f95f0 | returns the count D+0x60 (0 when the planet has no D) |
| 0x3f7b30 | walks the records and returns true when a record's +0x10 equals the other planet's id (+0x50) |

The TradeRoutes token (0x65f200, legacy docs/re/AI_GALACTIC_TOKENS.md §2 table) reads the same count D+0x60 as the planet's route
count.

Each record therefore stands for one trade route out of the planet: the route object (+8), the far planet's id (+0x10)
and its object (+0x18).

## Answer [H for the list; M that nothing else is added]

0x3f7b30 is true only when a campaign trade route joins the two planets. A planet pair that is within
MaximumFleetMovementDistance but has no trade route (a free hop of the fleet graph) has no record: there is no route
object to store, and no writer that adds a record without one was found [M].

## Port consequence

- `GalacticSimTokenResolver`'s `trade_linked` (the campaign route list) already stands for 0x3f7b30 in the
  connectivity graph (legacy docs/re/MCX4_GAL2_RE_ANSWERS.md Q1).
- IsConnectedTo's attenuation takes d = 0 only for a trade-linked neighbour: `1 - d / GC+0x108 * A` with d the 3-D
  distance applies to every other neighbour, a free-hop neighbour included.
- The planet evaluator's neighbour list (0x661da0) takes the planets within GC+0x108 or trade-linked. The port built the
  "linked" part from the fleet graph's edges, which leave out a planet that is not a graph node; it now walks the
  planet's campaign trade routes, as 0x3f7b30 does.
- The Nearby*Total tokens' attenuation (`1 - d / GC+0x108 * (1 - A)`, legacy docs/re/AI_GALACTIC_TOKENS.md) has no route
  test: d is always the distance. Unchanged.
