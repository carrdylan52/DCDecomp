#include "ridepod_walk.hpp"
#ifdef IXION_HAS_DC2_RIDEPOD
#include "btactstatus.hpp"

void RidepodDungeonUpkeep(BT_ACT_STATUS &status) {
    // PLAY normally owns these timers; the opt-in eye-task walk must not park them.
    if (status.recoil_frames > 0) --status.recoil_frames;
    if (status.invincible_frames > 0) --status.invincible_frames;
}

bool RidepodDungeonVelocity(const ixdc2_robo_input &input, ixdc2_robo_state &state,
                            ixdc2_robo_output &output, const BT_ACT_STATUS &status,
                            bool held, bool alive, bool pad_locked) {
    bool allowed = !status.movement_locked && status.can_act && !status.action_on &&
                   !status.action_busy && !held && alive && !pad_locked;
    auto effective = input;
    if (!allowed) effective.stick_x = effective.stick_y = 0;
    if (!ixdc2_robo_walk_step(&effective, &state, &output)) {
        state.velocity[0] = state.velocity[2] = 0;
        return false;
    }
    return allowed;
}

bool RidepodDungeonHostVelocity(const ixdc2_robo_input &input, ixdc2_robo_state &state,
    ixdc2_robo_output &output, ixdc2_robo_clock &clock, float host_hz, float delta[3],
    const BT_ACT_STATUS &status, bool held, bool alive, bool pad_locked) {
    bool allowed = !status.movement_locked && status.can_act && !status.action_on &&
                   !status.action_busy && !held && alive && !pad_locked;
    auto effective = input;
    if (!allowed) effective.stick_x = effective.stick_y = 0;
    if (!ixdc2_robo_host_step(&effective, &state, &output, &clock, host_hz,
            IXDC2_ROBO_LOGIC_HZ, IXDC2_ROBO_DC1_SCALE, delta)) {
        delta[0] = delta[1] = delta[2] = 0;
        return false;
    }
    return allowed;
}
#endif
