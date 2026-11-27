// alamo_game App: the in-game cinematic camera of a battle - the tactical super weapon's firing shot and the
// explosion shake it carries (legacy docs/re/TSW_PLANET_DESTROY.md §11; the math is battle_fx.hpp), and the battle cam
// (#462, docs/re/BATTLE_CAM.md; battle_cam.hpp): Movie Mode, Cinematic View and b_camera_t.
//
// Presentation only. The shot starts on the Fire cue (TacticalBattle::special_weapon_events, read after the tick), runs on
// the tactical frame count (stops while paused, 30 Hz steps), and only overrides what is drawn and heard: the RTS camera
// controller is saved at the start and restored at the end, the HUD and the cursor are hidden, mouse input is swallowed
// or ends the shot. Nothing here is written into the sim; the shake's NoiseOffset draws come from cine_.rng (unsynced).
#include "game_app.hpp"
#include "alamo/objects/type_fields.hpp"
#include "alamo/ui/hotkeys.hpp"

namespace alamo_game_app {

namespace {
constexpr const char* kStationMarker = "Space Station Position";  // MARKERS.XML (Behavior MARKER)
}

// Fire (0x42a200) -> 0x2ccf30 -> 0x4a5ef0(controller, mode, TSW, dummy, 1) + cinematic mode on (0x49b1a0).
void App::tsw_shot_begin(const TswCue& c, const sim::TacticalBattle& b, const objects::ObjectStore& st) {
    // [port] In_Game_Cinematics false: no shot (the original would hold it at frame 0 until input ends it [L]).
    if (!cine_.enabled || !bview_.active()) return;
    if (c.target == objects::kNoObjectId) return;  // 0x4a5ef0 fails without a dummy: no camera move at all
    const objects::ObjectHandle dh = st.find(c.target);
    if (!st.exists(dh)) return;
    if (cine_.shot.active()) tsw_shot_end();  // a running shot is stopped first (0x2cbb80)
    if (cine_.cam.active()) {                 // and so is the battle cam (#462)
        cine_.cam.stop();
        battle_cam_sync();
    }
    TswShotInputs in;
    in.tsw_position = c.position;
    const auto dp = st.position(dh);
    in.dummy_position = {dp.x, dp.y, dp.z};
    const ui::Vec3 e = shot_half_extents(c.type);
    in.half_extents = e;
    in.z_adjust = shot_z_adjust(c.type);
    // M: the first `Space Station Position` marker of the store query 0x2a9ff0(store, 0x11) [M: the marker list; here
    // the primary store's objects of that type, creation order].
    if (const objects::TypeIndex mt = data_.types.resolve_name(kStationMarker); mt != objects::kNone)
        for (const objects::ObjectHandle h : st.find_all_of_type(mt))
            if (st.alive(h)) {
                const auto mp = st.position(h);
                in.have_station = true;
                in.station_position = {mp.x, mp.y, mp.z};
                break;
            }
    cine_.shot.begin(in, b.frame(), input_.mouse_x(), input_.mouse_y());
    cine_.tsw = c.object;
    cine_.clock = 0;
    ++cine_.shots;
    // Cinematic mode on: the mode camera saved once (restored with its projection at the end; the shot's own near / far
    // planes are applied to the drawn params, TswFiringShot::kNearPlane / kFarPlane); HUD and cursor hidden
    // (cinematic_hides_*); FS +0x37 (30 ticks/s, pacing only); the letterbox slides in. [L] the two toggles 0x13ecc0 /
    // 0x48a3a0 and the counter 0x140b2c198 have no known role: not modelled.
    cine_.saved_camera.save_once(bview_.camera());
    if (session_) session_->set_presentation_rate_lock(true);
    cine_.letterbox.slide_in();
    std::printf("cinematic: TSW firing shot (tsw %u, tick %u, E %.1f %.1f %.1f, station marker %s)\n", static_cast<unsigned>(c.object),
                b.frame(), static_cast<double>(e.x), static_cast<double>(e.y), static_cast<double>(e.z), in.have_station ? "yes" : "no");
}

// TSW state 9: 0x2cdbb0(controller, dummy, the chosen Death_Explosions type, fixed = 1).
void App::tsw_shake(const TswCue& c, const sim::TacticalBattle& b) {
    // §11.1 gate (cinematic_shake): only while the cinematic runs (a battle = land / space mode).
    const objects::TypeIndex ex = tsw_explosion_type(data_.types, c.type, c.pick);
    if (ex == objects::kNone) return;  // no Death_Explosions entry: no explosion, no shake (§11.3)
    if (cinematic_shake(cine_.shake, cine_.shot.active(), true, explosion_jitter_factor(data_.types, ex), kTswShakeDistance,
                        cine_.shot.clock(b.frame()), cine_.rng))
        ++cine_.shakes;
}

// The shot timer ran out, input skipped it, or a new Fire replaces it: End_Cinematic_Mode (0x49b5c0: shake trackers
// freed, HUD / cursor / rate lock back, the saved mode camera restored) and 0x2cbb80 (letterbox out).
// [L] the end-of-shot chain (combat / list / interesting-object shots, held by Tab) is the attract demo's: not modelled,
// a normal battle is expected to find nothing there (probe).
void App::tsw_shot_end() {
    if (!cine_.shot.active()) return;
    std::printf("cinematic: TSW firing shot ended at shot frame %d\n", static_cast<int>(cine_.clock));
    cine_.shot.end();
    cine_.shake.reset();
    if (bview_.active()) cine_.saved_camera.restore(bview_.camera());
    cine_.saved_camera.drop();
    if (session_) session_->set_presentation_rate_lock(false);
    // [port][L] mode +0x98 (slide out over 0.5 s scaled by how far it is in, else removed at once) is unknown: slide out.
    cine_.letterbox.slide_out();
    cine_.tsw = objects::kNoObjectId;
}

void App::cinematic_reset() {
    engine_loop_to(objects::kNoObjectId);
    cine_.interesting = {};
    cine_.glory_seen.clear();  // #572
    cine_.glory_any = false;
    cine_.cam.stop();  // the swallowed keys stay hidden until released (else a held one would read as a new press)
    cine_.cam_on = false;
    cine_.shot.end();
    cine_.shake.reset();
    cine_.saved_camera.drop();
    cine_.letterbox.remove();
    cine_.tsw = objects::kNoObjectId;
    cine_.clock = 0;
    if (session_) session_->set_presentation_rate_lock(false);
}

void App::cinematic_step() {
    cine_.letterbox.update(dt_);
    if (screen() == Screen::Battle) glory_death_path();  // #572: every frame, so a clone counts once, as it appears
    if (cine_.cam.active()) battle_cam_step();
    if (!cine_.shot.active()) return;
    const sim::TacticalBattle* b = battle();
    if (!b || !bview_.active()) {
        cinematic_reset();
        return;
    }
    if (cine_.shot.expired(b->frame())) {  // the shot timer (765 frames)
        ++cine_.completed;
        tsw_shot_end();
        return;
    }
    cine_.clock = cine_.shot.clock(b->frame());
}

// after the frame's Lua ops (lua_presentation) - the hand-over timer on the battle frame, the scripts' lock
// (Lock_Controls on, cinematic camera or mode on) for the P11 block, the bars' slide. No battle: nothing held.
void App::linked_ui_step() {
    const sim::TacticalBattle* b = battle();
    if (!b) {
        linked_ui_.reset();
        return;
    }
    bool locked = false;
#ifdef ALAMO_SIM_HAVE_STORY
    if (sim::StorySession* story = session_->host().story())
        if (const lua::PresentationLog* log = story->presentation()) {
            const lua::PresentationState& ps = log->state();
            locked = ps.controls_locked || ps.cinematic_mode || ps.camera == lua::CameraMode::Cinematic;
        }
#endif
    linked_ui_.update(b->frame(), locked, dt_);
}

// 0x2cc3e0 (no GUI dialog has the focus): the controller is offered the mouse events before the HUD and the world and
// consumes them. A move within 1/20 of the screen of the previous cursor is swallowed (#564: the baseline follows the
// cursor); a larger one or any button / wheel event ends the shot at once. [M] the event codes 2, 4, 5, 7, 8, 10, 12 = button / wheel events. Keys are not consumed.
void App::cinematic_input(ui::InputFrame& raw) {
    battle_cam_input(raw);
    if (!cine_.shot.active() || ui_.gui.has_focus()) return;
    const bool button = raw.buttons_pressed != 0 || raw.buttons_released != 0 || raw.wheel != 0.0f;
    const bool cancel = cine_.shot.input_cancels(raw.mouse_x, raw.mouse_y, raw.width, raw.height, button);
    raw.buttons_pressed = raw.buttons_released = raw.buttons_down = 0;
    raw.wheel = 0.0f;
    if (cancel) {
        ++cine_.skipped;
        tsw_shot_end();
    }
}

// End of 0x49cd00: the keyframed eye / look at the shot clock (attached keys on the TSW's drawn transform), the shake,
// the shot's near / far planes; FOV stays the mode camera's (no FOV keys).
bool App::cinematic_camera(ui::CameraParams& cp) {
    if (story_camera_view(cp)) return true;  // the story script's cinematic camera (Start_Cinematic_Camera)
    if (!cine_.shot.active()) return battle_cam_camera(cp);
    ObjectFrame f;
    (void)drawn_object(cine_.tsw, &f);
    const int32_t c = cine_.clock;
    const ui::Vec3 pos = cine_.shot.eye(c, f), look = cine_.shot.look(c, f);
    const CinematicPose p = apply_shake(pos, look, 0.0f, cine_.shake, c);  // no roll keys
    cp.eye = p.eye;
    cp.target = p.look;
    cp.up = p.up;
    cp.roll = p.roll_deg * (3.14159265358979f / 180.0f);
    const ui::Vec3 d = p.eye - p.look;
    const float dist = std::sqrt(tactical::dot(d, d));
    if (dist > 0.0f) {  // the orbit form of the same view (render_bridge uses it when up is +Z)
        cp.distance = dist;
        cp.orbit_yaw = std::atan2(d.y, d.x);
        cp.orbit_pitch = std::asin(std::clamp(d.z / dist, -1.0f, 1.0f));
    }
    cp.znear = TswFiringShot::kNearPlane;
    cp.zfar = TswFiringShot::kFarPlane;
    return true;
}

// ---- shared shot inputs ----------------------------------------------------------------------------------------------
const render::SnapshotObject* App::drawn_object(objects::ObjectId id, ObjectFrame* frame) const {
    const std::vector<render::SnapshotObject>& objs = bview_.frame().objects;  // ascending id
    const auto it = std::lower_bound(objs.begin(), objs.end(), id,
                                     [](const render::SnapshotObject& o, objects::ObjectId v) { return o.id < v; });
    if (it == objs.end() || it->id != id) return nullptr;
    if (frame) {
        const assets::Mat3x4 m = render::object_transform(*it, 1.0f);  // rotation + translation (E carries the scale) [M]
        frame->valid = true;
        frame->position = {m.m[0][3], m.m[1][3], m.m[2][3]};
        for (int j = 0; j < 3; ++j) frame->axis[j] = {m.m[0][j], m.m[1][j], m.m[2][j]};
    }
    return &*it;
}

// E: [M] the render model's box (model path; the world AABB 0x3982b0 without a model) x Scale_Factor. [port] no model
// loaded yet: a cube of the drawn sphere radius, else 200.
ui::Vec3 App::shot_half_extents(objects::TypeIndex type) {
    ui::Vec3 e = bview_.type_half_extents(type);
    if (!(e.x > 0.0f) || !(e.y > 0.0f) || !(e.z > 0.0f)) {
        const float r = bview_.type_radius(type);
        const float h = r > 0.0f ? r : 200.0f;
        e = {h, h, h};
    }
    return e;
}

// Ranged_Target_Z_Adjust (type +0x4dc), constructor default 6.0: 0x49d640 adds it to every attached key's z.
float App::shot_z_adjust(objects::TypeIndex type) const {
    static const uint32_t f = objects::find_type_field("Ranged_Target_Z_Adjust");
    const objects::FieldValue* v = f != 0xFFFFFFFFu ? data_.types.value(type, f) : nullptr;
    return v ? v->f : 6.0f;
}

// ---- #462 the battle cam (docs/re/BATTLE_CAM.md) ------------------------------------------------------------------------
namespace {
// The land clash list as the battle cam walks it (land mode +0x3f8). [port] The melee clashes, then the action clashes,
// in start order; a clash is known by its kind and first participant. Its participants: the two heroes, or the
// action's objects.
struct ClashEntry {
    uint64_t key;
    std::vector<objects::ObjectId> who;
};
std::vector<ClashEntry> clash_list(const tactical::combat::HeroClashManager& m) {
    std::vector<ClashEntry> list;
    for (const auto& c : m.clashes()) list.push_back({(uint64_t{1} << 32) | static_cast<uint32_t>(c.hero1), {c.hero1, c.hero2}});
    for (const auto& c : m.action_clashes())
        if (!c.objects.empty()) list.push_back({(uint64_t{2} << 32) | static_cast<uint32_t>(c.objects.front()), c.objects});
    return list;
}
bool modifier_key(uint32_t k) { return k == ui::vk::kShift || k == ui::vk::kControl || k == ui::vk::kAlt || (k >= 0xA0u && k <= 0xA5u); }
}  // namespace

BattleCamGates App::battle_cam_gates() const {
    BattleCamGates g;
    g.in_game_cinematics = cine_.enabled;
    if (session_)
        if (const galactic::GalacticSim* gs = session_->galaxy()) g.combat_cinematic = gs->story_gates().combat_cinematic;
    g.controls_locked = story_controls_locked();
    g.space = bview_.space();
    return g;
}

BattleCamPicks App::battle_cam_picks() {
    BattleCamPicks p;
    // 0x4a6790: the local player's first selected object (it must belong to the local player), or the framed one.
    p.view = [this](uint32_t target, uint32_t& id, ui::Vec3& half_extents) {
        const auto& sel = orders_.selection();
        if (target == 0 && sel.empty()) return false;
        const render::SnapshotObject* o = drawn_object(static_cast<objects::ObjectId>(target != 0 ? target : sel.front()), nullptr);
        if (!o || o->owner != local_player()) return false;
        id = static_cast<uint32_t>(o->id);
        half_extents = shot_half_extents(o->type);
        return true;
    };
    // 0x4a4ac0 on the land clash list (clash_list).
    p.clash = [this](uint64_t after, BattleCamShot& out) {
        const sim::TacticalBattle* b = battle();
        if (!b || bview_.space()) return false;  // space and galactic have no clash list (vf +0x210 = null)
        const std::vector<ClashEntry> list = clash_list(b->hero_clashes());
        if (list.empty()) return false;
        size_t pick = 0;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].key == after) {
                if (list.size() == 1) return false;  // never the same clash again
                pick = (i + 1) % list.size();
                break;
            }
        for (const objects::ObjectId id : list[pick].who) {
            const render::SnapshotObject* o = drawn_object(id, nullptr);
            // (+0x3a0 & 2) == 0 [L: not dying]: the drawn object is not dying nor a death clone.
            if (!o || o->owner != local_player() || (o->flags & (render::kSnapDying | render::kSnapDeathClone)) != 0) continue;
            out = hero_clash_shot(static_cast<uint32_t>(o->id), list[pick].key, shot_half_extents(o->type), bview_.space(), cine_.rng.range(0, 1) != 0);
            return true;
        }
        return false;  // no participant of the local player
    };
    p.interesting = [this](bool first, BattleCamShot& out) { return interesting_object_shot(first, out); };
    // #563 0x49f370: the local player's land bombing run (the sim's presentation query).
    p.bombing_run = [this](BattleCamShot& out) {
        const sim::TacticalBattle* b = battle();
        if (!b || bview_.space()) return false;  // mode type 1 only
        sim::TacticalBattle::BombingRunCamera c;
        if (!b->bombing_run_camera(local_player(), c)) return false;
        BombingRunView v;
        v.bomber = static_cast<uint32_t>(c.bomber);
        v.target = static_cast<uint32_t>(c.marker);
        v.bomber_at = {c.bomber_at.x, c.bomber_at.y, c.bomber_at.z};
        v.target_at = {c.marker_at.x, c.marker_at.y, c.marker_at.z};
        v.speed = c.speed;
        v.range = c.range;
        return bombing_run_shot(v, cine_.rng, out);
    };
    return p;
}

