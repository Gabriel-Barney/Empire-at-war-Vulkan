// test_gui_screens: tests split from test_gui_screens.cpp.
#include "test_gui_screens_common.hpp"
#include "alamo/ui/credits_roll.hpp"
#include "alamo/ui/movie_caption.hpp"
#include "alamo/ui/text_crawl.hpp"

namespace test_gui_screens_t {

// ---- mouse cursor ---------------------------------------------------------------------------------------------------
// CreditsText DAT (legacy docs/formats/SPEC.md §7: records in file order, HEADER / CENTER keys, "[TBL]" blank) and the roll.
void test_credits_roll() {
    std::vector<uint8_t> b;
    auto u32le = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    const std::vector<std::pair<std::string, std::u16string>> recs = {
        {"CENTER", u"[TBL]"}, {"HEADER", u"Producer"}, {"CENTER", u"A NAME"}, {"SUBHEADER", u"Art"}};
    u32le(static_cast<uint32_t>(recs.size()));
    for (const auto& [k, v] : recs) {
        u32le(0);
        u32le(static_cast<uint32_t>(v.size()));
        u32le(static_cast<uint32_t>(k.size()));
    }
    for (const auto& r : recs)
        for (const char16_t c : r.second) {
            b.push_back(static_cast<uint8_t>(c & 0xFF));
            b.push_back(static_cast<uint8_t>(c >> 8));
        }
    for (const auto& r : recs) b.insert(b.end(), r.first.begin(), r.first.end());
    std::vector<CreditsLine> lines;
    CHECK(parse_credits(b, lines) && lines.size() == 4);
    if (lines.size() == 4)
        CHECK(lines[0].kind == CreditsLine::Kind::Blank && lines[1].kind == CreditsLine::Kind::Header && lines[1].text == U"Producer" &&
              lines[2].kind == CreditsLine::Kind::Name && lines[2].text == U"A NAME" &&
              lines[3].kind == CreditsLine::Kind::Header);  // #426: a key that contains HEADER
    std::vector<uint8_t> cut(b.begin(), b.end() - 3);
    CHECK(!parse_credits(cut, lines));
    CHECK(credits_path("ENGLISH") == "Data\\Text\\CreditsText_ENGLISH.DAT");
    // #426 (docs/re/CREDITS_ROLL.md): the retail defaults, the pixel target and the one-pixel-per-frame cap, the pitch
    // floor(spacing x height), the end when the last record's top reaches the top, and no fade.
    {
        const CreditsStyle d;
        CHECK(d.font == "Arial Unicode MS" && d.font_size == 10.0f && d.spacing == 0.01f && d.scroll_rate == -0.01f && d.margin == 0.0f &&
              d.top.r == 255 && d.header_bottom.b == 255);
        CHECK(d.logos[0].name.empty() && d.logos[2].width == 0.1f && d.logos[1].height == 0.1f && d.logos[0].y_offset == 0.5f);
    }
    // #458: the logo rectangles (0x44ecdc) and the margin fade.
    {
        CreditsLogo bink{"Logo_Bink.DDS", 0.15f, 0.15f, 0.1f};
        // 256 x 128 texture at 1024 x 768, 4 records 30 px apart: w = 153.6, h = 0.15 x (128/768)/(256/1024) x 768 = 76.8
        const CreditsRect r0 = credits_logo_rect(0, bink, 4, 30, 1024, 768, 256.0f, 128.0f);
        CHECK(std::fabs(r0.x) < 1e-3f && std::fabs(r0.w - 153.6f) < 1e-3f && std::fabs(r0.h - 76.8f) < 1e-3f &&
              std::fabs(r0.y - (768.0f + 76.8f + 120.0f)) < 1e-3f);
        const CreditsRect r1 = credits_logo_rect(1, bink, 4, 30, 1024, 768, 256.0f, 128.0f);
        const CreditsRect r2 = credits_logo_rect(2, bink, 4, 30, 1024, 768, 256.0f, 128.0f);
        CHECK(std::fabs(r1.x - 435.2f) < 1e-3f && std::fabs(r2.x - 870.4f) < 1e-3f && r1.y == r0.y);
        // the offsets do not add up: logo 2 with its own offset sits at 1 + N x pitch + its offset
        const CreditsRect gs = credits_logo_rect(1, {"Logo_GameSpy.DDS", 0.3f, 0.3f, 0.19f}, 4, 30, 1024, 768, 256.0f, 256.0f);
        CHECK(std::fabs(gs.y - (768.0f * 1.19f + 120.0f)) < 1e-2f);
        CHECK(credits_logo_alpha(0.05f, 0.1f) == 0.0f && std::fabs(credits_logo_alpha(0.15f, 0.1f) - 0.5f) < 1e-6f &&
              credits_logo_alpha(0.5f, 0.1f) == 1.0f && std::fabs(credits_logo_alpha(0.85f, 0.1f) - 0.5f) < 1e-6f &&
              credits_logo_alpha(0.95f, 0.1f) == 0.0f && std::fabs(credits_logo_alpha(0.2f, 0.1f) - 1.0f) < 1e-6f);
        CHECK(credits_logo_alpha(0.5f, 0.0f) == 1.0f && credits_logo_alpha(-0.01f, 0.0f) == 0.0f && credits_logo_alpha(1.01f, 0.0f) == 0.0f);
    }
    CHECK(credits_scroll_target_px(-0.10f, 768, 3.0) == 232);  // floor(230.4) + 2
    CHECK(credits_scroll_target_px(-0.10f, 768, 0.0) == 2);
    CHECK(credits_scroll_target_px(0.0f, 768, 9.0) == 2);  // a rate of 0 stops after two pixels
    CreditsStyle st;
    st.scroll_rate = -0.10f;
    st.spacing = 0.04f;  // 30 px at 768
    CreditsRoll roll;
    std::vector<CreditsLine> parsed;
    CHECK(parse_credits(b, parsed));
    roll.start(parsed, st, 100.0);
    CHECK(roll.active() && roll.lines() == 4);
    auto raster = make_box_rasterizer();
    FontCache fonts(*raster);
    const HudCanvas cv = HudCanvas::make(1024, 768);
    HudDrawList dl;
    for (int f = 0; f < 3; ++f) {  // frames at 0, 0.01 and 0.02 s: one pixel each
        dl.clear();
        CHECK(roll.build(dl, fonts, cv, 100.0 + 0.01 * f) && roll.moved_px() == f + 1);
    }
    CHECK(roll.drawn() == 0);  // every line still below the screen
    dl.clear();
    // A long gap: still one pixel for this frame, then the roll catches up one pixel per frame.
    CHECK(roll.build(dl, fonts, cv, 103.0) && roll.moved_px() == 4);
    for (int f = 0; f < 400; ++f) {
        dl.clear();
        roll.build(dl, fonts, cv, 103.0);
    }
    CHECK(roll.moved_px() == 232 && roll.last_top_px() == 536.0f);  // line 0's top: 768 - 232
    CHECK(roll.drawn() == 3);  // the three text lines (tops 566, 596, 626) are on screen
    bool opaque = true;
    for (const HudQuad& q : dl.items(kHudLayers - 1, HudList::Text)) opaque = opaque && q.color.a == 255;
    CHECK(opaque);  // the text never fades
    // The end: the last record's top (768 + 3 x 30 - moved) reaches 0 at 858 px.
    double t = 103.0;
    for (int f = 0; f < 2000 && roll.active(); ++f) {
        dl.clear();
        t += 0.01;
        roll.build(dl, fonts, cv, t);
    }
    CHECK(!roll.active() && roll.moved_px() == 858);
    // #458: with a logo (Y_Offset 0.19) the roll runs until its top leaves too: 768 x 1.19 + 4 x 30 = 1033.92 -> 1034 px;
    // a logo whose texture does not resolve is skipped.
    {
        CreditsStyle ls = st;
        ls.margin = 0.04f;
        ls.logos[1] = {"Logo_GameSpy.DDS", 0.3f, 0.3f, 0.19f};
        ls.logos[2] = {"missing.dds", 0.2f, 0.2f, 0.9f};
        const GuiImageResolver images = [](std::string_view t) {
            GuiImage im;
            if (t != "Logo_GameSpy.DDS") return im;
            im.valid = true;
            im.page = kHudPageImageBase;
            im.size = {256.0f, 256.0f};
            return im;
        };
        CreditsRoll lr;
        lr.start(parsed, ls, 0.0, false);
        double lt = 0.0;
        size_t most = 0;
        uint8_t faded = 255, full = 0;
        for (int f = 0; f < 3000 && lr.active(); ++f) {
            dl.clear();
            lt += 0.01;
            if (!lr.build(dl, fonts, cv, lt, &images)) break;
            most = std::max(most, lr.logos_drawn());
            for (const HudQuad& q : dl.items(kHudLayers - 1, HudList::Alpha))
                if (q.page == kHudPageImageBase) {
                    faded = std::min(faded, q.color.a);
                    full = std::max(full, q.color.a);
                }
        }
        CHECK(!lr.active() && lr.moved_px() == 1034);
        CHECK(most == 1 && full == 255 && faded < 255);  // one logo, faded in and out by the margin
        // a page that resolves once and then is gone (reloading / evicted) still holds the end: the size is kept
        int calls = 0;
        const GuiImageResolver flaky = [&calls, &images](std::string_view t) { return ++calls <= 2 ? images(t) : GuiImage{}; };
        CreditsRoll fr;
        fr.start(parsed, ls, 0.0, false);
        lt = 0.0;
        for (int f = 0; f < 3000 && fr.active(); ++f) {
            dl.clear();
            lt += 0.01;
            if (!fr.build(dl, fonts, cv, lt, &flaky)) break;
        }
        CHECK(!fr.active() && fr.moved_px() == 1034 && fr.logos_drawn() == 0);
    }
    // A black backdrop only when asked for (no Credits movie).
    CreditsRoll over_movie;
    over_movie.start(parsed, st, 0.0, false);
    dl.clear();
    CHECK(over_movie.build(dl, fonts, cv, 0.0) && dl.items(kHudLayers - 1, HudList::Alpha).empty());
    roll.start(parsed, st, 0.0);
    dl.clear();
    CHECK(roll.build(dl, fonts, cv, 0.0) && dl.items(kHudLayers - 1, HudList::Alpha).size() == 1);

    // the text crawl - nothing before its lines reach the screen, lines once it scrolled, the header first and
    // larger at the start, all yellow, alpha following Fadein, nothing once every line passed the horizon.
    {
        HudDrawList cl;
        TextCrawlLayout lay;
        lay.fadein_start = 0;
        lay.fadein_end = 60;
        const std::u32string paras[] = {U"First paragraph of the crawl", U"Second paragraph"};
        CHECK(add_text_crawl(cl, 1, fonts, cv, lay, U"EPISODE", paras, 0) == 0);
        cl.clear();
        const uint32_t early = add_text_crawl(cl, 1, fonts, cv, lay, U"EPISODE", paras, 30);
        bool yellow = true;
        uint8_t alpha = 0;
        for (const HudQuad& q : cl.items(1, HudList::Text)) {
            yellow = yellow && q.color.r == 255 && q.color.g == 232 && q.color.b == 31;
            alpha = std::max(alpha, q.color.a);
        }
        CHECK(early >= 1 && yellow && alpha > 0 && alpha < 255);  // half way through the fade-in
        cl.clear();
        const uint32_t mid = add_text_crawl(cl, 1, fonts, cv, lay, U"EPISODE", paras, 30 * 8);
        CHECK(mid >= early && mid >= 3);
        cl.clear();
        CHECK(add_text_crawl(cl, 1, fonts, cv, lay, U"EPISODE", paras, 30 * 3000) == 0);
        std::printf("text crawl: %u lines at 1 s, %u at 8 s\n", early, mid);
    }

    // ui5 t12 (legacy docs/re/AUDIO.md §3.4): a movie caption - white, centred, the last line's top at 0.95 of the screen height, the
    // line above one font line higher; nothing for an empty text.
    dl.clear();
    MovieCaptionStyle cs;
    CHECK(add_movie_caption(dl, 1, fonts, cv, U"", cs) == 0 && dl.total() == 0);
    CHECK(add_movie_caption(dl, 1, fonts, cv, U"First line\nSecond", cs) == 2);
    const std::span<const HudQuad> glyphs = dl.items(1, HudList::Text);
    CHECK(!glyphs.empty());
    if (!glyphs.empty()) {
        const int32_t font = fonts.font(cs.font, cs.font_size, cv.font_height());
        const float lh_units = static_cast<float>(fonts.metrics(font).line_height) * (cv.pixel_to_canvas(0.0f, 0.0f).y -
                                                                                       cv.pixel_to_canvas(0.0f, 1.0f).y);
        float lo = 1e9f, hi = -1e9f, left = 1e9f, right = -1e9f;
        bool white = true;
        for (const HudQuad& q : glyphs) {
            lo = std::min(lo, q.min.y);
            hi = std::max(hi, q.max.y);
            left = std::min(left, q.min.x);
            right = std::max(right, q.max.x);
            white = white && q.color.r == 255 && q.color.g == 255 && q.color.b == 255 && q.color.a == 255;
        }
        const float bottom_line_top = cv.pixel_to_canvas(0.0f, 0.95f * static_cast<float>(cv.screen_height)).y;
        CHECK(white);
        CHECK(hi <= bottom_line_top + 2.0f * lh_units + 0.01f && hi > bottom_line_top);  // two lines: the top one above 0.95
        CHECK(lo >= bottom_line_top - lh_units - 0.01f);                                  // the last starts at 0.95
        CHECK(std::fabs(0.5f * (left + right)) < 0.25f * cv.width);                       // centred on the screen
    }
}

void test_cursor() {
    const char* xml = R"(<MousePointers>
      <MousePointer Name="POINTER_NORMAL"><Base_Texture> DEFAULTPOINTER_00.TGA </Base_Texture><Hot_X>0</Hot_X><Hot_Y>0</Hot_Y><Anim_Frame_Delay>5</Anim_Frame_Delay></MousePointer>
      <MousePointer Name="POINTER_ATTACK"><Base_Texture>ATTACKPOINTER_00.TGA</Base_Texture><Hot_X>16</Hot_X><Hot_Y>16</Hot_Y><Anim_Frame_Delay>3</Anim_Frame_Delay></MousePointer>
      <MousePointer Name="POINTER_MOVIE_MODE"><Base_Texture>MOVIEPOINTER.TGA</Base_Texture><Hot_X>0</Hot_X><Hot_Y>0</Hot_Y></MousePointer>
    </MousePointers>)";
    const data::XmlDocument doc = data::XmlDocument::parse(std::string_view(xml), "MousePointers.xml");
    CursorTable t;
    CHECK(t.parse(doc.root()) == 3);
    TextureAtlas atlas;
    atlas.texture_width = atlas.texture_height = 256;
    for (const char* n : {"DEFAULTPOINTER_00.TGA", "ATTACKPOINTER_00.TGA", "ATTACKPOINTER_01.TGA", "ATTACKPOINTER_02.TGA"})
        atlas.add({n, 0, 0, 32, 32, true});
    CHECK(t.resolve_frames([&](std::string_view n) { return atlas.find(n) != nullptr; }) == 5);
    CHECK(t.find(Pointer::Attack)->frames.size() == 3 && t.find(Pointer::Attack)->frames[2] == "ATTACKPOINTER_02.TGA" &&
          t.find(Pointer::MovieMode)->frames.size() == 1);
    Pointer p;
    CHECK(pointer_from_name("pointer_repair_hardpoint", p) && p == Pointer::RepairHardpoint && kPointerCount == 53);
    // Context -> pointer.
    CursorContext c;
    CHECK(choose_pointer(c) == Pointer::Normal);
    c.hover = CursorContext::Hover::Enemy;
    CHECK(choose_pointer(c) == Pointer::Normal);  // nothing selected
    c.selection = true;
    CHECK(choose_pointer(c) == Pointer::Attack);
    c.in_range = false;
    CHECK(choose_pointer(c) == Pointer::AttackOutOfRange);
    c.can_attack = false;
    CHECK(choose_pointer(c) == Pointer::CantAttack);
    c.can_attack = c.in_range = true;
    // own -> SELECT, garrisonable own / friendly / neutral with a selection -> GARRISON_IN.
    c.hover = CursorContext::Hover::Own;
    CHECK(choose_pointer(c) == Pointer::Select);
    c.garrisonable = true;
    CHECK(choose_pointer(c) == Pointer::GarrisonIn);
    // #477: an own object with a sell offer -> TACTICAL_SELL, with or without a selection; only for an own object.
    c.sellable = true;
    CHECK(choose_pointer(c) == Pointer::TacticalSell);
    c.selection = false;
    CHECK(choose_pointer(c) == Pointer::TacticalSell);
    c.selection = true;
    c.hover = CursorContext::Hover::Friendly;
    CHECK(choose_pointer(c) == Pointer::GarrisonIn);
    c.sellable = false;
    c.hover = CursorContext::Hover::Neutral;
    CHECK(choose_pointer(c) == Pointer::GarrisonIn);
    c.hover = CursorContext::Hover::Friendly;
    CHECK(choose_pointer(c) == Pointer::GarrisonIn);
    c.selection = false;
    CHECK(choose_pointer(c) == Pointer::Normal);
    c.selection = true;
    c.garrisonable = false;
    c.hover = CursorContext::Hover::Ground;
    CHECK(choose_pointer(c) == Pointer::Move);
    c.rotating = true;
    CHECK(choose_pointer(c) == Pointer::MapRotatingMode);
    c.rotating = false;
    c.scrolling = true;
    CHECK(choose_pointer(c) == Pointer::MapScrollingMode);
    c.scrolling = false;
    c.passable = false;
    CHECK(choose_pointer(c) == Pointer::CantMove);
    c.targeting = CursorContext::Targeting::AbilityEnemy;
    c.target_valid = false;
    CHECK(choose_pointer(c) == Pointer::AbilityEnemyInvalid);
    c.over_ui = true;
    c.over_sell = true;  // #477: over the tactical_sell button
    CHECK(choose_pointer(c) == Pointer::TacticalSell);
    c.over_sell = false;
    c.over_draggable = true;
    CHECK(choose_pointer(c) == Pointer::CanGrab);
    c.fleet_drag = true;
    c.drop_on_planet = true;
    CHECK(choose_pointer(c) == Pointer::Drop);
    c.busy = true;
    CHECK(choose_pointer(c) == Pointer::Wait);
    // Animation: 3 frames, 3 ticks (0.1 s) each; restarts on change.
    MouseCursor mc;
    mc.init(t, &atlas, {});
    mc.update(Pointer::Attack, 10.0);
    CHECK(mc.frame(10.05) == 0 && mc.frame(10.15) == 1 && mc.frame(10.25) == 2 && mc.frame(10.35) == 0);
    CHECK(mc.texture(10.15) == "ATTACKPOINTER_01.TGA");
    const HudCanvas cv = HudCanvas::make(1024, 768);
    HudDrawList dl;
    CHECK(mc.build(dl, cv, {512, 384}, 10.0));
    const auto q = dl.items(kHudLayers - 1, HudList::Alpha);
    CHECK(q.size() == 1 && std::fabs(q[0].min.x + 16.0f) < 1e-3f && std::fabs(q[0].max.y - 16.0f) < 1e-3f &&
          std::fabs(q[0].max.x - 16.0f) < 1e-3f);  // hot spot 16,16 at the cursor
    mc.update(Pointer::CantGrab, 11.0);  // no entry -> falls back to POINTER_NORMAL
    dl.clear();
    CHECK(mc.build(dl, cv, {0, 0}, 11.0) && mc.texture(11.0) == "DEFAULTPOINTER_00.TGA");
}

