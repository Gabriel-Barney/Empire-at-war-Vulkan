#include "alamo/ui/hud_world.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "alamo/core/hash.hpp"
#include "alamo/data/values.hpp"
#include "alamo/ui/hud_font.hpp"

namespace alamo::ui {

namespace {

constexpr Rgba8 kHostileBracket{255, 64, 64, 255};
// [port] fallback bar sizes (canvas units) per class when the atlas has no bar texture.
constexpr Vec2f kFallbackBar[3] = {{32, 4}, {48, 5}, {64, 6}};
constexpr Vec2f kFallbackBracket[3] = {{6, 16}, {8, 24}, {10, 32}};
constexpr Rgba8 kFallbackHealth{40, 220, 40, 255}, kFallbackShield{60, 140, 255, 255}, kFallbackPower{240, 200, 40, 255};
constexpr Rgba8 kFallbackFrame{0, 0, 0, 200};

const std::string* pick(const std::vector<std::string>& v, size_t i) {
    if (v.empty()) return nullptr;
    return &v[std::min(i, v.size() - 1)];
}

}  // namespace

BracketSize bracket_size(const HudTypeInfo& t, bool land) {
    if (t.gui_bracket_size >= 0 && t.gui_bracket_size <= 2) return static_cast<BracketSize>(t.gui_bracket_size);
    return land ? t.land_class : t.space_class;
}

BracketClasses derive_bracket_classes(std::span<const std::string_view> cats) {
    auto has = [&](std::string_view token) {
        return std::any_of(cats.begin(), cats.end(), [&](std::string_view t) { return iequals(data::trim(t), token); });
    };
    BracketClasses c;
    c.land = has("Infantry") ? BracketSize::Small : has("Structure") ? BracketSize::Large : BracketSize::Medium;
    c.space = has("Fighter") || has("Bomber") ? BracketSize::Small
              : has("Capital") || has("Super") || has("Structure") ? BracketSize::Large
                                                                   : BracketSize::Medium;
    return c;
}

void FlyTextList::update(float dt) {
    for (FlyText& f : items_) f.age += dt;
    items_.erase(std::remove_if(items_.begin(), items_.end(), [](const FlyText& f) { return f.age >= f.lifetime; }),
                 items_.end());
}

UnitScreenBounds unit_screen_bounds(const View& v, const HudUnit& u, const HudTypeInfo& t) {
    UnitScreenBounds b;
    float sx = 0, sy = 0;
    if (!project(v, u.position, sx, sy)) return b;
    // [port] no model radius from the host: the selection circle's half-size (Select_Box_Scale x Scale_Factor) is the
    // closest per-type size the HUD has; 10 world units only when that is 0 too.
    const float box = t.select_box_scale * (t.scale > 0.0f ? t.scale : 1.0f);
    const float base = u.radius > 0.0f ? u.radius : box > 0.0f ? box : 10.0f;
    const float r = base * (t.bounds_scale > 0.0f ? t.bounds_scale : 1.0f);
    float ex = 0, ey = 0;
    float rp = 0.0f;
    if (project(v, u.position + v.right * r, ex, ey)) rp = std::sqrt((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy));
    b.radius_px = std::max(rp, 6.0f);
    b.cx = sx;
    b.cy = sy;
    const auto w = static_cast<float>(v.width), h = static_cast<float>(v.height);
    b.visible = sx + b.radius_px >= 0.0f && sx - b.radius_px <= w && sy + b.radius_px >= 0.0f && sy - b.radius_px <= h;
    return b;
}

bool unit_icon_centre(const WorldHudContext& ctx, const HudCanvas& cv, const HudUnit& u, const HudTypeInfo& t, Vec2f& centre) {
    HudUnit marker = u;
    if (u.icon_anchored) marker.position = u.icon_anchor;
    const UnitScreenBounds ib = unit_screen_bounds(ctx.view, marker, t);
    if (!ib.visible) return false;
    const float lift_px = ib.radius_px + ctx.health_bar_spacing_px + 0.5f * ctx.unit_icon_size / cv.units_per_pixel_y + 4.0f;
    centre = cv.pixel_to_canvas(ib.cx, ib.cy - lift_px);
    return true;
}

std::string weather_status_icon(std::string_view icon_name, float accuracy, float sight, float min_accuracy, float min_sight) {
    if (icon_name.empty() || !(accuracy < min_accuracy || sight < min_sight)) return {};
    return "i_pa" + std::string(icon_name.substr(std::min<size_t>(14, icon_name.size())));
}

void assign_unit_icons(std::span<HudUnit> units, std::span<const UnitIconLink> links) {
    struct Member {
        uint32_t squad = 0;
        size_t unit = 0;
    };
    std::vector<UnitIconLink> sorted(links.begin(), links.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const UnitIconLink& a, const UnitIconLink& b) { return a.unit < b.unit; });
    std::vector<Member> members;
    members.reserve(sorted.size());
    for (size_t i = 0; i < units.size(); ++i) {
        units[i].unit_icon = false;
        units[i].squadron_member = false;
        units[i].icon_anchored = false;
        const auto it = std::lower_bound(sorted.begin(), sorted.end(), units[i].id,
                                         [](const UnitIconLink& l, uint32_t id) { return l.unit < id; });
        if (it != sorted.end() && it->unit == units[i].id) {
            members.push_back({it->squad, i});
            units[i].squadron_member = true;
        }
    }
    std::stable_sort(members.begin(), members.end(), [](const Member& a, const Member& b) { return a.squad < b.squad; });
    for (size_t first = 0; first < members.size();) {
        size_t end = first;
        float sx = 0.0f, sy = 0.0f, sz = 0.0f, health = 0.0f;
        for (; end < members.size() && members[end].squad == members[first].squad; ++end) {
            const HudUnit& m = units[members[end].unit];
            sx += m.position.x;
            sy += m.position.y;
            sz += m.position.z;
            health += m.health;
        }
        const float inv = 1.0f / static_cast<float>(end - first);
        HudUnit& lead = units[members[first].unit];
        lead.unit_icon = true;
        lead.icon_anchored = true;
        lead.icon_anchor = Vec3{sx * inv, sy * inv, sz * inv};
        lead.icon_health = health * inv;
        first = end;
    }
}

