// #295 (docs/re/GALACTIC_HOME_KEY.md): the planet the galactic Home key (hotkey slot 75, HUD home action 0x3d2330) goes to.
#pragma once
#include <cstdint>

#include "alamo/galactic/campaign.hpp"

namespace alamo::galactic {

// The planet of the first object of `player`'s Faction_Leader type (faction +0x2ac8), up its parent chain to the planet
// (object +0x335, the PLANET behaviour slot) [H]: the surface the leader's team stands on, or the planet its fleet
// orbits. Any owner counts, and only the first match [H]. kInvalidPlanet with no leader type, no such object, or the
// fleet in transit. [port] A company counts when its Company_Units hold the leader (team members are not objects here);
// the search order is fleets, then planet surfaces, in index order (retail's object-list order is not modelled; the
// shipped data has one leader object). Presentation only: no rule or AI reads it.
PlanetIndex leader_planet(const GalacticSim& g, int32_t player);

}  // namespace alamo::galactic
