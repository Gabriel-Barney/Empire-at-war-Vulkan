#pragma once
// #147: the tactical AI's side of an AI order. TacticalBattle::apply_order runs the AI's order rules (TacticalBattle::ai_order)
// when the issuer passes one of these; the records the rules read and write that belong to the AI stay with it (BattleAi).

#include <cstdint>
#include <vector>

#include "alamo/objects/object_store.hpp"

namespace alamo::sim {

class AiOrderIssuer {
public:
    // the object-goal follows (A1_READINGS §33.4): a newer order takes the units out of theirs (signal 0xb, §33.3); an
    // Attack_Move / Guard_Target on an object adds one; a failed first land guard plan delays the newest one, when it is
    // `target`'s, until `frame`.
    virtual void follow_drop(const std::vector<objects::ObjectId>& units) = 0;
    virtual void follow_add(const std::vector<objects::ObjectId>& units, objects::ObjectId target, float x, float y, float threat,
                            bool guard) = 0;
    virtual void follow_retry_at(objects::ObjectId target, uint32_t frame) = 0;
    // the tactical population cap and use (AI_GALACTIC_3 §3.2 / §3.3) the AI's Reinforce tests
    virtual float population_cap(int32_t player) = 0;
    virtual float population_used(int32_t player) = 0;
    // statistics: an object order whose target died before it ran (a no-op); a unit that refused a task-force Order_Attack
    virtual void stale_target() = 0;
    virtual void order_refused(objects::ObjectId unit) = 0;

protected:
    ~AiOrderIssuer() = default;
};

}  // namespace alamo::sim
