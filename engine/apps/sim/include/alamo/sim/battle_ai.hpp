// A1 stage 2 hookup (owner: A1 agent): the tactical AI (engine/ai/tactical tactical_ai.hpp) in an S10 tactical
// battle. BattleAi is the TacticalAiHost of one TacticalBattle: it fills the PerceptionWorld snapshot from the
// mode's primary store, the battle's units / participants, the type database, the galaxy's players and the
// battle's CombatSystem (hard points = the S9 callback), and executes the AI's orders on the battle's own
// movement / combat / abilities paths:
//   MoveToPositionFacing        -> MovementSystem::move (the unit list, clamped to the mode bounds)
//   Attack                      -> CombatSystem::order_attack (ref2 = 1 + HardPointType -> the target's first
//                                  targetable hard point of that type) + a move into 80 % of the attack range;
//                                  the target is also the units' attack goal
//   PortExtension LuaOrder      -> MoveTo / AttackMove / Guard: a move to the target object / point; Stop.
//                                  refixf t06 (TACTICAL_COMBAT §2.8): AttackMove on an enemy combat object sets it as
//                                  each unit's attack goal (CombatSystem::set_attack_goal, path B); the task-force order
//                                  (arg bit kTfAttackMoveIssueAttack) first issues Order_Attack per unit
//                                  (order_attack_issue, path A) and drops refusing units; land: min-range staging [M].
//                                  Every other move / stop clears the attack goal
//   Reinforce                   -> A2 tactical: TacticalBattle::order_reinforce (the human path: undeployed reserve,
//                                  side not retreating, space unit cap; land: the company drop layout, space: rows of 6 [port])
//   Retreat                     -> TacticalBattle::request_retreat
//   40 / 43 / 57 / 58           -> TacticalBattle::handle_command (abilities)
//   TacticalBuild               -> A2 tactical: the player build event 0x42edf0 refusals (UC / built present, pad
//                                  fogged, not allied-owned, cooldown, type without TACTICAL_UNDER_CONSTRUCTION),
//                                  then TacticalBattle::pad_build without charge (AI players build at once, §3.6; b0147:
//                                  the plan paid at accept into its escrow; a build that started nothing credits the
//                                  base price back: TacticalBattle::ai_tactical_build)
//   Production                  -> TacticalBattle::hg_production_command for an AI player: no charge at the enqueue
//                                  (b0147: 0x289050, the plan's escrow pays)
// Only units owned by the issuing player are ordered (legal orders: the human rules of the same handlers).
// [port] The battle's stand-in orders are switched off by the host when the AI is on; BattleAi keeps the
// stand-in (attack-move toward the nearest enemy every 60 frames) for sides without a tactical AI, except the
// human players' units under BattleConfig::skip_human_units (they take commands only; TacticalBattle::order_*).
//
// Why not SimHost::execute_direct: SimHost::apply handles MoveToPosition / MoveToObject / Attack with the G0
// stub locomotor of the TacticalMode, not the battle's MovementSystem / CombatSystem (integrator ask in
// legacy docs/PHASE4.md); the AI uses its own CommandPipeline::execute_direct whose handler is BattleAi::execute.
#pragma once
#ifdef ALAMO_SIM_HAVE_TACTICAL_AI
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/ai/tactical/tactical_ai.hpp"
#include "alamo/core/file_provider.hpp"
#include "alamo/lua/object_bridge.hpp"     // sp3 slice 4b: LaneAbilityView, LaneWrite
#include "alamo/lua/fog_bridge.hpp"        // Find_Nearest's fog test in the tactical AI VMs
#include "alamo/lua/script_gaps.hpp"       // battle host: the tactical AI VMs' TacticalScriptHooks
#include "alamo/lua/script_remaining.hpp"  // L1 bindings: the tactical AI VMs' RemainingScriptHooks
#include "alamo/sim/ai_order_issuer.hpp"     // #147
#include "alamo/tactical/abilities/special_weapons.hpp"  // sp3 slice 4e: the view's SpecialWeaponSystem copy
#include "alamo/tactical/movement/common.hpp"  // kThreatOff

namespace alamo::galactic {
class GalacticSim;
}

