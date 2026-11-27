#pragma once
// the test-only seam of TacticalBattle. The battle's public interface hands out read-only views of its inner
// systems; tests that drive them (activate an ability, reveal the fog, crush a unit) or call the test hooks go through
// this friend instead. Tests only: engine code never includes this header.
#include "alamo/sim/tactical_battle.hpp"

namespace alamo::sim {

struct TacticalBattleTestAccess {
    // #661: mark the running battle a sandbox story battle (BattleRequest::sandbox; the pad / upgrade persistence gates).
    static void set_sandbox(TacticalBattle& b, bool sandbox) { b.req_.sandbox = sandbox; }
    // #420: the battle's tech level bonus of `player` (Tactical_Build_Increments_Tech_Level builds minus destructions)
    static int32_t tech_bonus(const TacticalBattle& b, int32_t player) {
        return player >= 0 && static_cast<size_t>(player) < b.hg_tech_bonus_.size() ? b.hg_tech_bonus_[static_cast<size_t>(player)] : 0;
    }
    // #418: the BOMBARD_DELAY story reward's override (TacticalBattle::reward_bombard_delay; 0 = none)
    static float bombard_delay_override(const TacticalBattle& b) { return b.bombard_delay_override_; }
    static tactical::abilities::AbilitySystem& abilities(TacticalBattle& b) { return *b.abilities_; }
    // #487: the battle's special-weapon packs, mutable (a test restarts a new pack's charge: packs start ready).
    static tactical::abilities::SpecialWeaponSystem& special_weapons(TacticalBattle& b) { return b.l1_sw_; }
    // #88: the battle's movement system, mutable (a test puts fighters in Dead_Stop, whose retail writer is untraced).
    static tactical::movement::MovementSystem& movement(TacticalBattle& b) { return *b.movement_; }
    // #755: Die on a battle unit between ticks, its events consumed as a script kill's (consume_kill_events)
    static void kill(TacticalBattle& b, TacticalMode& mode, objects::ObjectId id) {
        b.combat_->kill_unit(id, tactical::combat::kNoObject, tactical::combat::kKindNormal, b.frame_);
        if (b.bt_rng_) b.consume_kill_events(mode, *b.bt_rng_);
    }
    // #491: the land formation records, mutable (a test puts a block flag no port writer sets, 0x40).
    static std::vector<LandFormationRecord>& land_formations(TacticalBattle& b) { return b.lformations_; }
    // #44 t01: a unit's DEPLOY_TROOPERS attachment (the walker and the bone index; kNoObjectId / -1 when free). False for
    // an unknown unit.
    static bool attachment(TacticalBattle& b, objects::ObjectId id, objects::ObjectId& to, int32_t& bone) {
        const TacticalBattle::Unit* u = b.unit_by_id(id);
        if (!u) return false;
        to = u->attached_to;
        bone = u->attach_bone;
        return true;
    }
    // #646: the build check and its parts (the AI capture's one-pass builders).
    static uint8_t build_check(const TacticalBattle& b, const objects::ObjectStore& st, int32_t p, objects::ObjectId id, objects::TypeIndex t) {
        return b.hg_build_check(st, p, id, t);
    }
    static uint8_t build_limits(const TacticalBattle& b, const objects::ObjectStore& st, int32_t p, objects::TypeIndex t,
                                bool counted = false) {
        if (!counted) return b.hg_build_limits(st, p, t);
        const TacticalBattle::HgTypeCounts c = TacticalBattle::hg_type_counts(st);
        return b.hg_build_limits(st, p, t, &c);
    }
    static void builders(const TacticalBattle& b, const objects::ObjectStore& st, int32_t p, std::span<const objects::TypeIndex> types,
                         std::vector<std::vector<objects::ObjectId>>& out) {
        b.hg_builders(st, p, types, out);
    }
    // The battle's fog, mutable (null when fog is off or the battle is not set up).
    static tactical::fog::FogOfWar* fog(TacticalBattle& b) { return b.fog_rw(); }
    // #90: the Redirect_Blaster facing order the battle installs as AbilityHost::face_direction.
    static void face_direction(TacticalBattle& b, objects::ObjectId id, tactical::Vec3 dir) { b.ab_face_direction(id, dir); }
#ifdef ALAMO_SIM_HAVE_AI_TACTICAL
    static ai::tactical::DamageTrackingStore& damage_tracking(TacticalBattle& b) { return b.damage_tracking_; }
#endif
    // CombatHooks::attack_idle on `id` and a clip play on the battle's animation world (TacticalBattle::test_*).
    static bool attack_idle(TacticalBattle& b, objects::ObjectId id, bool arg, SimRng& rng) { return b.test_attack_idle(id, arg, rng); }
    static bool play_clip(TacticalBattle& b, objects::ObjectId id, int32_t clip_type) { return b.test_play_clip(id, clip_type); }
    // `crusher` crushes `victim` now, as a movement crush event does.
    static bool crush(TacticalBattle& b, objects::ObjectId crusher, objects::ObjectId victim) { return b.test_crush(crusher, victim); }
    // #357: spin-away wreck `id`'s path control points (empty when V = 0) and travelled distance; false: no such wreck.
    static bool spin_wreck(const TacticalBattle& b, objects::ObjectId id, std::vector<Vec3>& pts, float& dist) {
        for (const TacticalBattle::SpinWreck& w : b.spin_wrecks_)
            if (w.id == id) {
                pts = w.pts;
                dist = w.dist;
                return true;
            }
        return false;
    }
    // #221: the battle's-end removal of every death clone left (no Death_Explosions_End spawn).
    static void drop_death_clones(TacticalBattle& b, TacticalMode& mode) { b.remove_death_clones(mode, nullptr); }
    // the joint box half-extents of movement-backed land squad container `id`.
    static bool squad_joint_box(const TacticalBattle& b, const objects::ObjectStore& st, objects::ObjectId id, float& hx, float& hy) {
        return b.squad_joint_box(st, id, hx, hy);
    }
    // tst2 t03 (bkt1 coverage): the land attack staging 0x40e260 (attack_stage_goal), a land attack plan
    // (land_attack_order; returns the AttackPlan as an int) and the map extents the staging's search keeps inside.
    static Vec3 stage_goal(TacticalBattle& b, const TacticalMode& mode, std::span<const objects::ObjectId> ids, objects::ObjectId target,
                           Vec3 tp, size_t movers) {
        return b.attack_stage_goal(mode, ids, target, tp, movers);
    }
    static int land_attack_order(TacticalBattle& b, TacticalMode& mode, std::span<const objects::ObjectId> ids, objects::ObjectId target,
                                 Vec3 tp, uint8_t flags, SimRng& rng, bool guard) {
        return static_cast<int>(b.land_attack_order(mode, ids, target, tp, flags, false, rng, guard));
    }
    static tactical::Aabb map_extents(const TacticalBattle& b, const TacticalMode& mode) { return b.map_extents(mode); }
    // #156: the land terrain box map_extents caches at setup (nullopt: no terrain grid).
    static std::optional<tactical::Aabb> land_terrain_box(const terrain::Terrain& t) { return TacticalBattle::land_terrain_box(t); }
    // attack_target_unseen for battle unit `viewer` (false, false when it is not one).
    static void target_unseen(TacticalBattle& b, const objects::ObjectStore& st, objects::ObjectId viewer, objects::ObjectId target,
                              bool& hidden, bool& stealthed) {
        hidden = stealthed = false;
        if (const TacticalBattle::Unit* u = b.unit_by_id(viewer)) b.attack_target_unseen(st, *u, target, hidden, stealthed);
    }
    // #91: the last-observed point (AttackBlock::ox / oy / oz) of mover `mover`'s land attack block (false: no block).
    static bool attack_block_seen(const TacticalBattle& b, objects::ObjectId mover, Vec3& p) {
        for (const TacticalBattle::AttackBlock& r : b.attack_blocks_)
            if (r.mover == mover) {
                p = Vec3{r.ox, r.oy, r.oz};
                return true;
            }
        return false;
    }
    // #222: a Production command as the battle AI issues it (BattleHost Production: hg_production_command with ai = true).
    static commands::RejectReason ai_production(TacticalBattle& b, TacticalMode& mode, uint8_t player, const commands::EventData& ev) {
        return b.hg_production_command(mode, player, ev, true);
    }
    // the 8 (x, y) points of object `h`'s fog multisample footprint.
    static void fog_footprint(const TacticalBattle& b, const objects::ObjectStore& st, objects::ObjectHandle h, float* xy16) {
        b.fog_footprint_samples(st, h, xy16);
    }
    // #327 t05: one space hangar launch of `type` with its flyout point `center` (hg_spawn, space_launch).
    static objects::ObjectId hangar_space_launch(TacticalBattle& b, TacticalMode& mode, int32_t type, int32_t owner, Vec3 center,
                                                 SimRng& rng) {
        std::vector<objects::ObjectId> ordered;
        return b.hg_spawn(mode, type, owner, center, Vec3{1.0f, 0.0f, 0.0f}, center, true, rng, ordered, true);
    }
};

}  // namespace alamo::sim