// #568 0x2cd930 / 0x2cd0d0 / 0x4a53f0 (docs/re/BATTLE_CAM.md "The interesting-object shot"): the pick reads the drawn
// objects and the sim (presentation only), then the template is built on the picked object's drawn transform.
// [port] Readings of retail's object fields: "in the world, not hidden" = drawn, not dying or contained; "a model" =
// drawable; "may follow" (+0x3a0 & 0x40 clear) = drawn and not gone; fogged = hidden from the local player (FOW); the
// selection is its drawn objects of the local player; the ObjBox and the ModelBox are one box (shot_half_extents);
// health = the drawn health; speed = the movement's current speed; power = the type's AI_Combat_Power (a FLEET's
// members are not summed); the target count leaves out the hardpoints' targets. #571: a hardpoint is live when it is
// not destroyed (CombatSystem::hardpoint_destroyed), and its target (0x3856a0 [M]: CombatSystem::hardpoint_target) must
// be drawn with a model; the hardpoint shot's target gate reads "not fogged or hidden" as drawn, not fog-hidden and not
// gone, and the fade as the drawn fade.
bool App::interesting_object_shot(bool first, BattleCamShot& out) {
    const sim::TacticalBattle* b = battle();
    const sim::TacticalMode* tm = session_ ? session_->host().modes().top_tactical() : nullptr;
    if (!b || !tm || !bview_.active()) return false;
    const objects::ObjectStore& st = tm->primary();
    const bool space = bview_.space();
    constexpr uint8_t kGone = render::kSnapDying | render::kSnapDeathClone | render::kSnapCorpse | render::kSnapContained;
    namespace beh = objects::beh;
    const auto has = [&st](objects::ObjectId id, objects::BehaviorTypeId bt) {
        const objects::ObjectHandle h = st.find(id);
        return st.alive(h) && st.has_behavior(h, bt);
    };
    const auto flying = [b](objects::TypeIndex t) {  // FlyingLocomotorBehaviorClass: the mode's locomotor token
        return ascii_iequals(b->locomotor_token(static_cast<uint32_t>(t)), "FLYING_LOCOMOTOR");
    };
    const objects::EnumConverter& cats = data_.types.enums().get(objects::EnumId::GameObjectCategory);
    const uint64_t wall = static_cast<uint64_t>(cats.lookup("Wall").value_or(0x800000));
    InterestingWorld w;
    w.weights = saliency_;
    for (const uint32_t id : orders_.selection())
        if (const render::SnapshotObject* o = drawn_object(static_cast<objects::ObjectId>(id), nullptr); o && o->owner == local_player())
            w.selection.push_back(id);
    w.type_of = [this](uint32_t id) {
        const render::SnapshotObject* o = drawn_object(static_cast<objects::ObjectId>(id), nullptr);
        return o && (o->flags & kGone) == 0 ? static_cast<int32_t>(o->type) : -1;
    };
    w.may_follow = [this](uint32_t id) {
        const render::SnapshotObject* o = drawn_object(static_cast<objects::ObjectId>(id), nullptr);
        return o && (o->flags & kGone) == 0;
    };
    // #571: the hardpoint's target as 0x3856a0 returns it: drawn, with a model.
    const auto hardpoint_target = [this, b](uint32_t id, uint32_t hp) -> uint32_t {
        const objects::ObjectId t = b->combat().hardpoint_target(static_cast<objects::ObjectId>(id), hp);
        const render::SnapshotObject* o = t != objects::kNoObjectId ? drawn_object(t, nullptr) : nullptr;
        return o && (o->flags & render::kSnapDrawable) != 0 ? static_cast<uint32_t>(t) : 0u;
    };
    w.hardpoint_count = [b](uint32_t id) { return b->combat().hardpoint_count(static_cast<objects::ObjectId>(id)); };
    w.hardpoint_ready = [b, &hardpoint_target](uint32_t id, uint32_t hp) {
        return !b->combat().hardpoint_destroyed(static_cast<objects::ObjectId>(id), hp) && hardpoint_target(id, hp) != 0;
    };
    w.target = [this, b, &has](uint32_t id) -> uint32_t {
        const objects::ObjectId t = b->combat().current_target(static_cast<objects::ObjectId>(id));
        const render::SnapshotObject* o = t != objects::kNoObjectId ? drawn_object(t, nullptr) : nullptr;
        if (!o || (o->flags & (kGone | render::kSnapFogHidden)) != 0 || (o->flags & render::kSnapDrawable) == 0) return 0;
        if (has(t, beh::Team) || has(t, beh::Transport)) return 0;
        return static_cast<uint32_t>(t);
    };
    // Step C: every drawn mobile object (any owner) with a model that is not a wall, not flying on land, not fogged.
    const std::vector<render::SnapshotObject>& objs = bview_.frame().objects;
    std::vector<const render::SnapshotObject*> step_c;
    for (const render::SnapshotObject& o : objs) {
        if ((o.flags & render::kSnapFogHidden) != 0 || (o.flags & render::kSnapDrawable) == 0 || !has(o.id, beh::Locomotor)) continue;
        if ((data_.types.get_mask(o.type, "CategoryMask") & wall) != 0) continue;
        if (!space && flying(o.type)) continue;
        step_c.push_back(&o);
    }
    // Step D, pass 2: faded in, in the world, not a TEAM or TRANSPORT. Pass 1's target counts run over step C's list.
    std::vector<const render::SnapshotObject*> cand;
    for (const render::SnapshotObject* o : step_c)
        if ((o->flags & kGone) == 0 && o->fade >= 0.9f && !has(o->id, beh::Team) && !has(o->id, beh::Transport)) cand.push_back(o);
    for (const render::SnapshotObject* o : cand) {
        SaliencyFeatures f;
        const ui::Vec3 e = shot_half_extents(o->type);
        f.size = tactical::dot(e, e);
        f.power = data_.types.get_float(o->type, "AI_Combat_Power");
        f.x = o->position.x;
        f.y = o->position.y;
        f.health = o->health;
        for (const render::SnapshotObject* c : step_c)
            if (b->combat().current_target(c->id) == o->id) f.targets += 1.0f;
        f.speed = b->movement().view(o->id).speed;
        w.candidate_ids.push_back(static_cast<uint32_t>(o->id));
        w.candidate_types.push_back(static_cast<int32_t>(o->type));
        w.candidate_features.push_back(f);
    }
    if (!interesting_pick(cine_.interesting, first, w, cine_.rng)) return false;
    ObjectFrame frame;
    const render::SnapshotObject* o = drawn_object(static_cast<objects::ObjectId>(cine_.interesting.current), &frame);
    if (!o) return false;
    InterestingObject io;
    io.id = static_cast<uint32_t>(o->id);
    io.category = data_.types.get_mask(o->type, "CategoryMask");
    io.locomotor = has(o->id, beh::Locomotor);
    io.flying = flying(o->type);
    io.model = (o->flags & render::kSnapDrawable) != 0;
    io.half_extents = shot_half_extents(o->type);
    io.frame = frame;
    io.type_name = data_.types.name(o->type);
    // #570 0x397a40(obj, 0): the current target, any drawn object (the attack run and the structure shot read it).
    if (const objects::ObjectId t = b->combat().current_target(o->id); t != objects::kNoObjectId) {
        ObjectFrame tf;
        if (const render::SnapshotObject* to = drawn_object(t, &tf)) {
            io.combat_target.id = static_cast<uint32_t>(t);
            io.combat_target.model = (to->flags & render::kSnapDrawable) != 0;
            io.combat_target.half_extents = shot_half_extents(to->type);
            io.combat_target.frame = tf;
        }
    }
    // #571 the dispatcher's step 3 (0x4a53f0): the drawn hardpoint's target now, then the template's gates on it.
    io.hardpoint = cine_.interesting.hardpoint;
    if (io.hardpoint >= 0)
        if (const uint32_t t = hardpoint_target(io.id, static_cast<uint32_t>(io.hardpoint)); t != 0) {
            const render::SnapshotObject* to = drawn_object(static_cast<objects::ObjectId>(t), nullptr);
            io.hardpoint_target = t;
            io.hardpoint_target_shown =
                to && (to->flags & (kGone | render::kSnapFogHidden)) == 0 && to->fade >= 0.75f;  // 0x4a48ae: fade >= 0.75
        }
    InterestingCategories c;
    c.fighter = static_cast<uint64_t>(cats.lookup("Fighter").value_or(0x1));
    c.bomber = static_cast<uint64_t>(cats.lookup("Bomber").value_or(0x2));
    c.frigate = static_cast<uint64_t>(cats.lookup("Frigate").value_or(0x10));
    c.corvette = static_cast<uint64_t>(cats.lookup("Corvette").value_or(0x8));
    c.infantry = static_cast<uint64_t>(cats.lookup("Infantry").value_or(0x80));
    c.vehicle = static_cast<uint64_t>(cats.lookup("Vehicle").value_or(0x100));
    io.glory_files = glory_files(o->type);  // #572 step 1
    std::printf("Start_Interesting_Object_Cinematic: Type %.*s\n", static_cast<int>(io.type_name.size()), io.type_name.data());
    return interesting_shot(io, space, c, cine_.rng, out, glory_loader());
}