namespace alamo::sim {

class SimHost;
class TacticalBattle;
class TacticalMode;

// What the tactical AI needs from the session (loaded once by the host).
// the process-wide default of TacticalAiSetup::ai_lane / k_ai (alamo_headless --ai-lane [--ai-lane-k K]). Set once
// at start-up, before any setup is built; never read by sim code after that (each world keeps its own copy, saved).
struct AiLaneDefaults {
    bool on = false;
    uint32_t k_ai = ai::tactical::kDefaultAiLaneLatency;
};
AiLaneDefaults& ai_lane_defaults();
// how many times AI lane code (think()) read the live battle through a BattleAi host answer, process-wide, and
// the first such answer's name (null when none). Diagnostic: alamo_headless fails a lane run with a nonzero count.
uint64_t ai_lane_live_reads(const char** first = nullptr);
// #634: process-wide, the battles run with the AI lane on, the lane applies whose result differed from the prediction
// (BattleAi::lane_mismatches) and the Spawn_Unit calls refused in lane code. alamo_headless prints them after a lane run.
struct AiLaneCounts {
    uint64_t battles = 0, mismatches = 0, refused_spawns = 0;
};
AiLaneCounts ai_lane_counts();
// #634: `inner` whose bodies run with the calling thread's AI-lane context (the AI whose lane code it runs, the facts'
// snapshot mode); BattleAi gives it to its world as the fan-out of the goal candidates and solves when the lane is on.
ParallelFor ai_lane_context_pf(ParallelFor inner);

struct TacticalAiSetup {
    const ai::AiDefinitions* defs = nullptr;
    const ai::EquationSet* equations = nullptr;
    const FileProvider* files = nullptr;
    std::vector<ai::tactical::TacticalAiDifficulty> difficulties;
    std::vector<std::string> land_plans, space_plans;  // tactical_plan_files per mode
    // AIPlayerType of a battle player ("" = no tactical AI). Default: default_tactical_ai_type.
    std::function<std::string(int32_t player, const galactic::GalacticSim& galaxy)> ai_type;
    int32_t difficulty = 1;  // 0 Easy, 1 Normal, 2 Hard
    // per player index its difficulty (0..2; -1 or missing = `difficulty`): a skirmish slot's own (SKIRMISH_SETUP
    // §1, slot +0x158).
    std::vector<int32_t> player_difficulty;
    ai::tactical::TacticalAiConstants constants;
    bool log_decisions = true;
    // OPT2 t01 / t02 (TacticalAiConfig::stagger / path_latency): per-player planning phases; the land path finder's
    // background recompute latency (0 = the original timing). Stagger off by default.
    bool stagger = false;
    uint32_t path_latency = galactic::kDefaultAllPairsLatency;
    // OPT2 t10 (TacticalAiConfig::pp_streams / pp_latency): slot-10 solves on their own RNG streams, applied pp_latency
    // frames after their slice; pp_streams false = the original synced draws in proposal order.
    bool pp_streams = true;
    uint32_t pp_latency = ai::tactical::kDefaultPpLatency;
    bool plan_retest = true;  // skai t15 / t18 (TacticalAiConfig::plan_retest)
    // sp3 (TacticalAiConfig::ai_lane / k_ai, docs/SIMULATION.md "AI lane cycle"): the AI lane, off by default; the process
    // default (ai_lane_defaults) is what a setup starts with, so every host that builds one follows the command line.
    bool ai_lane = ai_lane_defaults().on;
    uint32_t k_ai = ai_lane_defaults().k_ai;
};

// The AIPlayerType of a non-human, playable, non-neutral player: its faction's Basic_AI (FactionRules::basic_ai, faction
// +0x378; legacy docs/re/AI_TACTICAL.md "Skirmish slots use Faction.Basic_AI"); "" for none or "None". From the data, not by
// faction name (FoC's Basic_AI values are the old built-in mapping Empire -> BasicEmpire, Rebel -> BasicRebel,
// Underworld -> AI_Player_Underworld, so base-FoC battles are unchanged).
std::string default_tactical_ai_type(int32_t player, const galactic::GalacticSim& galaxy);

// the session data of the tactical AI, one loader for every battle host (alamo_headless --skirmish-ai and
// galactic --battles, alamo_game): the tactical_plan_files of both modes (list = the VFS glob lister; empty = the plan
// lists are left as they are), the difficulty table (load_tactical_ai_difficulties) and the GameConstants values
// (TacticalAiConstants::load; gc null or a missing tag keeps the current value, i.e. the default).
void load_tactical_ai_data(TacticalAiSetup& s, const FileProvider& files, const data::GameConstants* gc,
                           const std::function<std::vector<std::string>(std::string_view)>& list);

class BattleAi final : public ai::tactical::TacticalAiHost, private AiOrderIssuer {
public:
    BattleAi(TacticalBattle& battle, TacticalMode& mode, SimHost& host, const TacticalAiSetup& setup);
    ~BattleAi() override;
    BattleAi(const BattleAi&) = delete;
    BattleAi& operator=(const BattleAi&) = delete;

