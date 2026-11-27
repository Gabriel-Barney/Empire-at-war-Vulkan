# Audio: conversation managers, the Miles 3D distance setup, planet destruction activation (#330)

Source: the Ghidra export of StarWarsG.exe (x64, RVAs below). Evidence tags as in the other docs/re files. Answers the
three open questions of #330, which were split out of #62 (legacy docs/re/AUDIO.md §1.7, §8).

## 1. The conversation managers

Neither manager is a general conversation system.

**TheSpeechConversationManager** (`0x140b2b3a8`, init `0x3226e0`) sequences only the Hero Clash dialogue [H]:

- **Name table:** `0x140a17260`, read in order: `Hero_Clash_Obiwan_Vader__Vader_01`, `..._Obiwan_01`, and so on up to `_04`, then
  NULL.
- **State:** the state is `0x140a17250`, with these values:

  | Value | Meaning |
  | --- | --- |
  | 0 | idle |
  | 1 | first line of a pair |
  | 2 | second line |
  | 3 | pair done, waiting for the next round |
  | 4 | table exhausted |
  | −1 | shut down |

  The table index is `0x140b2b398`, and the current SpeechEvent is `0x140b2b3a0`.
- **Start `0x3225a0`:**
  - Its only caller is a MeleeHeroClash step, `0x6ad230` [M].
  - It kills every playing and queued speech line (`0x321830(&SpeechEventManager, 1)`).
  - It then queues `table[index++]` (`0x320a50` name lookup, `0x321170` queue) and sets state 1. A name the lookup
    does not find resets the state and the index to 0 (here and in the callback).
- **No per-frame step:** a speech-done callback (`0x322400`, registered at init) moves the state on.
  - The end of the first line queues the next one (state 2).
  - The end of the second line moves to state 4 when the next table entry is NULL, and to state 3 otherwise. State 3
    waits for the next clash round, which calls `0x3225a0` again.
- **Queries:** `0x3223f0` is true while a pair plays (state 1 or 2). `0x3223c0` is true while lines remain; `0x6ac1b0`
  uses it to pick the talking clash mode.
- **Reset `0x322780`:** kills all speech and zeroes the state. It runs on mode changes and loads.
- **Pause / resume:** `0x320780` / `0x320790`, on WM_ACTIVATEAPP (`0x60c60`).
- **No queue rules:** there is no queue of conversations and no priority comparison. A start replaces everything
  [H for the code that exists, M that nothing else does it].

**TheSFXConversationManager** (`0x140b2b2f0`) is a stub [H]. It has only init (`0x320760`), pause / resume, shutdown
(`0x3207a0`) and reset (`0x3207c0`), and nothing starts a conversation on it.

The two "indirect step-14 managers" in the frame function `0x5d990` are these two. Both are called through guarded
indirect calls to an empty update [M].

**The port:** `audio::ConversationPlayer` keeps its [port] queue and priority rules. Retail has no such rules to follow,
and only `cancel_all` is called today. The hero-clash dialogue is not ported; it is a follow-up issue.

## 2. The Miles 3D distance setup

- **Per sample** (`0x22a500`, the 3D start) [H]:
  - `min = max(1, Volume_Saturation_Distance (SFXEvent +0xdc) × the mode scale (+0x1024))`;
  - then `AIL_set_sample_3D_distances(S, max = 15000.0, min)`.
- **Rolloff:** `0x21d0b0` calls `AIL_set_3D_rolloff_factor(driver, f)` [H]. The mode scales come from the Audio
  GameConstants [M]:

  | Mode | Setter | Min-distance scale | Rolloff factor |
  | --- | --- | --- | --- |
  | Land | `0x3b86c0` | `Audio_Land_3D_Saturation_Distance_Mod` | `Audio_Land_3D_Rolloff_Distance_Mod` |
  | Galactic | `0x4b2330` | `Audio_Galactic_3D_Saturation_Distance_Mod` | `Audio_Galactic_3D_Rolloff_Distance_Mod` |
  | Space | `0x4d7ce0` | `Audio_Space_3D_Saturation_Distance_Mod` | `Audio_Space_3D_Rolloff_Distance_Mod` |

  The mode exits (`0x3b7aa0`, `0x4b1fb0`, `0x4d7540`) set both back to 1.0 [H].
- **No distance factor:** `AIL_set_3D_distance_factor` is not imported [H].
- **Listener:** `0x21caf0` sets the listener position, forward and up vectors every frame [H].
- **The law:** Miles applies its own inverse distance law, `gain = min / (min + rolloff × (d − min))`, at full volume
  inside min [M: the binary only sets the inputs; a capture or the MSS documentation would confirm the curve]. What
  Miles does past the 15000 maximum is Miles behaviour, not in the binary [L].

**The port:** `AudioSystem::attenuation` already uses these inputs and this law. Past the maximum it is silent [L].

## 3. The planet destruction activation presentation

- `0x707b90` is PlanetIncomeBonusAbility's activate, not planet destruction [M]. It uses +0x178 / +0x17c / +0x180 /
  +0x188, the income-modifier fields.
- `0x707670` is the activate of Planet_Destruction_Ability [M].
  - When an object on the planet blocks the shot, it plays `SFXEvent_GUI_Negative_Feedback` 2D to the local player and
    refuses.
  - Otherwise it ends with `0x48e4d0(ability, user, planet)` [H].
- `0x48e4d0` reads its fields from the ability [H for the code, M for the field names]:
  - **The user:**
    - the owner light effect (+0xb0..), when the user runs none;
    - Owner_Particle_Effect (+0x100) on the first bone from +0x108 that exists, else `ROOT`;
    - **SFXEvent_Activate** (+0x148): 2D when Is_2D (`0x2d5290`), else 3D on the user (`0x2d5320`).
  - **The target:** the target light effect, the target particle (+0x128 on a bone from +0x130), and
    **SFXEvent_Target_Affected** (+0x158), 2D when Is_2D, else 3D on the planet.
- Before the presentation, 0x707670 destroys the planet (`0x3f76c0`) and, when the human-player gate
  (`(*0x140b15418)+0xf8`, `0x3464b0(.., 7, owner)`) passes, raises `0x343b90(.., {7, owner, 30})`: the victory claim 7
  with its 30-cycle countdown, which the port already has (`GalacticSim::super_weapon_destroy_planet`, claim 7) [M].

**The port:** a successful Planet_Destruction apply raises `GalacticEventKind::SpecialPresented`. This is
presentation only: no story event, since `0x707670` calls no `0x3284c0`. The game plays SFXEvent_Activate on the shooter
and SFXEvent_Target_Affected on the planet through the same `GameAudioHost::galactic_ability_effect` as the other
galactic abilities. The light effects and particles of `0x48e4d0`, and its Is_2D branch, are not ported for galactic
abilities (every one plays 3D at its object).
