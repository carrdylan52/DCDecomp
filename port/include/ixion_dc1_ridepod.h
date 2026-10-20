#ifndef IXION_DC1_RIDEPOD_H
#define IXION_DC1_RIDEPOD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ixdc2_robo_motion {
    IXDC2_ROBO_STAND = 0,
    IXDC2_ROBO_WALK = 1,
    IXDC2_ROBO_RUN = 2
};

typedef struct ixdc2_robo_input {
    float stick_x, stick_y; /* -1..1, positive Y produces donor +Z at camera 0 */
    float camera_yaw;       /* radians, donor convention */
    int32_t mode;           /* original SetMotion chain argument: 0 or 1 */
    float frame_rate;       /* R1 accepts only the donor's mgFrameRate = 2 */
    float max_speed;        /* animation divisor, normally 4; not a speed cap */
    int32_t lock_on;        /* R1 accepts only 0 */
} ixdc2_robo_input;

typedef struct ixdc2_robo_state {
    float yaw;
    float velocity[3];
    int32_t stand_flag, menu_flag;
} ixdc2_robo_state;

typedef struct ixdc2_robo_output {
    int32_t motion;
    char motion_name[16];   /* original Shift-JIS bytes, NUL terminated */
    int32_t motion_param, motion_chain;
} ixdc2_robo_output;

/* One donor logic step, no dt. Invalid/unsupported input returns 0 and leaves
   state/output unchanged. All storage belongs to the caller; no game types.
   Synchronous, non-reentrant: call on the game logic thread. */
int ixdc2_robo_walk_step(const ixdc2_robo_input *input, ixdc2_robo_state *state,
                        ixdc2_robo_output *output);
/* DC2 Ridepod tail's vertical update, applied after DC1 collision. */
float ixdc2_robo_gravity(float velocity_y, int32_t landed);

/* PAL donor geometry uses the inverse of the DC1 -> DC2 room scale. */
#define IXDC2_ROBO_DC1_SCALE (1.0f / 1.5584790683f)
#define IXDC2_ROBO_LOGIC_HZ 25.0f
/* Bounded robo.stb table backend: attack types 0 (punch), 30 (machine gun).
   A step is one PAL25 tick. Key ranges/steps come from the private donor pack.
   Confirm is an edge/history bit; fire is held. No game objects cross the ABI. */
typedef struct ixdc2_robo_key { float start, end, step; } ixdc2_robo_key;
typedef struct ixdc2_robo_combat_input {
    int32_t attack_type, confirm, fire;
    float arm_attack, gun_attack, arm_whp, gun_whp;
    ixdc2_robo_key punch[3];
} ixdc2_robo_combat_input;
typedef struct ixdc2_robo_combat_state {
    int32_t program, history, gun_count, shot_wait, hit_sent, muzzle;
    float frame;
    uint64_t serial;
} ixdc2_robo_combat_state;
typedef struct ixdc2_robo_combat_output {
    int32_t punch, shot, attack, muzzle, started;
    float damage, radius, frame;
} ixdc2_robo_combat_output;
int ixdc2_robo_combat_step(const ixdc2_robo_combat_input *input,
    ixdc2_robo_combat_state *state, ixdc2_robo_combat_output *output);
typedef struct ixdc2_robo_clock {
    double phase;
    int32_t initialized, stepped;
    uint64_t steps;
} ixdc2_robo_clock;
/* Advance the unchanged handler at donor_hz, distribute its displacement over
   host frames, and return DC1 units per host frame. State stays in donor units.
   Host must report landing to state.velocity[1]; gravity advances on donor ticks.
   Invalid inputs leave all caller storage untouched. Neutral stops immediately. */
int ixdc2_robo_host_step(const ixdc2_robo_input *input, ixdc2_robo_state *state,
    ixdc2_robo_output *output, ixdc2_robo_clock *clock, float host_hz,
    float donor_hz, float world_scale, float delta[3]);

#ifdef __cplusplus
}
#endif
#endif
