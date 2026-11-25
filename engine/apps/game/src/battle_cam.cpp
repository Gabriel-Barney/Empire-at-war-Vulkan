// The tactical battle-cam controller (battle_cam.hpp; docs/re/BATTLE_CAM.md). #462 red: stubs.
#include "alamo/game/battle_cam.hpp"

namespace alamo::game {

using tactical::Vec3;

float cinematic_view_yaw(int32_t) { return -1.0f; }
float cinematic_view_pitch(bool) { return -1.0f; }
float cinematic_view_distance(const Vec3&, float) { return 0.0f; }
Vec3 cinematic_view_offset(float, float, float) { return {}; }
BattleCamShot cinematic_view_shot(uint32_t, const Vec3&, float, float, float) { return {}; }
float hero_clash_radius(const Vec3&, bool) { return 0.0f; }
BattleCamShot hero_clash_shot(uint32_t, uint64_t, const Vec3&, bool, bool) { return {}; }

void BattleCam::start(State, BattleCamShot) {}
void BattleCam::stop() {}
bool BattleCam::open_view(const BattleCamGates&, const BattleCamPicks&) { return false; }
bool BattleCam::key_down(Key, const BattleCamGates&, const BattleCamPicks&) { return false; }
bool BattleCam::movie_key_up(const BattleCamGates&, const BattleCamPicks&) { return false; }
void BattleCam::toggle(const BattleCamGates&, const BattleCamPicks&) {}
void BattleCam::mouse(int32_t, int32_t, uint32_t, uint32_t, bool, const BattleCamPicks&) {}
void BattleCam::clash_ended(const BattleCamPicks&) {}
Vec3 BattleCam::smooth_eye(const Vec3&, int32_t) { return {}; }

}  // namespace alamo::game
