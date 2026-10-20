#include "ixion_dc1_ridepod.h"
#include <cmath>

namespace ixdc2 {
// robo.stb INIT code offsets 944/1076/1208. Combo gates are distinct from
// damage windows: MAIN fd@4596, args at 16144. See combat-table.md.
struct Window { float begin, end, power, radius, chain_begin, chain_end; };
static constexpr Window punch_table[] = {
    {.36f,.44f,1.f,10.f,.462f,.5f},
    {.25f,.36f,1.1f,10.f,.341f,.386f},
    {.38f,.42f,1.2f,12.f,2.f,2.f}
};
}
extern "C" int ixdc2_robo_combat_step(const ixdc2_robo_combat_input *in,
    ixdc2_robo_combat_state *s, ixdc2_robo_combat_output *out) {
    if (!in || !s || !out || (in->attack_type != 0 && in->attack_type != 30) ||
        !std::isfinite(in->arm_attack) || !std::isfinite(in->gun_attack) ||
        !std::isfinite(in->arm_whp) || !std::isfinite(in->gun_whp) ||
        in->arm_attack < 0 || in->gun_attack < 0 ||
        !std::isfinite(s->frame) || s->program < 0 || s->program > 3) return 0;
    for (auto &k : in->punch)
        if (!std::isfinite(k.start) || !std::isfinite(k.end) || !std::isfinite(k.step) ||
            k.end <= k.start || k.step <= 0 || k.step > k.end-k.start) return 0;
    *out = {};
    out->attack = -1;
    if (s->shot_wait > 0) --s->shot_wait; // actionchara.cpp:3605
    if (in->confirm) s->history = 1;
    if (s->program == 0 && in->attack_type == 0 && in->confirm) {
        s->program = 1; s->frame = in->punch[0].start;
        s->history = s->hit_sent = 0; ++s->serial; out->started = 1;
    }
    if (s->program > 0) {
        int index = s->program-1;
        auto k = in->punch[index];
        auto w = ixdc2::punch_table[index];
        float ratio = (s->frame-k.start)/(k.end-k.start);
        if (index < 2 && s->history && ratio >= w.chain_begin && ratio <= w.chain_end) {
            ++s->program; ++index; ++s->serial;
            s->frame = in->punch[index].start;
            s->history = s->hit_sent = 0; out->started = 1;
            k = in->punch[index]; w = ixdc2::punch_table[index]; ratio = 0;
        }
        out->attack = index; out->frame = s->frame;
        if (ratio >= w.begin && ratio <= w.end && in->arm_whp > 0) {
            out->punch = 1; out->damage = in->arm_attack*w.power; out->radius = w.radius;
            s->hit_sent = 1;
        }
        s->frame += k.step*1.2f; // donor NormalDrive
        if (s->frame > k.end) { s->program = 0; s->history = 0; }
        s->gun_count = 0;
    } else if (in->attack_type == 30 && in->fire) {
        // fd@8288: held action (56), G1 reset and _SHOT every third step.
        if (++s->gun_count >= 3) {
            s->gun_count = 0;
            if (s->shot_wait == 0) {
                s->shot_wait = 2; // _SHOT sets this before its durability gate
                if (in->gun_whp > 0) {
                    out->shot = 1; out->damage = in->gun_attack;
                    out->muzzle = s->muzzle; s->muzzle ^= 1; ++s->serial;
                }
            }
        }
    } else s->gun_count = 0;
    return 1;
}
