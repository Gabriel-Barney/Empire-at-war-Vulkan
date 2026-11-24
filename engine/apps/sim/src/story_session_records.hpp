// #151: StorySession's save records and their one field list each, walked by the save writer (save_io::Out), the reader
// (save_io::In) and the per-tick CRC (save_io::Hash), so a field is hashed in the width it is saved in and in the same
// order. StorySession::write / read / hash call these; alamo_sim_story_rewards_tests pins them.
#pragma once
#include <concepts>
#include <cstdint>
#include <string>
#include <type_traits>

#include "alamo/sim/story_session.hpp"
#include "save_io.hpp"

namespace alamo::sim::story_records {

template <class R, class T>
concept Of = std::same_as<std::remove_const_t<R>, T>;

// ---- a plot definition edit (PICK_PLANET / RANDOM_STORY; story save v8 on). Every field is in the CRC.
template <class Ar, Of<StorySession::DefinitionEdit> R>
void visit(Ar& a, R& e) {
    a(e.kind, e.set, e.a, e.b, e.tactical);
}

// ---- one plot set ahead of its StoryModel (the galactic sets in order, then the attached tactical one). The CRC reads
// the identity numbers and the ACCUMULATE poll state; the plot file, the plot and script counts (read() checks them
// against the session's set) and the fired-log cursor are saved only. The StoryModel follows in both: a section in the
// save, StoryModel::hash in the CRC.
struct PlotSetFields {
    int32_t player = -1;
    uint32_t number = 0;
    std::string file;
    uint32_t plot_count = 0;
    uint32_t script_count = 0;
    bool credits_seen = false;
    float last_credits = 0.0f;
    uint64_t fired_seen = 0;
    friend bool operator==(const PlotSetFields&, const PlotSetFields&) = default;
};

template <class Ar, Of<PlotSetFields> R>
void visit(Ar& a, R& s) {
    a(s.player, s.number);
    a.save_only(s.file, s.plot_count, s.script_count);
    a(s.credits_seen, s.last_credits);
    a.save_only(s.fired_seen);
}

}  // namespace alamo::sim::story_records
