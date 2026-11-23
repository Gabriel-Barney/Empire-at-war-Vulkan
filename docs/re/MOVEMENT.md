# Land movement update order

<!-- land-tail-stages: begin -->
| # | stage | port function | what it follows |
|---|---|---|---|
| 1 | special | `tick_special` | the special locomotors (REMAINING_GAPS §4.2-4.7) |
| 2 | deploy | `tick_deploy` | the Walk deploy states 0x16-0x1A (§3.2, §3.2.1) |
| 3 | turrets | `tick_turrets` | the TURRET service `0x632270` of the Deploys units (§3.2 "Turret recentre around deploy") |
| 4 | commit | `land_commit` | the walk's end: arrival pop / state 0x15, TerrainCollision report, conform and set position (§3.5 item 4, §3.5.1, §3.6) |
| 5 | crush | `crush_pass` | crush (§5.5) |
| 6 | despawn | `despawned_` | bombers dropped at their path end (REMAINING_GAPS §4.6) |
<!-- land-tail-stages: end -->

The deploy stage runs before commit because retail ends the arrival frame at 0x16. The 0x15 pop branch of `0x61fc30` sets
0x16 and returns "done for this frame", so a Walk unit's 0x16 handler, a droid's deploy at the stop among them, runs in the
frame after its arrival (anm1 t03, read 2026-11-23 [H]).