    // Loads the mode's plans and creates the AI players. False when no player got a tactical AI.
    bool start(std::vector<std::string>* diagnostics = nullptr);
    // One AI frame, after the battle tick (T7 order, see tactical_ai.hpp).
    void update();
    void end();  // battle end: every plan released
    // skm2 t01 (SKIRMISH_SETUP 1.1): TacticalAiWorld::set_difficulty for `player`, then the battle's build-time multipliers
    // again. False when the player has no tactical AI here or the difficulty is unchanged.
    bool set_difficulty(int32_t player, int32_t difficulty);
    // the object-goal follow records' 30-frame re-path (the first step of update(); public for the host tests).
    void follow_service();
    // sp3 t04 (AI lane, docs/SIMULATION.md "AI lane cycle"): with TacticalAiConfig::ai_lane on, the AI's unit orders (moves,
    // MoveToObject, Attack, the Lua object orders Move_To / Attack_Move / Guard_Target / Stop) are not executed when given:
    // execute() answers with predict_order() and queues an accepted order as a command due on global frame N + k_ai (N =
    // the frame of the AI update). apply_lane_batch(frame) runs at the start of a tick (SimHost T2, before the players'
    // commands) and executes every queued order due by then in (due, player, sequence) order with the same rules as
    // before (execute_order), signals included. The other AI writes stay immediate until t05. Lane off: nothing queued.
    void apply_lane_batch(uint64_t frame);
    // with the lane on, update() runs the world as TacticalAiWorld::publish() (critical lane) + think() (the AI
    // lane; inline until t03 moves it to the AI thread). Each think() is a job whose outputs - the orders it queued and
    // the digest of the AI world after it - become visible only when the job is collected: at the next update(), or at
    // the start of the tick its orders are due (k_ai = 1), whichever comes first; the battle.ai CRC hashes the digest of
    // the last collected job, so the CRC never reads the world while a job may run. lane_sync() waits until the running
    // job has finished (saves; inline until t03), without collecting it: the save keeps it as the pending job.
    void lane_sync() {}
    size_t lane_pending() const { return lane_batch_.size(); }
    // #633: Lua Prevent_AI_Usage on object `id` (CampaignBattles' object-script hook). Lane off: TacticalAiWorld's answer
    // at once. Lane on: the owner's answer published at collect (no AI player / not initialised / done), and on done
    // queued for the next AI slot like the other pushes; a delivered push answering otherwise counts in lane_mismatches().
    lua::PreventAiResult prevent_ai_usage(objects::ObjectId id, int32_t owner, bool on);
    // #634: Lua Release_Credits_For_Tactical of the AI VMs (lua::TacticalGalacticContext::release_credits): `live` itself
    // with the lane off; with it on, a call from lane code is a batch write applied at the due tick (`live` then).
    std::function<void(int32_t, double)> lane_release_credits(std::function<void(int32_t, double)> live);
    // #634: Get_Name of the AI VMs (lua::ObjectBridge::ship_name): `live` with the lane off; with it on, lane code reads
    // the host's ship names as captured.
    std::function<std::string_view(objects::ObjectHandle)> lane_ship_name(std::function<std::string_view(objects::ObjectHandle)> live);
    // #634: the AI bridge's Hide_Sub_Object lists for presentation: with the lane on, the copy published at collect.
    std::span<const std::string> hidden_sub_objects(objects::ObjectId id) const;
    size_t lane_pushes_pending() const { return lane_pushes_.size(); }  // #633 (tests)
    uint32_t lane_mismatches() const { return lane_mismatch_; }  // applied with another result than predicted [diagnostic]
    // ref5 t06 (host tests): captures the lane view now and runs `f` as lane code, so the host answers `f` asks for come from
    // the view (what think() sees). Lane on only (TacticalAiSetup::ai_lane); otherwise `f` runs live.
    void run_as_lane(const std::function<void()>& f) {
        if (world_->ai_lane()) capture_lane_view();
        set_thinking(world_->ai_lane());
        f();
        set_thinking(false);
    }
    ai::tactical::TacticalAiWorld& world() { return *world_; }
    const ai::tactical::TacticalAiWorld& world() const { return *world_; }
    bool controls(int32_t player) const;
    std::string summary() const;
    // The CRC "battle.ai": the world's digest (the last collected job's with the AI lane), then the saved fields in their
    // save order (visit_saved; one list for write() and hash(), the statistics left out).
    void hash(StateHasher& h) const;
    // N1 stage 2 (owner: n2-host; CampaignBattles chunk "battle.ai"): u32 version, a section with the TacticalAiWorld
    // (TacticalAiWorld::write / read, then relink() - battle.ai is the last battle chunk read), the AI sides
    // (checked: the loader's start() rebuilt the same AI players), stale order count, executed-event counters, the
    // opportunity-target snapshot. False (unchanged) on bad data.
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);

private:
    // the fields after the world section, for save_io::Out (write) and save_io::Hash (hash). battle_ai.cpp.
    template <class A>
    void visit_saved(A& a) const;

public:
    // ---- TacticalAiHost ----
    void fill(ai::tactical::PerceptionWorld& w, std::vector<ai::tactical::TacticalAiObject>& objects) override;
    // The battle's signal lists of the tick just run (FrameEvents; one call for the six lists).
    //   opportunity: hard points whose opportunity target changed to a new live target since the previous call (ascending
    //     unit id, then hard-point index [port: no in-tick order]); owner = the unit's team container when it has one.
    //   attack_states: s78f t08, CombatSystem::attack_state_events. damaged: skai t15, signal 0x18 (unit_damaged_signals).
    //   ability_finished: S7 t15 A1, TacticalBattle::ability_finished_events (signal 0x29, in order).
    //   unloaded: a1j t01, TacticalBattle::landings_unloaded() (owner, galactic id, units), then the lane applies' records.
    //   produced: q6 t19, TacticalBattle::production_done (land builds that went to the reinforcement pool).
    void frame_events(uint32_t kinds, ai::tactical::FrameEvents& out) override;
    commands::RejectReason execute(uint8_t player, const commands::EventData& ev) override;
    // the rules of every fact family are this class's private answers below (wire_facts); with the AI lane on,
    // think() reads the capture (capture_lane_view) instead.
    ai::tactical::BattleFacts& facts() override { return facts_; }
    // HIJACK_UNIT (AI_GALACTIC_3 §4.3): GalacticSim::magic_spawn at the battle planet, a surface unit launched into the
    // orbit fleet (FAILED when that fails, e.g. blockaded), TacticalBattle::add_reserve; returns the new pool entry.
    // Refused before spawning when the type is not in the battle tables. [port] the galactic AI free store is not touched.
    ai::tactical::TacticalReinforcement hijack_unit(int32_t player, int32_t type, bool steal) override;
    // Active_By_Default re-apply (TACTICAL_GAPS §1.2, A1_READINGS §28.8): AbilitySystem::apply_active_by_default for the
    // player's live battle object. OrderLockDone: always (the AI released the script lock, 0x3a8e50(obj, 0), first; the
    // ability share of the counter is not touched). TaskForce: skipped while movement is locked (0x39ad40: ability
    // share AbilitySystem::immobilize_count > 0, script orders_locked / locomotor_suspended; [port] HUNT active stands
    // in for the locomotor term).
    void unit_released(int32_t player, int32_t object_id, ai::tactical::TacticalAiHost::UnitRelease path) override;
    // a1k t01 (A1_READINGS §39.2, 0x5ff610): a unit left a task force -> its combat priority-set override is cleared
    // (CombatSystem::set_priority_override(id, -1): the type's Targeting_Priority_Set again; hard points share the unit's
    // set in the combat model). cu8f t04a (CU8F_T04_T06_RE §1.1): and its stickiness threshold, then each TEAM member's,
    // := the type's Targeting_Stickiness_Time_Threshold (CombatSystem::reset_stickiness).
    void unit_left_task_force(int32_t object_id) override;
    // b0147 (legacy docs/re/BKG1_RE_ANSWERS.md §6 "b0138"): the tactical plan escrow's charge / refund on the player's
    // battle account (TacticalBattle's credit hooks: the galactic account in a campaign battle); a batch command with the
    // lane on, like the other host writes.
    void add_credits(int32_t player, float amount) override;
    // a1g t04 follow-up - land TF.Reinforce drop validity: TacticalBattle::land_drop_valid (moved to the host so the
    // Reinforce command re-runs it) with this battle's mode. AI drops pass human = false (movers ignored, landing
    // transports block).
    bool land_drop_valid(int32_t player, int32_t key, const ai::tactical::Vec3f& p, bool human);
    // --- end battle host ---

private:
    // ---- the fact rules (BattleFacts, read from the battle; wired by wire_facts) ----
    ai::tactical::BattleFacts facts_;
    void wire_facts();
    // #634: this thread runs this AI's lane code (and the facts' snapshot mode, the view only while it is valid); per
    // thread, so the critical lane never sees it, and carried into the AI's fan-outs (lane_context_pf).
    void set_thinking(bool on);
    bool lane_thinking() const;
    std::vector<ai::tactical::TacticalReinforcement> reinforcement_pool(int32_t player);
    bool can_reinforce(int32_t player);
    float population_cap(int32_t player) override;  // also the AI order rules' (AiOrderIssuer)
    float population_used(int32_t player) override;
    // --- A2 tactical: build pads (L1_S4_REMAINING §3.6 on TacticalBattle's pads) ---
    bool can_build_now(int32_t player, int32_t type);
    std::vector<int32_t> tactical_builders(int32_t player, int32_t type);  // skirmish station production
    // --- end A2 tactical ---
    // TacticalBattle::movement_locked (0x39ad40) for the ambush member list.
    bool movement_locked(int32_t object_id);
    // the L1 weather system's Sight_Range_Modifier for the type's Weather_Category (every owner weather-affected
    // [port], as the fog's land sight range).
    float sight_weather_factor(int32_t type, int32_t object_id);
    // space TF.Reinforce ring search (AI_TACTICAL §7.4, TACTICAL_GAPS §4.2). Entry facing = the player's side
    // deploy facing (TacticalBattle facing_ [port: stands in for the mode +0x288 record]); playable box = the mode bounds.
    // Prevention = live objects with Reinforcement_Prevention_Radius whose owner is not allied (not the player, and an
    // enemy in the combat relation table, or no owner). Drop test: box, prevention, fog (force 1; fog builds), then the
    // space occupancy sweep (SpaceOccupancy::query_segment, statics included [M]) of the pool entry's type footprint
    // (movement radius) from p - sweep x (cos, sin)(facing) to p over frames [now, now + 115] on its Space_Layer (raw
    // mask, 0 -> 4) | 8 [M: the moving query stands in for the swept box; state-4 reservations are not tolerated].
    bool space_drop_frame(int32_t player, ai::tactical::SpaceDropFrame& out);
    bool space_drop_ok(int32_t player, int32_t key, const ai::tactical::Vec3f& p, float facing_deg, float sweep);
    // land TF.Reinforce with BattleConfig::landing_unload_timed (A1_READINGS §36): reinforce_in_flight = the
    // player's timed landing object of pool entry `key` (participant galactic id) has not finished its unload.
    bool reinforce_in_flight(int32_t player, int32_t key);
    // mcx6 t06 (legacy docs/re/LUA_API.md "Attack position 0x4f5390", 0x4f48a0 [M]): the land passability grid's static line test
    // (0x5a4c10, line_passable) for the movement class of `object_id` (a TEAM container: its first live member's).
    bool attack_position_line_clear(int32_t object_id, const ai::tactical::Vec3f& a, const ai::tactical::Vec3f& b);
    // The signal lists (frame_events)
    std::vector<ai::tactical::OpportunityTargetEvent> opportunity_targets();
    std::vector<ai::tactical::AttackStateSignal> attack_state_signals();
    std::vector<ai::tactical::UnitDamagedSignal> unit_damaged_signals();  // signal 0x18
    std::vector<int32_t> ability_finished();
    std::vector<ai::tactical::ReinforcementsUnloaded> reinforcements_unloaded();
    std::vector<ai::tactical::ProductionCompleted> production_completed();

private:
    // sp3 t01 (PERF_AUDIT S6): the type facts fill() and the per-object scans (population_cap, space_prevention_hit) read for
    // every object, resolved once per battle from the string-keyed GameObjectTypes getters (types do not change after
    // load). Index = objects::TypeIndex; the extra last entry stands for an out-of-range index (the getters' field
    // defaults, no behaviours). Built serially by ensure_type_facts(); read-only (also from fill's parallel build) after.
    struct TypeFacts {
        bool has_land_evaluator = false, has_space_evaluator = false;
        bool last_state_visible_under_fow = false, initial_state_visible_under_fow = false;
        bool create_team = false, is_dummy = false, stay_in_transport = false;
        bool valid_target = false;  // Collidable_By_Projectile_Living && Is_Valid_Target
        bool squadron_member = false, company_member = false;
        bool indigenous = false, dummy_ground_structure = false, dummy_ground_base = false;  // behaviour union 0x20 / 0x5F / 0x5B
        bool dummy_orbital_structure = false, dummy_star_base = false;                      // behaviour union 0x60 / 0x5C
        bool influences_capture = false;  // Influences_Capture_Point (in-range types only)
        int32_t base_level = 0;
        int32_t additional_population_capacity = 0;
        float reinforcement_enabling_radius = 0.0f;
        float reinforcement_prevention_radius = 0.0f;
    };
    std::vector<TypeFacts> type_facts_;
    void ensure_type_facts();
    const TypeFacts& type_facts(objects::TypeIndex t) const {
        return t < type_facts_.size() - 1 ? type_facts_[t] : type_facts_.back();
    }
    float type_population(int32_t type) const;  // A2 tactical: pop(T, mode) 0x373500
    // build_pad_check's blocker scan. The live Influences_Capture_Point objects (store order) are listed once
    // per build_pads call instead of once per pad, and the type flag is memoised per type (0 unknown, 1 no, 2 yes).
    struct CaptureInfluencer {
        objects::ObjectHandle h;
        int32_t owner = -1;
        Vec3 position;
    };
    bool influences_capture(objects::TypeIndex t) const;
    // what the store-reading fact rules read - the battle, or (lane view) the snapshot store and the
    // captured pad states, relations and live units. One rule each, over either.
    struct FactEnv;
    FactEnv live_env() const;
    FactEnv view_env() const;
    std::vector<CaptureInfluencer> capture_influencers(const objects::ObjectStore& st) const;
    ai::tactical::PadCheck pad_check(const FactEnv& env, int32_t player, int32_t pad, const std::vector<CaptureInfluencer>& influencers) const;
    std::vector<ai::tactical::TacticalPad> build_pads_in(const FactEnv& env, int32_t player) const;
    // Build / Build_All's three refusals (AI_TACTICAL §7.5), the pads `player` can build on, a space drop point inside a
    // non-allied prevention radius, a live allied star base (skirmish), the station that replaced builder `id`.
    ai::tactical::PadCheck build_pad_check_in(const FactEnv& env, int32_t player, int32_t pad) const;
    bool prevention_hit_in(const FactEnv& env, int32_t player, const ai::tactical::Vec3f& p) const;
    bool base_alive_in(const FactEnv& env, int32_t player) const;
    int32_t replaced_builder_in(const FactEnv& env, int32_t id) const;
    float allow_reinforcement_fraction_ = 0.0f;  // A2 tactical: Allow_Reinforcement_Percentage_Normalized
    bool use_reinforcement_points_ = true;       // a1g t04 follow-up: Use_Reinforcement_Points (GC +0x118a0)
    void standin_orders();
    // a1h t02 (A1_READINGS §33.4 / §33.5 rule 2): a group move with an object goal (Guard_Target or Attack_Move on an
    // object, kind 3; s78n t26 review round 2: Move_To on an object is a kind-1 position snapshot and is not followed)
    // follows the object in the movement layer, not by AI re-orders: at most once per 30 frames the group is
    // re-pathed when the object is more than R (half the lead member's attack range) from the recorded goal. Any newer
    // order for a unit takes it out of its follow record (signal 0xb, §33.3); an empty record or a dead object ends it.
    struct ObjectFollow {
        std::vector<objects::ObjectId> units;
        objects::ObjectId target = objects::kNoObjectId;
        float gx = 0.0f, gy = 0.0f;  // the recorded goal point (formation +0x210..+0x218)
        float r = 0.0f;              // R
        float threat = 0.0f;         // the move's threat tolerance (kThreatOff = none)
        uint32_t next = 0;           // the next battle frame the formation update may re-path (+0x1d8)
        // s78n t25.4 (MOVEMENT §4.3.2 items 2 / 8): a land Guard_Target on an object - the re-path is the guard block's
        // re-plan (TacticalBattle::land_attack_order, guard, replan: the slot kept). Saved (battle.ai 4), hashed when true.
        bool guard = false;
    };
    std::vector<ObjectFollow> follows_;  // issue order
    // AiOrderIssuer (#147): the records TacticalBattle::ai_order reads and writes
    void follow_drop(const std::vector<objects::ObjectId>& units) override;
    void follow_add(const std::vector<objects::ObjectId>& units, objects::ObjectId target, float x, float y, float threat,
                    bool guard) override;
    void follow_retry_at(objects::ObjectId target, uint32_t frame) override;
    void stale_target() override;
    void order_refused(objects::ObjectId unit) override;
    // cmb1 t04 (FIGHTER_FLYING §4.11 A / C 3 / G 1, BKR2 Q6 [H]): a land guard follow's formation step (every tick)
    void guard_follow_step(ObjectFollow& f, Vec3 tp);
    // #147: TacticalBattle::apply_order with this issuer (the AI's order rules are TacticalBattle::ai_order)
    commands::RejectReason execute_order(uint8_t player, const commands::EventData& ev);

