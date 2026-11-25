# AntiStealthEffectiveness (0x6d9180) and the Detect_Hero_Ability value

Issue #245 (parent #60, gai2/t01, backlog b0376). Source: the Ghidra decompile of StarWarsG.exe, the RTTI vtable names
of the Ghidra export (functions.csv) and the parser field table docs/re/xml_field_tables.md.

Tiers: **[H]** read directly in the code, **[M]** inferred from the code with a gap named, **[L]** a guess.

## 1. The per-unit value

- 0x395830(obj) returns the float at special-ability pack (obj +0x100) +0x8c, or 0 without a pack [H].
- Its only writer is 0x3a7b90(obj, v) [H]. Two callers:
  - **DetectHeroAbilityClass vf30 (0x6f6b70)**, the ability's apply: v = ability +0x17c / ability +0x178 [H];
  - **vf31 (0x6f6dd0)**, the ability's remove: v = 0 [H].
- The class's XML tag is `Detect_Hero_Ability`: its name function returns the string, and it is registered under that
  name at 0x68b1f0 [H].
- The parser table gives the fields [H for the offsets]:

| Offset | Tag | Parser type |
|---|---|---|
| +0x178 | `Detect_Interval_In_Seconds` | 0x8 (float) |
| +0x17c | `Detection_Chance` | 0x9 (fraction) |
| +0x180 | `Detect_Credit_Siphon_Effects` | bool |

So a unit whose Detect_Hero_Ability is applied carries p = Detection_Chance / Detect_Interval_In_Seconds. That is the
chance per second, since the service 0x6f6e30 draws one synced float against +0x17c per detection pass [M on
intent]. Retail divides as float with no guard on the interval.

The shipped FoC XML has no `Detect_Hero_Ability`, so p is 0 for every vanilla unit and the token is 0 in retail too.
The change matters for mods.

## 2. The token

0x6d9180 (EnemyForce / FriendlyForce `AntiStealthEffectiveness`) returns 0 without a planet object. Otherwise it starts
v = 0, k = 1 and adds every value p in this order with `v += p·k; k *= 1 − p` in double. That gives
v = 1 − Π(1 − pᵢ), the chance that at least one detector succeeds [H]:

1. **Orbit:** the planet data's fleet list (D +0xc0, 0x395900) in list order. Only fleets that pass the side filter
   (evaluator vf +0x58) count. Each fleet's contents are flattened (0x3965f0, recursive), and each unit adds its own p.
   A unit with a TRANSPORT behaviour (slot byte +0x337 ≠ -1) adds its cargo's values (0x397bf0, obj +0xc8 list)
   instead.
2. **Surface:** the planet data's surface list (D +0xd8, 0x397120). Each entry adds the values of its member list
   (0x397bf0). **No side filter is applied** here [H].

## 3. The port

- `TypeSpecial::detect_interval` holds Detect_Interval_In_Seconds next to `detection_chance`. The ability's constructor
  default for a missing tag was not read [M]; the port uses 0.
- `anti_stealth_value(type)` = (float) detection_chance / detect_interval for a type that has the ability. The port
  treats the ability as applied, as its detector already does: vf30 runs when the ability is applied, and for a
  galactic ability that is creation for an `Initially_Enabled` one [M].
- The token walks the same lists in the same order:
  - the orbit fleets of the side, each content unit adding its own value (the port keeps ground cargo as direct fleet
    contents, with no transport object);
  - then every surface entry with no side filter, each adding its company members' values (`company_members`), or its
    own when it has none [port].
