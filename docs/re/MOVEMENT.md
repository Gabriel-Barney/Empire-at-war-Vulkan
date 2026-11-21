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
