#pragma once
#ifdef IXION_HAS_DC2_RIDEPOD
#include "ixion_dc1_ridepod.h"
bool RidepodVisualCombatInput(ixdc2_robo_combat_input &input);
void RidepodVisualCombatPose(int attack, float frame, bool gun);
bool RidepodVisualMuzzle(int side, float position[4]);
void RidepodVisualAim(const float target[4], int side);
void RidepodVisualActivate(bool active);
void RidepodVisualStep(const float position[4], float yaw, int motion);
bool RidepodVisualReady();
bool RidepodVisualDraw();
float RidepodVisualLegFrame();
#endif