// #572 (type +0x21b8 / +0x21c0, a list of strings [H]; glory_file_names splits the entries).
std::vector<std::string> App::glory_files(objects::TypeIndex t) const {
    if (t == objects::kNone || t >= data_.types.size()) return {};
    return glory_file_names(data_.types.get_list(t, "Glory_Cinematics"));
}

// #572 0x4a3b40 [H]: .\Data\Art\Cinematics\ + the name as written (the .tec is in it), through the file system
// (cinematics.meg in the shipped game); logs "Init_Glory_Cinematics - %s" once the file opened.
GloryLoader App::glory_loader() const {
    return [this](std::string_view name, GloryCinematic& out) {
        if (!data_.files) return false;
        const std::string path = glory_file_path(name);
        std::vector<uint8_t> bytes;
        if (!data_.files->read(path, bytes)) return false;
        std::printf("Init_Glory_Cinematics - %s\n", path.c_str());
        return parse_glory_cinematic(bytes, out);
    };
}

// #572 0x39bdb0 -> 0x2cc940 [H]: a death clone appears (the object's death) while the battle cam runs (state 3), at
// most one glory shot every FPS x 5 frames, and its type has Glory_Cinematics: the running shot stops, one file is
// drawn and loaded; a loaded one plays on the clone (+0x360 = the clone), a failed one leaves the cam stopped. [port]
// (tier 1, presentation) A clone counts once, as it first shows in the drawn frame; the +0x34c and DAT_140b15490
// gates, the cursor re-baseline and +0x34a are not modelled; the first glory shot of a battle is not gated (+0x3a0's
// start value is not traced [M]).
void App::glory_death_path() {
    const sim::TacticalBattle* b = battle();
    if (!b || !bview_.active()) return;
    for (const render::SnapshotObject& o : bview_.frame().objects) {
        if ((o.flags & render::kSnapDeathClone) == 0 || !cine_.glory_seen.insert(o.id).second) continue;
        if (cine_.cam.state() != BattleCam::State::Movie) continue;
        if (cine_.glory_any && b->frame() - cine_.glory_frame < static_cast<uint32_t>(5 * kCinematicFps)) continue;
        const std::vector<std::string> files = glory_files(o.type);
        if (files.empty()) continue;
        ObjectFrame frame;
        (void)drawn_object(o.id, &frame);
        InterestingObject io;
        io.id = static_cast<uint32_t>(o.id);
        io.model = (o.flags & render::kSnapDrawable) != 0;
        io.half_extents = shot_half_extents(o.type);
        io.frame = frame;
        io.type_name = data_.types.name(o.type);
        GloryCinematic g;
        const bool ok = glory_pick(files, cine_.rng, glory_loader(), g);
        cine_.cam.death_glory(ok ? std::optional<BattleCamShot>(glory_shot(io, g)) : std::nullopt);
        if (ok) {
            cine_.interesting.current = static_cast<uint32_t>(o.id);
            cine_.glory_frame = b->frame();
            cine_.glory_any = true;
        }
        battle_cam_sync();
        return;
    }
}