    TacticalBattle* b_;
    TacticalMode* mode_;
    SimHost* host_;
    std::vector<ai::tactical::HintZone> zones_;
    ai::tactical::ZoneLocator zone_locator_;  // position -> zone (A1_READINGS §4)
    std::unique_ptr<ai::tactical::TacticalAiWorld> world_;
    uint32_t order_listener_ = 0;   // TacticalBattle::add_order_replaced_listener token (world_->order_replaced)
    std::vector<uint8_t> ai_side_;  // per player: has a tactical AI
    uint32_t stale_ = 0;  // object orders whose target died before execution (no-ops)
    // the AI lane's queued orders (header: player, source AI, sequence, scheduled_tick = due frame), in issue
    // order; hashed and saved with the lane on.
    std::vector<commands::Command> lane_batch_;
    // the TacticalAiHost answers think() reads, captured on the critical lane at each AI slot (capture_lane_view,
    // before TacticalAiWorld::publish) with the lane on; the facts' snapshot functions and the lane hooks read it while
    // `valid` (a load clears it: the relink's fill reads the battle). Per player: index = player id.
    // the per-player answers (pool, can_reinforce, population, drop frame), the movement locks, the timed landings
    // and the skirmish production are BattleFacts tables now (facts_, captured from the same rules).
    struct LaneView {
        bool valid = false;
        int32_t players = 0;         // host players
        std::vector<uint8_t> enemy;  // players x players: CombatSystem::is_enemy
        uint32_t frame = 0;                    // the battle frame
        int32_t allied_n = 0;                  // galactic players
        std::vector<uint8_t> allied;           // allied_n x allied_n: TacticalBattle::bt_allied
        std::vector<std::string> faction;      // per galactic player: faction name
        std::vector<int32_t> live_units;       // live, not retreated battle unit ids, ascending
        struct Pad {
            objects::ObjectId pad = objects::kNoObjectId, contents = objects::kNoObjectId;  // contents: built, else UC
            uint32_t cooldown_end = 0;
        };
        std::vector<Pad> pads;  // TacticalBattle::pads_ (ascending pad id)
        std::vector<float> weather_sight;                    // per weather_categories_ index
        ai::tactical::DamageTrackingStore damage;
        // sp3 slice 4c: what predict_order reads - the battle units (TacticalBattle::units_, ascending id), the containers
        // (containers_ order; `target` = team_target answers it), per player the retreat gate (retreat_block == 0), and
        // whether the battle has tactical production.
        struct Unit {
            objects::ObjectId id = objects::kNoObjectId;
            int32_t owner = -1;
            uint32_t container = 0xFFFFFFFFu;  // index into teams
            uint8_t live = 0, movable = 0;     // alive and not retreated; movable
            uint8_t mv = 0, has_goal = 0, arrived = 0;  // slice 4e: the movement system has it; its UnitView
            float gx = 0.0f, gy = 0.0f;                 // the move goal
            uint32_t mask = 0;                          // MovementSystem::mask_of (mv only)
        };
        std::vector<Unit> units;
        struct Team {
            objects::ObjectId id = objects::kNoObjectId;
            uint8_t removed = 0, target = 0;
            objects::TypeIndex rep_type = objects::kNone;  // slice 4e: TacticalBattle::representative_type
            objects::TypeIndex company_type = objects::kNone;  // TacticalBattle::team_company_type
        };
        std::vector<Team> teams;
        std::vector<uint8_t> retreat_ok;
        bool production = false;
        // slice 4e (part 2): a copy of the special weapons (reads, and the dry runs of begin_targeting / tsw_fire), the TSW
        // planet gate. The per-object and per-player L1 answers are fact tables (l1_, ...).
        std::optional<tactical::abilities::SpecialWeaponSystem> sw;
        bool tsw_planet_ok = false;
        // slice 4e (part 3): fog_object_hidden(force = 1) of every object for the AI players (ascending id; bit k for the
        // k-th entry of fog_players); empty without fog.
        std::vector<int32_t> fog_players;
        std::vector<std::pair<objects::ObjectId, uint32_t>> fog_hidden;
        // slice 4f: the perception's point test (force 1) - the fog running and FOW-active, the AI players' grids
        bool fog_live = false;
        std::vector<tactical::fog::FogGrid> fog_grids;  // fog_players order
        // the rest of what think() asked the live battle for.
        // The active BASE_SHIELDs l1_base_shield_covers counts (combat's shield on; centre, radius squared), store order.
        struct Shield {
            float x = 0.0f, y = 0.0f, z = 0.0f, r2 = 0.0f;
        };
        std::vector<Shield> shields;
        // The land passability grid's effective values (MovementSystem::grid), copied again only when its version moved;
        // per battle unit the movement mask is Unit::mask. Empty in space.
        bool grid_valid = false;
        uint64_t grid_version = 0;
        uint32_t grid_w = 0, grid_h = 0;
        std::vector<uint32_t> grid_eff;
        // The combat hard points the perception reads (w.hard_points): per combat unit (ascending id) its range in `hps`.
        std::vector<std::pair<objects::ObjectId, std::pair<uint32_t, uint32_t>>> hp_index;
        std::vector<ai::tactical::PerceptionHardPoint> hps;
        // The station upgrade records (old, new), battle order (replaced_builder).
        std::vector<std::pair<objects::ObjectId, objects::ObjectId>> upgrades;
        // The reserve entries the drop tests look up (not deployed), parts_ order, with what the snapshot tests read.
        struct Reserve {
            objects::ObjectId key = objects::kNoObjectId;
            int32_t owner = -1;
            uint32_t type = 0;
            float yaw = 0.0f;  // land: drop_yaw of its side
        };
        std::vector<Reserve> reserve;
        bool box = false;  // land: the map's playable box
        float box_min[2] = {0.0f, 0.0f}, box_max[2] = {0.0f, 0.0f};
        // #634: the battle's finished flag; per host player the entry-facing record (Reinforce_Unit's facing; the last
        // entry: a player without a side); the map objects' hint names (copied again when the list grew).
        bool finished = false;
        std::vector<Vec3> drop_facing;
        std::vector<std::pair<objects::ObjectId, std::string>> map_hints;
        std::map<objects::ObjectId, std::string> ship_names;  // SimHost::ship_names (copied again when it grew)
    };
    LaneView view_;
    // lane code reads only view_. lane_thinking() holds on the thread running think() (and, lane on, the start scripts)
    // and its fan-outs; every live battle read of a host answer goes through lane_live_read, which counts it
    // (ai_lane_live_reads; alamo_headless fails a lane run that had one) and asserts in debug builds.
    void lane_live_read(const char* what) const;
    void capture_lane_rest();  // the ref5 t06 part of capture_lane_view
    bool view_line_clear(int32_t object_id, const ai::tactical::Vec3f& a, const ai::tactical::Vec3f& b) const;
    bool view_point_fogged(int32_t player, float x, float y) const;
    const LaneView::Reserve* view_reserve(int32_t player, int32_t key) const;
    bool view_land_drop_ok(int32_t player, int32_t key, const ai::tactical::Vec3f& p) const;
    bool view_space_drop_ok(int32_t player, int32_t key, const ai::tactical::Vec3f& p);
    // the AI's host writes (unit_released, unit_left_task_force) as batch commands with the lane on.
    void queue_host_release(uint8_t op, int32_t player, int32_t object_id, uint8_t path);
    void apply_host_release(const commands::Command& c);
    void unit_released_live(int32_t player, int32_t object_id, ai::tactical::TacticalAiHost::UnitRelease path);
    void unit_left_task_force_live(int32_t object_id);
    void add_credits_live(int32_t player, float amount);
    bool lane_drop_valid(const commands::Command& c);  // a lane Reinforce's drop test at its apply
    // sp3 slice 4b: the ability / combat records the AI VMs' bindings read with the lane on (ObjectBridge::lane_abilities,
    // set at each capture and cleared by a load: before the first capture the bindings act live, on the critical lane).
    lua::LaneAbilityView lane_abilities_;
    void capture_lane_abilities();
    void queue_lane_write(const lua::LaneWrite& w);
    // slice 4e (part 2): with the lane on, the hooks the AI VMs call answer from the view and queue their writes (host ops
    // of lua::LaneWriteOp::HostFirst..); the live hook sets kept here are what the apply calls.
    lua::RemainingScriptHooks live_remaining_;
    lua::TacticalScriptHooks live_tactical_;
    uint32_t lane_refused_spawns_ = 0;  // [diagnostic] Spawn_Unit from an AI VM with the lane on (refused)
    void wrap_lane_hooks();
    void capture_l1();
    // the L1 hook reads of the AI VMs as fact tables (lane on only): one rule each, the battle's answer, read live
    // on the critical lane and from the capture inside lane code. L1Fact: one battle unit or container's answers.
    struct L1Fact {
        double rate = 0.0, ttd = 1e18;
        objects::TypeIndex cur_proj = objects::kNone;
        int32_t engines = -1;
        float energy = -1.0f;
        uint8_t clash = 0, clash_locked = 0;
        int8_t field[4] = {-1, -1, -1, -1};  // object_in_field for kinds 1, 2, 4, 8
        std::vector<objects::TypeIndex> proj;  // projectile_types
    };
    L1Fact l1_fact(objects::ObjectId id) const;
    ai::tactical::FactTable<objects::ObjectId, L1Fact> l1_;
    ai::tactical::FactTable<objects::ObjectId, int32_t> final_blow_{-1};
    ai::tactical::FactTable<int32_t, uint8_t> sw_allowed_, bomb_avail_, bombard_avail_, bomb_finished_;
    ai::tactical::FactTable<int32_t, float> bomb_result_{0.0f};
    ai::tactical::FactTable<int32_t, int32_t> retreat_inc_{0};
    void apply_host_write(const lua::LaneWrite& w);
    // slice 4e (part 3): the AI's GlobalValue.Set writes as batch commands (PortExtension kLaneGlobalSubtype)
    void queue_global_writes();
    std::vector<ai::tactical::TacticalAiWorld::GlobalWrite> global_overlay() const;  // the batch's, issue order
    // sp3 slice 4d: unload records of lane Reinforce applies that spawned their units at once (or were refused: no units),
    // handed to the AI with the battle's own at the next AI slot (the same tick: never pending at a save).
    std::vector<ai::tactical::ReinforcementsUnloaded> lane_unloaded_;
    bool landing_in_flight(int32_t player, int32_t key) const;  // the battle's timed landing of `key` (live)
    void apply_lane_write(const commands::Command& c);  // a kLaneWriteSubtype command of the batch
    static bool lane_write_command(const commands::EventData& ev);
    std::vector<int32_t> weather_category_;  // per type: index into weather_categories_ (-1: none)
    std::vector<std::string> weather_categories_;
    void resolve_weather_categories();  // fills the two above once per type table
    void capture_lane_view();
    void set_build_time_multipliers();  // TacticalBattle::ai_build_time_mult_ after the world's start
    // sp3 slice 4c: TacticalBattle::unit_by_id / team_units / hostile_owner on the view (predict_order on the AI lane).
    const LaneView::Unit* view_unit(objects::ObjectId id) const;
    std::vector<objects::ObjectId> view_team_units(uint8_t player, uint32_t wire) const;
    bool view_hostile(uint8_t player, int32_t owner) const;
    // answer from the view only inside lane code (think(), the lane start scripts); the critical lane (fill at
    // publish, the batch's apply, a load's relink) reads the battle, which the capture left unchanged.
    bool lane_view() const { return view_.valid && lane_thinking(); }
    struct LaneJob {
        bool pending = false;
        uint64_t frame = 0;   // global frame of the update (every lane write's issue tick; scheduled = frame + k_ai)
        // #632: the job stamps its own writes: sequence seq0 + (writes so far), seq0 = lane_seq_ at launch; lane_seq_
        // moves on by the batch when the job is collected. Not saved: a loaded job has seq0 = the loaded lane_seq_.
        uint32_t seq0 = 0;
        bool open = false;  // writes may be stamped (the job's think() and its global writes, the start scripts)
        uint64_t digest = 0;  // StateHasher digest of the world after think()
        std::vector<commands::Command> batch;  // the orders it queued (execute() appends while it runs)
        std::vector<lua::ScriptLockFact> locks;  // slice 4e: the AI bridge's movement-lock facts after think()
    };
    // slice 4e: the movement-lock facts the battle's movement reads with the lane on (the last collected job's; at start
    // the start scripts'); hashed and saved.
    std::vector<lua::ScriptLockFact> lane_locks_;
    LaneJob lane_job_;
    uint64_t lane_digest_ = 0;  // digest of the last collected job (hashed and saved)
    void lane_collect();
    // #633 (docs/SIMULATION.md "AI lane"): what the critical lane pushes into the AI (order replaced, a fog refog,
    // Prevent_AI_Usage) is queued in order and delivered at the next AI slot, before the job starts; movement's threat
    // mode and Prevent_AI_Usage's answer read copies published as of the last collected job. Saved and hashed.
    enum class LanePushKind : uint8_t { OrderReplaced = 1, Refog = 2, PreventAi = 3, SetDifficulty = 4 };  // 4: #634
    struct LanePush {
        LanePushKind kind = LanePushKind::OrderReplaced;
        int32_t id = 0;     // OrderReplaced / PreventAi: the object; Refog / SetDifficulty: the player
        int32_t owner = 0;  // PreventAi; SetDifficulty: the difficulty
        bool on = false;    // PreventAi
        float x = 0.0f;     // Refog: the point
        float y = 0.0f;
    };
    std::vector<LanePush> lane_pushes_;
    std::vector<std::pair<uint32_t, float>> lane_danger_;  // TacticalAiWorld::danger_layers of the last collected job
    std::vector<lua::PreventAiResult> lane_prevent_answer_;  // per player: TacticalAiWorld::prevent_ai_answer, likewise
    std::vector<int32_t> lane_attack_pos_;  // #634: TacticalAiWorld::attack_positioning_ids, likewise (follow_service)
    // #634: per player the difficulty set_difficulty answers from (the world's, then each queued change); presentation's
    // copy of the AI bridge's hidden sub-objects; the host's Release_Credits_For_Tactical the batch applies.
    std::vector<int32_t> lane_difficulty_;
    std::map<objects::ObjectId, std::vector<std::string>> lane_hidden_;
    std::function<void(int32_t, double)> release_credits_live_;
    bool attack_positioning(objects::ObjectId id) const;  // lane on: lane_attack_pos_
    void lane_rebuild_difficulty();
    void lane_publish_copies();
    void lane_deliver_pushes();
    // #632: a lane write's command header (player, AI source, the job's sequence and frames), appended to the job's batch
    // by the caller. Outside an open job it counts a live read (lane code must not stamp from the live battle).
    commands::Command lane_command(uint16_t player);
    uint32_t lane_seq_ = 0;  // the next sequence of a collected batch (sim thread: saved, hashed, moved at collect)
    void count_mismatch();  // #634: lane_mismatch_ and the process-wide count
    uint32_t lane_mismatch_ = 0;  // not saved, not hashed: orders refused, writes counting other than predicted
    static bool lane_batched(const commands::EventData& ev);
    void count_executed(const commands::EventData& ev, commands::RejectReason r);  // executed_ (diagnostic)
    // The rejections of execute_order that depend on the order and the battle state alone, without side effects.
    commands::RejectReason predict_order(uint8_t player, const commands::EventData& ev);
    commands::RejectReason execute_live(uint8_t player, const commands::EventData& ev);  // execute_order + signals + count
    std::map<std::string, std::pair<uint32_t, uint32_t>> executed_;  // event name -> (applied, rejected) [diagnostic]
    std::vector<std::pair<uint64_t, int32_t>> opp_prev_;  // battle host: (unit id << 8 | hp, target), ascending key
    // UnitAI data-pack re-projection stamps (A1_READINGS §14) of the listed UNIT_AI objects, drawn once per object
    // (draw_unit_ai_stamps, two synced draws) when fill() first lists it; ascending id.
    std::map<objects::ObjectId, int32_t> unit_ai_stamps_;
    // the second draw of the same pack (+0x34, UnitAiStamps::second: the attack-positioning stamp), same keys.
    // Not hashed (it is read only through the AI world's attack-positioning records, which are); saved (battle.ai 7 / 8).
    std::map<objects::ObjectId, int32_t> unit_ai_second_;
    // signal 0xb (a new order replaces the unit's order) on every unit an executed AI / script order took.
    void order_signals(uint8_t player, const commands::EventData& ev);
    lua::TacticalScriptHooks tactical_;    // battle host: script_gaps.hpp hooks of the tactical AI VMs (L1 final list)
#ifdef ALAMO_LUA_HAVE_FOG
    // object_hidden (Find_Nearest's FOW_Object_Hidden test); fog = the battle's fog (FogOfWar.*).
    lua::FogScriptHooks fog_hooks_;
#endif
    lua::RemainingScriptHooks remaining_;  // L1 bindings: script_remaining.hpp hooks of the tactical AI VMs (Divert; Int-final: strikes, damage stats, fields, weather, shields)
};

}  // namespace alamo::sim
#endif
