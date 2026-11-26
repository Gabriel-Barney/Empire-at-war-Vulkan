// alamo_game App: ui3 t01 - t04 - the tactical hotkeys and command-bar buttons beyond the camera keys and the move toggles
// (app_input.cpp): order buttons c_button00 / 02 / 04, ability hotkeys, selection keys, retreat / reinforcement / beacon /
// movie-mode buttons, the reinforcement pad's availability and the drop-point preview's state.
//
// Retail references: HUD_GAPS §5.1 (who enables the buttons), BATTLE_TRANSITIONS §3.4 (retreat), TACTICAL_GAPS §4.2.1
// (the human drop preview 0x345c0), legacy docs/re/UI.md §4.3 (slots). What a key does in the hotkey dispatch 0x63f00 is not traced
// for most slots: those are [port] choices, listed in legacy docs/PHASE4.md ("ui3 tactical hotkeys").
#include <filesystem>

#include "alamo/tactical/abilities/ability_types.hpp"
#include "game_app.hpp"

namespace alamo_game_app {

bool App::reinforce_available(objects::ObjectId galactic_id) const {
    const sim::TacticalBattle* b = battle();
    return b && !story_controls_locked() && b->reinforce_block(local_player(), galactic_id) == commands::RejectReason::None;
}

// R / b_reinforcement (#463, 0x2e5580 [H]; docs/re/TACTICAL_HOTKEYS.md "Reinforce"): toggle the reinforcement list
// (TacticalHud::toggle_reinforce_list). Opening it plays the local faction's Reinforcements_Selection line (mcx4 t13,
// faction +0x2250) and flashes b_reinforcement; a list entry then starts its drop pick (BeginReinforcement). [port] The
// open gates: a battle, controls not story-locked, and no drop point being picked (0x3400b0: nothing held [M]).
void App::toggle_reinforcement_list() {
    const bool allowed = battle() != nullptr && !story_controls_locked() && reinforce_unit_ == 0;
    if (ui_.tactical_hud.toggle_reinforce_list(allowed) != ui::TacticalHud::ReinforceListToggle::Opened) return;
    audio_.faction_sfx("Reinforcements_Selection_SFXEvent");  // ledger: consumes Faction.Reinforcements_Selection_SFXEvent
    story_present_.flash_component("b_reinforcement", hud_consts_.cb_flash_count, now_s_);
}

bool App::cancel_picks() {
    bool any = false;
    if (reinforce_unit_ != 0) {
        reinforce_unit_ = 0;
        reinforce_blob_.reset();
        any = true;
    }
    if (bombing_targeting_) {
        bombing_end(true);
        ++bombing_cancels_;
        any = true;
    }
    if (beacon_pick_) {
        beacon_pick_ = false;
        any = true;
    }
    return any;
}

// NextUnit / PreviousUnit (#461, 0x2fb150 [H]; ui::unit_cycle): the candidates are the local player's units, one per
// squadron (TacticalBattle::icon_links), ascending id ([port]: retail's filters and order 0x20a9b0 are open). An empty
// selection selects the first, with no camera move; otherwise the one after (before) the first selected object is
// selected and the camera centres on it.
void App::select_cycle(int32_t step) {
    const sim::TacticalBattle* b = battle();
    if (!b || !bview_.active()) return;
    const std::vector<render::SnapshotObject>& objs = bview_.frame().objects;  // ascending id
    std::vector<const render::SnapshotObject*> mine;
    for (const sim::TacticalBattle::IconLink& l : b->icon_links()) {
        const auto it = std::lower_bound(objs.begin(), objs.end(), l.unit,
                                         [](const render::SnapshotObject& x, objects::ObjectId v) { return x.id < v; });
        if (it != objs.end() && it->id == l.unit && it->owner == local_player()) mine.push_back(&*it);
    }
    std::sort(mine.begin(), mine.end(), [](const auto* x, const auto* y) { return x->id < y->id; });
    std::vector<uint32_t> ids;
    for (const render::SnapshotObject* o : mine) ids.push_back(static_cast<uint32_t>(o->id));
    const ui::UnitCyclePick pick = ui::unit_cycle(orders_.selection(), ids, step);
    if (pick.id == 0) return;
    submit(commands::ev::select({pick.id}));
    if (!pick.move_camera) return;
    const auto o = std::find_if(mine.begin(), mine.end(), [&](const auto* m) { return static_cast<uint32_t>(m->id) == pick.id; });
    bview_.camera().jump_to({(*o)->position.x, (*o)->position.y, (*o)->position.z});
}

// SelectLike (#461, 0x63f00 -> 0x3a690(type, 1) [H]; ui::select_like): the type of the local player's object under the
// cursor (hover_ship_, the HUD's hovered unit: no props, nothing over the HUD); every own object of that type on screen
// is appended to the selection. No own object under the cursor: nothing happens.
void App::select_like() {
    if (!bview_.active()) return;
    const uint32_t w = targets_.width, h = targets_.height;
    const ui::View view = ui::make_view(bview_.camera_params(w, h), w, h);
    std::optional<uint32_t> type;
    std::vector<ui::SelectableObject> visible;
    for (const render::SnapshotObject& o : bview_.frame().objects) {
        if (o.owner != local_player()) continue;
        if (hover_ship_ != 0 && static_cast<uint32_t>(o.id) == hover_ship_) type = static_cast<uint32_t>(o.type);
        float sx = 0.0f, sy = 0.0f;
        if (!ui::project(view, {o.position.x, o.position.y, o.position.z}, sx, sy)) continue;
        if (sx < 0.0f || sy < 0.0f || sx >= static_cast<float>(w) || sy >= static_cast<float>(h)) continue;
        visible.push_back({static_cast<uint32_t>(o.id), static_cast<uint32_t>(o.type)});
    }
    const std::vector<uint32_t> ids = ui::select_like(orders_.selection(), type, visible, ui::OrderTranslator::kMaxSelect);
    if (ids.size() > orders_.selection().size()) submit(commands::ev::select(ids));
}

// [port: Gabriel, 2026-11-13] Squadron wings: every own squadron (icon_links: unit -> its squadron's lead) whose member type
// is listed in some Squadron_Units joins the bomber wing when the member's CategoryMask has Bomber, else the fighter wing
// when it has Fighter. A wing's ids hold whole squadrons up to the selection cap (OrderTranslator::kMaxSelect); its count
// is every squadron. Selected look = every id of the wing selected.
void App::fill_wings(ui::TacticalHudFrame& hf) {
    wings_.assign(2, ui::HudWing{});
    wings_[0].label = U"FIGHTERS";
    wings_[0].hotkey = U"F2";
    wings_[1].label = U"BOMBERS";
    wings_[1].hotkey = U"F3";
    const sim::TacticalBattle* b = battle();
    if (b && bview_.active()) {
        const std::vector<render::SnapshotObject>& objs = bview_.frame().objects;  // ascending id
        const objects::EnumConverter& cat = data_.types.enums().get(objects::EnumId::GameObjectCategory);
        const auto fighter = static_cast<uint64_t>(cat.lookup("Fighter").value_or(0x1));
        const auto bomber = static_cast<uint64_t>(cat.lookup("Bomber").value_or(0x2));
        struct Squad {
            objects::ObjectId lead = objects::kNoObjectId;
            size_t wing = 0;
            std::vector<uint32_t> ids;
        };
        std::vector<Squad> squads;  // first-seen order (ascending member id)
        for (const sim::TacticalBattle::IconLink& l : b->icon_links()) {
            const auto it = std::lower_bound(objs.begin(), objs.end(), l.unit,
                                             [](const render::SnapshotObject& x, objects::ObjectId v) { return x.id < v; });
            if (it == objs.end() || it->id != l.unit || it->owner != local_player()) continue;
            const objects::TypeIndex t = it->type;
            if (t >= data_.types.size() || !data_.types.squadron_member(t)) continue;
            const uint64_t mask = data_.types.get_mask(t, "CategoryMask");
            const size_t wing = (mask & bomber) != 0 ? 1u : (mask & fighter) != 0 ? 0u : 2u;
            if (wing > 1) continue;
            Squad* sq = nullptr;
            for (Squad& s : squads)
                if (s.lead == l.squad) sq = &s;
            if (!sq) {
                squads.push_back(Squad{l.squad, wing, {}});
                sq = &squads.back();
            }
            sq->ids.push_back(static_cast<uint32_t>(l.unit));
        }
        for (const Squad& s : squads) {
            ui::HudWing& w = wings_[s.wing];
            ++w.squadrons;
            if (w.ids.size() + s.ids.size() <= ui::OrderTranslator::kMaxSelect) w.ids.insert(w.ids.end(), s.ids.begin(), s.ids.end());
        }
        std::vector<uint32_t> sel = orders_.selection();
        std::sort(sel.begin(), sel.end());
        for (ui::HudWing& w : wings_) {
            std::sort(w.ids.begin(), w.ids.end());
            w.selected = !w.ids.empty() && std::includes(sel.begin(), sel.end(), w.ids.begin(), w.ids.end());
        }
    }
    hf.wings = wings_;
}

// [port: Gabriel, 2026-11-22, #174] Fleet groups: the local player's groups (creation order) for the selected-unit bar,
// only while fleet groups are on in this session and battle.
void App::fill_fleet_groups(ui::TacticalHudFrame& hf) {
    fleet_groups_.clear();
    const sim::TacticalBattle* b = battle();
    hf.fleet_groups_on = session_ && session_->spec().fleet_groups && b && b->fleet_groups_enabled();
    if (hf.fleet_groups_on)
        for (const sim::FleetGroup& g : b->fleet_groups().groups()) {
            if (static_cast<int32_t>(g.owner) != local_player()) continue;
            ui::HudFleetGroup h;
            h.id = g.id;
            h.name = ui::utf8_to_utf32(g.name);
            h.locked = g.locked;
            for (const sim::FleetMember& m : g.members) h.members.push_back(static_cast<uint32_t>(m.id));  // ascending id
            fleet_groups_.push_back(std::move(h));
        }
    hf.fleet_groups = fleet_groups_;
}

// A wing button / F2 / F3: select the wing (`add`: the selection plus the wing); a second press of the same wing within
// 0.4 s [port], or `centre_only`, centres the camera on the wing's members.
void App::select_wing(size_t wing, bool add, bool centre_only) {
    if (screen() != Screen::Battle || !bview_.active() || wing >= wings_.size() || wings_[wing].ids.empty()) return;
    const std::vector<uint32_t>& members = wings_[wing].ids;
    const bool again = wing_last_ == static_cast<int32_t>(wing) && now_s_ - wing_last_s_ <= 0.4;
    wing_last_ = static_cast<int32_t>(wing);
    wing_last_s_ = now_s_;
    if (!centre_only) {
        std::vector<uint32_t> ids = members;
        if (add) {
            const std::vector<uint32_t> sel = orders_.selection();
            ids.insert(ids.end(), sel.begin(), sel.end());
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        }
        if (ids.size() > ui::OrderTranslator::kMaxSelect) ids.resize(ui::OrderTranslator::kMaxSelect);
        submit(commands::ev::select(ids));
    }
    if (again || centre_only) {
        double x = 0.0, y = 0.0, z = 0.0;
        size_t n = 0;
        for (const render::SnapshotObject& o : bview_.frame().objects)
            if (std::binary_search(members.begin(), members.end(), static_cast<uint32_t>(o.id))) {
                x += o.position.x;
                y += o.position.y;
                z += o.position.z;
                ++n;
            }
        if (n > 0) {
            const double k = 1.0 / static_cast<double>(n);
            bview_.camera().jump_to({static_cast<float>(x * k), static_cast<float>(y * k), static_cast<float>(z * k)});
        }
    }
}

// QuickSave (F6) / QuickLoad (F5) / Screenshot (F12), galactic and tactical: the save slot game::kQuickSaveName
// ("[QuickSave]", 0x55c00 / 0x562c0, #430 [H]) through the save / load dialogs' path. The screenshot (#466 [H]): the next
// presented frame as ScreenShots\EAW_<YYYYMMDD>_<HHMMSScc> (game::screenshot_file_name); [port] (docs/DECISIONS.md
// "Front-end and skirmish choices") a PNG, in a folder beside the saves (retail's is under the game's working folder).
void App::interface_hotkey(uint16_t slot) {
    const std::string quick(game::kQuickSaveName);
    if (slot == ui::hk::kQuickSave) {
        save_game_as(quick);
    } else if (slot == ui::hk::kQuickLoad) {
        std::error_code ec;
        if (!saves_dir_.empty() && std::filesystem::exists(host_path(save_path(saves_dir_, quick)), ec)) load_saved_game(quick);
        else audio_.negative_feedback();
    } else if (slot == ui::hk::kScreenshot && ctl_.shot_path.empty() && !saves_dir_.empty()) {
        std::error_code ec;
        const std::filesystem::path dir = host_path(saves_dir_).parent_path() / game::kScreenshotFolder;  // #137: UTF-8
        std::filesystem::create_directories(dir, ec);
        // One request at a time (shot_path is busy until the frame is written), so a name repeats only across runs.
        if (const std::string name = game::screenshot_file_name_now(); !name.empty()) ctl_.shot_path = host_utf8(dir / name);
        else std::printf("screenshot: the local time could not be read\n");
    }
}

void App::tactical_hotkeys(const std::vector<ui::HotkeyEvent>& hk, std::vector<commands::EventData>& cmds,
                           std::vector<ui::HudAction>& acts) {
    namespace hk_ = ui::hk;
    for (const ui::HotkeyEvent& e : hk) {
        if (e.kind == ui::HotkeyKind::Ability || e.slot == hk_::kDeploySquad) {
            // t02: the ability button of that type in the selection panel, as a left click (targeted abilities start
            // their pick). The slot's modifier is part of the key, so it is never the queue modifier here.
            // #430 (0x63f00): SwapWeapons and ForceCloak activate a second type too.
            bool used = false;
            for (const std::string_view name : {ui::hotkey_ability_name(e.slot), ui::hotkey_ability_name2(e.slot)}) {
                const uint8_t type = name.empty() ? uint8_t{0} : tactical::abilities::ability_type_from_name(name);
                if (type != 0 && !story_controls_locked() &&
                    (modern_hud() ? ui_.modern_hud.activate_ability(type, false, cmds, &acts)  // #586 the drawn style's button
                                  : ui_.tactical_hud.activate_ability(type, false, cmds, &acts)))
                    used = true;
            }
            if (used) ++hotkey_abilities_;
            continue;
        }
        switch (e.slot) {
            case hk_::kQuickSave:
            case hk_::kQuickLoad:
            case hk_::kScreenshot: interface_hotkey(e.slot); break;
            case hk_::kAttack: orders_.toggle_move_toggle(ui::MoveToggle::Attack); break;  // #430: a toggle, no button test
            case hk_::kMove: orders_.toggle_move_toggle(ui::MoveToggle::Move); break;
            case hk_::kSelectLike: select_like(); break;
            case hk_::kNextUnit: select_cycle(1); break;
            case hk_::kPrevUnit: select_cycle(-1); break;
            case hk_::kReinforce: toggle_reinforcement_list(); break;
            case hk_::kBeacon: {
                ui::HudAction a;
                a.name = "b_special_weapon2";
                tactical_button(a);
                break;
            }
            case hk_::kCameraTether: tether_id_ = ui::tether_toggle(tether_id_, orders_.selection()); break;
            // MovieMode (on key up) and CinematicView: the battle cam (battle_cam_input, #462).
            case hk_::kToggleInterface: interface_hidden_ = !interface_hidden_; break;
            case hk_::kHudStyle: toggle_hud_style(); break;  // #584 [port] Classic <-> Modern
            case hk_::kAlertJump:  // #589 [port] the Modern alert strip's Jump
                if (modern_hud() && !cinematic_hides_hud()) ui_.modern_hud.jump(acts);  // no Jump while the HUD is hidden
                break;
            case hk_::kHolocron: objectives_hidden_ = !objectives_hidden_; break;  // [port] the battle's holocron = the objectives box
            // Taunts, chat, give credits, player list: multiplayer / not traced - no action (legacy docs/PHASE4.md).
            default: break;
        }
    }
}

// Command-bar buttons with no HudAction kind of their own (HudAction::Kind::Button by name). True = handled.
bool App::tactical_button(const ui::HudAction& a) {
    if (screen() != Screen::Battle) return false;
    const std::string& n = a.name;
    if (ascii_iequals(n, "c_button00")) {  // #430 (0x347360): flip Attack; arming clears the other four
        orders_.toggle_move_toggle(ui::MoveToggle::Attack, true);
        return true;
    }
    if (ascii_iequals(n, "c_button02")) {  // #430 (0x347420): flip Move; arming clears the other four
        orders_.toggle_move_toggle(ui::MoveToggle::Move, true);
        return true;
    }
    if (ascii_iequals(n, "c_button04")) {  // full stop: StopMovement for the selection (as the Stop key)
        if (!orders_.selection().empty()) submit(commands::ev::stop());
        return true;
    }
    if (ascii_iequals(n, "b_reinforcement")) {  // #463: as the Reinforce key (0x2e5580)
        toggle_reinforcement_list();
        return true;
    }
    if (ascii_iequals(n, "b_camera_t")) {  // TEXT_MOVIE_MODE (#462, 0x347100)
        camera_button();
        return true;
    }
    if (ascii_iequals(n, "b_special_weapon2") || ascii_iequals(n, "b_beacon_t")) {  // TEXT_PLACE_BEACON
        if (beacon_pick_) {
            beacon_pick_ = false;
        } else if (now_s_ - beacon_last_s_ >= static_cast<double>(beacon_delay_s_) && !story_controls_locked()) {
            cancel_picks();
            targeting_.cancel();
            beacon_pick_ = true;
        }  // #430 (0x2e7e40 [H]): silent during the cooldown
        return true;
    }
    if (ascii_iequals(n, "b_droid_help_tactical")) {  // the advisor dome, as the Droid Advisor key (capture C2)
        open_holocron(ui::InfoPage::DroidLog);
        return true;
    }
    return false;
}

void App::fill_command_buttons(ui::TacticalHudFrame& hf) const {
    hf.world.interface_hidden = interface_hidden_;
    hf.world.health_bar_spacing_px = hud_consts_.health_bar_spacing;  // Health_Bar_Spacing
    hf.objectives_hidden = objectives_hidden_;  // ENABLE_OBJECTIVE_DISPLAY writes it too (story_present_frame)
    const sim::TacticalBattle* b = battle();
    if (!b) return;
    ui::HudCommandButtons& c = hf.command_buttons;
    c.present = true;
    c.controls_locked = story_controls_locked();
    const int32_t me = local_player();
    c.retreat_enabled = b->retreat_block(me) == 0 || b->retreat_cancellable(me);  // the second click cancels
    const float running = b->retreat_countdown_progress();
    const float allowed = b->retreat_allowed_progress();
    c.retreat_dial = running >= 0.0f ? running : allowed < 1.0f ? allowed : -1.0f;
    for (const sim::TacticalBattle::Reserve& r : b->reserves(me)) {
        ++c.reinforce_count;
        if (reinforce_available(r.galactic_id)) c.reinforce_enabled = true;
    }
    c.reinforce_selected = reinforce_unit_ != 0;
    c.beacon_enabled = now_s_ - beacon_last_s_ >= static_cast<double>(beacon_delay_s_);
    c.beacon_selected = beacon_pick_;
    c.movie_mode = cine_.cam.state() == BattleCam::State::Movie;
    const float cap = b->population_cap(me);
    if (cap >= 0.0f) {
        c.pop_used = static_cast<int32_t>(std::lround(b->population_used(me)));
        c.pop_cap = static_cast<int32_t>(std::lround(cap));
    }
}

}  // namespace alamo_game_app