// Cinematic mode on (0x49b1a0: the mode camera saved once, HUD and cursor hidden, FS +0x37) and the letterbox (0x44b550,
// 0.5 s) when the battle cam starts; 0x2cbb80 and End_Cinematic_Mode when it stops; a new shot restarts the clock.
void App::battle_cam_sync() {
    const bool on = cine_.cam.active();
    if (on && !cine_.cam_on) {
        if (cine_.shot.active()) tsw_shot_end();
        cine_.saved_camera.save_once(bview_.camera());
        if (session_) session_->set_presentation_rate_lock(true);
        cine_.letterbox.slide_in();
        std::printf("cinematic: battle cam on (state %d)\n", static_cast<int>(cine_.cam.state()));
    } else if (!on && cine_.cam_on) {
        if (bview_.active()) cine_.saved_camera.restore(bview_.camera());
        cine_.saved_camera.drop();
        cine_.shake.reset();  // End_Cinematic_Mode frees the trackers (the handheld noise)
        engine_loop_to(objects::kNoObjectId);
        if (session_) session_->set_presentation_rate_lock(false);
        cine_.letterbox.slide_out();
        cine_.interesting.current = 0;  // End_Cinematic_Mode clears +0x360 [H]; +0x368 / +0x370 are kept [M]
        std::printf("cinematic: battle cam off\n");
    }
    cine_.cam_on = on;
    if (on && cine_.cam.serial() != cine_.cam_serial) {
        cine_.cam_serial = cine_.cam.serial();
        const sim::TacticalBattle* b = battle();
        cine_.cam_start = b ? b->frame() : 0;
        cine_.cam_clock = 0;
        // #564 0x4ab780: the handheld noise restarts with each shot (every tracker cleared first), at shot clock 0.
        cine_.shake.reset();  // 0x4a7a90: every shot start clears the trackers
        if (const BattleCamShot& s = cine_.cam.shot(); s.handheld)
            (void)cine_.shake.start(handheld_shake(s.handheld_frames), 0, s.handheld_frames);
        for (const GloryShake& k : cine_.cam.shot().shakes)  // #572 0x49fd70: a glory file's shake keys
            (void)cine_.shake.start(glory_shake_effect(k), k.start, k.duration);
        if (cine_.cam.shot().engine_loop)  // +0x318: off on the old, on on the new (#563: the bombing run leaves it)
            engine_loop_to(static_cast<objects::ObjectId>(cine_.cam.shot().target));
    }
}