void WorldHud::bind(const ComponentTable& table) {
    table_ = &table;
    static constexpr const char* kSuffix[3] = {"_small", "_medium", "_large"};
    for (int i = 0; i < 3; ++i) {
        bracket_[i] = table.find(std::string("st_bracket") + kSuffix[i]);
        health_[i] = table.find(i == 0 ? std::string("st_health") : std::string("st_health") + kSuffix[i]);
        shield_[i] = table.find(i == 0 ? std::string("st_shields") : std::string("st_shields") + kSuffix[i]);
    }
    power_ = table.find("st_power");
    group_ = table.find("st_control_group");
    flytext_ = table.find("generic_flytext");
    bribed_ = table.find("bribed_icon");
    bomb_ = table.find("remote_bomb_icon");
    garrison_flag_ = table.find("st_garrison_icon");
    garrison_slot_ = table.find("garrison_slot_icon");
    ability_icon_ = table.find("st_ability_icon");
    weather_icon_ = table.find("lt_weather_icon");
}

WorldHud::BarBox WorldHud::bars(const WorldHudContext& ctx, const HudUnit& u, BracketSize sz, const UnitScreenBounds& b,
                                const TextureAtlas& atlas, HudDrawList& dl) {
    BarBox health_box;
    const HudCanvas& cv = *ctx.canvas;
    const auto si = static_cast<size_t>(sz);
    struct BarSpec {
        int32_t comp;
        float value;
        Rgba8 fallback;
    };
    const BarSpec specs[3] = {{health_[si], u.health, kFallbackHealth},
                              {shield_[si], u.shield, kFallbackShield},
                              {power_, u.power, kFallbackPower}};
    // Stack upwards from the top of the screen bounds (pixel y decreases going up).
    float y_px = b.cy - b.radius_px - ctx.health_bar_spacing_px;
    for (const BarSpec& s : specs) {
        if (s.value < 0.0f) continue;
        const float frac = std::clamp(s.value, 0.0f, 1.0f);
        const CommandBarComponent* c = s.comp >= 0 && table_ ? &(*table_)[static_cast<size_t>(s.comp)] : nullptr;
        const int32_t max_level = c ? std::max(c->max_bar_level, 1) : 10;
        const auto level = static_cast<size_t>(std::lround(frac * static_cast<float>(max_level)));
        const uint32_t layer = c ? static_cast<uint32_t>(c->base_layer) : 0u;
        const float scale = c ? c->scale : 1.0f;
        const std::string* frame = c ? pick(c->bar_texture, level) : nullptr;
        const std::string* fill = c ? pick(c->bar_overlay, c->smooth_bar ? c->bar_overlay.size() - 1 : level) : nullptr;
        // Bar height in canvas units: the frame's atlas height, else the fill's, else the fallback.
        const AtlasEntry* fe = frame ? atlas.find(*frame) : nullptr;
        const AtlasEntry* le = fill ? atlas.find(*fill) : nullptr;
        const Vec2f fb = kFallbackBar[si];
        const float h_units = (fe ? static_cast<float>(fe->height) : le ? static_cast<float>(le->height) : fb.y) * scale;
        const float h_px = h_units / cv.units_per_pixel_y;
        const Vec2f center = cv.pixel_to_canvas(b.cx, y_px - 0.5f * h_px);
        const Vec2f frame_size = fe ? Vec2f{} : Vec2f{fb.x + 2.0f, fb.y + 2.0f};
        if (&s == &specs[0]) {
            const float w_units = fe ? static_cast<float>(fe->width) * scale : frame_size.x;
            health_box = {true, b.cx, y_px - 0.5f * h_px, w_units / cv.units_per_pixel_x, h_px};
        }
        if (!frame || !add_atlas_quad(dl, layer, HudList::Alpha, atlas, *frame, center, {}, scale, Rgba8{}))
            add_atlas_quad(dl, layer, HudList::Alpha, atlas, {}, center, {}, 1.0f, kFallbackFrame, frame_size);
        const bool smooth = c ? c->smooth_bar : true;
        const Vec2f fill_fb = le ? Vec2f{} : fb;
        if (fill && le) {
            if (smooth) {
                add_atlas_quad_clipped(dl, layer, HudList::Alpha, atlas, *fill, {center.x - 0.0f, center.y}, {}, scale, frac,
                                       Rgba8{});
            } else {
                add_atlas_quad(dl, layer, HudList::Alpha, atlas, *fill, center, {}, scale, Rgba8{});
            }
        } else {
            // White fallback, clipped to the fraction, left-aligned inside the frame.
            add_atlas_quad_clipped(dl, layer, HudList::Alpha, atlas, {}, center, {}, 1.0f, frac, s.fallback, fill_fb);
        }
        ++stats_.bars;
        health_box.top = y_px - h_px;
        y_px -= h_px + ctx.health_bar_spacing_px;
    }
    return health_box;
}

