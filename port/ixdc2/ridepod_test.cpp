#include "ixion_dc1_ridepod.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr float pi = 3.1415927f;
ixdc2_robo_input Input(float x = 0, float y = 0, float camera = 0, int mode = 0) {
    return {x, y, camera, mode, 2, 4, 0};
}
}
TEST(RidepodDC2, CameraRelativeVelocityCarriesVerticalAndUsesDonorScale) {
    for (int mode : {0, 1}) {
        ixdc2_robo_state state{0, {0, -3, 0}, 1, 0};
        ixdc2_robo_output out{};
        auto input = Input(0, 1, 0, mode);
        ASSERT_EQ(ixdc2_robo_walk_step(&input, &state, &out), 1);
        EXPECT_FLOAT_EQ(state.velocity[0], 0);
        EXPECT_FLOAT_EQ(state.velocity[1], -3);
        EXPECT_FLOAT_EQ(state.velocity[2], 4);
        EXPECT_EQ(state.stand_flag, 0); // Original writes false even standing.
        EXPECT_EQ(state.menu_flag, 1);
        EXPECT_EQ(out.motion_chain, mode);
        EXPECT_EQ(out.motion_param, 0);
        input.camera_yaw = pi / 2;
        ASSERT_EQ(ixdc2_robo_walk_step(&input, &state, &out), 1);
        EXPECT_NEAR(state.velocity[0], 4, 1e-6);
        EXPECT_NEAR(state.velocity[2], 0, 1e-6);
        input = Input(0.3f, 0.4f, pi / 3, mode);
        ASSERT_EQ(ixdc2_robo_walk_step(&input, &state, &out), 1);
        EXPECT_NEAR(state.velocity[0], 4 * (0.15f + 0.4f * std::sqrt(3.0f) / 2), 1e-6);
        EXPECT_NEAR(state.velocity[2], 4 * (0.2f - 0.3f * std::sqrt(3.0f) / 2), 1e-6);
    }
}
TEST(RidepodDC2, TurnAddsPiOverTwentyThenSnapsToRight) {
    ixdc2_robo_state state{};
    ixdc2_robo_output out{};
    auto input = Input(1, 0);
    for (int step = 1; step <= 8; ++step) {
        ASSERT_EQ(ixdc2_robo_walk_step(&input, &state, &out), 1);
        EXPECT_NEAR(state.yaw, step * pi / 20, 1e-6);
    }
    ASSERT_EQ(ixdc2_robo_walk_step(&input, &state, &out), 1);
    EXPECT_NEAR(state.yaw, pi / 2, 1e-4);
}
TEST(RidepodDC2, WrappedTurnUsesDonorDoubleStepAndCanonicalYaw) {
    ixdc2_robo_state state{pi - 0.1f, {}, 0, 0};
    ixdc2_robo_output out{};
    auto input = Input(-std::sin(0.5f), -std::cos(0.5f));
    ASSERT_EQ(ixdc2_robo_walk_step(&input, &state, &out), 1);
    EXPECT_NEAR(state.yaw, -pi - 0.1f + pi / 10, 1e-6);
}
TEST(RidepodDC2, OriginalMotionNamesThresholdAndStandYaw) {
    ixdc2_robo_state state{0.7f, {}, 0, 0};
    ixdc2_robo_output out{};
    for (float strength : {0.5f, 0.799f, 0.8f, 0.9f}) {
        auto input = Input(0, strength);
        ASSERT_EQ(ixdc2_robo_walk_step(&input, &state, &out), 1);
        EXPECT_EQ(out.motion, strength < 0.8f ? IXDC2_ROBO_WALK : IXDC2_ROBO_RUN);
        EXPECT_STREQ(out.motion_name, strength < 0.8f ? "\x95\xE0\x82\xAB-\x91\xAB" : "\x91\x96\x82\xE8-\x91\xAB");
    }
    auto input = Input();
    float yaw = state.yaw;
    ASSERT_EQ(ixdc2_robo_walk_step(&input, &state, &out), 1);
    EXPECT_EQ(out.motion, IXDC2_ROBO_STAND);
    EXPECT_STREQ(out.motion_name, "\x97\xA7\x82\xBF-\x91\xAB");
    EXPECT_FLOAT_EQ(state.yaw, yaw);
    EXPECT_FLOAT_EQ(state.velocity[0], 0);
    EXPECT_FLOAT_EQ(state.velocity[2], 0);
}
TEST(RidepodDC2, AnimationDivisorDoesNotCapDiagonalVelocity) {
    ixdc2_robo_state state{};
    ixdc2_robo_output out{};
    auto input = Input(1, 1);
    input.max_speed = 20;
    ASSERT_EQ(ixdc2_robo_walk_step(&input, &state, &out), 1);
    EXPECT_FLOAT_EQ(state.velocity[0], 4);
    EXPECT_FLOAT_EQ(state.velocity[2], 4);
    EXPECT_EQ(out.motion, IXDC2_ROBO_WALK);
}
TEST(RidepodDC2, RejectUnsupportedInputsWithoutMutation) {
    ixdc2_robo_state original{0.2f, {1, 2, 3}, 1, 0};
    ixdc2_robo_output sentinel{42, "unchanged", 12, 34};
    for (int kind = 0; kind < 6; ++kind) {
        auto input = Input();
        if (kind == 0) input.lock_on = 1;
        if (kind == 1) input.frame_rate = 1;
        if (kind == 2) input.mode = 2;
        if (kind == 3) input.max_speed = 0;
        if (kind == 4) input.stick_y = 1.1f;
        if (kind == 5) input.camera_yaw = std::numeric_limits<float>::quiet_NaN();
        auto state = original;
        auto out = sentinel;
        EXPECT_EQ(ixdc2_robo_walk_step(&input, &state, &out), 0);
        EXPECT_EQ(std::memcmp(&state, &original, sizeof(state)), 0);
        EXPECT_EQ(std::memcmp(&out, &sentinel, sizeof(out)), 0);
    }
    EXPECT_EQ(ixdc2_robo_walk_step(nullptr, &original, &sentinel), 0);
}
TEST(RidepodDC2, GravitySubtractsThreeClampsMinusFiveAndResetsOnLanding) {
    EXPECT_FLOAT_EQ(ixdc2_robo_gravity(0, 0), -3);
    EXPECT_FLOAT_EQ(ixdc2_robo_gravity(-3, 0), -5);
    EXPECT_FLOAT_EQ(ixdc2_robo_gravity(-5, 0), -5);
    EXPECT_FLOAT_EQ(ixdc2_robo_gravity(-5, 1), 0);
}

