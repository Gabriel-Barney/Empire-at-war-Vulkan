# The galactic Home key

Retail FoC x64 `StarWarsG.exe`, read in the Ghidra export and the disassembly (#295, 2026-11-24). Tiers: **[H]** read
directly, **[M]** inferred with good support, **[L]** guess. Addresses are image offsets from 0x140000000.

**The Home key goes to the planet where the local faction's leader is, not to its Home_Planet.**

## The dispatch [H]

The hotkey switch `0x63f00`, case 0x4b (slot 75, Home):
- in galactic mode it calls the HUD home action `0x3d2330(HUD)`;
- in tactical modes it calls the camera's home `0x2cf800(cam, 1)`.

## `0x3d2330`

1. **The cache [H].** When the HUD's cached planet (+0xf80) is empty, it searches for one. Otherwise it uses the cache.
2. **The local faction [H].** `0x294a40` returns the local player, and player +0x68 is its faction.
3. **The leader type [H].** Faction +0x2ac8 is the **Faction_Leader** type: the field table entry `0xa3be90`
   {Faction_Leader, kind 0x1d, offset 0x2ac0}. Kind 0x1d is a name reference: the name at +0, the resolved type at +8.
   - The same layout holds Home_Planet at +0x2ab0 / +0x2ab8 and Faction_Leader_Company at +0x2ad0 / +0x2ad8.
   - FoC values: Mon_Mothma, Emperor_Palpatine, Tyber_Zann.
4. **The object [H].** `0x2a87d0` lists every object of that type, of any owner. Only the first one is used.
5. **The planet [H, M].** From that object it walks the parent chain (+0x2b0) to the first object whose byte +0x335 is
   not -1. That byte is the PLANET behaviour's slot [M], so the walk ends on a planet. The result is:
   - the surface planet of the leader's team;
   - or the planet its fleet orbits.
6. **The move [H].** The camera moves to the planet (`0x448c20`), and the HUD selects it (`0x3de570(hud, planet, 0)`).

**No planet found [H].** Nothing happens: no camera move, no selection, no sound. The next press searches again. This
covers:
- no leader type;
- a dead leader;
- a fleet in transit;
- a variant type such as `Tyber_Zann_Passenger`.

**Cache reset [H, M].** Only the HUD init `0x3d3200` clears +0xf80. It runs on a new galactic game (`0x4b21f0`), on a
load (`0x4b23c0`) and from `0x2ea110` (trigger not traced [L]). So the first planet found stays even when the leader
moves.

## Port

- **The search:** `galactic::leader_planet` (engine/galactic/src/leader_planet.cpp).
  - It looks through fleets first, then planet surfaces.
  - A company counts when its Company_Units hold the leader, since the port has no separate team-member objects
    [port].
  - A fleet in transit gives none.
- **The key:** `App::galactic_home` handles it on the galactic screen.
  - It caches the planet until the next session start (new game or load), then centres the camera and selects the
    planet.
  - It is ignored while story controls are locked, as the hero-roster focus is [port].

## Open

- Retail's object-list order when several leader objects exist [L]. The shipped data never creates two.
- A select guard on planet data +0xb8, byte +0x2c9 [L].
- What triggers `0x2ea110` [L].
