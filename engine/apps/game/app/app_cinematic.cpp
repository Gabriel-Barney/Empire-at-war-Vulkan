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
// consumes them. A move within 1/20 of the screen of the cursor at Fire is swallowed; a larger one or any button / wheel
// event ends the shot at once. [M] the event codes 2, 4, 5, 7, 8, 10, 12 = button / wheel events. Keys are not consumed.
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
    return p;
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
        std::printf("cinematic: battle cam off\n");
    }
    cine_.cam_on = on;
    if (on && cine_.cam.serial() != cine_.cam_serial) {
        cine_.cam_serial = cine_.cam.serial();
        const sim::TacticalBattle* b = battle();
        cine_.cam_start = b ? b->frame() : 0;
        cine_.cam_clock = 0;
        // #564 0x4ab780: the handheld noise restarts with each shot (every tracker cleared first), at shot clock 0.
        if (cine_.cam.shot().handheld) {
            cine_.shake.reset();
            (void)cine_.shake.start(handheld_shake(kHandheldFrames), 0, kHandheldFrames);
        }
        engine_loop_to(static_cast<objects::ObjectId>(cine_.cam.shot().target));  // +0x318: off on the old, on on the new
    }
}

// 0x3a7bb0(old, 0) then 0x3a7bb0(new, 1). [port] A squadron member would use its team's type in retail; the shots here
// frame heroes and selected units with their own type.
void App::engine_loop_to(objects::ObjectId id) {
    if (id == cine_.engine_object) return;
    if (cine_.engine_object != objects::kNoObjectId) {
        const render::SnapshotObject* o = drawn_object(cine_.engine_object, nullptr);
        (void)audio_.cinematic_engine_loop(data_.types, cine_.engine_type, static_cast<uint64_t>(cine_.engine_object),
                                           o ? o->position : alamo::Vec3{}, false);
    }
    cine_.engine_object = objects::kNoObjectId;
    cine_.engine_type = objects::kNone;
    if (id == objects::kNoObjectId) return;
    const render::SnapshotObject* o = drawn_object(id, nullptr);
    if (!o || (o->flags & (render::kSnapDying | render::kSnapDeathClone)) != 0) return;  // not dying (+0x3a0 & 2)
    cine_.engine_object = id;
    cine_.engine_type = o->type;
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
    if (cine_.cam.active() && !drawn_object(static_cast<objects::ObjectId>(cine_.cam.shot().target), nullptr)) cine_.cam.stop();  // [port] the object is gone
    battle_cam_sync();
    if (cine_.cam.active()) cine_.cam_clock = b->frame() >= cine_.cam_start ? static_cast<int32_t>(b->frame() - cine_.cam_start) : 0;
}

// 0x49cd00 for the battle cam: the keyed eye / look on the framed object's drawn transform, the eye smoothing, near / far
// 1 / 10000 (0x49b1a0); FOV stays the mode camera's.
bool App::battle_cam_camera(ui::CameraParams& cp) {
    if (!cine_.cam.active()) return false;
    ObjectFrame f;
    const render::SnapshotObject* o = drawn_object(static_cast<objects::ObjectId>(cine_.cam.shot().target), &f);
    if (!o) return false;
    const BattleCamShot& s = cine_.cam.shot();
    const float z = shot_z_adjust(o->type);
    const int32_t c = cine_.cam_clock;
    const ui::Vec3 look = shot_keys_look(s.look, c, f, z);
    ui::Vec3 keyed = shot_keys_eye(s.eye, s.look, c, f, z);
    // #564 (+0x315, 0x49cd00 [H]): on land, with the sticky clamp set, the eye stays 15 over the highest ground of a 3 x 3
    // grid at +-15 around it; before the smoothing.
    if (!bview_.space() && cine_.cam.terrain_clamp() && bview_.scene()) {
        const terrain::MapData& m = bview_.scene()->map();
        float h = -std::numeric_limits<float>::infinity();
        for (int i = -1; i <= 1; ++i)
            for (int j = -1; j <= 1; ++j)
                h = std::max(h, m.terrain.sample_height(keyed.x + 15.0f * static_cast<float>(i), keyed.y + 15.0f * static_cast<float>(j)));
        keyed.z = std::max(keyed.z, h + 15.0f);
    }
    const ui::Vec3 eye = cine_.cam.smooth_eye(keyed, c);
    const CinematicPose pose = apply_shake(eye, look, 0.0f, cine_.shake, c);
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