TEST(RidepodDC2, PALCadenceAndReciprocalScaleMatchOneSecondAtSixtyHz) {
    for (float donor_hz : {25.0f, 30.0f}) {
        ixdc2_robo_state state{};
        ixdc2_robo_output out{};
        ixdc2_robo_clock clock{};
        float delta[3]{}, distance = 0;
        auto input = Input(0, 1);
        for (int frame = 0; frame < 60; ++frame) {
            ASSERT_TRUE(ixdc2_robo_host_step(&input, &state, &out, &clock,
                60, donor_hz, IXDC2_ROBO_DC1_SCALE, delta));
            distance += delta[2];
        }
        EXPECT_EQ(clock.steps, static_cast<uint64_t>(donor_hz));
        EXPECT_NEAR(distance, 4 * donor_hz / 1.5584790683, 0.0001);
        EXPECT_NEAR(distance * 1.5584790683, 4 * donor_hz, 0.0002);
        EXPECT_EQ(out.motion, IXDC2_ROBO_RUN);
    }
}

TEST(RidepodDC2, HostCadenceTurnsOnlyOnDonorTicksAndNeutralStopsBetweenThem) {
    ixdc2_robo_state state{};
    ixdc2_robo_output out{};
    ixdc2_robo_clock clock{};
    float delta[3]{};
    auto input = Input(1, 0);
    for (int frame = 0; frame < 12; ++frame) {
        auto yaw = state.yaw;
        ASSERT_TRUE(ixdc2_robo_host_step(&input, &state, &out, &clock,
            60, 25, IXDC2_ROBO_DC1_SCALE, delta));
        if (clock.stepped) EXPECT_NEAR(state.yaw - yaw, pi / 20, 1e-6);
        else EXPECT_FLOAT_EQ(state.yaw, yaw);
    }
    EXPECT_EQ(clock.steps, 5u);
    input = Input();
    ASSERT_TRUE(ixdc2_robo_host_step(&input, &state, &out, &clock,
        60, 25, IXDC2_ROBO_DC1_SCALE, delta));
    EXPECT_FLOAT_EQ(delta[0], 0);
    EXPECT_FLOAT_EQ(delta[2], 0);
    EXPECT_EQ(out.motion, IXDC2_ROBO_STAND);
    auto saved = clock;
    input.stick_x = 2;
    EXPECT_FALSE(ixdc2_robo_host_step(&input, &state, &out, &clock,
        60, 25, IXDC2_ROBO_DC1_SCALE, delta));
    EXPECT_EQ(std::memcmp(&saved, &clock, sizeof(clock)), 0);
}
namespace {
ixdc2_robo_combat_input CombatInput() {
    return {0,0,0,40,61,60,138,{{60,86,.5f},{88,110,.5f},{114,139,.5f}}};
}
}
TEST(RidepodCombat, DecodedPunchWindowsPowersAndDurability) {
    const float begin[]={.36f,.25f,.38f},end[]={.44f,.36f,.42f},power[]={1,1.1f,1.2f};
    auto in=CombatInput();
    for (int a=0;a<3;++a) for (float ratio : {begin[a]-.001f,begin[a]+.001f,end[a]-.001f,end[a]+.001f}) {
        ixdc2_robo_combat_state s{};
        s.program=a+1;
        s.frame=in.punch[a].start+ratio*(in.punch[a].end-in.punch[a].start);
        ixdc2_robo_combat_output out{};
        ASSERT_TRUE(ixdc2_robo_combat_step(&in,&s,&out));
        EXPECT_EQ(out.punch,ratio>=begin[a] && ratio<=end[a]);
        if (out.punch) EXPECT_FLOAT_EQ(out.damage,40*power[a]);
        s.program=a+1; s.frame=in.punch[a].start+.4f*(in.punch[a].end-in.punch[a].start);
        in.arm_whp=0;
        ASSERT_TRUE(ixdc2_robo_combat_step(&in,&s,&out)); EXPECT_FALSE(out.punch);
        in.arm_whp=60;
    }
}
TEST(RidepodCombat, ConfirmStartsOnePunchAndHistoryChainsOnlyInDecodedGates) {
    auto in=CombatInput(); in.confirm=1;
    ixdc2_robo_combat_state s{}; ixdc2_robo_combat_output out{};
    ASSERT_TRUE(ixdc2_robo_combat_step(&in,&s,&out));
    EXPECT_EQ(s.serial,1u); EXPECT_EQ(out.attack,0);
    in.confirm=0;
    for (int i=0;i<100;++i) ASSERT_TRUE(ixdc2_robo_combat_step(&in,&s,&out));
    EXPECT_EQ(s.program,0); EXPECT_EQ(s.serial,1u);
    for (int a=0;a<2;++a) {
        s={}; s.program=a+1; s.history=1;
        float gate=a==0 ? .462f : .341f;
        s.frame=in.punch[a].start+(gate+.001f)*(in.punch[a].end-in.punch[a].start);
        ASSERT_TRUE(ixdc2_robo_combat_step(&in,&s,&out));
        EXPECT_EQ(out.attack,a+1); EXPECT_EQ(s.history,0);
    }
}
TEST(RidepodCombat, MachineGunEveryThirdPALStepAlternatesMuzzlesAndStopsOnRelease) {
    auto in=CombatInput(); in.attack_type=30; in.fire=1;
    ixdc2_robo_combat_state s{}; ixdc2_robo_combat_output out{};
    int shots=0;
    for (int tick=1;tick<=25;++tick) {
        ASSERT_TRUE(ixdc2_robo_combat_step(&in,&s,&out));
        EXPECT_EQ(out.shot,tick%3==0);
        if (out.shot) { EXPECT_EQ(out.muzzle,shots%2); EXPECT_FLOAT_EQ(out.damage,61); ++shots; }
    }
    EXPECT_EQ(shots,8);
    in.fire=0; ASSERT_TRUE(ixdc2_robo_combat_step(&in,&s,&out)); EXPECT_EQ(s.gun_count,0);
    in.fire=1; in.gun_whp=0;
    for(int i=0;i<6;++i) { ASSERT_TRUE(ixdc2_robo_combat_step(&in,&s,&out)); EXPECT_FALSE(out.shot); }
    EXPECT_EQ(s.serial,8u);
}
TEST(RidepodCombat, ShotWaitAndSeparateWeaponStatusAreHonored) {
    auto in=CombatInput(); in.attack_type=30; in.fire=1; in.arm_whp=0;
    ixdc2_robo_combat_state s{}; ixdc2_robo_combat_output out{};
    s.gun_count=2; s.shot_wait=2;
    ASSERT_TRUE(ixdc2_robo_combat_step(&in,&s,&out)); EXPECT_FALSE(out.shot);
    s.gun_count=2;
    ASSERT_TRUE(ixdc2_robo_combat_step(&in,&s,&out)); EXPECT_TRUE(out.shot);
    in.gun_whp=0; in.arm_whp=60; in.attack_type=0; in.fire=0;
    s.program=1; s.frame=70;
    ASSERT_TRUE(ixdc2_robo_combat_step(&in,&s,&out)); EXPECT_TRUE(out.punch);
}
TEST(RidepodCombat, InvalidUnsupportedInputDoesNotMutateStorage) {
    auto in=CombatInput(); in.attack_type=40;
    ixdc2_robo_combat_state s{},before=s; ixdc2_robo_combat_output out{1,1,1},saved=out;
    EXPECT_FALSE(ixdc2_robo_combat_step(&in,&s,&out));
    EXPECT_EQ(std::memcmp(&s,&before,sizeof(s)),0); EXPECT_EQ(std::memcmp(&out,&saved,sizeof(out)),0);
}
