// #357: spin-away death wrecks as simulation state (docs/re/SPIN_AWAY_DEATH.md): the DeathBehavior service 0x58d430's spin
// branch and its path builder 0x58c7a0 for the death clones whose plan spins away (schedule_death_clone), and the movable
// units' position snapshots the clone's V comes from (0x29f270).
#include "tactical_battle_internal.hpp"
#include "alamo/tactical/movement/fighter.hpp"  // 0x20acd0 angles, wrap180

namespace alamo::sim {

using namespace bo3split;

void TacticalBattle::snapshot_unit_motion(const objects::ObjectStore& store) {
    // The object service 0x3a6b80 copies its locomotor pack's +0x164 snapshot to +0x194 and the object matrix to +0x164 at
    // its start (MOVEMENT s78n t26 RE 2 [H]); [port] once per tick, before the movement, for the movable battle units (an
    // object without a locomotor pack has V = 0). The first snapshot fills both (no motion yet).
    for (Unit& u : units_) {
        if (!u.alive || !u.movable || !store.alive(u.h)) continue;
        const Vec3 p = store.position(u.h);
        u.snap1 = u.snapped ? u.snap0 : p;
        u.snap0 = p;
        u.snapped = true;
    }
}

TacticalBattle::DeathMotion TacticalBattle::death_motion(const Unit& u) const {
    // 0x29f270 [H formula; M for the frame alignment]: V = (30 (cur.x - prev.x), (cur.y - prev.y) 30, (cur.z - prev.z) 30)
    // from the pack's two snapshots; the clone takes the dying object's orientation (+0x84 roll, +0x88 pitch).
    DeathMotion m;
    if (u.movable && u.snapped) {
        m.v = Vec3{30.0f * (u.snap0.x - u.snap1.x), (u.snap0.y - u.snap1.y) * 30.0f, (u.snap0.z - u.snap1.z) * 30.0f};
        if (movement_ && !movement_->attitude(u.id, m.roll, m.pitch)) m.roll = m.pitch = 0.0f;
    }
    return m;
}

const TacticalBattle::SpinWreck* TacticalBattle::spin_wreck(objects::ObjectId id) const {
    for (const SpinWreck& w : spin_wrecks_)
        if (w.id == id) return &w;
    return nullptr;
}

void TacticalBattle::rebuild_spin_paths() {
    // the load 0x58c030 [H]: the control points re-appended in order, then Build(20); the coefficients and segment lengths
    // are recomputed, not saved.
    for (SpinWreck& w : spin_wrecks_) {
        w.spline.clear();
        for (const Vec3& p : w.pts) w.spline.append(tv(p));
        w.spline.build(20);
    }
}

void TacticalBattle::drop_gone_wrecks(const objects::ObjectStore& store) {
    spin_wrecks_.erase(std::remove_if(spin_wrecks_.begin(), spin_wrecks_.end(), [&store](const SpinWreck& w) { return !store.alive(w.h); }),
                       spin_wrecks_.end());
}

void TacticalBattle::spin_wreck_path(SpinWreck& w, Vec3 P0, float seconds, SimRng& rng) const {
    // 0x58c7a0 [H, every constant and operand order read in the disassembly 0x58c8d2..0x58ccf0]. V = 0: no path, no draw.
    w.pts.clear();
    w.spline.clear();
    const Vec3 V = w.v;
    if (dmath::sqrt((V.x * V.x + V.y * V.y) + V.z * V.z) == 0.0f) return;
    const float Ts = seconds;
    const Vec3 E{P0.x + Ts * V.x, P0.y + Ts * V.y, P0.z + Ts * V.z};
    const Vec3 d{E.x - P0.x, E.y - P0.y, E.z - P0.z};
    const Vec3 m{(P0.x + E.x) * 0.5f + 1.0f, (P0.y + E.y) * 0.5f + 1.0f, (P0.z + E.z) * 0.5f + 1.0f};
    Vec3 c1{m.y * d.z - m.z * d.y, m.z * d.x - m.x * d.z, m.x * d.y - d.x * m.y};
    Vec3 c2{c1.y * d.z - c1.z * d.y, c1.z * d.x - c1.x * d.z, c1.x * d.y - d.x * c1.y};  // from the raw c1
    if (const float l1 = dmath::sqrt((c1.x * c1.x + c1.y * c1.y) + c1.z * c1.z); l1 > 0.0f) {
        const float r = 1.0f / l1;
        c1 = Vec3{c1.x * r, c1.y * r, c1.z * r};
    }
    if (const float l2 = dmath::sqrt((c2.x * c2.x + c2.y * c2.y) + c2.z * c2.z); l2 > 0.0f) {
        const float r = 1.0f / l2;
        c2 = Vec3{c2.x * r, c2.y * r, c2.z * r};
    }
    const float L = dmath::sqrt((d.y * d.y + d.x * d.x) + d.z * d.z);  // sum order y, x, z
    const float k = L * 0.3f;
    if (L < 0.0f) return;  // never (the original's test)
    const Vec3 P1{(P0.x + E.x) * 0.5f, (P0.y + E.y) * 0.5f, (P0.z + E.z) * 0.5f};
    // one synced Get_Float(0, 2 pi) (0x1ffbb0, [0, 2 pi] inclusive); sinf / cosf with the tiny-angle shortcut (theta 0
    // exactly: s = theta, c = 1). [port] dmath::sin / cos for the CRT's (parity by behaviour, as the rocket spline).
    const float theta = rng.range_float(0.0f, 6.2831855f);
    float s = theta, c = 1.0f;
    if (theta != 0.0f) {
        s = dmath::sin(theta);
        c = dmath::cos(theta);
    }
    const Vec3 P2{E.x + (s * c2.x + c * c1.x) * k, E.y + (c * c1.y + s * c2.y) * k, E.z + std::fabs(c * c1.z + s * c2.z) * k};
    w.pts = {P0, P1, P2, E};
    for (const Vec3& p : w.pts) w.spline.append(tv(p));
    w.spline.build(20);  // 0x22ffc0(spline, 0x14): the spacing sizes the polyline table only
    w.dist = 0.0f;
}

void TacticalBattle::service_spin_wrecks(TacticalMode& mode, SimRng& rng) {
    // DeathBehavior service 0x58d430, spin branch [H, decompile and disassembly 0x58d484..0x58d5f8], once per tick for every
    // wreck in spawn order. [port] The first service is on the tick after the death (the clone's place in the object walk
    // is untraced, docs/re/SPIN_AWAY_DEATH.md §1).
    objects::ObjectStore& store = mode.primary();
    drop_gone_wrecks(store);  // removed by another path (a collision push kill)
    if (spin_wrecks_.empty()) return;
    const objects::GameObjectTypes& types = catalog_->types();
    const tc::CombatConfig& cc = combat_->config();
    size_t keep = 0;
    for (size_t i = 0; i < spin_wrecks_.size(); ++i) {
        SpinWreck& w = spin_wrecks_[i];
        const objects::TypeIndex t = store.type(w.h);
        const float seconds = types.get(t, objects::tags::Spin_Away_On_Death_Time);  // ledger: consumes GameObjectType.Spin_Away_On_Death_Time
        const Vec3 pos = store.position(w.h);
        if (w.acc == 0.0f) {  // the first service (ucomiss: -0.0 too); the roll is never wrapped, so r0 = -20 n re-runs it
            spin_wreck_path(w, pos, seconds, rng);
            w.acc = w.roll;
        }
        const float speed = dmath::sqrt((w.v.x * w.v.x + w.v.y * w.v.y) + w.v.z * w.v.z);
        const float step = speed / 30.0f;  // (float)FPS
        const float limit = speed * seconds;
        w.dist = step + w.dist;
        tactical::Vec3 s;
        if (w.dist >= limit || step == 0.0f || w.pts.empty() || !w.spline.sample(w.dist, s)) {
            // the end: phase 0 (Death_Explosions) at the wreck, its Death_SFXEvent_Start_Die, then the world removal 0x391310 -
            // the #221 clone removal (remove_death_clones, next in expire_objects), whose deletion runs phase 2.
            spawn_death_explosion_start(mode, static_cast<uint32_t>(t), pos, store.owner(w.h), rng, false);
            present_spin_end(store.owner(w.h), w.id, static_cast<int32_t>(t), pos);
            for (TimedObject& c : death_clones_)
                if (c.id == w.id) c.frame = frame_;
            continue;
        }
        const Vec3 p{s.x, s.y, s.z};
        // Turn_Toward 0x3abe40(obj, angles_to(pos, P), -1.0, yaw, pitch): rate <= 0 -> 0x372560 = the clone type's
        // Max_Rate_Of_Turn x Object_Max_Speed_Multiplier_{Space,Land} x 1.0 degrees a service ([port] the type flag +0x790
        // that skips the multiplier is not read, as CombatSystem::projectile_turn_rate).
        const mv::FAngles a = mv::fighter_vector_angles(mv::F3{pos.x, pos.y, pos.z}, mv::F3{p.x, p.y, p.z});
        const float rate = types.get(t, objects::tags::Max_Rate_Of_Turn) * (space_ ? cc.turn_multiplier_space : cc.turn_multiplier_land);
        const auto clamp_turn = [rate](float delta) {
            const float d = mv::fighter_wrap180(delta);  // 0x20b6d0
            if (d == 0.0f) return 0.0f;
            return d > rate ? rate : (d < -rate ? -rate : d);
        };
        float pitch = w.pitch + clamp_turn(a.pitch - w.pitch);  // add_angles 0x20b070: one correction each
        if (pitch >= 360.0f) pitch -= 360.0f;
        else if (pitch <= -360.0f) pitch += 360.0f;
        float yaw = w.yaw + clamp_turn(a.yaw - w.yaw);
        if (yaw >= 360.0f) yaw -= 360.0f;
        else if (yaw < 0.0f) yaw += 360.0f;
        w.acc = w.acc + 20.0f;  // 0x8007e8
        w.roll = w.acc;
        w.pitch = pitch;
        w.yaw = yaw;
        store.set_position(w.h, p);  // 0x3a8f90
        float sy = 0.0f, cy = 1.0f;
        dmath::sincos(yaw * (dmath::kPiF / 180.0f), sy, cy);
        store.set_facing(w.h, Vec3{cy, sy, 0.0f});  // the level yaw frame; pitch and roll through unit_attitude
        // [port] a land clone obstacle (register_clone_obstacle) moves with the wreck (retail marks the transform dirty;
        // how its land collision follows a moving clone is untraced [L]).
        if (movement_ && movement_->has_unit(w.id)) movement_->teleport(w.id, mv::V2{p.x, p.y}, p.z, yaw, frame_);
        if (keep != i) spin_wrecks_[keep] = std::move(w);
        ++keep;
    }
    spin_wrecks_.resize(keep);
}

void TacticalBattle::spin_hash(StateHasher& h) const {
    h.u32(static_cast<uint32_t>(spin_wrecks_.size()));
    for (const SpinWreck& w : spin_wrecks_) {
        h.i32(w.id);
        h.f32(w.v.x);
        h.f32(w.v.y);
        h.f32(w.v.z);
        h.u32(static_cast<uint32_t>(w.pts.size()));
        for (const Vec3& p : w.pts) {
            h.f32(p.x);
            h.f32(p.y);
            h.f32(p.z);
        }
        h.f32(w.dist);
        h.f32(w.acc);
        h.f32(w.roll);
        h.f32(w.pitch);
        h.f32(w.yaw);
    }
}

}  // namespace alamo::sim
