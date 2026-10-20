#pragma once
#include "runscript.hpp"
class CMonstorUnit;
namespace ixdc2::dc1mon {
constexpr float Scale = 0.641651222875f;
using Native = int (*)(RS_STACKDATA *, int);
Native *Table();
bool IsDonor(CMonstorUnit *unit, int slot);
void Install(CMonstorUnit *unit, int slot);
void Reset(CMonstorUnit *unit);
bool Tick(CMonstorUnit *unit, int slot);
void AfterStep(CMonstorUnit *unit, int slot);
void BeforeDamage(CMonstorUnit *unit, int slot);
void AfterDamage(CMonstorUnit *unit, int slot, int result);
void Reward(CMonstorUnit *unit, int slot, int xp_before);
int WeaponXp();
int Program(int dc1);
// A synthetic actor uses the same native dispatcher and private state as runtime.
void BindTest(CMonstorUnit *unit, int slot);
void FixtureMotion(const char *name, const char *joint, int start, int end, float step);
void TestLoadPack(bool fresh);
void PlayerHit(int owner, int damage, int before, int after);
}
