// per-tick presentation events - what the battle did this tick that only the presentation (audio, HUD) reacts
// to, as a list the host drains, instead of the host polling sim state for edges.
//
// Contract (determinism rule 3; the GalacticEvent stream's, galactic_events.hpp): TacticalBattle appends from serial code
// only (its command entry points and serial tick steps), in the order the changes happen; the index in the list is the
// tie-break. Nothing here is hashed, saved or read by the sim, so emitting never changes a CRC, a save or a golden.
// Emission is off by default (a host that never drains grows nothing); a host turns it on with
// TacticalBattle::set_emit_presentation(true) and drains once per tick (drain_presentation). alamo_game: GameSession
// collects the tick's events (GameSession::presentation_events), App hands them to GameAudioHost::on_events, which maps
// each kind to its sound (one row per kind, asserted in alamo_game_audio_host_tests).
//
//   ProductionBegin      a tactical production order was queued (Production command; skirmish build queues)
//   ProductionCanceled   a Production cancel command removed the order (MCX4_T13_RE_ANSWERS "GameObjectType.
//                        SFXEvent_Tactical_Build_Started family": cancel 0x498820 runs from the cancel-event executors
//                        only, so an order dropped with its builder is not a cancel)
//   ProductionCompleted  the order finished at a builder still alive (before the DUMMY_UPGRADE / unit branch)
//   HangarLaunch         #50 t06 (STATION_HANGARS_SOUND §2.5, 0x594dc0 [H]): a space hangar launched a single ship (a type
//                        without Squadron_Units); player = the launched ship's owner, type = its type, object = the ship.
//                        The original plays the faction's Reinforcements_Enroute_SFXEvent (2D) when that is the local player.
//   UnitDied             #327 t06: a battle unit died (process_death, not a silent removal) - from a combat tick, a late
//                        death (late_deaths) or a script kill between ticks alike. player = its owner, type = its type,
//                        object = it; clone / clone_type = its death clone (the Die step 8 clone 0x29f270: the object, none
//                        on the infantry corpse path; the type, -1 when the death makes no clone), x / y / z = where it
//                        died. The host plays Unit_Lost (0x39bdb0, the dying type, local owner) and the clone's
//                        Death_SFXEvent_Start_Die (DEATH's effects 0x58cf90 on the clone, the clone as gate 9's parent).
//                        #357: spin = the clone spins away: its phase 1 plays no Start_Die (SpinAwayEnded does).
//   SpinAwayEnded        #357: a spin-away wreck ended (DeathBehavior service 0x58d430, phase 0): object / clone = the
//                        clone, type / clone_type = its type, player = its owner, x / y / z = where it ended. The host
//                        plays the clone type's Death_SFXEvent_Start_Die there.
#pragma once
#include <cstdint>
#include <vector>

#include "alamo/objects/object_store.hpp"

namespace alamo::sim {

enum class PresentationEventKind : uint8_t { ProductionBegin = 0, ProductionCanceled, ProductionCompleted, HangarLaunch, UnitDied, SpinAwayEnded };

struct PresentationEvent {
    PresentationEventKind kind = PresentationEventKind::ProductionBegin;
    int32_t player = -1;                            // the order's player (HangarLaunch: the ship's owner)
    int32_t type = -1;                              // host TypeIndex of the built / launched type
    objects::ObjectId object = objects::kNoObjectId;  // the builder (HangarLaunch: the launched ship)
    uint32_t order = 0;                             // TacticalProduction order id
    uint32_t frame = 0;                             // battle frame
    objects::ObjectId clone = objects::kNoObjectId;  // UnitDied: the death clone object
    int32_t clone_type = -1;                         // UnitDied: the death clone's type
    float x = 0.0f, y = 0.0f, z = 0.0f;              // UnitDied: the death position (SpinAwayEnded: the end)
    bool spin = false;                               // UnitDied: the clone spins away (#357)
};
using PresentationEvents = std::vector<PresentationEvent>;

}  // namespace alamo::sim