void WorldHud::status_icons(const HudUnit& u, bool allied, const BarBox& bar, const HudCanvas& cv, const TextureAtlas& atlas,
                            HudDrawList& dl) {
    // #475 (TACTICAL_COMMANDBAR_COMPONENTS.md "Overhead status record", 0x2e7fd0 / 0x31de70 [H]). Each component draws its
    // Icon_Texture_Name (the slot icon's Icon_Alternate_Texture_Name 0 when lit) at its atlas size x Scale; a missing
    // component or texture draws nothing.
    struct Icon {
        const CommandBarComponent* c = nullptr;
        const std::string* tex = nullptr;
        float w = 0, h = 0;  // pixels
    };
    auto icon = [&](int32_t comp, bool alternate) {
        Icon ic;
        if (comp < 0 || !table_) return ic;
        const CommandBarComponent& c = (*table_)[static_cast<size_t>(comp)];
        const std::string* tex = pick(alternate ? c.icon_alternate_texture : c.icon_texture, 0);
        const AtlasEntry* e = tex ? atlas.find(*tex) : nullptr;
        if (!e) return ic;
        ic = {&c, tex, static_cast<float>(e->width) * c.scale / cv.units_per_pixel_x,
              static_cast<float>(e->height) * c.scale / cv.units_per_pixel_y};
        return ic;
    };
    auto draw = [&](const Icon& ic, float px, float py) {
        if (!ic.tex) return;
        add_atlas_quad(dl, static_cast<uint32_t>(ic.c->base_layer), HudList::Alpha, atlas, *ic.tex, cv.pixel_to_canvas(px, py), {},
                       ic.c->scale, Rgba8{});
        ++stats_.status_icons;
    };
    // bribed_icon (allied) and st_garrison_icon (allied) share the spot right of the health bar, x + bar_w/2 + icon_w/2 at the
    // bar's y; remote_bomb_icon follows, one bribed-icon width further right while that shows.
    const float right = bar.cx + 0.5f * bar.w;
    float bomb_left = right;
    if (allied && u.bribed) {
        const Icon ic = icon(bribed_, false);
        draw(ic, right + 0.5f * ic.w, bar.cy);
        bomb_left += ic.w;
    }
    if (allied && u.garrison_flag) {
        const Icon ic = icon(garrison_flag_, false);
        draw(ic, right + 0.5f * ic.w, bar.cy);
    }
    if (u.bomb) {
        const Icon ic = icon(bomb_, false);
        draw(ic, bomb_left + 0.5f * ic.w, bar.cy);
    }
    // garrison_slot_icon: one per Num_Garrison_Slots, the first `garrison_used` lit (alternate texture 0, radio_on), the
    // rest the Icon (radio_off); a row centred on the bar, one icon width apart. [port] the row sits just under the health
    // bar (retail's y offset constant DAT_140b2767c is not read).
    if (u.garrison_slots > 0) {
        const Icon off = icon(garrison_slot_, false), on = icon(garrison_slot_, true);
        const float step = off.tex ? off.w : on.w;
        const float n = static_cast<float>(u.garrison_slots);
        for (uint8_t i = 0; i < u.garrison_slots; ++i) {
            const Icon& ic = i < u.garrison_used ? on : off;
            draw(ic, bar.cx + (static_cast<float>(i) - 0.5f * (n - 1.0f)) * step, bar.cy + 0.5f * bar.h + 0.5f * ic.h);
        }
    }
}

