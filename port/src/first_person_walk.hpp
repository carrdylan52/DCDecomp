#pragma once

class CDungeonMap;
struct DRAN_MAP_FIELD_SET;
struct MoveCheckInfo;
// Character collision, not camera collision; returns false without moving on invalid geometry.
bool FirstPersonDungeonStep(CDungeonMap &map, DRAN_MAP_FIELD_SET *fields, float *pos, float *velocity, MoveCheckInfo &info, int mode);
// Uses the same bounded world collector; endpoint is clipped by DC1 CheckHit.
bool RidepodDungeonTrace(CDungeonMap &map, float *from, float *to, float *endpoint);

struct BT_ACT_STATUS;
bool FirstPersonDungeonKeyboardVelocity(float *velocity, float heading, const BT_ACT_STATUS &status, bool held, bool alive, bool slow);
