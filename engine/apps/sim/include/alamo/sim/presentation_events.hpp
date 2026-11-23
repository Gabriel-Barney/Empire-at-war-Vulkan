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
#pragma once
#include <cstdint>
#include <vector>

#include "alamo/objects/object_store.hpp"

namespace alamo::sim {

enum class PresentationEventKind : uint8_t { ProductionBegin = 0, ProductionCanceled, ProductionCompleted, HangarLaunch };

struct PresentationEvent {
    PresentationEventKind kind = PresentationEventKind::ProductionBegin;
    int32_t player = -1;                            // the order's player (HangarLaunch: the ship's owner)
    int32_t type = -1;                              // host TypeIndex of the built / launched type
    objects::ObjectId object = objects::kNoObjectId;  // the builder (HangarLaunch: the launched ship)
    uint32_t order = 0;                             // TacticalProduction order id
    uint32_t frame = 0;                             // battle frame
};
using PresentationEvents = std::vector<PresentationEvent>;

}  // namespace alamo::sim