void WorldHud::ability_icons(const HudUnit& u, bool allied, const BarBox& bar, const HudCanvas& cv, const TextureAtlas& atlas,
                             HudDrawList& dl) {
    // #476 st_ability_icon (0x2e7fd0 / 0x31de70 [H]): allied units only. Slot 0's icon, then slot 1's as a second layer on
    // the same button (the first layer when slot 0 has none), each at its atlas size x the component's Scale on its
    // Base_Layer. The button is centred on the bar, 3 px plus half the first layer's height above it; the second layer
    // shares that centre. [port] above the top of the bar stack rather than the health bar alone: the port stacks the
    // shield and power bars over the health bar (retail's placement of those is not traced), which the icon would cover.
    // The component's own Icon_Texture_Name is not drawn.
    // #296 lt_weather_icon (0x31de70 [H]): any unit with the status record, its texture HudUnit::weather_icon at its atlas
    // size x the component's Scale, 3 px plus half its height above the bar, centred on the bar. While the ability icon
    // shows too, both move apart by (weather_w / 2 + ability_w / 2) / 2: the ability icon left, the weather icon right.
    const CommandBarComponent* wc = weather_icon_ >= 0 && table_ ? &(*table_)[static_cast<size_t>(weather_icon_)] : nullptr;
    const AtlasEntry* we = wc && !u.weather_icon.empty() ? atlas.find(u.weather_icon) : nullptr;
    const CommandBarComponent* ac = allied && ability_icon_ >= 0 && table_ ? &(*table_)[static_cast<size_t>(ability_icon_)] : nullptr;
    const AtlasEntry* ae = nullptr;  // the ability button's first layer
    if (ac)
        for (const std::string& tex : u.ability_icon)
            if (!ae && !tex.empty()) ae = atlas.find(tex);
    float shift = 0.0f;  // pixels
    if (we && ae)
        shift = 0.5f * (0.5f * static_cast<float>(we->width) * wc->scale + 0.5f * static_cast<float>(ae->width) * ac->scale) /
                cv.units_per_pixel_x;
    if (we) {
        const float h = static_cast<float>(we->height) * wc->scale / cv.units_per_pixel_y;
        add_atlas_quad(dl, static_cast<uint32_t>(wc->base_layer), HudList::Alpha, atlas, u.weather_icon,
                       cv.pixel_to_canvas(bar.cx + shift, bar.top - 3.0f - 0.5f * h), {}, wc->scale, Rgba8{});
        ++stats_.weather_icons;
    }
    if (!ac || !ae) return;
    std::optional<Vec2f> centre;
    for (const std::string& tex : u.ability_icon) {
        const AtlasEntry* e = tex.empty() ? nullptr : atlas.find(tex);
        if (!e) continue;
        if (!centre) {
            const float h = static_cast<float>(e->height) * ac->scale / cv.units_per_pixel_y;
            centre = cv.pixel_to_canvas(bar.cx - shift, bar.top - 3.0f - 0.5f * h);
        }
        add_atlas_quad(dl, static_cast<uint32_t>(ac->base_layer), HudList::Alpha, atlas, tex, *centre, {}, ac->scale, Rgba8{});
        ++stats_.ability_icons;
    }
}

