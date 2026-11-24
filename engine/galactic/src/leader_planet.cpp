#include "alamo/galactic/leader_planet.hpp"

#include <algorithm>

namespace alamo::galactic {

PlanetIndex leader_planet(const GalacticSim& g, int32_t player) {
    if (player < 0 || static_cast<size_t>(player) >= g.players().size()) return kInvalidPlanet;
    const uint32_t faction = g.players()[static_cast<size_t>(player)].faction;
    const GalacticRules& rules = g.rules();
    if (faction >= rules.factions.size()) return kInvalidPlanet;
    const uint32_t leader = rules.factions[faction].leader;
    if (leader >= rules.types.size()) return kInvalidPlanet;
    const objects::ObjectStore& st = g.store();
    auto is_leader = [&](objects::ObjectHandle u) {
        if (!st.alive(u)) return false;
        const auto t = static_cast<uint32_t>(st.type(u));
        if (t == leader) return true;
        if (t >= rules.types.size()) return false;
        const std::vector<uint32_t>& members = rules.types[t].company_units;
        return std::find(members.begin(), members.end(), leader) != members.end();
    };
    for (const FleetState& f : g.fleets()) {
        if (!f.alive) continue;
        for (const objects::ObjectHandle u : f.contents)
            if (is_leader(u)) return f.moving ? kInvalidPlanet : f.planet;  // in transit: no planet
    }
    const auto planets = g.planets();
    for (size_t i = 0; i < planets.size(); ++i)
        for (const objects::ObjectHandle u : planets[i].surface)
            if (is_leader(u)) return static_cast<PlanetIndex>(i);
    return kInvalidPlanet;
}

}  // namespace alamo::galactic