// ---- Can_Animate, hack panel, fleet drag and drop -------------------------------------------------------------------
void test_anim_hack_drag() {
    PanelEnv env;
    for (const char* n : {"i_anim_static.tga", "i_anim_00.tga", "i_anim_01.tga", "i_anim_02.tga"})
        env.atlas.add({n, static_cast<uint32_t>(env.atlas.size()) * 8u, 0, 8, 8, true});
    const CommandBarComponent& ab = *env.table.get("anim_button");
    const CommandBarComponent& au = *env.table.get("anim_upper");
    CHECK(ab.can_animate && ab.anim_fps == 5.0f);
    // anim_button: 5 fps, 3 frames, one pass; anim_upper: 10 fps ping-pong 0 1 2 1 0 1 ..., looping.
    CHECK(anim_frame(ab, 1.0, 1.0, 3, false) == 0 && anim_frame(ab, 1.0, 1.21, 3, false) == 1 && anim_frame(ab, 1.0, 1.45, 3, false) == 2 &&
          anim_frame(ab, 1.0, 1.61, 3, false) == -1 && anim_frame(ab, 1.0, 1.61, 3, true) == 0);
    CHECK(anim_frame(au, 0.0, 0.25, 3, true) == 2 && anim_frame(au, 0.0, 0.35, 3, true) == 1 && anim_frame(au, 0.0, 0.45, 3, true) == 0 &&
          anim_frame(au, 0.0, 0.55, 3, true) == 1);
    HudButtons b;
    b.bind(env.table);
    const auto ci = static_cast<uint32_t>(env.table.find("anim_button"));
    const HudCanvas cv = HudCanvas::make(1024, 768);
    auto icon_u = [&](double now, HudList list) {
        HudDrawList dl;
        b.draw(dl, env.atlas, nullptr, cv, ci, {0, 0}, {}, 1, now);
        const auto q = dl.items(0, list);
        return q.empty() ? -1.0f : q.back().uv0.x * 512.0f;
    };
    const float idle = icon_u(0.0, HudList::Alpha);
    b.play_anim(ci, 5.0);
    const float f1 = icon_u(5.25, HudList::Alpha);
    CHECK(idle != f1 && f1 == static_cast<float>(env.atlas.find("i_anim_01.tga")->x));
    CHECK(icon_u(6.0, HudList::Alpha) == idle);  // one pass done
    const auto cu = static_cast<uint32_t>(env.table.find("anim_upper"));
    b.play_anim(cu, 0.0, true);
    HudDrawList dl;
    b.draw(dl, env.atlas, nullptr, cv, cu, {0, 0}, {}, 1, 0.05);
    // static icon, then the frame over it (later in the same PrimAlpha list)
    CHECK(dl.items(0, HudList::Alpha).size() == 2 && dl.items(0, HudList::Alpha)[1].uv0.x * 512.0f != dl.items(0, HudList::Alpha)[0].uv0.x * 512.0f);
    b.stop_anim(cu);
    dl.clear();
    b.draw(dl, env.atlas, nullptr, cv, cu, {0, 0}, {}, 1, 0.05);
    CHECK(dl.items(0, HudList::Alpha).size() == 1);

    // Hack panel.
    HackPanel hp;
    CHECK(hp.init(env.table, env.atlas, env.fonts));
    HackFrame hf;
    hf.open = true;
    hf.target = 9001;
    hf.planet = 600;
    hf.title = U"Hack";
    hf.lines = {U"Disable the shield generator"};
    hf.cost = U"500";
    hp.build(hf);
    std::vector<HudAction> acts;
    env.click(hp, "hack_hack", acts);
    env.click(hp, "hack_close", acts);
    CHECK(acts.size() == 2 && acts[0].kind == HudAction::Kind::HackConfirm && acts[0].unit == 9001 && acts[0].ids == std::vector<uint32_t>{600} &&
          acts[1].kind == HudAction::Kind::PanelClose && acts[1].name == "hack_shell");

    // Fleet drag and drop on the galactic HUD.
    ComponentTable ft;
    const data::XmlDocument fd = data::XmlDocument::parse(std::string_view(kFleetXml), "f.xml");
    ft.parse(fd.root());
    TextureAtlas fa;
    fa.texture_width = fa.texture_height = 64;
    fa.add({"i_big.tga", 0, 0, 30, 30, true});
    fa.add({"i_frame.tga", 0, 0, 40, 40, true});
    fa.add({"fleet_stacks_01.tga", 0, 0, 36, 45, true});
    fa.add({"fleet_empty.tga", 0, 0, 36, 45, true});
    GalacticHud hud;
    hud.init(ft, fa, env.fonts);
    hud.resize(1024, 768);
    GalacticCameraController cam;
    std::vector<GalacticPlanetInfo> planets(2);
    planets[0].planet = 100;
    planets[0].position = {200, 100, 0};
    planets[0].selected = true;
    planets[1].planet = 101;
    planets[1].position = {350, 200, 0};
    GalacticHudFrame gf;
    gf.view = make_view(cam.params(4.0f / 3.0f), 1024, 768);
    gf.planets = planets;
    gf.providers.fleets = [](uint32_t, std::vector<GalacticFleet>& out) {
        out.push_back({900, {{901, "i_frame.tga", 1, false}, {902, "i_frame.tga", 1, false}}});
        out.push_back({950, {{951, "i_frame.tga", 1, false}}});
    };
    gf.providers.land_forces = [](uint32_t, std::vector<GalacticFleetSlot>& out) { out.push_back({701, "i_frame.tga", 1, false}); };
    hud.build(gf);
    CHECK(hud.draggable(ft.find("q1_0001")) && hud.draggable(ft.find("q1_fleet")) && hud.draggable(ft.find("land_fleet_00")));
    auto center_px = [&](std::string_view name) {
        const int32_t c = ft.find(name);
        for (const HudHitBox& h : hud.hit_boxes())
            if (h.component == c) return hud.canvas().canvas_to_pixel(0.5f * (h.min.x + h.max.x), 0.5f * (h.min.y + h.max.y));
        return Vec2f{-1, -1};
    };
    // GalacticHud::drag_update takes drag().valid from a dry run of drop(), so the highlight (valid) and the drop
    // (commands emitted) cannot disagree; every drag below is checked against that.
    bool drag_valid = false;
    auto drag = [&](std::string_view from, Vec2f to, int32_t to_component) {
        const int32_t fc = ft.find(from);
        const Vec2f p = center_px(from);
        CHECK(p.x >= 0);
        std::vector<commands::EventData> out;
        RoutedInput r;
        r.world.mouse_x = static_cast<int32_t>(p.x);
        r.world.mouse_y = static_cast<int32_t>(p.y);
        r.hover_component = fc;
        r.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
        r.hud = {{HudPointerEvent::Kind::Press, Button::Left, fc, r.world.mouse_x, r.world.mouse_y}};
        hud.update_input(r, false, false, 1.0, out);
        r.hud.clear();
        r.world.mouse_x = static_cast<int32_t>(to.x);
        r.world.mouse_y = static_cast<int32_t>(to.y);
        r.hover_component = to_component;
        hud.update_input(r, false, false, 1.1, out);
        CHECK(hud.drag().active && hud.drag().component == fc);
        drag_valid = hud.drag().valid;
        hud.build(gf);  // the dragged icon follows the cursor
        CHECK(!hud.draw_list().items(kHudLayers - 1, HudList::Alpha).empty());
        r.owner = {};
        r.hud = {{HudPointerEvent::Kind::Release, Button::Left, to_component, r.world.mouse_x, r.world.mouse_y}};
        hud.update_input(r, false, false, 1.2, out);
        CHECK(!hud.drag().active);
        CHECK(drag_valid == !out.empty());
        hud.build(gf);
        return out;
    };
    // unit 902 of fleet 900 onto fleet 950's pad -> merge (op 3) {target 950, fleet 900, unit 902}
    std::vector<commands::EventData> o = drag("q1_0002", center_px("q2_0001"), ft.find("q2_0001"));
    CHECK(o.size() == 1 && o[0] == fleet_management_event(commands::kFleetMerge, 950, 900, 902));
    // fleet 900 header onto the other planet -> MoveObjectToObject(900, 101)
    float sx = 0, sy = 0;
    CHECK(project(gf.view, planets[1].position, sx, sy));
    o = drag("q1_fleet", {sx, sy}, -1);
    CHECK(o.size() == 1 && o[0] == commands::ev::move_object_to_object(900, 101));
    // fleet header onto its own planet -> nothing; land unit onto a fleet -> launch (op 4)
    CHECK(project(gf.view, planets[0].position, sx, sy));
    CHECK(drag("q1_fleet", {sx, sy}, -1).empty());
    // a fleet unit onto its own planet: drop() orders the move (it did before cu13 t07), so the highlight says valid now (the
    // old hand-written predicate refused it); the same unit onto a HUD component that takes no drop is invalid and sends nothing.
    o = drag("q1_0001", {sx, sy}, -1);
    CHECK(drag_valid && o.size() == 1 && o[0] == commands::ev::move_object_to_object(901, 100));
    o = drag("q1_0001", center_px("b_play_pause"), ft.find("b_play_pause"));
    CHECK(!drag_valid && o.empty());
    o = drag("land_fleet_00", center_px("q1_0001"), ft.find("q1_0001"));
    CHECK(o.size() == 1 && o[0] == fleet_management_event(commands::kFleetLaunch, 900, commands::kNoObject, 701));
    // unit into empty space -> detach (op 2) into a new fleet at the selected planet
    o = drag("q1_0001", {1000, 10}, -1);
    CHECK(o.size() == 1 && o[0] == fleet_management_event(commands::kFleetDetach, 100, commands::kNoObject, 901));
    // a short press (no drag) still clicks: GalacticSelect
    std::vector<HudAction> ga;
    std::vector<commands::EventData> co;
    RoutedInput r;
    const int32_t fc = ft.find("q1_0001");
    r.hover_component = fc;
    r.hud = {{HudPointerEvent::Kind::Press, Button::Left, fc, 0, 0}, {HudPointerEvent::Kind::Release, Button::Left, fc, 0, 0},
             {HudPointerEvent::Kind::Click, Button::Left, fc, 0, 0}};
    hud.update_input(r, false, false, 2.0, co, &ga);
    CHECK(co.empty() && ga.size() == 1 && ga[0].kind == HudAction::Kind::GalacticSelect);

    // GALAXY_MAP_PRESENTATION Â§1 map fleet buttons (one g_planet_fleet component, one instance per slot): a left click
    // picks the instance under the cursor -> GalacticSelect {fleet}; right click nothing; an own fleet drags to another
    // planet -> MoveObjectToObject; an enemy fleet does not drag; a land-forces button is click-only.
    {
        planets[0].orbit_local = true;
        GalacticPlanetStack own;
        own.slot = 2;
        own.units = 3;
        own.fleet = 960;
        GalacticPlanetStack foe;
        foe.slot = 0;
        foe.units = 2;
        foe.enemy = true;
        foe.fleet = 990;
        GalacticPlanetStack none;  // no fleet id: drawn, not clickable
        none.slot = 1;
        none.units = 1;
        GalacticPlanetStack land;
        land.land = true;
        land.units = 4;
        land.fleet = 970;
        planets[0].stacks = {own, foe, none, land};
        hud.build(gf);
        const int32_t gpf = ft.find("g_planet_fleet"), gpl = ft.find("g_planet_land_forces");
        std::vector<Vec2f> fleet_px;  // hit centres (pixels) in draw order: slot 0 (foe), slot 2 (own)
        Vec2f land_px{-1, -1};
        for (const HudHitBox& h : hud.hit_boxes()) {
            const Vec2f c = hud.canvas().canvas_to_pixel(0.5f * (h.min.x + h.max.x), 0.5f * (h.min.y + h.max.y));
            if (h.component == gpf) fleet_px.push_back(c);
            if (h.component == gpl) land_px = c;
            if (h.component == gpf) CHECK(std::fabs(h.max.x - h.min.x - 36.0f) < 0.01f && std::fabs(h.max.y - h.min.y - 45.0f) < 0.01f);
        }
        CHECK(fleet_px.size() == 2 && land_px.x >= 0 && hud.draggable(gpf) && !hud.draggable(gpl));
        // Right presses on these buttons go to the world (planet picking / right-click move); left presses stay on the HUD.
        CHECK(!hud.takes_button(gpf, Button::Right) && !hud.takes_button(gpl, Button::Right) && hud.takes_button(gpf, Button::Left) &&
              hud.takes_button(ft.find("q1_0001"), Button::Right));
        {
            InputRouter router;
            RouteHooks hooks;
            hooks.hud_hit = [&](int32_t, int32_t) { return gpl; };
            hooks.hud_takes = [&](int32_t c, Button b) { return hud.takes_button(c, b); };
            InputFrame raw;
            raw.width = 1024;
            raw.height = 768;
            raw.buttons_down = button_bit(Button::Right);
            raw.buttons_pressed = button_bit(Button::Right);
            RoutedInput ro;
            router.route(raw, hooks, ro);
            CHECK(ro.owner[static_cast<size_t>(Button::Right)] == PointerTarget::World && (ro.world.buttons_pressed & button_bit(Button::Right)) &&
                  ro.hud.empty());
            raw.buttons_down = static_cast<uint8_t>(button_bit(Button::Right) | button_bit(Button::Left));
            raw.buttons_pressed = button_bit(Button::Left);
            router.route(raw, hooks, ro);
            CHECK(ro.owner[static_cast<size_t>(Button::Left)] == PointerTarget::Hud);
        }
        auto click_at = [&](int32_t comp, Vec2f px, Button b) {
            std::vector<HudAction> acts;
            std::vector<commands::EventData> cmds;
            RoutedInput ri;
            const auto x = static_cast<int32_t>(px.x), y = static_cast<int32_t>(px.y);
            ri.world.mouse_x = x;
            ri.world.mouse_y = y;
            ri.hover_component = comp;
            ri.hud = {{HudPointerEvent::Kind::Press, b, comp, x, y}, {HudPointerEvent::Kind::Release, b, comp, x, y},
                      {HudPointerEvent::Kind::Click, b, comp, x, y}};
            hud.update_input(ri, false, false, 3.0, cmds, &acts);
            CHECK(cmds.empty());
            return acts;
        };
        if (fleet_px.size() == 2) {
            std::vector<HudAction> a = click_at(gpf, fleet_px[1], Button::Left);
            CHECK(a.size() == 1 && a[0].kind == HudAction::Kind::GalacticSelect && a[0].ids == std::vector<uint32_t>{960} &&
                  a[0].unit == 100 && a[0].value == 2);
            a = click_at(gpf, fleet_px[0], Button::Left);
            CHECK(a.size() == 1 && a[0].ids == std::vector<uint32_t>{990} && a[0].value == 0);
            CHECK(click_at(gpf, fleet_px[1], Button::Right).empty());
            a = click_at(gpl, land_px, Button::Left);
            CHECK(a.size() == 1 && a[0].ids == std::vector<uint32_t>{970} && a[0].value == -1);
            // Drag the own fleet onto planet 101.
            auto drag_stack = [&](Vec2f from, Vec2f to) {
                std::vector<commands::EventData> out;
                RoutedInput rr;
                rr.world.mouse_x = static_cast<int32_t>(from.x);
                rr.world.mouse_y = static_cast<int32_t>(from.y);
                rr.hover_component = gpf;
                rr.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
                rr.hud = {{HudPointerEvent::Kind::Press, Button::Left, gpf, rr.world.mouse_x, rr.world.mouse_y}};
                hud.update_input(rr, false, false, 4.0, out);
                rr.hud.clear();
                rr.world.mouse_x = static_cast<int32_t>(to.x);
                rr.world.mouse_y = static_cast<int32_t>(to.y);
                rr.hover_component = -1;
                hud.update_input(rr, false, false, 4.1, out);
                const bool active = hud.drag().active;
                const bool ok = active && hud.drag().fleet == 960 && hud.drag().fleet_index == kMapFleetIndex && hud.drag().planet == 100 &&
                                hud.drag().valid;
                rr.owner = {};
                rr.hud = {{HudPointerEvent::Kind::Release, Button::Left, -1, rr.world.mouse_x, rr.world.mouse_y}};
                hud.update_input(rr, false, false, 4.2, out);
                hud.build(gf);
                return std::make_pair(active && ok, out);
            };
            float tx = 0, ty = 0;
            CHECK(project(gf.view, planets[1].position, tx, ty));
            auto [ok, out] = drag_stack(fleet_px[1], {tx, ty});
            CHECK(ok && out.size() == 1 && out[0] == commands::ev::move_object_to_object(960, 101));
            auto [ok2, out2] = drag_stack(fleet_px[0], {tx, ty});  // enemy fleet: no drag
            CHECK(!ok2 && out2.empty());
            // #455 (docs/re/GALACTIC_ABILITY_ROWS.md "The row", 0x3eb200; drop 0x2f6c30 -> 0x3d2900): while the fleet is
            // dragged, every planet the host labels shows one g_special_ability row (none without a drag). A drop on its
            // own planet's row asks the host to use the ability there; on another planet's row it is the planet drop's move.
            {
                const int32_t sc = ft.find("g_special_ability");
                std::vector<std::pair<uint32_t, uint32_t>> asked;
                gf.providers.drag_special_ability = [&asked](uint32_t fleet, uint32_t planet, GalacticSpecialAbility& r) {
                    asked.emplace_back(fleet, planet);
                    r.text_key = planet == 100 ? "TEXT_ABILITY_SLOT_SPY" : "TEXT_ABILITY_SLOT_RAID";
                    return true;
                };
                hud.build(gf);
                CHECK(hud.stats().ability_rows == 0 && asked.empty());
                auto row_px = [&](size_t pi) {
                    float sx = 0, sy = 0;
                    CHECK(project(gf.view, planets[pi].position, sx, sy));
                    const Vec2f at = hud.canvas().pixel_to_canvas(sx, sy);
                    return hud.canvas().canvas_to_pixel(at.x - 48.0f, at.y - 20.0f);
                };
                auto drop_on_row = [&](size_t pi, std::vector<HudAction>& acts) {
                    std::vector<commands::EventData> out;
                    RoutedInput rr;
                    rr.world.mouse_x = static_cast<int32_t>(fleet_px[1].x);
                    rr.world.mouse_y = static_cast<int32_t>(fleet_px[1].y);
                    rr.hover_component = gpf;
                    rr.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
                    rr.hud = {{HudPointerEvent::Kind::Press, Button::Left, gpf, rr.world.mouse_x, rr.world.mouse_y}};
                    hud.update_input(rr, false, false, 5.0, out, &acts);
                    rr.hud.clear();
                    rr.world.mouse_x = static_cast<int32_t>(tx);
                    rr.world.mouse_y = static_cast<int32_t>(ty);
                    rr.hover_component = -1;
                    hud.update_input(rr, false, false, 5.1, out, &acts);
                    asked.clear();
                    hud.build(gf);  // the rows of this drag
                    CHECK(hud.drag().active && hud.stats().ability_rows == 2 && asked.size() == 2 && asked[0].first == 960 &&
                          asked[0].second == 100 && asked[1].second == 101);
                    const Vec2f p = row_px(pi);
                    const auto x = static_cast<int32_t>(p.x), y = static_cast<int32_t>(p.y);
                    CHECK(hud.hit_test(x, y) == sc);
                    rr.world.mouse_x = x;
                    rr.world.mouse_y = y;
                    rr.hover_component = sc;
                    hud.update_input(rr, false, false, 5.2, out, &acts);
                    CHECK(hud.drag().valid);
                    rr.owner = {};
                    rr.hud = {{HudPointerEvent::Kind::Release, Button::Left, sc, x, y}, {HudPointerEvent::Kind::Click, Button::Left, sc, x, y}};
                    hud.update_input(rr, false, false, 5.3, out, &acts);
                    hud.build(gf);
                    CHECK(hud.stats().ability_rows == 0);  // the drag ended
                    return out;
                };
                std::vector<HudAction> row_acts;
                std::vector<commands::EventData> row_out = drop_on_row(0, row_acts);  // its own planet
                CHECK(row_out.empty() && row_acts.size() == 1 && row_acts[0].kind == HudAction::Kind::Button &&
                      row_acts[0].name == "g_special_ability" && row_acts[0].unit == 960 && row_acts[0].ids == std::vector<uint32_t>{100});
                row_acts.clear();
                row_out = drop_on_row(1, row_acts);  // another planet: the move
                CHECK(row_acts.empty() && row_out.size() == 1 && row_out[0] == commands::ev::move_object_to_object(960, 101));
                gf.providers.drag_special_ability = nullptr;
            }
        }
        planets[0].stacks.clear();
        planets[0].orbit_local = false;
        hud.build(gf);
    }

    // Playtest 3 fleet slots (base 40.jpg): the free slots of every planet show as fleet_empty ovals with a fleet selected
    // or while dragging; drops on a slot issue FleetManagement / MoveObjectToObject (galactic_hud.hpp "Fleet-slot drops").
    {
        constexpr uint32_t kNone = commands::kNoObject;
        const int32_t gpf = ft.find("g_planet_fleet");
        planets[0].orbit_local = planets[1].orbit_local = true;
        GalacticPlanetStack a;  // the host feeds the sim's slots (playtest 3 t07); a drop only issues commands
        a.units = 3;
        a.fleet = 960;
        a.slot = 0;
        GalacticPlanetStack b2 = a;
        b2.fleet = 961;
        b2.slot = 1;
        planets[0].stacks = {a, b2};
        planets[1].stacks.clear();
        cam.set_look_at({275.0f, 150.0f, 0.0f});  // both planets on screen
        gf.view = make_view(cam.params(4.0f / 3.0f), 1024, 768);
        gf.show_fleet_slots = false;
        hud.build(gf);
        CHECK(!hud.stats().fleet_slots_shown && hud.stats().empty_slots == 0);
        gf.show_fleet_slots = true;
        hud.build(gf);
        CHECK(hud.stats().fleet_slots_shown && hud.stats().empty_slots == 1 + 3);
        // R5 (base 40.jpg): the empty ovals take the frame's tint (host: the local player's faction colour) at full alpha.
        gf.empty_slot_color = Rgba8{54, 134, 242, 255};
        hud.build(gf);
        int tinted = 0;
        for (const HudQuad& q : hud.draw_list().items(0, HudList::Alpha))
            if (q.color.r == 54 && q.color.g == 134 && q.color.b == 242 && q.color.a == 255) ++tinted;
        CHECK(tinted == 1 + 3);
        gf.empty_slot_color = Rgba8{};
        hud.build(gf);
        auto slot_px = [&](size_t pi, int k) {
            float sx = 0, sy = 0;
            CHECK(project(gf.view, planets[pi].position, sx, sy));
            const Vec2f at = hud.canvas().pixel_to_canvas(sx, sy);
            const Vec2f off[3] = {{-36, 42}, {0, 47}, {36, 42}};
            return hud.canvas().canvas_to_pixel(at.x + off[k].x, at.y + off[k].y);
        };
        // The fleet a click at a slot selects (0 = none / empty oval).
        auto fleet_at = [&](size_t pi, int k) -> uint32_t {
            const Vec2f p = slot_px(pi, k);
            std::vector<HudAction> acts;
            std::vector<commands::EventData> cmds;
            RoutedInput ri;
            const auto x = static_cast<int32_t>(p.x), y = static_cast<int32_t>(p.y);
            ri.world.mouse_x = x;
            ri.world.mouse_y = y;
            ri.hover_component = hud.hit_test(x, y);
            ri.hud = {{HudPointerEvent::Kind::Press, Button::Left, ri.hover_component, x, y},
                      {HudPointerEvent::Kind::Release, Button::Left, ri.hover_component, x, y},
                      {HudPointerEvent::Kind::Click, Button::Left, ri.hover_component, x, y}};
            hud.update_input(ri, false, false, 5.0, cmds, &acts);
            CHECK(cmds.empty());
            return acts.size() == 1 && acts[0].ids.size() == 1 && acts[0].value == k ? acts[0].ids[0] : 0u;
        };
        CHECK(hud.hit_test(static_cast<int32_t>(slot_px(0, 2).x), static_cast<int32_t>(slot_px(0, 2).y)) == gpf);  // empty oval
        CHECK(fleet_at(0, 0) == 960 && fleet_at(0, 1) == 961 && fleet_at(0, 2) == 0);
        // Map drag: press on a slot's fleet, move to `to`, release there (hover from the HUD hit test, as the router does).
        auto drag_map = [&](Vec2f from, Vec2f to, bool* valid) {
            std::vector<commands::EventData> out;
            RoutedInput rr;
            rr.world.mouse_x = static_cast<int32_t>(from.x);
            rr.world.mouse_y = static_cast<int32_t>(from.y);
            rr.hover_component = gpf;
            rr.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
            rr.hud = {{HudPointerEvent::Kind::Press, Button::Left, gpf, rr.world.mouse_x, rr.world.mouse_y}};
            hud.update_input(rr, false, false, 6.0, out);
            rr.hud.clear();
            rr.world.mouse_x = static_cast<int32_t>(to.x);
            rr.world.mouse_y = static_cast<int32_t>(to.y);
            rr.hover_component = -1;
            hud.update_input(rr, false, false, 6.1, out);  // drag starts
            hud.build(gf);                                   // slots shown while dragging, the source slot blank
            rr.hover_component = hud.hit_test(rr.world.mouse_x, rr.world.mouse_y);
            hud.update_input(rr, false, false, 6.2, out);
            if (valid) *valid = hud.drag().active && hud.drag().valid;
            rr.owner = {};
            rr.hud = {{HudPointerEvent::Kind::Release, Button::Left, rr.hover_component, rr.world.mouse_x, rr.world.mouse_y}};
            hud.update_input(rr, false, false, 6.3, out);
            hud.build(gf);
            return out;
        };
        // 1. Whole fleet onto an empty slot of its planet -> op 8 {planet, fleet, slot}; it is drawn there, the other stays.
        bool valid = false;
        std::vector<commands::EventData> so = drag_map(slot_px(0, 0), slot_px(0, 2), &valid);
        CHECK(valid && so.size() == 1 && so[0] == fleet_management_event(commands::kFleetSlot, 100, 960, kNone, commands::kNoObject, 2));
        CHECK(fleet_at(0, 0) == 960 && fleet_at(0, 2) == 0);  // the HUD keeps no slot state: nothing moved yet
        a.slot = 2;                                            // the sim applied op 8; the host feeds the new slot
        planets[0].stacks = {a, b2};
        hud.build(gf);
        CHECK(fleet_at(0, 2) == 960 && fleet_at(0, 1) == 961 && fleet_at(0, 0) == 0);
        // 2. Onto another own fleet's slot at the same planet -> merge (op 3) {target, source, all}.
        so = drag_map(slot_px(0, 2), slot_px(0, 1), &valid);
        CHECK(valid && so.size() == 1 && so[0] == fleet_management_event(commands::kFleetMerge, 961, 960, kNone));
        // 3. Onto an empty slot of another planet -> op 8 {planet, fleet, slot, a = destination} (the slot it takes on
        //    arrival), then MoveObjectToObject {fleet, planet}.
        so = drag_map(slot_px(0, 2), slot_px(1, 1), &valid);
        CHECK(valid && so.size() == 2 && so[0] == fleet_management_event(commands::kFleetSlot, 100, 960, kNone, 101, 1) &&
              so[1] == commands::ev::move_object_to_object(960, 101));
        // 4. An organiser unit onto an empty slot of its planet -> detach (op 2); the new fleet takes that slot (not the
        //    first free one).
        planets[0].stacks = {b2};  // 961 in slot 1; 0 and 2 free
        hud.build(gf);
        so = drag("q1_0002", slot_px(0, 2), gpf);
        CHECK(so.size() == 1 && so[0] == fleet_management_event(commands::kFleetDetach, 100, 900, 902).set("b", 2u));  // the sim gives the slot
        GalacticPlanetStack c = a;
        c.fleet = 905;
        c.slot = 2;
        planets[0].stacks = {b2, c};
        hud.build(gf);
        CHECK(fleet_at(0, 2) == 905 && fleet_at(0, 1) == 961 && fleet_at(0, 0) == 0);
        // 5. A land unit onto the empty slot -> op 6 {planet, unit, a = planet} (launched as a new fleet).
        so = drag("land_fleet_00", slot_px(0, 0), gpf);
        CHECK(so.size() == 1 && so[0] == fleet_management_event(commands::kFleetMoveNew, 100, kNone, 701, 100).set("b", 0u));
        // 6. An enemy fleet's slot at the same planet: no drop.
        GalacticPlanetStack foe = a;
        foe.fleet = 990;
        foe.enemy = true;
        foe.slot = 0;
        planets[0].stacks = {b2, c, foe};
        hud.build(gf);
        CHECK(fleet_at(0, 0) == 990);
        so = drag_map(slot_px(0, 1), slot_px(0, 0), &valid);
        CHECK(!valid && so.empty());
        // 7. Hover highlight on the slot under the cursor while the slots are shown.
        gf.cursor_px = slot_px(1, 0);
        hud.build(gf);
        CHECK(hud.stats().slot_highlights == 1);
        gf.cursor_px = {};
        gf.show_fleet_slots = false;
        planets[0].stacks.clear();
        hud.build(gf);
        CHECK(!hud.stats().fleet_slots_shown && hud.stats().empty_slots == 0);
    }
    // ledger: verifies CommandBarComponent.Snap_Drag
    // ledger: verifies CommandBarComponent.Snap_Location
    {  // #722 (docs/re/COMMAND_BAR_DRAG.md "Snapping") Snap_Drag on the dragged pad + Snap_Location on the pad under the
       // cursor: the icon sits on that pad
        std::string xml = kFleetXml;
        const auto add_tag = [&xml](std::string_view name, std::string_view tag) {
            const std::string open = "<CommandBarComponent Name=\"" + std::string(name) + "\">";
            const size_t at = xml.find(open);
            CHECK(at != std::string::npos);
            xml.insert(at + open.size(), "<" + std::string(tag) + ">Yes</" + std::string(tag) + ">");
        };
        add_tag("q1_0002", "Snap_Drag");
        add_tag("q2_0001", "Snap_Location");
        add_tag("q1_0001", "Should_Ghost");  // #723
        add_tag("q1_0001", "Should_Render_At_Drag_Pos");
        add_tag("q2_0001", "Should_Ghost");
        add_tag("q2_0001", "Ghost_Base_Only");
        add_tag("q2_0001", "Should_Render_At_Drag_Pos");
        add_tag("q1_0002", "Should_Ghost");  // without Should_Render_At_Drag_Pos: never a ghost (base FoC's case)
        ComponentTable st;
        const data::XmlDocument sd = data::XmlDocument::parse(std::string_view(xml), "s.xml");
        st.parse(sd.root());
        GalacticHud sh;
        sh.init(st, fa, env.fonts);
        sh.resize(1024, 768);
        sh.build(gf);
        const auto centre = [&](std::string_view name) {
            for (const HudHitBox& h : sh.hit_boxes())
                if (h.component == st.find(name)) return Vec2f{0.5f * (h.min.x + h.max.x), 0.5f * (h.min.y + h.max.y)};
            return Vec2f{-1e9f, -1e9f};
        };
        std::u32string ghost_text;  // #723 the dragged pad's text / icon and the ghost count while it is dragged
        std::string ghost_icon;
        uint32_t ghosts = 0;
        const auto press_and_move = [&](std::string_view from, std::string_view over_name, Vec2f to_px) {
            const int32_t fc = st.find(from);
            const Vec2f p = sh.canvas().canvas_to_pixel(centre(from).x, centre(from).y);
            std::vector<commands::EventData> out;
            RoutedInput r;
            r.world.mouse_x = static_cast<int32_t>(p.x);
            r.world.mouse_y = static_cast<int32_t>(p.y);
            r.hover_component = fc;
            r.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
            r.hud = {{HudPointerEvent::Kind::Press, Button::Left, fc, r.world.mouse_x, r.world.mouse_y}};
            sh.update_input(r, false, false, 1.0, out);
            r.hud.clear();
            r.world.mouse_x = static_cast<int32_t>(to_px.x);
            r.world.mouse_y = static_cast<int32_t>(to_px.y);
            r.hover_component = over_name.empty() ? -1 : st.find(over_name);
            sh.update_input(r, false, false, 1.1, out);
            CHECK(sh.drag().active && sh.drag().component == fc);
            sh.build(gf);
            const Vec2f at = sh.drag_icon_position();
            const ButtonRuntime* br = sh.buttons().find_state(static_cast<uint32_t>(fc));
            ghost_text = br ? br->text : U"?";
            ghost_icon = br ? br->icon_override : "?";
            ghosts = sh.stats().ghosts;
            r.owner = {};
            r.hud = {{HudPointerEvent::Kind::Release, Button::Left, -1, 0, 0}};
            sh.update_input(r, false, false, 1.2, out);  // a release in empty space ends the drag
            sh.build(gf);
            return at;
        };
        const Vec2f q2 = centre("q2_0001");
        // Off the pad's centre, over it: the icon snaps to the pad.
        const Vec2f near_px = sh.canvas().canvas_to_pixel(q2.x + 3.0f, q2.y - 2.0f);
        Vec2f at = press_and_move("q1_0002", "q2_0001", near_px);
        CHECK(std::fabs(at.x - q2.x) < 0.01f && std::fabs(at.y - q2.y) < 0.01f);
        // Over no component: the cursor.
        at = press_and_move("q1_0002", "", {40.0f, 40.0f});
        const Vec2f cur = sh.canvas().pixel_to_canvas(40.0f, 40.0f);
        CHECK(std::fabs(at.x - cur.x) < 0.01f && std::fabs(at.y - cur.y) < 0.01f);
        // Snap_Drag over a pad without Snap_Location (q1_0001): no snap either.
        const Vec2f q1 = centre("q1_0001");
        const Vec2f near_q1 = sh.canvas().canvas_to_pixel(q1.x + 3.0f, q1.y - 2.0f);
        at = press_and_move("q1_0002", "q1_0001", near_q1);
        const Vec2f nq1 = sh.canvas().pixel_to_canvas(near_q1.x, near_q1.y);
        CHECK(std::fabs(at.x - nq1.x) < 0.01f && std::fabs(at.y - nq1.y) < 0.01f);
        // The dragged pad without Snap_Drag (q1_0001): no snap over q2_0001.
        at = press_and_move("q1_0001", "q2_0001", near_px);
        const Vec2f nc = sh.canvas().pixel_to_canvas(near_px.x, near_px.y);
        CHECK(std::fabs(at.x - nc.x) < 0.01f && std::fabs(at.y - nc.y) < 0.01f);
        // ledger: verifies CommandBarComponent.Should_Ghost
        // ledger: verifies CommandBarComponent.Ghost_Base_Only
        // ledger: verifies CommandBarComponent.Should_Render_At_Drag_Pos
        // #723 (docs/re/COMMAND_BAR_DRAG.md "Ghosting") Should_Ghost without Ghost_Base_Only: a pad of one unit has no
        // ghost.
        CHECK(ghosts == 0 && ghost_icon == "i_frame.tga");
        press_and_move("q1_0002", "", {40.0f, 40.0f});
        CHECK(ghosts == 0 && ghost_icon == "i_frame.tga");
        // A stack of three on q1_0001: the ghost is the stack less the dragged unit, "x2".
        GalacticHudFrame g3 = gf;
        g3.providers.fleets = [](uint32_t, std::vector<GalacticFleet>& out) {
            out.push_back({900, {{901, "i_frame.tga", 3, false}, {902, "i_frame.tga", 3, false}}});
            out.push_back({950, {{951, "i_big.tga", 2, false}}});
        };
        const GalacticHudFrame saved = gf;
        gf = g3;
        sh.build(gf);
        press_and_move("q1_0001", "", {40.0f, 40.0f});
        CHECK(ghosts == 1 && ghost_text == U"x2" && ghost_icon == "i_frame.tga");
        // A stack of three on q1_0002, Should_Ghost without Should_Render_At_Drag_Pos: no ghost, the pad keeps "3".
        press_and_move("q1_0002", "", {40.0f, 40.0f});
        CHECK(ghosts == 0 && ghost_text == U"3");
        // Ghost_Base_Only (q2_0001, a stack of two): the pad's base only, no unit icon and no count.
        press_and_move("q2_0001", "", {40.0f, 40.0f});
        CHECK(ghosts == 1 && ghost_text.empty() && ghost_icon.empty());
        // After the drop the pad shows its stack again.
        const ButtonRuntime* back = sh.buttons().find_state(static_cast<uint32_t>(st.find("q2_0001")));
        CHECK(back && back->text == U"2" && back->icon_override == "i_big.tga" && sh.stats().ghosts == 0);
        // A stack of two on q1_0001: the ghost holds one unit, so no count.
        gf.providers.fleets = [](uint32_t, std::vector<GalacticFleet>& out) {
            out.push_back({900, {{901, "i_frame.tga", 2, false}, {902, "i_frame.tga", 1, false}}});
        };
        sh.build(gf);
        press_and_move("q1_0001", "", {40.0f, 40.0f});
        CHECK(ghosts == 1 && ghost_text.empty() && ghost_icon == "i_frame.tga");
        gf = saved;
    }
    // ledger: verifies CommandBarComponent.Stackable
    // ledger: verifies CommandBarComponent.Can_Drag_Stack
    {  // #763 (docs/re/COMMAND_BAR_DRAG.md "Ctrl-drag of a stack"): with Ctrl held, a Stackable pad's drop moves its whole
       // stack; Can_Drag_Stack (+0x7dd) decides whether its text follows the drag and whether it ghosts.
        std::string xml = kFleetXml;
        const auto add_tag = [&xml](std::string_view name, std::string_view tag) {
            const std::string open = "<CommandBarComponent Name=\"" + std::string(name) + "\">";
            const size_t at = xml.find(open);
            CHECK(at != std::string::npos);
            xml.insert(at + open.size(), "<" + std::string(tag) + ">Yes</" + std::string(tag) + ">");
        };
        add_tag("q1_0001", "Stackable");  // q1_0002 is not Stackable
        add_tag("q1_0001", "Should_Render_At_Drag_Pos");
        add_tag("q1_0001", "Should_Ghost");
        add_tag("q1_0002", "Should_Render_At_Drag_Pos");
        ComponentTable st;
        const data::XmlDocument sd = data::XmlDocument::parse(std::string_view(xml), "s.xml");
        st.parse(sd.root());
        GalacticHudFrame g = gf;
        // Fleet 900: two units of type 5 (pad q1_0001, "2") and two of type 6 (q1_0002, "2"); fleet 950 in pane 2.
        g.providers.fleets = [](uint32_t, std::vector<GalacticFleet>& out) {
            GalacticFleetSlot a{901, "i_frame.tga", 1, false}, b{903, "i_frame.tga", 1, false};
            GalacticFleetSlot c{902, "i_big.tga", 1, false}, d{904, "i_big.tga", 1, false};
            a.type = b.type = 5;
            c.type = d.type = 6;
            out.push_back({900, {a, c, b, d}});
            out.push_back({950, {{951, "i_frame.tga", 1, false}}});
        };
        const auto run = [&](const ComponentTable& table, std::string_view from, std::string_view onto, bool ctrl,
                             uint32_t* texts = nullptr, uint32_t* ghosts = nullptr, bool* whole = nullptr,
                             std::u32string* pad_text = nullptr) {
            GalacticHud h;
            h.init(table, fa, env.fonts);
            h.resize(1024, 768);
            h.build(g);
            const auto px = [&](std::string_view name) {
                for (const HudHitBox& hb : h.hit_boxes())
                    if (hb.component == table.find(name))
                        return h.canvas().canvas_to_pixel(0.5f * (hb.min.x + hb.max.x), 0.5f * (hb.min.y + hb.max.y));
                return Vec2f{1000.0f, 10.0f};  // empty space
            };
            const int32_t fc = table.find(from);
            const int32_t oc = onto.empty() ? -1 : table.find(onto);
            const Vec2f p = px(from), q = onto.empty() ? Vec2f{1000.0f, 10.0f} : px(onto);
            std::vector<commands::EventData> out;
            RoutedInput r;
            r.world.mouse_x = static_cast<int32_t>(p.x);
            r.world.mouse_y = static_cast<int32_t>(p.y);
            r.hover_component = fc;
            r.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
            r.hud = {{HudPointerEvent::Kind::Press, Button::Left, fc, r.world.mouse_x, r.world.mouse_y}};
            h.update_input(r, false, ctrl, 1.0, out);
            r.hud.clear();
            r.world.mouse_x = static_cast<int32_t>(q.x);
            r.world.mouse_y = static_cast<int32_t>(q.y);
            r.hover_component = oc;
            h.update_input(r, false, ctrl, 1.1, out);
            CHECK(h.drag().active && h.drag().component == fc);
            const bool valid = h.drag().valid;
            h.build(g);
            if (texts) *texts = h.stats().drag_texts;
            if (ghosts) *ghosts = h.stats().ghosts;
            if (whole) *whole = h.drag().whole_stack;
            if (pad_text)
                if (const ButtonRuntime* br = h.buttons().find_state(static_cast<uint32_t>(fc))) *pad_text = br->text;
            r.owner = {};
            r.hud = {{HudPointerEvent::Kind::Release, Button::Left, oc, r.world.mouse_x, r.world.mouse_y}};
            h.update_input(r, false, ctrl, 1.2, out);
            CHECK(valid == !out.empty());
            return out;
        };
        using commands::kFleetMerge;
        // The Stackable pad onto fleet 950: with Ctrl both units merge, in stack order; without, the first only.
        o = run(st, "q1_0001", "q2_0001", true);
        CHECK(o.size() == 2 && o[0] == fleet_management_event(kFleetMerge, 950, 900, 901) &&
              o[1] == fleet_management_event(kFleetMerge, 950, 900, 903));
        o = run(st, "q1_0001", "q2_0001", false);
        CHECK(o.size() == 1 && o[0] == fleet_management_event(kFleetMerge, 950, 900, 901));
        // A pad that is not Stackable moves one unit, Ctrl or not.
        o = run(st, "q1_0002", "q2_0001", true);
        CHECK(o.size() == 1 && o[0] == fleet_management_event(kFleetMerge, 950, 900, 902));
        // Onto the land forces at the selected planet: a land command per unit.
        o = run(st, "q1_0001", "land_fleet_00", true);
        CHECK(o.size() == 2 && o[0] == fleet_management_event(commands::kFleetLand, 100, 900, 901) &&
              o[1] == fleet_management_event(commands::kFleetLand, 100, 900, 903));
        // [port] Into empty space (a detach) the one unit still goes.
        o = run(st, "q1_0001", "", true);
        CHECK(o.size() == 1 && o[0] == fleet_management_event(commands::kFleetDetach, 100, commands::kNoObject, 901));
        // The text at the drag position (Should_Render_At_Drag_Pos): a Stackable pad without Can_Drag_Stack never shows it
        // (one unit moves), Ctrl or not; a pad that is not Stackable shows its count; +0x7dd stays clear.
        uint32_t texts = 99, ghosts = 99;
        bool whole = true;
        (void)run(st, "q1_0001", "", true, &texts, &ghosts, &whole);
        CHECK(texts == 0 && ghosts == 1 && !whole);  // Should_Ghost + Should_Render_At_Drag_Pos, a stack of 2: the ghost
        std::u32string pad_text = U"?";
        (void)run(st, "q1_0002", "", false, &texts, &ghosts, &whole, &pad_text);
        CHECK(texts == 1 && ghosts == 0 && !whole && pad_text.empty());  // the count left the pad for the drag position
        // Can_Drag_Stack on the Stackable pad: with Ctrl, +0x7dd is set, the count follows the drag and there is no ghost;
        // without Ctrl, as before.
        add_tag("q1_0001", "Can_Drag_Stack");
        ComponentTable ct;
        const data::XmlDocument cd = data::XmlDocument::parse(std::string_view(xml), "c.xml");
        ct.parse(cd.root());
        (void)run(ct, "q1_0001", "", true, &texts, &ghosts, &whole);
        CHECK(texts == 1 && ghosts == 0 && whole);
        (void)run(ct, "q1_0001", "", false, &texts, &ghosts, &whole);
        CHECK(texts == 0 && ghosts == 1 && !whole);
        // A fleet slot holding an own fleet at the same planet: the stack merges unit by unit; an empty slot (a new
        // fleet) keeps the one unit [port].
        GalacticOrganiserDrag sdrag{true, 900, 901, 0, {901, 903}};
        GalacticDropTarget slot{GalacticDropTarget::Kind::Slot, 950, false, false, 100, 1};
        std::vector<commands::EventData> so;
        CHECK(galactic_slot_drop(sdrag, slot, 100, &so) && so.size() == 2 && so[0] == fleet_management_event(kFleetMerge, 950, 900, 901) &&
              so[1] == fleet_management_event(kFleetMerge, 950, 900, 903));
        so.clear();
        slot.fleet = 0;
        CHECK(galactic_slot_drop(sdrag, slot, 100, &so) && so.size() == 1);
    }
    // ledger: verifies CommandBarComponent.Scale_Duration
    // ledger: verifies CommandBarComponent.Cross_Fade
    {  // #767 (the lane-c RE on #767): Scale_Duration's scale-up (0x350df0 / 0x34ff70) and Cross_Fade (0x352d80 / 0x350360)
        const char* kXml = R"(<CommandBarComponents>
  <CommandBarComponent Name="pad"><Type>TextButton</Type><Blank_Texture_Name>i_frame.tga</Blank_Texture_Name><Scale>1.0</Scale><Scale_Duration>0.125</Scale_Duration></CommandBarComponent>
  <CommandBarComponent Name="emblem"><Type>Icon</Type><Cross_Fade>Yes</Cross_Fade></CommandBarComponent>
  <CommandBarComponent Name="plain"><Type>Icon</Type></CommandBarComponent>
</CommandBarComponents>)";
        ComponentTable t7;
        const data::XmlDocument d7 = data::XmlDocument::parse(std::string_view(kXml), "t7.xml");
        t7.parse(d7.root());
        HudButtons b7;
        b7.bind(t7);
        const HudCanvas cv7 = HudCanvas::make(1024, 768);
        const auto pad = static_cast<uint32_t>(t7.find("pad"));
        const auto width_at = [&](double now) {
            HudDrawList dl;
            b7.draw(dl, fa, nullptr, cv7, pad, {0, 0}, {}, 0, now);
            const auto q = dl.items(0, HudList::Alpha);
            return q.empty() ? -1.0f : q.front().max.x - q.front().min.x;
        };
        const float full = width_at(1.0);
        CHECK(full > 0.0f);
        b7.state(pad).scale_up_start = 1.0;
        CHECK(std::fabs(width_at(1.0)) < 1e-3f);                       // starts from nothing
        CHECK(std::fabs(width_at(1.0625) - 0.5f * full) < 1e-3f);      // half way through 0.125 s
        CHECK(std::fabs(width_at(1.5) - full) < 1e-3f);                // then its Scale
        // Cross_Fade: a new icon fades in over 0.1 s while the old one fades out; a plain icon swaps at once.
        const auto icons_at = [&](std::string_view name, const char* icon, double now) {
            const auto ci = static_cast<uint32_t>(t7.find(name));
            b7.state(ci).icon_override = icon;
            HudDrawList dl;
            b7.draw(dl, fa, nullptr, cv7, ci, {0, 0}, {}, 0, now);
            return std::vector<HudQuad>(dl.items(0, HudList::Alpha).begin(), dl.items(0, HudList::Alpha).end());
        };
        CHECK(icons_at("emblem", "i_frame.tga", 2.0).size() == 1);  // the first icon: no fade
        std::vector<HudQuad> mid = icons_at("emblem", "i_big.tga", 3.0);
        CHECK(mid.size() == 2 && mid[0].color.a == 255 && mid[1].color.a == 0);  // the change: old opaque, new clear
        mid = icons_at("emblem", "i_big.tga", 3.05);
        CHECK(mid.size() == 2 && std::abs(static_cast<int>(mid[0].color.a) - 128) <= 2 && std::abs(static_cast<int>(mid[1].color.a) - 128) <= 2);
        mid = icons_at("emblem", "i_big.tga", 3.2);
        CHECK(mid.size() == 1 && mid[0].color.a == 255);
        CHECK(icons_at("plain", "i_frame.tga", 2.0).size() == 1 && icons_at("plain", "i_big.tga", 3.0).size() == 1);
        // The trigger: a unit dragged onto another fleet pops in at its new pad once the fleets show the move.
        std::string xml = kFleetXml;
        const std::string open = "<CommandBarComponent Name=\"q2_0001\">";
        if (xml.find(open) != std::string::npos)
            xml.insert(xml.find(open) + open.size(), "<Scale_Duration>0.125</Scale_Duration>");
        ComponentTable pt;
        const data::XmlDocument pd = data::XmlDocument::parse(std::string_view(xml), "p.xml");
        pt.parse(pd.root());
        GalacticHud h;
        h.init(pt, fa, env.fonts);
        h.resize(1024, 768);
        GalacticHudFrame g = gf;
        bool moved = false;
        g.providers.fleets = [&moved](uint32_t, std::vector<GalacticFleet>& out) {
            if (moved) {
                out.push_back({900, {{901, "i_frame.tga", 1, false}}});
                out.push_back({950, {{902, "i_big.tga", 1, false}}});
            } else {
                out.push_back({900, {{901, "i_frame.tga", 1, false}, {902, "i_big.tga", 1, false}}});
                out.push_back({950, {{951, "i_frame.tga", 1, false}}});
            }
        };
        g.time = 10.0;
        h.build(g);
        const auto px = [&](std::string_view name) {
            for (const HudHitBox& hb : h.hit_boxes())
                if (hb.component == pt.find(name))
                    return h.canvas().canvas_to_pixel(0.5f * (hb.min.x + hb.max.x), 0.5f * (hb.min.y + hb.max.y));
            return Vec2f{-1.0f, -1.0f};
        };
        const int32_t fc7 = pt.find("q1_0002"), oc = pt.find("q2_0001");
        const Vec2f p = px("q1_0002"), q = px("q2_0001");
        std::vector<commands::EventData> out;
        RoutedInput r7;
        r7.world.mouse_x = static_cast<int32_t>(p.x);
        r7.world.mouse_y = static_cast<int32_t>(p.y);
        r7.hover_component = fc7;
        r7.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
        r7.hud = {{HudPointerEvent::Kind::Press, Button::Left, fc7, r7.world.mouse_x, r7.world.mouse_y}};
        h.update_input(r7, false, false, 10.0, out);
        r7.hud.clear();
        r7.world.mouse_x = static_cast<int32_t>(q.x);
        r7.world.mouse_y = static_cast<int32_t>(q.y);
        r7.hover_component = oc;
        h.update_input(r7, false, false, 10.1, out);
        r7.owner = {};
        r7.hud = {{HudPointerEvent::Kind::Release, Button::Left, oc, r7.world.mouse_x, r7.world.mouse_y}};
        h.update_input(r7, false, false, 10.2, out);
        CHECK(out.size() == 1 && out[0] == fleet_management_event(commands::kFleetMerge, 950, 900, 902));
        g.time = 10.2;
        h.build(g);  // the sim has not moved it yet: no pad pops
        const auto pop_of = [&](std::string_view name) {
            const ButtonRuntime* br = h.buttons().find_state(static_cast<uint32_t>(pt.find(name)));
            return br ? br->scale_up_start : -2.0;
        };
        CHECK(pop_of("q1_0002") < 0.0 && pop_of("q2_0001") < 0.0);
        moved = true;
        g.time = 10.3;
        h.build(g);  // 902 now shows on q2_0001 (fleet 950's pad): it pops, once
        CHECK(pop_of("q2_0001") == 10.3);
        g.time = 10.4;
        h.build(g);
        CHECK(pop_of("q2_0001") == 10.3);
    }
}

