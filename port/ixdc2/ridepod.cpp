#include "ixion_dc1_ridepod.h"

#include <cmath>
#include <cstring>

// This unit deliberately includes no DC1/DC2 engine or SCE header.
namespace ixdc2 {
using sceVu0FVECTOR = float[4];
constexpr float PI = 3.1415927f, TWO_PI = 6.2831855f;
static float mgFrameRate = 2.0f;
static void sceVu0CopyVector(float *to, const float *from) {
    std::memcpy(to, from, sizeof(sceVu0FVECTOR));
}
static float mgDistVector(float *v) {
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}
struct mgCFrame {
    float yaw = 0;
    void GetRotation(float *r) const { r[0] = r[2] = r[3] = 0; r[1] = yaw; }
};
struct CObjectFrame { mgCFrame *frame; };
struct Pad {
    float x = 0, y = 0;
    float GetLXf() const { return x; }
    float GetLYf() const { return y; }
};
static Pad GamePad__2;
struct Camera {
    float yaw = 0;
    float GetAngle() const { return yaw; }
};
static Camera camera;
static struct { Camera *camera; } action_info{&camera};

#define CActionChara ixdc2_RidepodChara
struct CActionChara : CObjectFrame {
    sceVu0FVECTOR velocity{};
    float max_speed = 4;
    bool stand_flag = false, menu_flag = false;
    int lock_on = 0, target_no = -1, chara_kind = 0;
    ixdc2_robo_output motion{};
    void GetPosition(float *p) const { p[0] = p[1] = p[2] = p[3] = 0; }
    void GetRotation(float *r) const { frame->GetRotation(r); }
    void SetRotation(float, float yaw, float) { frame->yaw = yaw; }
    CActionChara *SearchChara(const char *) { return nullptr; } // No arms in R1.
    void RockOn() {} // R1 rejects lock_on; no input/target ownership here.
    void SetMotion(const char *name, int param, int chain) {
        std::strcpy(motion.motion_name, name);
        motion.motion = static_cast<unsigned char>(name[0]) == 0x91 ? IXDC2_ROBO_RUN :
                        static_cast<unsigned char>(name[0]) == 0x95 ? IXDC2_ROBO_WALK : IXDC2_ROBO_STAND;
        motion.motion_param = param;
        motion.motion_chain = chain;
    }
    [[gnu::noinline]] int RoboWalkMoveIF(int mode);
};
constexpr int ACTION_KIND_SCRIPT = 1;
struct Scene { CActionChara *GetCharacter(int) { return nullptr; } };
static Scene scene;
static Scene *nowScene__2 = &scene;

static bool ValidInput(const ixdc2_robo_input *input, const ixdc2_robo_state *state,
                       const ixdc2_robo_output *output) {
    if (!input || !state || !output || input->lock_on != 0 || input->frame_rate != 2.0f ||
        (input->mode != 0 && input->mode != 1) || !std::isfinite(input->max_speed) || input->max_speed <= 0 ||
        !std::isfinite(input->stick_x) || !std::isfinite(input->stick_y) ||
        std::fabs(input->stick_x) > 1 || std::fabs(input->stick_y) > 1 ||
        !std::isfinite(input->camera_yaw) || !std::isfinite(state->yaw) ||
        state->yaw < -PI || state->yaw >= PI) return false;
    for (float v : state->velocity) if (!std::isfinite(v)) return false;
    return true;
}

// Two original bodies, byte-checked against the pinned donor by helpers.json.
[[gnu::noinline]] float unitRotation(mgCFrame *frame, float target, float speed);
#include "helpers.inc"
#undef CActionChara
} // namespace ixdc2

extern "C" int ixdc2_robo_walk_step(const ixdc2_robo_input *input, ixdc2_robo_state *state,
                                    ixdc2_robo_output *output) {
    if (!ixdc2::ValidInput(input,state,output)) return 0;
    ixdc2::mgCFrame frame{state->yaw};
    ixdc2::ixdc2_RidepodChara actor;
    actor.frame = &frame;
    actor.max_speed = input->max_speed;
    actor.stand_flag = state->stand_flag != 0;
    actor.menu_flag = state->menu_flag != 0;
    for (int i = 0; i < 3; ++i) actor.velocity[i] = state->velocity[i];
    ixdc2::GamePad__2 = {input->stick_x, input->stick_y};
    ixdc2::camera.yaw = input->camera_yaw;
    actor.RoboWalkMoveIF(input->mode);
    state->yaw = frame.yaw;
    for (int i = 0; i < 3; ++i) state->velocity[i] = actor.velocity[i];
    state->stand_flag = actor.stand_flag;
    state->menu_flag = actor.menu_flag;
    *output = actor.motion;
    return 1;
}

extern "C" float ixdc2_robo_gravity(float velocity_y, int32_t landed) {
    if (landed) return 0.0f;
    return std::fmax(-5.0f, velocity_y - 3.0f);
}

extern "C" int ixdc2_robo_host_step(const ixdc2_robo_input *input, ixdc2_robo_state *state,
    ixdc2_robo_output *output, ixdc2_robo_clock *clock, float host_hz,
    float donor_hz, float world_scale, float delta[3]) {
    if (!input || !state || !output || !clock || !delta ||
        !std::isfinite(host_hz) || !std::isfinite(donor_hz) || !std::isfinite(world_scale) ||
        donor_hz <= 0 || host_hz < donor_hz || world_scale <= 0 ||
        !std::isfinite(clock->phase) || clock->phase < 0 || clock->phase >= host_hz) return 0;
    if (!ixdc2::ValidInput(input,state,output)) return 0;
    auto next_clock = *clock;
    if (!next_clock.initialized) {
        next_clock.phase = host_hz - donor_hz;
        next_clock.initialized = 1;
    }
    next_clock.phase += donor_hz;
    next_clock.stepped = next_clock.phase >= host_hz;
    if (next_clock.stepped) {
        next_clock.phase -= host_hz;
        ++next_clock.steps;
        ixdc2_robo_walk_step(input,state,output);
    } else if (input->stick_x == 0 && input->stick_y == 0) {
        ixdc2_robo_walk_step(input,state,output); // neutral has no turn/displacement
    }
    *clock = next_clock;
    const float factor = world_scale * donor_hz / host_hz;
    for (int i = 0; i < 3; ++i) delta[i] = state->velocity[i] * factor;
    return 1;
}
