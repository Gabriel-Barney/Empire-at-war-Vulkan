#include "alamo/ui/upgrade_sidebar.hpp"

#include <algorithm>
#include <cstdio>

namespace alamo::ui {

std::u32string sidebar_percent(float progress) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d%%", static_cast<int>(progress * 100.0f));
    std::u32string out;
    for (const char* c = buf; *c; ++c) out.push_back(static_cast<char32_t>(static_cast<unsigned char>(*c)));
    return out;
}

void UpgradeSidebar::clear() {
    list0_.clear();
    list1_.clear();
}

// 0x2deba0(HUD, type, list, at_front) [H]: a slot of the list showing `type` (its stale mark cleared); a slot showing a
// lower level of its chain takes `type` in place; a slot showing a higher level means `type` is not shown (nullptr); else
// a new slot, at the front (after the specials [port]) or appended.
UpgradeSidebar::Slot* UpgradeSidebar::share(int32_t type, bool list1, bool at_front, const SidebarInput& in) {
    std::vector<Slot>& l = list1 ? list1_ : list0_;
    const auto chain_has = [&](int32_t from, int32_t want, bool up) {  // walk Next (up) or Previous from `from`
        int32_t t = from;
        for (int guard = 0; guard < 64 && t >= 0; ++guard) {
            const SidebarType st = in.type ? in.type(t) : SidebarType{};
            t = up ? st.next : st.previous;
            if (t == want) return true;
        }
        return false;
    };
    for (Slot& s : l) {
        if (s.kind != Kind::Upgrade) continue;
        if (s.type == type) {
            s.stale = false;
            return &s;
        }
        if (chain_has(type, s.type, false)) {  // a lower level: the higher one replaces it in place
            s.type = type;
            s.building = -1;
            s.stale = false;
            s.tray = in.type ? in.type(type).tray : true;
            return &s;
        }
        if (chain_has(type, s.type, true)) return nullptr;  // a higher level is already shown
    }
    Slot n;
    n.type = type;
    n.tray = in.type ? in.type(type).tray : true;
    if (at_front) {
        const auto first = std::find_if(l.begin(), l.end(), [](const Slot& s) { return s.kind == Kind::Upgrade; });
        return &*l.insert(first, std::move(n));
    }
    l.push_back(std::move(n));
    return &l.back();
}

void UpgradeSidebar::update(const SidebarInput& in) {
    // The specials are rebuilt at the front each time [port: retail reuses list0[0] / [1], see the doc's quirks].
    list0_.erase(std::remove_if(list0_.begin(), list0_.end(), [](const Slot& s) { return s.kind != Kind::Upgrade; }), list0_.end());
    for (Slot& s : list0_) s.stale = true;
    for (Slot& s : list1_) s.stale = true;
    const auto icon_of = [&](int32_t t) {
        std::string i = in.type ? in.type(t).icon : std::string();
        return i.empty() ? std::string("missing.tga") : i;
    };
    for (const int32_t t : in.completed) {
        const bool tray = in.type ? in.type(t).tray : true;
        Slot* s = share(t, false, !tray, in);
        if (!s) continue;
        s->icon = icon_of(t);
        s->fill = 1.0f;
        s->text.clear();
        s->building = -1;
    }
    for (const SidebarInput::Building& b : in.building) {
        const int32_t prev = in.type ? in.type(b.type).previous : -1;
        Slot* s = prev < 0 ? share(b.type, true, false, in) : share(prev, false, false, in);
        if (!s) continue;
        if (prev >= 0) s->building = b.type;
        s->icon = icon_of(b.type);
        s->fill = b.progress;
        s->text = sidebar_percent(b.progress);
    }
    std::erase_if(list0_, [](const Slot& s) { return s.stale; });
    std::erase_if(list1_, [](const Slot& s) { return s.stale; });
    std::vector<Slot> specials;
    if (!in.weather_icon.empty()) {
        Slot w;
        w.kind = Kind::Weather;
        w.icon = in.weather_icon;
        specials.push_back(std::move(w));
    }
    if (in.interdicted) {
        Slot d;
        d.kind = Kind::Interdictor;  // the icon is the component's Icon_Alternate_Texture_Name[0] (the host's)
        specials.push_back(std::move(d));
    }
    list0_.insert(list0_.begin(), specials.begin(), specials.end());
}

size_t UpgradeSidebar::top_count() const {
    size_t t = 0;
    while (t < list0_.size() && list0_[t].kind != Kind::Upgrade) ++t;
    while (t < list0_.size() && !list0_[t].tray) ++t;
    return t;
}

std::vector<UpgradeSidebar::Placed> UpgradeSidebar::place(const Layout& l) const {
    std::vector<Placed> out;
    const size_t T = top_count(), n0 = list0_.size(), n1 = list1_.size();
    float x = l.offset.x + l.anchor_x, y = l.offset.y;
    const bool weather_and_interdictor = n0 >= 2 && list0_[0].kind == Kind::Weather && list0_[1].kind == Kind::Interdictor;
    for (size_t k = 0; k < T; ++k) {
        const Slot& s = list0_[k];
        int32_t seg = -1;  // a Tray = False slot has no frame
        if (s.kind == Kind::Weather) seg = weather_and_interdictor ? 0 : 3;
        else if (s.kind == Kind::Interdictor) seg = weather_and_interdictor ? 2 : 3;
        out.push_back({&s, {x, y}, seg});
        y += l.step.y;
    }
    y += l.gap;
    int32_t r = -1;
    const auto next = [&](const Slot& s, int32_t seg) {
        if (++r >= l.icons_per_column) {
            r = 0;
            x += l.step.x;
            y = l.offset.y;  // the new column starts level with the top set's first slot
        }
        out.push_back({&s, {x, y}, seg});
        y += l.step.y;
    };
    for (size_t i = T; i < n0; ++i) {
        int32_t seg = 1;
        if (n0 == T + 1 && n1 == 0) seg = 3;
        else if (i == T) seg = 0;
        else if (i == n0 - 1 && n1 == 0) seg = 2;
        next(list0_[i], seg);
    }
    for (size_t j = 0; j < n1; ++j) {
        int32_t seg;
        if (n0 == T) seg = n1 == 1 ? 3 : 0;  // retail: every slot a top piece when list 0 has no tray slot
        else seg = j == n1 - 1 ? 2 : 1;
        next(list1_[j], seg);
    }
    return out;
}

}  // namespace alamo::ui