// #623 GalacticDrag::over_planet is an index into the frame's planets (the host looks the planet up in its own list of the
// frame's planets), not into the planets on screen: a hidden planet and one behind the camera before the hovered one do not
// shift it, over the world and over a fleet slot alike. The drops send what they did.
void test_drag_over_planet_index() {
    PanelEnv env;
    ComponentTable ft;
    const data::XmlDocument fd = data::XmlDocument::parse(std::string_view(kFleetXml), "f.xml");
    ft.parse(fd.root());
    TextureAtlas fa;
    fa.texture_width = fa.texture_height = 64;
    fa.add({"i_frame.tga", 0, 0, 40, 40, true});
    fa.add({"fleet_stacks_01.tga", 0, 0, 36, 45, true});
    fa.add({"fleet_empty.tga", 0, 0, 36, 45, true});
    GalacticHud hud;
    hud.init(ft, fa, env.fonts);
    hud.resize(1024, 768);
    GalacticCameraController cam;
    cam.set_look_at({275.0f, 150.0f, 0.0f});
    GalacticHudFrame gf;
    gf.view = make_view(cam.params(4.0f / 3.0f), 1024, 768);
    std::vector<GalacticPlanetInfo> planets(4);
    planets[0].planet = 98;  // hidden
    planets[0].position = {100, 300, 0};
    planets[0].visible = false;
    planets[1].planet = 99;  // at the eye: it does not project
    planets[1].position = gf.view.eye;
    planets[2].planet = 100;
    planets[2].position = {200, 100, 0};
    planets[2].selected = true;
    planets[3].planet = 101;
    planets[3].position = {350, 200, 0};
    float sx = 0, sy = 0;
    CHECK(!project(gf.view, planets[1].position, sx, sy));
    gf.planets = planets;
    gf.providers.fleets = [](uint32_t, std::vector<GalacticFleet>& out) {
        out.push_back({900, {{901, "i_frame.tga", 1, false}, {902, "i_frame.tga", 1, false}}});
    };
    hud.build(gf);
    auto center_px = [&](std::string_view name) {
        const int32_t c = ft.find(name);
        for (const HudHitBox& h : hud.hit_boxes())
            if (h.component == c) return hud.canvas().canvas_to_pixel(0.5f * (h.min.x + h.max.x), 0.5f * (h.min.y + h.max.y));
        return Vec2f{-1, -1};
    };
    // Press `from_c` at `from`, move to `to` (the drag starts), rebuild, hover there (the HUD hit test when `hover_hit`, else
    // the world), release: the drag state over `to` and the drop's commands.
    auto drag_to = [&](int32_t from_c, Vec2f from, Vec2f to, bool hover_hit, GalacticDrag& seen) {
        std::vector<commands::EventData> out;
        RoutedInput r;
        r.world.mouse_x = static_cast<int32_t>(from.x);
        r.world.mouse_y = static_cast<int32_t>(from.y);
        r.hover_component = from_c;
        r.owner[static_cast<size_t>(Button::Left)] = PointerTarget::Hud;
        r.hud = {{HudPointerEvent::Kind::Press, Button::Left, from_c, r.world.mouse_x, r.world.mouse_y}};
        hud.update_input(r, false, false, 1.0, out);
        r.hud.clear();
        r.world.mouse_x = static_cast<int32_t>(to.x);
        r.world.mouse_y = static_cast<int32_t>(to.y);
        r.hover_component = -1;
        hud.update_input(r, false, false, 1.1, out);
        hud.build(gf);
        r.hover_component = hover_hit ? hud.hit_test(r.world.mouse_x, r.world.mouse_y) : -1;
        hud.update_input(r, false, false, 1.2, out);
        seen = hud.drag();
        r.owner = {};
        r.hud = {{HudPointerEvent::Kind::Release, Button::Left, r.hover_component, r.world.mouse_x, r.world.mouse_y}};
        hud.update_input(r, false, false, 1.3, out);
        hud.build(gf);
        return out;
    };
    // The organiser's fleet header over a planet in the world.
    GalacticDrag d;
    CHECK(project(gf.view, planets[3].position, sx, sy));
    std::vector<commands::EventData> o = drag_to(ft.find("q1_fleet"), center_px("q1_fleet"), {sx, sy}, false, d);
    CHECK(d.active && d.over_component == -1 && d.over_planet == 3 && d.valid);
    CHECK(o.size() == 1 && o[0] == commands::ev::move_object_to_object(900, 101));
    CHECK(project(gf.view, planets[2].position, sx, sy));
    o = drag_to(ft.find("q1_fleet"), center_px("q1_fleet"), {sx, sy}, false, d);
    CHECK(d.active && d.over_planet == 2 && !d.valid && o.empty());  // its own planet
    // A map fleet over another planet's empty slot oval: the slot's planet is the planet under the cursor.
    {
        constexpr uint32_t kNone = commands::kNoObject;
        const int32_t gpf = ft.find("g_planet_fleet");
        GalacticPlanetStack s;
        s.units = 3;
        s.fleet = 960;
        s.slot = 0;
        planets[2].stacks = {s};
        gf.show_fleet_slots = true;
        hud.build(gf);
        auto slot_px = [&](size_t pi, int k) {
            float px = 0, py = 0;
            CHECK(project(gf.view, planets[pi].position, px, py));
            const Vec2f at = hud.canvas().pixel_to_canvas(px, py);
            const Vec2f off[3] = {{-36, 42}, {0, 47}, {36, 42}};
            return hud.canvas().canvas_to_pixel(at.x + off[k].x, at.y + off[k].y);
        };
        o = drag_to(gpf, slot_px(2, 0), slot_px(3, 1), true, d);
        CHECK(d.active && d.over_component == gpf && d.over_slot == 1 && d.over_slot_planet == 101 && d.over_planet == 3 && d.valid);
        CHECK(o.size() == 2 && o[0] == fleet_management_event(commands::kFleetSlot, 100, 960, kNone, 101, 1) &&
              o[1] == commands::ev::move_object_to_object(960, 101));
    }
}

