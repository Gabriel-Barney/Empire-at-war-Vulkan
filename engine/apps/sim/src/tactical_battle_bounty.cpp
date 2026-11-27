// #574: the tactical kill bounty (Bounty_On_Faction_Ability; 0x392600's tactical branch [H], read 2026-11-26). Serial code,
// called from process_death; no synced draws; credits go through the host's credit hooks.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/core/ascii.hpp"
#include "alamo/galactic/bounty.hpp"
#include "alamo/galactic/campaign.hpp"
#include "alamo/objects/game_object_types.hpp"
#include "alamo/objects/type_fields.hpp"
#include "alamo/sim/tactical_battle.hpp"

namespace alamo::sim {

namespace {

float to_f(std::string_view v) { return static_cast<float>(std::atof(std::string(ascii_trim(v)).c_str())); }

}  // namespace

std::vector<BountyOnFactionDef> parse_bounty_on_faction(const objects::GameObjectTypes& types, uint32_t type) {
    std::vector<BountyOnFactionDef> out;
    if (type >= types.size()) return out;
    const objects::ObjectTypeDatabase& raw = types.raw();
    const objects::FieldColumns& fc = raw.fields();
    for (const objects::FieldIndex f : types.sub_objects(static_cast<objects::TypeIndex>(type), objects::tags::Abilities)) {
        if (!ascii_iequals(raw.tag_name(f), "Bounty_On_Faction_Ability")) continue;
        BountyOnFactionDef d;
        for (objects::FieldIndex c = f + 1; c < fc.size() && fc.depth[c] > fc.depth[f]; ++c) {
            if (fc.parent[c] != f) continue;
            const std::string_view k = raw.tag_name(c);
            if (ascii_iequals(k, "Bounty_Percentage_Amount")) {
                d.percentage = to_f(fc.text[c]);
            } else if (ascii_iequals(k, "Stacking_Category")) {
                d.category = static_cast<int32_t>(std::atoi(std::string(ascii_trim(fc.text[c])).c_str()));
            } else if (ascii_iequals(k, "Apply_To_All_Allies")) {
                d.all_allies = objects::parse_type_bool(ascii_trim(fc.text[c]));
            } else if (ascii_iequals(k, "Target_Faction_Names")) {
                std::string_view rest = fc.text[c];
                while (!rest.empty()) {
                    const size_t comma = rest.find(',');
                    const std::string_view name = ascii_trim(rest.substr(0, comma));
                    if (!name.empty()) d.factions.push_back(ascii_lower(name));
                    if (comma == std::string_view::npos) break;
                    rest.remove_prefix(comma + 1);
                }
            }
        }
        out.push_back(std::move(d));
    }
    return out;
}

float bounty_fold(std::span<const BountyEntry> entries) {
    // 0x527260: GreaterThan<float> over each category's entries, Plus<float> over the categories (ascending category)
    std::vector<std::pair<int32_t, float>> best;
    for (const BountyEntry& e : entries) {
        const auto it = std::lower_bound(best.begin(), best.end(), e.category,
                                         [](const std::pair<int32_t, float>& b, int32_t c) { return b.first < c; });
        if (it != best.end() && it->first == e.category) {
            if (e.value > it->second) it->second = e.value;
        } else {
            best.insert(it, {e.category, e.value});
        }
    }
    float sum = 0.0f;
    for (const auto& [c, v] : best) sum += v;
    return sum;
}

int32_t bounty_credits(float fold, float bounty) {
    const int32_t n = static_cast<int32_t>(std::round(fold * bounty));  // 0x22f9c0
    return std::max(n, 1);
}

const std::vector<BountyOnFactionDef>& TacticalBattle::bounty_defs_of(uint32_t type) {
    if (bounty_defs_.size() <= type) bounty_defs_.resize(static_cast<size_t>(type) + 1);
    BountyDefs& b = bounty_defs_[type];
    if (!b.parsed) {
        b.parsed = true;
        b.defs = parse_bounty_on_faction(catalog_->types(), type);
    }
    return b.defs;
}

void TacticalBattle::pay_kill_bounty(const TacticalMode& mode, const Unit& victim, int32_t killer) {
    // 0x392600, the tactical branch (the mode's vf +0xf0 true), called by the death handler 0x39bdb0 while the global
    // bounty flag (DAT_140b305e9; [M] the ENABLE / DISABLE_BOUNTY_COLLECTION story gate) is set [H]:
    // - gates: a killer player K (victim +0x330, the killer object's owner: the port's DeathEvent::killer unit's owner [M]);
    //   the victim's owner O is not K and not allied with K; B = the victim type's bounty (Override_Bounty_SP / _MP, else the
    //   largest category default: galactic/bounty.hpp, unrounded) > 0; O has a faction (an ownerless victim pays nothing);
    // - for each player p in index order allied with K (relation 0, K itself included [M]): A = the mode's per-player bounty
    //   object (vf +0x220(p)); f = 0x527260(A, O's faction name lower-cased); f > 0: n = max(1, round(f x B)),
    //   Add_Credits(p, n), the player's bounty stat +0x454 += n.
    // A (BountyOnFactionAbilityClass apply 0x6f1ab0 / 0x6f1bc0, remove 0x6f1fb0 [H]): every live object with the ability
    // adds, for its owner (Apply_To_All_Allies: every player allied with the owner), one entry {its Stacking_Category, its
    // Bounty_Percentage_Amount} per Target_Faction_Names name; the object's removal takes them out again. [port] The
    // holders are read at the kill: the live battle units (the victim included [M]: its abilities go with its removal,
    // after the death handler) and the upgrade objects (DUMMY_UPGRADE: galactic, corruption and produced upgrades), each
    // with its current owner; #542: then the corruption upgrades of the players who are not fighting (set_absent_upgrades),
    // which retail creates as objects and the port does not.
    // #664: each payment goes to the bounties_paid feed for the local player's floating "+n" text (0x2de390, the host).
    // [port] Not modelled: the log line.
    if (!credits_add_ || killer < 0) return;
    if (!galaxy_->story_gates().bounty_collection) return;
    const auto& ps = galaxy_->players();
    const objects::ObjectStore& st = mode.primary();
    const int32_t owner = st.owner(victim.h);
    if (static_cast<size_t>(killer) >= ps.size() || owner < 0 || static_cast<size_t>(owner) >= ps.size()) return;
    if (owner == killer || bt_allied(owner, killer)) return;
    const float b = galactic::type_bounty_value(catalog_->bounty_table(cfg_.skirmish), catalog_->types(),
                                                static_cast<objects::TypeIndex>(victim.type), cfg_.skirmish);
    if (!(b > 0.0f)) return;
    const uint32_t vf = ps[static_cast<size_t>(owner)].faction;
    const auto& factions = galaxy_->rules().factions;
    if (vf >= factions.size()) return;
    const std::string name = ascii_lower(factions[vf].name);
    struct Held {
        int32_t owner;
        const BountyOnFactionDef* def;
    };
    std::vector<Held> held;
    const auto add_holder = [&](uint32_t type, int32_t o) {
        if (o < 0) return;
        for (const BountyOnFactionDef& d : bounty_defs_of(type))
            if (std::find(d.factions.begin(), d.factions.end(), name) != d.factions.end()) held.push_back({o, &d});
    };
    for (const Unit& u : units_)
        if (&u == &victim || (u.alive && !u.retreated && st.alive(u.h))) add_holder(u.type, st.owner(u.h));
    for (const HgUpgradeObject& o : hg_upgrade_objs_)
        if (st.alive(o.h)) add_holder(static_cast<uint32_t>(st.type(o.h)), st.owner(o.h));
    for (const auto& [p, t] : absent_upgrades_) add_holder(t, p);  // #542: a non-fighting player's corruption upgrades
    if (held.empty()) return;
    std::vector<BountyEntry> entries;
    for (int32_t p = 0; p < static_cast<int32_t>(ps.size()); ++p) {
        if (!bt_allied(p, killer)) continue;
        entries.clear();
        for (const Held& h : held)
            if (h.owner == p || (h.def->all_allies && bt_allied(h.owner, p))) entries.push_back({h.def->category, h.def->percentage});
        const float f = bounty_fold(entries);
        if (!(f > 0.0f)) continue;
        const int32_t n = bounty_credits(f, b);
        credits_add_(p, static_cast<float>(n));
        bounties_paid_.push_back({p, n, st.position(victim.h), static_cast<int32_t>(victim.type)});  // #664
        ++stats_.bounties;
        stats_.bounty_credits += n;
    }
}

}  // namespace alamo::sim