// 0x3a7bb0(old, 0) then 0x3a7bb0(new, 1). [port] A squadron member would use its team's type in retail; the shots here
// frame heroes and selected units with their own type.
void App::engine_loop_to(objects::ObjectId id) {
    if (id == cine_.engine_object) return;  // 0x4a4ac0 / 0x4a6790 move it only to a different object
    if (cine_.engine_object != objects::kNoObjectId) {
        // The off call's gate (not dying): a gone or dying object only has its loop stopped, without the ambient one-shot.
        const render::SnapshotObject* o = drawn_object(cine_.engine_object, nullptr);
        const bool live = o && (o->flags & (render::kSnapDying | render::kSnapDeathClone)) == 0;
        (void)audio_.cinematic_engine_loop(data_.types, cine_.engine_type, static_cast<uint64_t>(cine_.engine_object),
                                           live ? o->position : alamo::Vec3{}, false, live);
    }
    cine_.engine_object = objects::kNoObjectId;
    cine_.engine_type = objects::kNone;
    if (id == objects::kNoObjectId) return;
    const render::SnapshotObject* o = drawn_object(id, nullptr);
    if (!o || (o->flags & (render::kSnapDying | render::kSnapDeathClone)) != 0) return;  // not dying (+0x3a0 & 2)
    cine_.engine_object = id;
    cine_.engine_type = o->type;
    std::printf("cinematic: starting cinematic engine loop on object %u\n", static_cast<unsigned>(id));
    (void)audio_.cinematic_engine_loop(data_.types, o->type, static_cast<uint64_t>(id), o->position, true);
}