void test_controls() {
    // 1. Pause: the port extension event round trip, the hotkey (Pause key) toggling, and both HUD buttons.
    bool pz = false;
    CHECK(read_pause_event(pause_event(true), pz) && pz && read_pause_event(pause_event(false), pz) && !pz);
    CHECK(!read_pause_event(commands::ev::stop(), pz));
    CHECK(pause_event(true).type() == commands::EventType::PortExtension && pause_event(true).u("subtype") == kPortPauseSubtype);
    {
        HotkeyTable table;
        CHECK(table.binding(hk::kPause).key == vk::kPause && kHotkeySlots == 168 && hotkey_slot("Pause") == hk::kPause);
        HotkeyDispatcher disp;
        InputState in;
        std::vector<commands::EventData> cmds;
        std::vector<HotkeyEvent> evs;
        in.update(key_frame({}, {vk::kPause}, 1.0));
        disp.update(in, table, {}, 1.0, cmds, evs);
        in.update(key_frame({}, {}, 1.1));
        in.update(key_frame({}, {vk::kPause}, 1.2));
        disp.update(in, table, {}, 1.2, cmds, evs);
        CHECK(cmds.size() == 2 && cmds[0] == pause_event(true) && cmds[1] == pause_event(false) && !disp.paused());
        Bindings b;
        table.apply_to_bindings(b);
        CHECK(b.binding(Action::Pause, 0).key == vk::kPause);
        Bindings d;  // P1 defaults have the action too
        CHECK(d.binding(Action::Pause, 0).key == vk::kPause && action_name(Action::Pause) == "Pause");
    }
    {
        ComponentTable ft;
        const data::XmlDocument fd = data::XmlDocument::parse(std::string_view(kFleetXml), "f.xml");
        ft.parse(fd.root());
        TextureAtlas fa;
        fa.texture_width = fa.texture_height = 64;
        fa.add({"i_big.tga", 0, 0, 30, 30, true});
        fa.add({"i_frame.tga", 0, 0, 40, 40, true});
        auto raster = make_box_rasterizer();
        FontCache fonts(*raster);
        auto click = [](auto& hud, int32_t ci, std::vector<commands::EventData>& out) {
            RoutedInput r;
            r.hover_component = ci;
            r.hud = {{HudPointerEvent::Kind::Press, Button::Left, ci, 0, 0}, {HudPointerEvent::Kind::Release, Button::Left, ci, 0, 0},
                     {HudPointerEvent::Kind::Click, Button::Left, ci, 0, 0}};
            hud.update_input(r, false, false, 1.0, out);
        };
        GalacticHud g;
        g.init(ft, fa, fonts);
        g.resize(1024, 768);
        GalacticHudFrame gf;
        gf.paused = true;
        g.build(gf);
        std::vector<commands::EventData> out;
        click(g, ft.find("b_play_pause"), out);
        CHECK(out.size() == 1 && out[0] == pause_event(false));  // paused -> resume
        TacticalHud t;
        t.init(ft, fa, fonts);
        t.resize(1024, 768);
        TacticalHudFrame tf;
        t.build(tf, {}, {});
        bool on_hud = false;
        for (const HudHitBox& h : t.hit_boxes()) on_hud = on_hud || h.component == ft.find("b_play_pause_t");
        CHECK(on_hud);
        out.clear();
        click(t, ft.find("b_play_pause_t"), out);
        CHECK(out.size() == 1 && out[0] == pause_event(true));
        // Queue markers through the tactical HUD frame.
        TacticalCameraController cam;
        tf.world.view = make_view(cam.params(4.0f / 3.0f), 1024, 768);
        HudOrderQueue q;
        q.unit = 7;
        q.position = {0, 0, 0};
        q.steps = {{HudQueuedStep::Kind::Attack, {200, 100, 0}, true},
                   {HudQueuedStep::Kind::Ability, {}, false},
                   {HudQueuedStep::Kind::Move, {-150, 250, 0}, true}};
        tf.order_queues = std::span<const HudOrderQueue>(&q, 1);
        t.build(tf, {}, {});
        CHECK(t.stats().queue_markers == 2 && t.stats().queue_dots > 10);
        // the markers are numbered 1 and 3 (the pointless ability step keeps its place in the count)
        HudDrawList dl;
        const QueueMarkerStats qs = add_queue_markers(dl, &fonts, t.canvas(), tf.world.view, tf.order_queues, 1);
        CHECK(qs.markers == 2 && dl.items(1, HudList::Text).size() >= 2);
        bool red = false;
        for (const HudQuad& qd : dl.items(1, HudList::Alpha)) red = red || qd.color == queue_step_color(HudQueuedStep::Kind::Attack);
        CHECK(red);
    }

    // 2. Field of view: tactical override, galactic setting, the options dialog.
    {
        TacticalCameraController tc;
        tc.set_fov_override(80.0f);
        CHECK(std::fabs(tc.params(4.0f / 3.0f).fov_x - 80.0f * kDegToRad) < 1e-5f && tc.fov_deg() == 80.0f);
        tc.set_fov_override(0.0f);
        CHECK(tc.fov_deg() == 55.0f);
        GalacticCameraController gc;
        CHECK(std::fabs(gc.fov_deg() - 26.565f) < 1e-2f);
        gc.set_fov_deg(40.0f);
        CHECK(std::fabs(gc.params(4.0f / 3.0f).fov_x - 40.0f * kDegToRad) < 1e-5f);  // the setting is the 4:3 FOV (Hor+ above it)
        CHECK(std::fabs(gc.params(1.5f).fov_x - galactic_fov_x(40.0f * kDegToRad, 1.5f)) < 1e-6f);
    }
    {
        Env env;
        CHECK(env.script.merge(kPortDialogsRc) && env.gui.has_dialog(kPortControlsDialog));
        FrontEnd fe;
        HotkeyTable keys;
        Bindings binds;
        MenuProviders p;
        p.hotkeys = &keys;
        p.bindings = &binds;
        CameraSettings cs;
        cs.tactical_fov_deg = 70.0f;
        cs.fleet_groups = true;  // #175
        p.camera_settings = [&] { return cs; };
        env.text.add("TEXT_KEY_SHIFT", u"Umschalt");  // #45 t08: retail's key name, in a German table
        fe.init(env.gui, {}, p);
        std::vector<MenuAction> acts;
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, "IDD_MAIN_OPTIONS_DIALOG", "IDC_MAIN_OPTIONS_MODS")}, InputFrame{}, acts);
        CHECK(fe.top() == kPortControlsDialog);
        GuiDialog* d = env.gui.top();
        CHECK(d && d->get("IDC_PORT_TACTICAL_FOV_SLIDER")->value == 70 && d->get("IDC_PORT_GALACTIC_FOV_SLIDER")->value == 27 &&
              d->get("IDC_PORT_TACTICAL_FOV_SLIDER")->range_max == 110 && d->get("IDC_EDIT_PORT_PAUSE")->text == U"Pause" &&
              d->get("IDC_EDIT_PORT_SCROLL_UP")->text == U"W" && d->get("IDC_PORT_QUEUE_MODIFIER")->selected == 0);
        CHECK((d && d->get("IDC_PORT_QUEUE_MODIFIER")->items ==
                        std::vector<std::u32string>{U"Umschalt", U"Ctrl", U"Alt", U"None"}));  // the table lacks the others
        // #175: the fleet groups checkbox shows the setting; unticked here, so Accept reports it off.
        CHECK(d && d->get("IDC_PORT_FLEET_GROUPS") && d->get("IDC_PORT_FLEET_GROUPS")->checked);
        if (d && d->get("IDC_PORT_FLEET_GROUPS")) d->set_checked("IDC_PORT_FLEET_GROUPS", false);
        // Slider moves update the value label; key capture rebinds Pause to F8 (F9 is the HUD style key, #584); the queue modifier becomes Ctrl.
        d->set_value("IDC_PORT_GALACTIC_FOV_SLIDER", 35);
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::ValueChanged, kPortControlsDialog, "IDC_PORT_GALACTIC_FOV_SLIDER", 35)},
                  InputFrame{}, acts);
        CHECK(d->get("IDC_PORT_GALACTIC_FOV_VALUE")->text == U"35");
        d->focus("IDC_EDIT_PORT_PAUSE");
        InputFrame kf;
        kf.keys_pressed.set(0x77);  // F8
        kf.keys_down.set(0x77);
        fe.update({}, kf, acts);
        CHECK(d->get("IDC_EDIT_PORT_PAUSE")->text == U"F8");
        d->set_selected("IDC_PORT_QUEUE_MODIFIER", 1);
        acts.clear();
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, kPortControlsDialog, "IDC_BUTTON_ACCEPT")}, InputFrame{}, acts);
        CHECK(acts.size() == 2 && acts[0].kind == MenuAction::Kind::ApplyCamera && acts[0].camera.galactic_fov_deg == 35.0f &&
              acts[0].camera.tactical_fov_deg == 70.0f && acts[0].camera.queue_mods == kModCtrl && !acts[0].camera.fleet_groups &&
              acts[1].kind == MenuAction::Kind::KeybindingsChanged);
        CHECK(keys.binding(hk::kPause).key == 0x77 && binds.binding(Action::Pause, 0).key == 0x77 && binds.queue_mods() == kModCtrl);
        CHECK(env.gui.find(kPortControlsDialog) == nullptr);
    }
    // #590 the Interface page: opened from the camera and controls page; it shows the host's settings, greys the Modern
    // rows while Classic is chosen, and Accept reports the page (ApplyHud) and commits its two keys only.
    {
        Env env;
        CHECK(env.script.merge(kPortDialogsRc) && env.gui.has_dialog(kPortHudDialog));
        FrontEnd fe;
        HotkeyTable keys;
        Bindings binds;
        MenuProviders p;
        p.hotkeys = &keys;
        p.bindings = &binds;
        HudPageSettings hs;
        hs.ui_scale = 125;
        hs.modern.text_scale = 150;
        hs.modern.colours = HudColours::ColourBlind;
        hs.modern.panel_opacity = 60;
        p.hud_settings = [&] { return hs; };
        fe.init(env.gui, {}, p);
        std::vector<MenuAction> acts;
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, "IDD_MAIN_OPTIONS_DIALOG", "IDC_MAIN_OPTIONS_MODS")}, InputFrame{}, acts);
        CHECK(fe.top() == kPortControlsDialog);
        const GuiDialog* c = env.gui.top();
        CHECK(c && c->get("IDC_PORT_OPEN_HUD") && c->get("IDC_PORT_OPEN_HUD")->visible);
        // An edit on the camera and controls page (Pause to F8) is not committed by the Interface page's Accept.
        GuiDialog* cd = env.gui.top();
        cd->focus("IDC_EDIT_PORT_PAUSE");
        InputFrame f8;
        f8.keys_pressed.set(0x77);
        f8.keys_down.set(0x77);
        fe.update({}, f8, acts);
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, kPortControlsDialog, "IDC_PORT_OPEN_HUD")}, InputFrame{}, acts);
        CHECK(fe.top() == kPortHudDialog);
        GuiDialog* d = env.gui.top();
        CHECK(d && d->get("IDC_PORT_HUD_STYLE")->selected == 0 && d->get("IDC_PORT_HUD_TEXT_SIZE")->selected == 3 &&
              d->get("IDC_PORT_HUD_COLOURS")->selected == 1 && d->get("IDC_PORT_HUD_OPACITY_SLIDER")->value == 60 &&
              d->get("IDC_PORT_HUD_OPACITY_VALUE")->text == U"60 %" && d->get("IDC_PORT_HUD_KEY_HINTS")->checked &&
              d->get("IDC_EDIT_PORT_HUD_STYLE")->text == U"F9" && d->get("IDC_EDIT_PORT_ALERT_JUMP")->text == U"F4");
        CHECK(d && d->get("IDC_PORT_HUD_UI_SCALE")->items.size() == std::size(kUiScaleChoices));
        CHECK(d && !d->get("IDC_PORT_HUD_TEXT_SIZE")->enabled && !d->get("IDC_PORT_HUD_CONTEXTUAL")->enabled);  // Classic: greyed
        // The map side (#591) and the contextual deck (#587) are shown.
        CHECK(d && d->get("IDC_PORT_HUD_MAP_SIDE")->visible && d->get("IDC_PORT_HUD_CONTEXTUAL")->visible &&
              d->get("IDC_PORT_HUD_KEY_HINTS")->visible);
        // Picking Modern enables the rows; the slider's label follows it.
        d->set_selected("IDC_PORT_HUD_STYLE", 1);
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::SelectionChanged, kPortHudDialog, "IDC_PORT_HUD_STYLE", 1)}, InputFrame{}, acts);
        CHECK(d->get("IDC_PORT_HUD_TEXT_SIZE")->enabled && d->get("IDC_PORT_HUD_CONTEXTUAL")->enabled &&
              d->get("IDC_PORT_HUD_OPACITY_SLIDER")->enabled);
        // High contrast has near-opaque panels: the opacity slider is greyed while it is chosen.
        d->set_selected("IDC_PORT_HUD_COLOURS", 2);
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::SelectionChanged, kPortHudDialog, "IDC_PORT_HUD_COLOURS", 2)}, InputFrame{}, acts);
        CHECK(!d->get("IDC_PORT_HUD_OPACITY_SLIDER")->enabled && d->get("IDC_PORT_HUD_TEXT_SIZE")->enabled);
        d->set_selected("IDC_PORT_HUD_COLOURS", 1);
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::SelectionChanged, kPortHudDialog, "IDC_PORT_HUD_COLOURS", 1)}, InputFrame{}, acts);
        CHECK(d->get("IDC_PORT_HUD_OPACITY_SLIDER")->enabled);
        d->set_value("IDC_PORT_HUD_OPACITY_SLIDER", 90);
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::ValueChanged, kPortHudDialog, "IDC_PORT_HUD_OPACITY_SLIDER", 90)}, InputFrame{}, acts);
        CHECK(d->get("IDC_PORT_HUD_OPACITY_VALUE")->text == U"90 %");
        d->set_selected("IDC_PORT_HUD_TEXT_SIZE", 5);
        d->set_selected("IDC_PORT_HUD_MAP_SIDE", 1);
        d->set_checked("IDC_PORT_HUD_ORDER_LABELS", false);
        d->focus("IDC_EDIT_PORT_ALERT_JUMP");  // Jump to Alert -> F7 (free by default)
        InputFrame f7;
        f7.keys_pressed.set(0x76);
        f7.keys_down.set(0x76);
        fe.update({}, f7, acts);
        CHECK(d->get("IDC_EDIT_PORT_ALERT_JUMP")->text == U"F7");
        acts.clear();
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, kPortHudDialog, "IDC_BUTTON_ACCEPT")}, InputFrame{}, acts);
        CHECK(acts.size() == 2 && acts[0].kind == MenuAction::Kind::ApplyHud && acts[1].kind == MenuAction::Kind::KeybindingsChanged);
        if (acts.size() == 2) {
            const HudPageSettings& got = acts[0].hud;
            CHECK(got.style == HudStyle::Modern && got.ui_scale == 125 && got.modern.text_scale == 200 && got.modern.map_right &&
                  !got.modern.order_labels && got.modern.panel_opacity == 90 && got.modern.colours == HudColours::ColourBlind);
        }
        CHECK(keys.binding(hk::kAlertJump).key == 0x76 && keys.binding(hk::kPause).key != 0x77);
        CHECK(fe.top() == kPortControlsDialog);  // back to the page it was opened from
        // A stored text size between rows (118 %) is kept while its row is untouched; Defaults then Accept gives 115 %.
        hs.modern.text_scale = 118;
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, kPortControlsDialog, "IDC_PORT_OPEN_HUD")}, InputFrame{}, acts);
        acts.clear();
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, kPortHudDialog, "IDC_BUTTON_ACCEPT")}, InputFrame{}, acts);
        CHECK(!acts.empty() && acts[0].kind == MenuAction::Kind::ApplyHud && acts[0].hud.modern.text_scale == 118);
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, kPortControlsDialog, "IDC_PORT_OPEN_HUD")}, InputFrame{}, acts);
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, kPortHudDialog, "IDC_DEFAULTS_BUTTON")}, InputFrame{}, acts);
        acts.clear();
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, kPortHudDialog, "IDC_BUTTON_ACCEPT")}, InputFrame{}, acts);
        CHECK(!acts.empty() && acts[0].kind == MenuAction::Kind::ApplyHud && acts[0].hud.modern.text_scale == kModernTextScaleDefaultPct &&
              acts[0].hud.style == HudStyle::Classic);
    }
    // the mod picker. The port's main-menu Mods button (shown only with a mods provider), the rows (the active one
    // marked and selected first), a selection asks the host for the row's report (CheckMod), the report arriving enables
    // Launch (not for the active row), Launch reports the row, Back / Esc return to the main menu.
    {
        Env env;
        CHECK(env.script.merge(kPortDialogsRc) && env.script.extend(kPortDialogExtensionsRc) && env.gui.has_dialog(kPortModsDialog));
        {  // no provider: no button
            FrontEnd fe0;
            fe0.init(env.gui, {}, MenuProviders{});
            fe0.show_main_menu();
            const GuiDialog* m = env.gui.find("IDD_MAIN_MENU_DIALOG");
            CHECK(m && m->get("IDC_PORT_MAIN_MENU_MODS") && !m->get("IDC_PORT_MAIN_MENU_MODS")->visible);
        }
        FrontEnd fe;
        MenuProviders p;
        p.mods = [] {
            return std::vector<ModPickerEntry>{{"", U"Base game (no mod)", false}, {"STEAMMOD=900000001", U"Workshop mod", true},
                                               {"MODPATH=Mods\\Local", U"Local mod", false}};
        };
        fe.init(env.gui, {}, p);
        fe.show_main_menu();
        const GuiDialog* m = env.gui.find("IDD_MAIN_MENU_DIALOG");
        CHECK(m && m->get("IDC_PORT_MAIN_MENU_MODS") && m->get("IDC_PORT_MAIN_MENU_MODS")->visible);
        std::vector<MenuAction> acts;
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, "IDD_MAIN_MENU_DIALOG", "IDC_PORT_MAIN_MENU_MODS")}, InputFrame{}, acts);
        CHECK(fe.top() == kPortModsDialog);
        CHECK(acts.size() == 1 && acts[0].kind == MenuAction::Kind::CheckMod && acts[0].mod == "STEAMMOD=900000001");
        GuiDialog* d = env.gui.find(kPortModsDialog);
        CHECK(d && d->get("IDC_PORT_MODS_LIST")->items.size() == 3 && d->get("IDC_PORT_MODS_LIST")->selected == 1 &&
              d->get("IDC_PORT_MODS_LIST")->items[1] == U"Workshop mod  (running)");
        CHECK(d && d->get("IDC_PORT_MODS_REPORT")->items == std::vector<std::u32string>{U"Checking..."} &&
              !d->get("IDC_PORT_MODS_LAUNCH")->enabled);
        fe.set_mod_report("STEAMMOD=900000001", {U"Loads: OK", U"3 campaigns"});
        CHECK(d && d->get("IDC_PORT_MODS_REPORT")->items.size() == 2 && !d->get("IDC_PORT_MODS_LAUNCH")->enabled);  // active row
        acts.clear();
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::SelectionChanged, kPortModsDialog, "IDC_PORT_MODS_LIST", 2)}, InputFrame{}, acts);
        CHECK(acts.size() == 1 && acts[0].kind == MenuAction::Kind::CheckMod && acts[0].mod == "MODPATH=Mods\\Local" &&
              fe.selected_mod() == "MODPATH=Mods\\Local");
        CHECK(d && !d->get("IDC_PORT_MODS_LAUNCH")->enabled);
        fe.set_mod_report("", {U"base"});  // another row's report: kept, not shown
        CHECK(d && d->get("IDC_PORT_MODS_REPORT")->items == std::vector<std::u32string>{U"Checking..."});
        acts.clear();
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, kPortModsDialog, "IDC_PORT_MODS_LAUNCH")}, InputFrame{}, acts);
        CHECK(acts.empty());  // Launch is off until the report is there
        fe.set_mod_report("MODPATH=Mods\\Local", {U"Loads: OK", U"Submod of X"});
        CHECK(d && d->get("IDC_PORT_MODS_LAUNCH")->enabled && d->get("IDC_PORT_MODS_REPORT")->items[1] == U"Submod of X");
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, kPortModsDialog, "IDC_PORT_MODS_LAUNCH")}, InputFrame{}, acts);
        CHECK(acts.size() == 1 && acts[0].kind == MenuAction::Kind::LaunchMod && acts[0].mod == "MODPATH=Mods\\Local");
        // Back, then the picker again: the row selected last and its kept report; the base row's kept report too.
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, kPortModsDialog, "IDC_CANCEL_BUTTON")}, InputFrame{}, acts);
        CHECK(fe.top() == "IDD_MAIN_MENU_DIALOG");
        acts.clear();
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, "IDD_MAIN_MENU_DIALOG", "IDC_PORT_MAIN_MENU_MODS")}, InputFrame{}, acts);
        d = env.gui.find(kPortModsDialog);
        CHECK(d && d->get("IDC_PORT_MODS_LIST")->selected == 2 && d->get("IDC_PORT_MODS_LAUNCH")->enabled);
        CHECK(acts.size() == 1 && acts[0].mod == "MODPATH=Mods\\Local");
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::SelectionChanged, kPortModsDialog, "IDC_PORT_MODS_LIST", 0)}, InputFrame{}, acts);
        CHECK(d && d->get("IDC_PORT_MODS_REPORT")->items == std::vector<std::u32string>{U"base"} && d->get("IDC_PORT_MODS_LAUNCH")->enabled);
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Cancel, kPortModsDialog, "")}, InputFrame{}, acts);
        CHECK(fe.top() == "IDD_MAIN_MENU_DIALOG");
        CHECK(std::string(menu_action_name(MenuAction::Kind::LaunchMod)) == "LaunchMod");
    }
    // t36 (QA V6): every way out of Camera and Controls lands on the Options page that opened it, not the pause menu. Real
    // mouse clicks and Esc through the GUI; the pause menu stays underneath.
    {
        Env env;
        CHECK(env.script.merge(kPortDialogsRc));
        CHECK(env.script.merge("IDD_GAME_OPTIONS_DIALOG DIALOGEX 0, 0, 321, 328\nFONT 8, \"Arial\", 0, 0, 0x0\nBEGIN\n"
                               "    PUSHBUTTON \"o\",IDC_INGAME_OPTIONS,10,100,100,20\nEND\n"
                               "IDD_IN_GAME_OPTIONS_DIALOG DIALOGEX 0, 0, 321, 328\nFONT 8, \"Arial\", 0, 0, 0x0\nBEGIN\n"
                               "    PUSHBUTTON \"n\",IDC_IN_GAME_OPTIONS_NETWORK,10,100,100,20\n"
                               "    PUSHBUTTON \"b\",IDC_OK_IN_GAME,10,200,100,20\nEND\n"));
        FrontEnd fe;
        HotkeyTable keys;
        Bindings binds;
        MenuProviders p;
        p.hotkeys = &keys;
        p.bindings = &binds;
        fe.init(env.gui, {}, p);
        std::vector<MenuAction> acts;
        fe.show_pause_menu();
        fe.update(env.click("IDD_GAME_OPTIONS_DIALOG", "IDC_INGAME_OPTIONS"), InputFrame{}, acts);
        CHECK(fe.top() == "IDD_IN_GAME_OPTIONS_DIALOG");
        const auto open_controls = [&] {
            fe.update(env.click("IDD_IN_GAME_OPTIONS_DIALOG", "IDC_IN_GAME_OPTIONS_NETWORK"), InputFrame{}, acts);
            CHECK(fe.top() == kPortControlsDialog);
        };
        open_controls();
        fe.update(env.click(kPortControlsDialog, "IDC_BUTTON_ACCEPT"), InputFrame{}, acts);
        CHECK(fe.top() == "IDD_IN_GAME_OPTIONS_DIALOG");
        fe.update(env.frame({1.0f, 1.0f}, false), InputFrame{}, acts);  // the frame after: nothing more closes
        CHECK(fe.top() == "IDD_IN_GAME_OPTIONS_DIALOG");
        open_controls();
        fe.update(env.click(kPortControlsDialog, "IDC_CANCEL_BUTTON"), InputFrame{}, acts);
        CHECK(fe.top() == "IDD_IN_GAME_OPTIONS_DIALOG");
        open_controls();
        fe.update(env.frame({1.0f, 1.0f}, false, {vk::kEscape}), InputFrame{}, acts);
        CHECK(fe.top() == "IDD_IN_GAME_OPTIONS_DIALOG" && fe.pause_menu_open());
        fe.update(env.click("IDD_IN_GAME_OPTIONS_DIALOG", "IDC_OK_IN_GAME"), InputFrame{}, acts);
        CHECK(fe.top() == "IDD_GAME_OPTIONS_DIALOG");
    }

    // 2b. Gameplay options defaults (legacy docs/re/P1_CAMERA_INPUT.md Â§3.1): absolute scrolling on, scroll speed 1.0 at the default
    //     slider position; the Defaults button restores them. GalacticCameraController::set_tuning keeps the pose.
    {
        const GameplaySettings g;
        CHECK(g.absolute_scroll && !g.right_button_scroll && !g.both_buttons_rotate && g.scroll_speed == kDefaultScrollSpeedSlider);
        CHECK(scroll_speed_option_from_slider(kDefaultScrollSpeedSlider) == 1.0f && scroll_speed_option_from_slider(0) == 0.0f &&
              scroll_speed_option_from_slider(100) == 3.0f && scroll_speed_option_from_slider(500) == 3.0f &&
              scroll_speed_option_from_slider(66) > 1.9f && scroll_speed_option_from_slider(66) < 2.1f);
        Env env;
        CHECK(env.script.merge(R"RC(
IDD_GAMEPLAY_OPTIONS_DIALOG DIALOGEX 0, 0, 400, 200
STYLE DS_SETFONT | DS_MODALFRAME | DS_CENTER | WS_POPUP
CAPTION "Dialog"
FONT 8, "MS Sans Serif", 0, 0, 0x0
BEGIN
    CONTROL         "",IDC_SCROLL_SPEED_SLIDER,"msctls_trackbar32",TBS_BOTH | TBS_NOTICKS | WS_TABSTOP,10,10,200,18
    CONTROL         "Check",IDC_ABSOLUTE_SCROLLING,"Button",BS_AUTOCHECKBOX | WS_TABSTOP,10,40,100,12
    PUSHBUTTON      "TEXT_BUTTON_DEFAULTS",IDC_DEFAULTS_BUTTON,10,160,160,24
END
)RC"));
        FrontEnd fe;
        MenuProviders p;
        GameplaySettings cur;
        cur.scroll_speed = 90;
        cur.absolute_scroll = false;
        p.gameplay = [&] { return cur; };
        fe.init(env.gui, {}, p);
        std::vector<MenuAction> acts;
        fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, "IDD_MAIN_OPTIONS_DIALOG", "IDC_MAIN_OPTIONS_GAMEPLAY")}, InputFrame{}, acts);
        GuiDialog* d = env.gui.top();
        CHECK(d && d->name() == "IDD_GAMEPLAY_OPTIONS_DIALOG");
        if (d && d->name() == "IDD_GAMEPLAY_OPTIONS_DIALOG") {
            CHECK(!d->get("IDC_ABSOLUTE_SCROLLING")->checked && d->get("IDC_SCROLL_SPEED_SLIDER")->value == 90);
            fe.update(std::vector<GuiEvent>{event(GuiEvent::Kind::Clicked, "IDD_GAMEPLAY_OPTIONS_DIALOG", "IDC_DEFAULTS_BUTTON")},
                      InputFrame{}, acts);
            CHECK(d->get("IDC_ABSOLUTE_SCROLLING")->checked && d->get("IDC_SCROLL_SPEED_SLIDER")->value == kDefaultScrollSpeedSlider);
        }
        GalacticCameraController gc;
        gc.set_bounds({-100, -100, 100, 100});
        gc.set_look_at({30, 40, 0});
        gc.set_fov_deg(40.0f);
        CameraTuning tu;
        tu.scroll_speed_option = 2.0f;
        tu.absolute_scrolling = false;
        gc.set_tuning(tu);
        CHECK(gc.tuning().scroll_speed_option == 2.0f && !gc.tuning().absolute_scrolling && gc.look_at().x == 30.0f &&
              gc.look_at().y == 40.0f && gc.fov_deg() == 40.0f && gc.limits().max_x == 115.0f && gc.zoom_state() == 0);
    }

    // 3. Free tactical camera: no clamps on yaw / pitch, roll, keys on every axis, consistent picking view.
    {
        TacticalCameraController c;  // Space_Mode, free rotation by default
        c.rotate(0.0f, 100.0f);
        CHECK(std::fabs(c.pitch_deg() - 150.0f) < 1e-3f);  // past the vertical (data max 85)
        c.rotate(400.0f, 0.0f);
        CHECK(std::fabs(c.yaw_deg() - 40.0f) < 1e-3f);  // wrapped, not clamped at 1000
        c.roll(30.0f);
        CameraParams p = c.params(4.0f / 3.0f);
        CHECK(std::fabs(p.roll - 30.0f * kDegToRad) < 1e-5f && !(p.up.x == 0.0f && p.up.y == 0.0f && p.up.z == 1.0f));
        // up is perpendicular to the view direction and unit length
        const Vec3 v{p.target.x - p.eye.x, p.target.y - p.eye.y, p.target.z - p.eye.z};
        const float vl = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        CHECK(std::fabs((v.x * p.up.x + v.y * p.up.y + v.z * p.up.z) / vl) < 1e-4f &&
              std::fabs(p.up.x * p.up.x + p.up.y * p.up.y + p.up.z * p.up.z - 1.0f) < 1e-4f);
        // the target projects to the screen centre; a point along +up projects above it
        const View view = make_view(p, 1024, 768);
        float sx = 0, sy = 0, ux = 0, uy = 0;
        CHECK(project(view, p.target, sx, sy) && std::fabs(sx - 512.0f) < 0.01f && std::fabs(sy - 384.0f) < 0.01f);
        CHECK(project(view, {p.target.x + p.up.x * 50, p.target.y + p.up.y * 50, p.target.z + p.up.z * 50}, ux, uy) && uy < sy - 10.0f &&
              std::fabs(ux - sx) < 1.0f);
        c.reset();
        CHECK(c.roll_deg() == 0.0f && c.pitch_deg() == 50.0f);
        // Land_Mode locks the pitch in data; the free camera pitches with the keys / roll keys.
        TacticalCameraController land(land_mode_defaults());
        Bindings b;
        InputState in;
        in.update(key_frame({vk::kUp}, {}, 1.0));
        land.update(in, b, 0.5f);  // 60 deg/s * 0.5 s on top of the spline pitch 55 (fraction 1 from the start)
        CHECK(std::fabs(land.pitch_deg() - 85.0f) < 1e-3f);
        in.update(key_frame({vk::kNumpad6}, {}, 1.5));
        land.update(in, b, 0.5f);
        CHECK(std::fabs(land.roll_deg() - 30.0f) < 1e-3f);
        // the original clamps stay available
        CameraTuning clamped;
        clamped.free_rotation = false;
        TacticalCameraController old(space_mode_defaults(), UiConstants{}.tactical, clamped);
        old.rotate(0.0f, 100.0f);
        old.roll(10.0f);
        CHECK(old.pitch_deg() == 85.0f && old.roll_deg() == 0.0f);
    }

    // 4. W A S D: P1 defaults and the hotkey table; displaced hotkeys; modified combinations keep their meaning.
    {
        Bindings b;
        InputState in;
        TacticalCameraController c;
        in.update(key_frame({'W'}, {}, 1.0));
        c.update(in, b, 0.5f);
        CHECK(c.look_at().y > 100.0f && std::fabs(c.look_at().x) < 1e-3f);  // forward (yaw 0 = +Y)
        c.set_look_at({0, 0, 0});
        in.update(key_frame({vk::kShift, 'A'}, {}, 1.5));  // any modifier: Shift+A still scrolls left
        c.update(in, b, 0.5f);
        CHECK(c.look_at().x < -100.0f);
        CHECK(b.binding(Action::Stop, 0).key == 'H' && b.binding(Action::SelectAll, 0).key == 'Y' &&
              b.binding(Action::SelectAll, 0).mods == kModCtrl);
        // P1 Bindings: no action other than movement on W / A / S / D
        for (size_t a = 0; a < kActionCount; ++a) {
            const auto act = static_cast<Action>(a);
            const bool move = act == Action::ScrollUp || act == Action::ScrollDown || act == Action::ScrollLeft || act == Action::ScrollRight;
            for (size_t s = 0; s < Bindings::kSlots; ++s) {
                const uint16_t k = b.binding(act, s).key;
                CHECK(move || !(k == 'W' || k == 'A' || k == 'S' || k == 'D'));
            }
        }
        HotkeyTable t;
        CHECK(t.binding(hk::kScrollUp) == (HotkeyBinding{'W', HotkeyMod::None}) && t.binding(hk::kScrollLeft) == (HotkeyBinding{'A', HotkeyMod::None}) &&
              t.binding(hk::kScrollDown) == (HotkeyBinding{'S', HotkeyMod::None}) && t.binding(hk::kScrollRight) == (HotkeyBinding{'D', HotkeyMod::None}));
        CHECK(t.binding(hk::kAttack).key == 'X' && t.binding(hk::kStop).key == 'H' && t.binding(hk::kPrevUnit).key == 'V' &&
              t.binding(hk::kWaypoint).key == 'L');
        // no other slot uses a plain W / A / S / D; every default binding is unique
        bool unique = true, wasd_only_scroll = true;
        for (uint16_t i = 0; i < kHotkeySlots; ++i) {
            const HotkeyBinding& bi = t.binding(i);
            if (bi.key < 0) continue;
            for (uint16_t j = static_cast<uint16_t>(i + 1); j < kHotkeySlots; ++j) unique = unique && !(t.binding(j) == bi);
            const bool wasd = bi.key == 'W' || bi.key == 'A' || bi.key == 'S' || bi.key == 'D';  // any modifier
            if (wasd && i != hk::kScrollUp && i != hk::kScrollDown && i != hk::kScrollLeft && i != hk::kScrollRight) wasd_only_scroll = false;
        }
        CHECK(unique && wasd_only_scroll);
        // Keymap conflicts (P host open item): after the sync no key + modifier drives two Bindings actions, End / Home
        // only reset / home the camera (hotkey slots 73 / 75), zoom is on = / - (slots 160 / 161), defaults == synced.
        {
            Bindings synced;
            t.apply_to_bindings(synced);
            Bindings defaults;
            bool no_clash = true, same = true;
            for (size_t a = 0; a < kActionCount; ++a)
                for (size_t s = 0; s < Bindings::kSlots; ++s) {
                    const KeyBinding& ka = synced.binding(static_cast<Action>(a), s);
                    if (ka.key == 0) continue;
                    for (size_t a2 = a + 1; a2 < kActionCount; ++a2)
                        for (size_t s2 = 0; s2 < Bindings::kSlots; ++s2) {
                            const KeyBinding& kb2 = synced.binding(static_cast<Action>(a2), s2);
                            const bool any = (ka.mods & kModAny) != 0 || (kb2.mods & kModAny) != 0;
                            if (kb2.key == ka.key && (any || kb2.mods == ka.mods)) no_clash = false;
                        }
                }
            for (Action a : {Action::ZoomIn, Action::ZoomOut, Action::ResetCamera})
                same = same && synced.binding(a, 0) == defaults.binding(a, 0) && defaults.binding(a, 1).key == 0;
            CHECK(no_clash && same);
            CHECK(synced.binding(Action::ZoomIn, 0) == (KeyBinding{vk::kOemPlus, kModNone}) &&
                  synced.binding(Action::ZoomOut, 0) == (KeyBinding{vk::kOemMinus, kModNone}) &&
                  synced.binding(Action::ResetCamera, 0).key == vk::kEnd && t.binding(hk::kCameraHome).key == vk::kHome &&
                  t.binding(hk::kZoomIn).key == vk::kOemPlus && hotkey_slot("CameraZoomOut") == hk::kZoomOut);
            for (Action a : {Action::ZoomIn, Action::ZoomOut})
                for (size_t s = 0; s < Bindings::kSlots; ++s)
                    CHECK(synced.binding(a, s).key != vk::kHome && synced.binding(a, s).key != vk::kEnd);
        }
        // An original 153-slot profile with the original defaults: scroll arrows -> W A S D, every W / A / S / D use moves
        // to its port default (Attack A -> X, Ctrl+A -> Ctrl+Y, Shift+W -> Shift+Q, Alt+D -> Alt+\ ...), the arrows stay on
        // the port yaw / pitch slots.
        HotkeyProfile prof;
        {
            HotkeyTable orig;
            const std::pair<const char*, HotkeyBinding> original[] = {
                {"ScrollUp", {vk::kUp, HotkeyMod::None}},          {"ScrollDown", {vk::kDown, HotkeyMod::None}},
                {"ScrollLeft", {vk::kLeft, HotkeyMod::None}},      {"ScrollRight", {vk::kRight, HotkeyMod::None}},
                {"Attack", {'A', HotkeyMod::None}},                {"Stop", {'S', HotkeyMod::None}},
                {"PreviousUnit", {'D', HotkeyMod::None}},          {"Waypoint", {'W', HotkeyMod::None}},
                {"SelectAll", {'A', HotkeyMod::Ctrl}},             {"SuperWeapon", {'W', HotkeyMod::Ctrl}},
                {"Ability_Deploy", {'D', HotkeyMod::Ctrl}},        {"Ability_StimPack", {'S', HotkeyMod::Ctrl}},
                {"Ability_SpoilerLock", {'W', HotkeyMod::Shift}},  {"Ability_DestroyPlanet", {'D', HotkeyMod::Shift}},
                {"Ability_SuperLaser", {'A', HotkeyMod::Shift}},   {"Ability_SpaceSelfDestruct", {'S', HotkeyMod::Shift}},
                {"Ability_ReplenishWingmen", {'W', HotkeyMod::Alt}}, {"Ability_ForceSight", {'S', HotkeyMod::Alt}},
                {"Ability_DrainLife", {'D', HotkeyMod::Alt}},      {"Ability_FoCFullSalvo", {'A', HotkeyMod::Alt}}};
            for (const auto& [name, kb] : original) orig.bind(static_cast<uint16_t>(hotkey_slot(name)), kb);
            orig.save(prof);
        }
        prof["KeySettingsCount"] = kOriginalHotkeySlots;
        HotkeyTable loaded;
        CHECK(loaded.load(prof));
        bool migrated = true;
        for (uint16_t i = 0; i < kHotkeySlots; ++i) migrated = migrated && loaded.binding(i) == t.binding(i);
        CHECK(migrated);  // == the new defaults
        // a customised scroll key is kept; a port slot it collides with is unbound
        HotkeyTable custom;
        custom.bind(hk::kScrollUp, {vk::kNumpad4, HotkeyMod::None});
        HotkeyProfile cprof;
        custom.save(cprof);
        cprof["KeySettingsCount"] = kOriginalHotkeySlots;
        HotkeyTable cl;
        CHECK(cl.load(cprof) && cl.binding(hk::kScrollUp).key == vk::kNumpad4 && cl.binding(hk::kRollLeft).key == -1 &&
              cl.binding(hk::kYawLeft).key == vk::kLeft);
    }

    // 5. Queued orders: right click with the queue modifier (Shift; rebindable) sets the flag, a plain click clears it.
    {
        GalacticCameraController gcam;
        OrderContext ctx;
        ctx.mode = UiMode::Galactic;
        ctx.view = make_view(gcam.params(4.0f / 3.0f), 1024, 768);
        const PickSphere planet{77, {0, 0, 0}, 40.0f, -1, PickKind::Planet};
        ctx.galactic_items = std::span<const PickSphere>(&planet, 1);
        float px = 0, py = 0;
        CHECK(project(ctx.view, planet.center, px, py));
        OrderTranslator ot;
        ot.set_selection({5});
        Bindings b;
        InputState in;
        auto right_click = [&](std::initializer_list<uint16_t> mods, double t) {
            InputFrame f = key_frame(mods, {}, t);
            f.mouse_x = static_cast<int32_t>(px);
            f.mouse_y = static_cast<int32_t>(py);
            f.buttons_down = button_bit(Button::Right);
            f.buttons_pressed = button_bit(Button::Right);
            in.update(f);
            ot.update(in, b, ctx);
            f.buttons_down = 0;
            f.buttons_pressed = 0;
            f.buttons_released = button_bit(Button::Right);
            f.time = t + 0.05;
            in.update(f);
            ot.update(in, b, ctx);
        };
        right_click({vk::kShift}, 1.0);
        right_click({}, 3.0);
        CHECK(ot.pending().size() == 2 && ot.pending_queued() == (std::vector<uint8_t>{1, 0}));
        std::vector<UiOrder> orders = ot.take_orders();
        CHECK(orders.size() == 2 && orders[0].queued && !orders[1].queued &&
              orders[0].event == commands::ev::move_object_to_object(5, 77) && ot.pending().empty());
        b.set_queue_mods(kModCtrl);  // rebound
        right_click({vk::kShift}, 5.0);
        right_click({vk::kControl}, 7.0);
        orders = ot.take_orders();
        CHECK(orders.size() == 2 && !orders[0].queued && orders[1].queued);
        b.set_queue_mods(0);
        right_click({vk::kControl}, 9.0);
        CHECK(ot.take_orders().size() == 1 && !b.queue_held(in));
    }
}

