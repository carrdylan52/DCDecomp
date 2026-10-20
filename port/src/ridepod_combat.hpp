#pragma once
#ifdef IXION_HAS_DC2_RIDEPOD
#include "ixion_dc1_ridepod.h"
class CCollisionData;
class CMonstorUnit;
class CDungeonMap;
class CFrame;
struct RidepodCombatRuntime {
    ixdc2_robo_combat_state donor{};
    ixdc2_robo_combat_output output{0,0,-1};
    bool initialized = false, pending_confirm = false, consumed = false, owns = false;
    float arm_whp = 0, gun_whp = 0;
    int punch_record = -1, punch_attack = -1, locked_target = -1;
    uint64_t punch_serial = 0, punch_token = 0;
    CCollisionData *pool = nullptr;
    CMonstorUnit *monsters = nullptr;
    CDungeonMap *map = nullptr;
    int dungeon = -1, floor = -1, character = -1;
    CFrame *locked_frame = nullptr;
    int locked_model = -1;
};
// Observable state also permits focused tests of the actual host lifecycle seam.
RidepodCombatRuntime &RidepodCombatState();
void RidepodCombatMaintainOwnership(bool owns);
void RidepodCombatFrame(bool lane_active);
void RidepodCombatActorSpawn(CMonstorUnit *unit, int slot);
void RidepodCombatRetireScene();
void RidepodCombatContact(CCollisionData *pool, int record);
void RidepodCombatObservePunch();
void RidepodCombatStep(const float position[4], float yaw, int motion, bool tick, bool allowed);
#endif