// 0x2cbd00 / 0x2cc080 / 0x2cc3e0 (no GUI dialog with the focus, a battle view, no TSW shot): the keys map to their
// hotkey slot first (0x2127f0); a swallowed key stays hidden from the rest of the frame until it is released.
void App::battle_cam_input(ui::InputFrame& raw) {
    const ui::KeySet down = raw.keys_down;
    for (uint32_t k = 0; k < 256; ++k)
        if (cine_.cam_swallowed.test(k)) {
            if (down.test(k)) {
                raw.keys_down.set(k, false);
                raw.keys_pressed.set(k, false);
            } else {
                cine_.cam_swallowed.set(k, false);
            }
        }
    const ui::KeySet prev = cine_.cam_prev_down;
    cine_.cam_prev_down = down;
    if (screen() != Screen::Battle || !session_ || !bview_.active() || !battle() || ui_.gui.has_focus() || cine_.shot.active() ||
        rename_.active())  // the rename prompt takes the keyboard
        return;
    const BattleCamGates g = battle_cam_gates();
    const BattleCamPicks p = battle_cam_picks();
    const bool shift = down.test(ui::vk::kShift), ctrl = down.test(ui::vk::kControl), alt = down.test(ui::vk::kAlt);
    const auto slot = [&](uint32_t k) { return ui_.hotkeys.slot_for(k, shift, ctrl, alt); };
    for (uint32_t k = 0; k < 256; ++k) {
        if (!raw.keys_pressed.test(k)) continue;
        BattleCam::Key key = BattleCam::Key::Other;
        const int32_t s = slot(k);
        if (s == ui::hk::kMovieMode) key = BattleCam::Key::MovieMode;
        else if (s == ui::hk::kCinematicView) key = BattleCam::Key::CinematicView;
        else if (s == ui::hk::kCameraTether) key = BattleCam::Key::Tether;
        else if (k == ui::vk::kEscape) key = BattleCam::Key::Escape;
        else if (k == ui::vk::kControl || k == 0xA2u || k == 0xA3u) key = BattleCam::Key::Control;  // VK_(L/R)CONTROL
        if (cine_.cam.key_down(key, g, p)) {
            raw.keys_pressed.set(k, false);
            if (!modifier_key(k)) {  // a held modifier stays down: the next keys keep their Ctrl / Shift / Alt
                cine_.cam_swallowed.set(k);
                raw.keys_down.set(k, false);
            }
        }
        battle_cam_sync();
    }
    for (uint32_t k = 0; k < 256; ++k)
        if (prev.test(k) && !down.test(k) && slot(k) == ui::hk::kMovieMode) {
            (void)cine_.cam.movie_key_up(g, p);
            battle_cam_sync();
        }
    if (!cine_.cam.active()) return;
    // The mouse belongs to the controller while it runs: buttons and the wheel never reach the HUD or the world.
    const bool button = raw.buttons_pressed != 0 || raw.buttons_released != 0;
    cine_.cam.mouse(raw.mouse_x, raw.mouse_y, raw.width, raw.height, button, raw.wheel, p);
    raw.buttons_pressed = raw.buttons_released = raw.buttons_down = 0;
    raw.wheel = 0.0f;
    battle_cam_sync();
}