// ---- camera hotkeys --------------------------------------------------------------------------------------------
void test_camera_hotkeys() {
    TacticalCameraController cam(space_mode_defaults());
    cam.set_bounds({-3000, -3000, 3000, 3000});
    cam.set_look_at({900, -900, 0});
    cam.rotate(40.0f, 0.0f);
    std::vector<HotkeyEvent> ev(5);
    ev[0].slot = hk::kCameraCentre;  // #47 t11: space -> the origin, the record defaults
    ev[0].kind = HotkeyKind::Camera;
    ev[1].slot = hk::kTacticalOverview;
    ev[1].kind = HotkeyKind::Camera;
    ev[2].slot = 2;  // second recall of group 3
    ev[2].kind = HotkeyKind::SelectGroup;
    ev[2].group = 3;
    ev[2].camera_jump = true;
    ev[3].slot = hk::kScreenshot;
    ev[3].kind = HotkeyKind::Interface;
    ev[4].slot = hk::kCameraHome;  // #47 t11: the saved home look-at
    ev[4].kind = HotkeyKind::Camera;
    cam.set_home({-1200, 800, 0});
    CameraFocus focus;
    focus.space = true;
    focus.group = [](int8_t g) -> std::optional<Vec3> {
        if (g != 3) return std::nullopt;
        const Vec3 pts[2] = {{100, 200, 0}, {300, 400, 0}};
        return focus_point(pts);
    };
    std::vector<HotkeyEvent> centre_only(ev.begin(), ev.begin() + 1);
    CHECK(apply_camera_hotkeys(centre_only, cam, focus) == std::vector<uint16_t>{hk::kCameraCentre});
    CHECK(cam.look_at().x == 0.0f && cam.look_at().y == 0.0f && cam.yaw_deg() == space_mode_defaults().yaw_default);
    std::vector<HotkeyEvent> no_home(ev.begin() + 1, ev.begin() + 4);
    const std::vector<uint16_t> handled = apply_camera_hotkeys(no_home, cam, focus);
    CHECK(handled.size() == 2 && handled[0] == hk::kTacticalOverview && handled[1] == 2);
    CHECK(cam.overview_level() == 1 && cam.look_at().x == 200.0f && cam.look_at().y == 300.0f);
    std::vector<HotkeyEvent> home_only(ev.begin() + 4, ev.end());
    CHECK(apply_camera_hotkeys(home_only, cam, focus) == std::vector<uint16_t>{hk::kCameraHome});
    CHECK(cam.look_at().x == -1200.0f && cam.look_at().y == 800.0f && cam.overview_level() == 0);
    // HotkeyDispatcher marks the second recall within 1 s.
    HotkeyTable table;
    HotkeyDispatcher disp;
    InputState in;
    InputFrame f;
    std::vector<commands::EventData> cmds;
    std::vector<HotkeyEvent> out;
    for (int k = 0; k < 4; ++k) {
        f.time = 1.0 + 0.1 * k;
        f.keys_pressed = {};
        f.keys_down = {};
        if (k % 2 == 0) {
            f.keys_pressed.set('3');
            f.keys_down.set('3');
        }
        in.update(f);
        disp.update(in, table, {}, f.time, cmds, out);
    }
    CHECK(out.size() == 1 && out[0].camera_jump && out[0].group == 3 && cmds.size() == 2);
}

}  // namespace test_gui_screens_t
