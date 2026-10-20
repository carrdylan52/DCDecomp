#pragma once

#ifdef IXION_HAS_DC2_RIDEPOD
#include "ixion_dc1_ridepod.h"
struct BT_ACT_STATUS;
void RidepodDungeonUpkeep(BT_ACT_STATUS &status);
bool RidepodDungeonVelocity(const ixdc2_robo_input &input, ixdc2_robo_state &state,
                            ixdc2_robo_output &output, const BT_ACT_STATUS &status,
                            bool held, bool alive, bool pad_locked);
bool RidepodDungeonHostVelocity(const ixdc2_robo_input &input, ixdc2_robo_state &state,
    ixdc2_robo_output &output, ixdc2_robo_clock &clock, float host_hz, float delta[3],
    const BT_ACT_STATUS &status, bool held, bool alive, bool pad_locked);
#endif