void App::battle_cam_step() {
    const sim::TacticalBattle* b = battle();
    if (!b || !bview_.active()) {
        cinematic_reset();
        return;
    }
    if (const uint64_t clash = cine_.cam.shot().clash; clash != 0) {  // 0x2cbc40: the framed clash ended
        const std::vector<ClashEntry> list = clash_list(b->hero_clashes());
        if (std::none_of(list.begin(), list.end(), [clash](const ClashEntry& e) { return e.key == clash; }))
            cine_.cam.clash_ended(battle_cam_picks());
    }
    if (cine_.cam.shot().bombing_run && !b->bombing_run_in_progress(local_player()))  // #563 0x4aaf00: the run ended
        cine_.cam.bombing_run_ended(battle_cam_picks());
    if (cine_.cam.active() && !drawn_object(static_cast<objects::ObjectId>(cine_.cam.shot().target), nullptr)) cine_.cam.stop();  // [port] the object is gone
    battle_cam_sync();
    if (!cine_.cam.active()) return;
    // #568 0x4aaf00: a timed shot expires, freezes while the Movie Mode key or Tab is held, or chains.
    const int32_t raw = b->frame() >= cine_.cam_start ? static_cast<int32_t>(b->frame() - cine_.cam_start) : 0;
    const uint32_t serial = cine_.cam.serial();
    const int32_t c = cine_.cam.tick(raw, input_.key_down(ui::vk::kTab), battle_cam_picks());
    battle_cam_sync();
    cine_.cam_clock = cine_.cam.serial() != serial ? 0 : c;
}

// #571 0x49d640 (hp != -1) [H]: B = the Attachment_Bone's world translation (Fire_Bone_A's when the type names no
// Attachment_Bone), d = Fire_Bone_A's world X axis, both on the drawn model's current pose. [port] Without a loaded model
// or those bones: B = the sim's hardpoint position (CombatSystem::hardpoint_world) and d = the object's model X axis;
// retail reads (0, 0, 0) for B and no turn (the identity) when the bone lookups fail.
bool App::hardpoint_frame(const render::SnapshotObject& o, int32_t hp, const ObjectFrame& object_frame, ObjectFrame& out) {
    const sim::TacticalBattle* b = battle();
    if (!b || hp < 0) return false;
    const tactical::combat::CombatSystem& cs = b->combat();
    if (static_cast<uint32_t>(hp) >= cs.hardpoint_count(o.id)) return false;
    const tactical::combat::UnitDesc& ud = cs.data().units[cs.desc_index(o.id)];
    std::string_view attach, fire;
    if (static_cast<size_t>(hp) < ud.hardpoints.size() && ud.hardpoints[static_cast<size_t>(hp)] < cs.data().hardpoints.size()) {
        const objects::TypeIndex ht = data_.types.resolve_name(cs.data().hardpoints[ud.hardpoints[static_cast<size_t>(hp)]].name);
        if (ht != objects::kNone) {
            attach = ascii_trim(data_.types.get_string(ht, "Attachment_Bone"));
            fire = ascii_trim(data_.types.get_string(ht, "Fire_Bone_A"));
        }
    }
    assets::Mat3x4 ma, mf;
    const bool have_a = bview_.bone_world(o, attach, ma), have_f = bview_.bone_world(o, fire, mf);
    ui::Vec3 at;
    if (have_a) at = {ma.m[0][3], ma.m[1][3], ma.m[2][3]};
    else if (have_f) at = {mf.m[0][3], mf.m[1][3], mf.m[2][3]};
    else if (tactical::Vec3 p; cs.hardpoint_world(o.id, hp, p)) at = p;
    else at = object_frame.position;
    const ui::Vec3 d = have_f ? ui::Vec3{mf.m[0][0], mf.m[1][0], mf.m[2][0]} : object_frame.axis[0];
    out = hardpoint_eye_frame(at, d);
    return true;
}