void WorldHud::build(const WorldHudContext& ctx, std::span<const HudUnit> units, std::span<const HudTypeInfo> types,
                     const TextureAtlas& atlas, FontCache& fonts, HudDrawList& dl, std::vector<SelectionBlob>* blobs,
                     const FlyTextList* flytext) {
    stats_ = {};
    if (!ctx.canvas || ctx.interface_hidden) return;
    const HudCanvas& cv = *ctx.canvas;
    static const HudTypeInfo kNoType{};
    auto hostile = [&](int32_t owner) {
        return ctx.hostile ? ctx.hostile(owner) : (owner >= 0 && owner != ctx.local_player);
    };
    for (const HudUnit& u : units) {
        const HudTypeInfo& t = u.type < types.size() ? types[u.type] : kNoType;
        // Selection circle (3D): selected, owned by the local player [port: allies not shown], not while targeting
        // or in a cinematic, type with Select_Box_Scale > 0.
        if (blobs && u.selected && u.owner == ctx.local_player && t.select_box_scale > 0.0f && !ctx.targeting_active &&
            !ctx.cinematic) {
            SelectionBlob sb;
            sb.id = u.id;
            sb.center = u.position;
            sb.center.z += t.select_box_z_adjust;
            sb.radius = t.select_box_scale * t.scale;
            sb.land = ctx.land;
            if (!t.selection_blob_material.empty()) sb.material = t.selection_blob_material;
            else if (ctx.selection_material) sb.material = ctx.selection_material(u.owner, ctx.land, t.is_hero);
            blobs->push_back(std::move(sb));
            ++stats_.blobs;
        }
        // R5 stealth (ABILITY_VISUALS 1.4): health bars (and the icon that carries one) are hidden for hostile stealthed objects.
        const bool stealth_hidden = u.stealthed && hostile(u.owner);
        // [port] world unit icon (HudUnit::unit_icon): frame + Icon_Name + health bar, centred above the bounds.
        if (u.unit_icon && !t.icon_name.empty() && !stealth_hidden) {
            const float icon_health = u.icon_anchored ? u.icon_health : u.health;  // a squadron's icon: the squadron's
            Vec2f c;
            if (unit_icon_centre(ctx, cv, u, t, c)) {
                const float s = ctx.unit_icon_size;
                const Rgba8 fc = hostile(u.owner) ? ctx.unit_icon_hostile : ctx.unit_icon_own;
                add_atlas_quad(dl, 0, HudList::Alpha, atlas, {}, c, {}, 1.0f, Rgba8{0, 0, 0, 160}, {s, s});
                add_atlas_quad(dl, 0, HudList::Alpha, atlas, t.icon_name, c, {s - 4.0f, s - 4.0f}, 1.0f, Rgba8{});
                const float e = 1.0f;  // border width (canvas units)
                add_atlas_quad(dl, 0, HudList::Alpha, atlas, {}, {c.x, c.y + 0.5f * s - 0.5f * e}, {}, 1.0f, fc, {s, e});
                add_atlas_quad(dl, 0, HudList::Alpha, atlas, {}, {c.x, c.y - 0.5f * s + 0.5f * e}, {}, 1.0f, fc, {s, e});
                add_atlas_quad(dl, 0, HudList::Alpha, atlas, {}, {c.x - 0.5f * s + 0.5f * e, c.y}, {}, 1.0f, fc, {e, s});
                add_atlas_quad(dl, 0, HudList::Alpha, atlas, {}, {c.x + 0.5f * s - 0.5f * e, c.y}, {}, 1.0f, fc, {e, s});
                const Vec2f bar_at{c.x, c.y - 0.5f * s - 3.0f};
                add_atlas_quad_clipped(dl, 0, HudList::Alpha, atlas, {}, bar_at, {}, 1.0f, std::clamp(icon_health, 0.0f, 1.0f),
                                       kFallbackHealth, {s, 2.0f});
                ++stats_.unit_icons;
            }
        }
        if (!u.selected && !u.hovered) continue;
        const UnitScreenBounds b = unit_screen_bounds(ctx.view, u, t);
        if (!b.visible) {
            ++stats_.offscreen;
            continue;
        }
        const BracketSize sz = bracket_size(t, ctx.land);
        const auto si = static_cast<size_t>(sz);
        // Bracket: two corner pieces at the left / right bound.
        const CommandBarComponent* bc =
            bracket_[si] >= 0 && table_ ? &(*table_)[static_cast<size_t>(bracket_[si])] : nullptr;
        Rgba8 color = bc && bc->has_color ? bc->color : Rgba8{0, 255, 0, 255};
        if (hostile(u.owner)) color = kHostileBracket;
        const uint32_t layer = bc ? static_cast<uint32_t>(bc->base_layer) : 0u;
        const float scale = bc ? bc->scale : 1.0f;
        // The vanilla st_bracket_* pieces (i_icon_select_*_{l,r}.tga) are in no MTD atlas or MEG, so the original
        // draws no bracket there; a flat stand-in only without a component table (tests) [port: Gabriel playtest - the
        // stand-in boxes read as a thick "II" over the ship].
        for (int side = 0; side < 2; ++side) {
            const float px = side == 0 ? b.cx - b.radius_px : b.cx + b.radius_px;
            const Vec2f c = cv.pixel_to_canvas(px, b.cy);
            const std::string* tex = bc ? pick(bc->icon_alternate_texture, static_cast<size_t>(side)) : nullptr;
            if (tex && add_atlas_quad(dl, layer, HudList::Alpha, atlas, *tex, c, {}, scale, color)) continue;
            if (!bc) add_atlas_quad(dl, layer, HudList::Alpha, atlas, {}, c, {}, 1.0f, color, kFallbackBracket[si]);
        }
        ++stats_.brackets;
        if (!t.hide_health_bar && !stealth_hidden) {
            // #475: the status icons hang off the health bar; [port] none without one.
            const BarBox hb = bars(ctx, u, sz, b, atlas, dl);
            if (hb.valid) {
                const bool allied = u.owner >= 0 && !hostile(u.owner);
                status_icons(u, allied, hb, cv, atlas, dl);
                ability_icons(u, allied, hb, cv, atlas, dl);
            }
        }
        // Control-group number.
        if (u.control_group >= 0 && u.control_group <= 9) {
            const CommandBarComponent* gc = group_ >= 0 && table_ ? &(*table_)[static_cast<size_t>(group_)] : nullptr;
            HudTextStyle st;
            st.font = fonts.component_font(gc ? std::string_view(gc->font_name) : std::string_view(), gc ? gc->font_point_size : 9.0f,
                                           FontUse::World, cv.font_height());
            st.color = gc && gc->has_text_color ? gc->text_color : Rgba8{};
            st.outline = gc ? gc->text_outline : true;
            st.emboss = gc ? gc->text_emboss : false;
            st.align = TextAlign::Center;
            const Vec2f off = gc ? gc->text_offset : Vec2f{0, -32};
            const Vec2f anchor = cv.pixel_to_canvas(b.cx + off.x / cv.units_per_pixel_x, b.cy + off.y / cv.units_per_pixel_y);
            const char32_t digit[2] = {static_cast<char32_t>(U'0' + static_cast<char32_t>(u.control_group)), 0};
            add_text(dl, gc ? static_cast<uint32_t>(gc->base_layer) : 0u, HudList::Text, fonts, cv,
                     std::u32string_view(digit, 1), anchor, st);
            ++stats_.group_numbers;
        }
    }
    // Fly-text: re-projected every frame (+0x2af8 list), rising [port].
    if (flytext) {
        const CommandBarComponent* fc = flytext_ >= 0 && table_ ? &(*table_)[static_cast<size_t>(flytext_)] : nullptr;
        HudTextStyle st;
        st.font = fonts.component_font(fc ? std::string_view(fc->font_name) : std::string_view(), fc ? fc->font_point_size : 0.0f,
                                       FontUse::World, cv.font_height());
        st.outline = fc ? fc->text_outline : false;
        st.emboss = fc ? fc->text_emboss : true;
        st.align = TextAlign::Center;
        const uint32_t layer = fc ? static_cast<uint32_t>(fc->base_layer) : 1u;
        for (const FlyText& f : flytext->items()) {
            float sx = 0, sy = 0;
            if (!project(ctx.view, f.anchor, sx, sy)) continue;
            sx += f.offset_px.x;
            sy += f.offset_px.y - f.rise_px_per_s * f.age;
            if (sx < 0 || sy < 0 || sx > static_cast<float>(ctx.view.width) || sy > static_cast<float>(ctx.view.height)) continue;
            st.color = f.color;
            add_text(dl, layer, HudList::Text, fonts, cv, f.text, cv.pixel_to_canvas(sx, sy), st);
            ++stats_.flytexts;
        }
    }
}

std::optional<size_t> pick_hud_unit(std::span<const HudUnit> units, const View& view, int32_t px, int32_t py, bool skip_neutral) {
    std::vector<PickSphere> spheres;
    std::vector<size_t> index;
    spheres.reserve(units.size());
    index.reserve(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        const HudUnit& u = units[i];
        if (skip_neutral && u.owner < 0) continue;
        PickSphere ps;
        ps.id = u.id;
        ps.center = u.position;
        ps.radius = u.radius > 0.0f ? u.radius : 10.0f;
        ps.owner = u.owner;
        spheres.push_back(ps);
        index.push_back(i);
    }
    const std::optional<size_t> hit = pick_spheres(spheres, pixel_ray(view, px, py));
    if (!hit) return std::nullopt;
    return index[*hit];
}

}  // namespace alamo::ui