// 0x49cd00 for the battle cam: the keyed eye / look on the framed object's drawn transform, the eye smoothing, the look
// filter (#671), near / far 1 / 10000 (0x49b1a0). #571 0x49bdf0: the FOV from the shot's FOV keys or zoom trigger, F0 =
// the tactical camera's FOV.
bool App::battle_cam_camera(ui::CameraParams& cp) {
    if (!cine_.cam.active()) return false;
    ObjectFrame f;
    const render::SnapshotObject* o = drawn_object(static_cast<objects::ObjectId>(cine_.cam.shot().target), &f);
    if (!o) return false;
    const BattleCamShot& s = cine_.cam.shot();
    const float z = shot_z_adjust(o->type);
    const int32_t c = cine_.cam_clock;
    // #570: keys with `on_second` sit on the shot's second object (the target). [port] When it is no longer drawn they
    // fall back to the framed object.
    KeyAnchors an{f, z, f, z};
    if (ObjectFrame f2; s.second != 0)
        if (const render::SnapshotObject* o2 = drawn_object(static_cast<objects::ObjectId>(s.second), &f2)) {
            an.second = f2;
            an.z_second = shot_z_adjust(o2->type);
        }
    // #571: keys with `on_hardpoint` sit on the shot's hardpoint. [port] An unknown hardpoint falls back to the object.
    an.hardpoint = f;
    if (s.hardpoint >= 0) (void)hardpoint_frame(*o, s.hardpoint, f, an.hardpoint);
    const ui::Vec3 keyed_look = shot_keys_look(s.look, c, an);
    ui::Vec3 keyed = shot_keys_eye(s.eye, s.look, c, an);
    // #571 0x49bdf0, after the eye and look and before the terrain clamp: the zoom target's |E| and |look - eye|.
    {
        const float f0 = bview_.camera().fov_deg();
        std::optional<float> radius;
        if (const uint32_t zt = cine_.cam.fov().target(); zt != 0)
            if (const render::SnapshotObject* oz = drawn_object(static_cast<objects::ObjectId>(zt), nullptr)) {
                const ui::Vec3 e = shot_half_extents(oz->type);
                radius = std::sqrt(tactical::dot(e, e));
            }
        const ui::Vec3 dl = keyed_look - keyed;
        const float fov = cine_.cam.shot_fov(c, f0, radius, std::sqrt(tactical::dot(dl, dl)));
        if (fov != f0)
            cp.fov_x = ui::tactical_fov_x(fov, static_cast<float>(targets_.width) / static_cast<float>(std::max(1u, targets_.height)));
    }
    // #564 (+0x315 [H]): on land, with the sticky clamp set, the eye keeps over the ground; before the smoothing.
    if (!bview_.space() && cine_.cam.terrain_clamp() && bview_.scene()) {
        const terrain::MapData* m = &bview_.scene()->map();
        keyed = terrain_clamped_eye(keyed, [m](float x, float y) { return m->terrain.sample_height(x, y); });
    }
    const ui::Vec3 eye = cine_.cam.smooth_eye(keyed, c);
    const ui::Vec3 look = cine_.cam.filter_look(keyed_look, c);  // #671 0x4a96d0, after the smoothing
    // #572 0x49cd00 [H]: the roll keys (degrees), plus the shake's roll.
    const CinematicPose pose = apply_shake(eye, look, cine_.cam.shot_roll(c), cine_.shake, c);
    cp.eye = pose.eye;
    cp.target = pose.look;
    cp.up = pose.up;
    cp.roll = pose.roll_deg * (3.14159265358979f / 180.0f);
    const ui::Vec3 d = pose.eye - pose.look;
    const float dist = std::sqrt(tactical::dot(d, d));
    if (dist > 0.0f) {
        cp.distance = dist;
        cp.orbit_yaw = std::atan2(d.y, d.x);
        cp.orbit_pitch = std::asin(std::clamp(d.z / dist, -1.0f, 1.0f));
    }
    cp.znear = 1.0f;
    cp.zfar = 10000.0f;
    return true;
}

// b_camera_t (0x347100 [H]): pitch and yaw back to the XML defaults when they differ (0x2ce420 / 0x2cf880), otherwise
// the battle cam toggles (0x2cca80(ctrl, 0)).
void App::camera_button() {
    if (!bview_.active()) return;
    ui::TacticalCameraController& cam = bview_.camera();
    if (!cam.pitch_yaw_at_defaults()) {
        cam.reset_pitch_yaw();
        return;
    }
    cine_.cam.toggle(battle_cam_gates(), battle_cam_picks());
    battle_cam_sync();
}

// The letterbox bar overlay (0x140a28c10 [M]): two black bars, each Camera_FX_Manager_Letterbox_Height x level of the
// screen height, over the world and under the dialogs. The level is the higher of the TSW shot's bars and the
// linked-battle / story-script bars (LinkedBattleOverlay).
void App::draw_letterbox(ui::HudDrawList& dl, const ui::HudCanvas& canvas, uint32_t w, uint32_t h) const {
    const float level = std::max({cine_.letterbox.level(), linked_ui_.letterbox_level(), story_box_.level(now_s_)});
    if (!(level > 0.0f)) return;
    const float fw = static_cast<float>(w), fh = static_cast<float>(h);
    const float bar = std::clamp(cine_.letterbox_height, 0.0f, 0.5f) * level * fh;
    if (!(bar > 0.0f)) return;
    ui::HudQuad top, bottom;
    top.min = canvas.pixel_to_canvas(0.0f, bar);
    top.max = canvas.pixel_to_canvas(fw, 0.0f);
    bottom.min = canvas.pixel_to_canvas(0.0f, fh);
    bottom.max = canvas.pixel_to_canvas(fw, fh - bar);
    top.color = bottom.color = {0, 0, 0, 255};
    dl.add(0, ui::HudList::Alpha, top);
    dl.add(0, ui::HudList::Alpha, bottom);
}

}  // namespace alamo_game_app
