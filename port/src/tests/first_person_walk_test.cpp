#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <libpad.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <cstdlib>
#include <string>

#include "../first_person_walk.hpp"
#include "../ridepod_walk.hpp"
#include "../ridepod_hitscan.hpp"
#include "../ridepod_combat.hpp"
#include "../ridepod_records.hpp"
#include "runscript.hpp"
#include "runscript_opcodes.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "btactstatus.hpp"
#include "camera.hpp"
#include "character.hpp"
#include "collision.hpp"
#include "dungeonmap.hpp"
#include "ebattle.hpp"
#include "edit.hpp"
#include "frame.hpp"
#include "gamepad.hpp"
#include "gamepad_port.hpp"
#include "gameutil.hpp"
#include "collisiondata.hpp"
#include "dun/gameloop.hpp"
#include "dngstatusdata.hpp"
#include "hitvalue.hpp"
#include "monstorunit.hpp"

extern int   viewMode, chara_mode, chara_fishing;
extern int gameTask;
extern float viewAngleH, viewAngleV;
void         EyeCamera(CCamera *, CCharacter *, int);
int          PortEdMoveCheck(float *, float *, float *, MoveCheckInfo *, CCPoly *, int, int);

namespace {
void Keyboard(const std::vector<int> &keys, float dx = 0) {
    InputKeyboardMouse held;
    held.keys = keys;
    held.mouse_dx = dx;
    InputSetScriptedDevices(held);
    InputLatchPad(0);
}

void PrepareInput() {
    InputResetBindings();
    InputSetMovementLocked(false);
    InputSetMenuMouse(false);
    InputMouseSettings mouse;
    mouse.capture = false;
    InputSetMouseSettings(mouse);
    InputPadState pad;
    pad.connected = true;
    InputSetOverride(0, &pad);
    ClockSetUnbounded(true);
    ClockReset();
}

CCPoly Poly(std::array<float, 3> a, std::array<float, 3> b, std::array<float, 3> c, std::array<float, 3> normal) {
    CCPoly     p{};
    std::array vertices{a, b, c};
    for (int j = 0; j < 3; ++j) {
        p.normal[j] = normal[j];
        for (int i = 0; i < 3; ++i) {
            p.vertex[i][j] = vertices[i][j];
        }
    }
    return p;
}
} // namespace

TEST(FirstPersonWalk, KeyboardBindingsNormalizeCancelAndRespectMenuFocusAndPadLocks) {
    PrepareInput();
    Keyboard({SDL_SCANCODE_W, SDL_SCANCODE_D});
    auto move = InputGetKeyboardMovement();
    EXPECT_NEAR(std::hypot(move.x, move.y), 1, 1e-6);
    EXPECT_GT(move.x, 0);
    EXPECT_LT(move.y, 0);
    Keyboard({SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D});
    EXPECT_FLOAT_EQ(InputGetKeyboardMovement().x, 0);
    EXPECT_FLOAT_EQ(InputGetKeyboardMovement().y, 0);
    std::string_view keys[]{"Up"};
    ASSERT_TRUE(InputBindKeys("ly-", keys));
    Keyboard({SDL_SCANCODE_W});
    EXPECT_FLOAT_EQ(InputGetKeyboardMovement().y, 0);
    Keyboard({SDL_SCANCODE_UP});
    EXPECT_FLOAT_EQ(InputGetKeyboardMovement().y, -1);
    InputSetMenuMouse(true);
    EXPECT_FLOAT_EQ(InputGetKeyboardMovement().y, 0);
    InputSetMenuMouse(false);
    GamePad.KeyLock(1);
    EXPECT_FLOAT_EQ(InputGetKeyboardMovement().y, 0);
    GamePad.KeyLock(0);
    EXPECT_FLOAT_EQ(InputGetKeyboardMovement().y, -1);
    InputSetOverride(0, nullptr);
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_UP;
    event.key.down = true;
    InputHandleEvent(event);
    EXPECT_FLOAT_EQ(InputGetKeyboardMovement().y, -1);
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    InputHandleEvent(event);
    EXPECT_FLOAT_EQ(InputGetKeyboardMovement().y, 0);
}

TEST(FirstPersonWalk, TownKeyboardMovesAlongMouseHeadingWithoutSteeringViewAndLocksStopWalking) {
    PrepareInput();
    CMainChara character;
    character.Initialize();
    character.move_info = {};
    EdMoveCharaInfo.chara = &character;
    viewMode = 1;
    chara_mode = 0;
    chara_fishing = 0;
    viewAngleH = 0;
    viewAngleV = 0;
    EdSetKeyMode(1);
    Keyboard({SDL_SCANCODE_W}, 100);
    sceVu0FVECTOR pos{0, 0, 0, 1}, velocity{0, 0, 0, 0}, next{};
    MoveCheckInfo info{};
    PortEdMoveCheck(pos, velocity, next, &info, nullptr, 0, 0);
    float heading = -100 * 0.1f * 3.14159265358979323846f / 180;
    EXPECT_NEAR(velocity[0], 0.6f * std::sin(heading), 1e-5);
    EXPECT_NEAR(velocity[2], 0.6f * std::cos(heading), 1e-5);
    CCamera eye(1);
    EyeCamera(&eye, &character, 0);
    EXPECT_NEAR(viewAngleH, heading, 1e-5);
    EXPECT_FLOAT_EQ(viewAngleV, 0);
    Keyboard({SDL_SCANCODE_D});
    velocity[0] = velocity[2] = 0;
    PortEdMoveCheck(pos, velocity, next, &info, nullptr, 0, 0);
    EXPECT_NEAR(velocity[0], 0.6f * std::cos(heading), 1e-5);
    EXPECT_NEAR(velocity[2], -0.6f * std::sin(heading), 1e-5);
    EyeCamera(&eye, &character, 0);
    EXPECT_NEAR(viewAngleH, heading, 1e-5);
    for (int locked : {1, 2}) {
        chara_mode = locked;
        velocity[0] = velocity[2] = 0;
        PortEdMoveCheck(pos, velocity, next, &info, nullptr, 0, 0);
        EXPECT_FLOAT_EQ(velocity[0], 0);
        EXPECT_FLOAT_EQ(velocity[2], 0);
    }
    chara_mode = 0;
    EdMoveCharaInfo.key_lock = 1;
    velocity[0] = velocity[2] = 0;
    PortEdMoveCheck(pos, velocity, next, &info, nullptr, 0, 0);
    EXPECT_FLOAT_EQ(velocity[0], 0);
    EXPECT_FLOAT_EQ(velocity[2], 0);
}

TEST(FirstPersonWalk, DungeonWalkUsesBodyFloorWallAndObjectCollisionAndRejectsMalformedMaps) {
    auto map = std::make_unique<CDungeonMap>();
    map->map_type = 1;
    for (auto &cell : map->cells) {
        cell.parts_no = -1;
    }
    map->cells[0].parts_no = 0;
    map->cells[0].direction = 0;
    CFrame                   frame;
    CCollisionMDT            collision;
    std::array<CCPolyBox, 3> mesh{};
    mesh[0].poly = Poly({-100, 0, -100}, {-100, 0, 100}, {100, 0, -100}, {0, 1, 0});
    mesh[1].poly = Poly({100, 0, -100}, {-100, 0, 100}, {100, 0, 100}, {0, 1, 0});
    mesh[2].poly = Poly({-100, -100, 10}, {0, 100, 10}, {100, -100, 10}, {0, 0, -1});
    for (int j = 0; j < 3; ++j) {
        collision.min[j] = 1000;
        collision.max[j] = -1000;
        for (auto &m : mesh) {
            m.box.min[j] = std::min({m.poly.vertex[0][j], m.poly.vertex[1][j], m.poly.vertex[2][j]});
            m.box.max[j] = std::max({m.poly.vertex[0][j], m.poly.vertex[1][j], m.poly.vertex[2][j]});
            collision.min[j] = std::min(collision.min[j], m.box.min[j]);
            collision.max[j] = std::max(collision.max[j], m.box.max[j]);
        }
    }
    collision.mesh = mesh.data();
    collision.mesh_count = 3;
    frame.flags = 1;
    frame.collision = &collision;
    map->parts[0].collision = &frame;
    sceVu0FVECTOR pos{0, 0, 0, 1}, velocity{};
    MoveCheckInfo info{};
    for (int tick = 0; tick < 100; ++tick) {
        velocity[0] = 0;
        velocity[1] = -.1f;
        velocity[2] = .6f;
        ASSERT_TRUE(FirstPersonDungeonStep(*map, nullptr, pos, velocity, info, 1));
        EXPECT_NEAR(pos[1], 0, 1e-5);
        EXPECT_TRUE(info.landed);
        EXPECT_LE(pos[2], 5.001f);
    }
    EXPECT_GT(pos[2], 4);
    map->map_type = 0;
    map->parts[0].frame[0] = &frame;
    pos[0] = pos[2] = 0;
    ASSERT_TRUE(FirstPersonDungeonStep(*map, nullptr, pos, velocity, info, 1));
    EXPECT_GT(pos[2], 0);
    map->parts[0].collision = nullptr;
    map->box_collision_model = &frame;
    map->boxes[0].used = 1;
    for (int tick = 0; tick < 50; ++tick) {
        velocity[2] = .6f;
        velocity[1] = -.1f;
        ASSERT_TRUE(FirstPersonDungeonStep(*map, nullptr, pos, velocity, info, 1));
        EXPECT_LE(pos[2], 5.001f);
    }
    map->boxes[0].used = 0;
    map->collision_model = &frame;
    map->atra_num = 1;
    map->atra[0].used = 1;
    velocity[2] = .6f;
    ASSERT_TRUE(FirstPersonDungeonStep(*map, nullptr, pos, velocity, info, 1));
    EXPECT_LE(pos[2], 5.001f);
    map->parts[0].collision = &frame;
    map->map_type = 1;
    map->cells[0].parts_no = 72;
    auto z = pos[2];
    EXPECT_FALSE(FirstPersonDungeonStep(*map, nullptr, pos, velocity, info, 1));
    EXPECT_FLOAT_EQ(pos[2], z);
    map->cells[0].parts_no = 0;
    collision.mesh_count = 32769;
    EXPECT_FALSE(FirstPersonDungeonStep(*map, nullptr, pos, velocity, info, 1));
    EXPECT_FLOAT_EQ(pos[2], z);
}

TEST(FirstPersonWalk, ReservedZoomResetCannotAlsoWalkAndDungeonScriptLocksKeepTheirOwnership) {
    PrepareInput();
    Config config;
    config.mouse_zoom = true;
    config.mouse_capture = false;
    InputApplyConfig(config);
    std::string_view reset[]{"W"};
    ASSERT_TRUE(InputBindKeys("zoom_reset", reset));
    Keyboard({SDL_SCANCODE_W});
    EXPECT_FLOAT_EQ(InputGetKeyboardMovement().y, 0);
    InputResetBindings();
    Keyboard({SDL_SCANCODE_W});
    BT_ACT_STATUS status{};
    status.can_act = 1;
    sceVu0FVECTOR velocity{0, -0.1f, 0, 0};
    ASSERT_TRUE(FirstPersonDungeonKeyboardVelocity(velocity, 0, status, false, true, false));
    EXPECT_FLOAT_EQ(velocity[2], 0.6f);
    EXPECT_FLOAT_EQ(velocity[1], -0.1f);
    status.can_act = 0;
    status.movement_locked = 1;
    EXPECT_FALSE(FirstPersonDungeonKeyboardVelocity(velocity, 0, status, false, true, false));
    EXPECT_FLOAT_EQ(velocity[2], 0);
    EXPECT_EQ(status.movement_locked, 1);
    status.can_act = 1;
    EXPECT_FALSE(FirstPersonDungeonKeyboardVelocity(velocity, 0, status, false, true, false));
    status.movement_locked = 0;
    status.action_on = 1;
    EXPECT_FALSE(FirstPersonDungeonKeyboardVelocity(velocity, 0, status, false, true, false));
    status.action_on = 0;
    EXPECT_FALSE(FirstPersonDungeonKeyboardVelocity(velocity, 0, status, true, true, false));
    EXPECT_FALSE(FirstPersonDungeonKeyboardVelocity(velocity, 0, status, false, false, false));
}

#ifdef IXION_HAS_DC2_RIDEPOD
namespace {
void RidepodSetEnvironment(const char *value) {
#ifdef _WIN32
    _putenv_s("DC_IXION_RIDEPOD",value ? value : "");
#else
    if (value) setenv("DC_IXION_RIDEPOD",value,1);
    else unsetenv("DC_IXION_RIDEPOD");
#endif
}
struct RidepodEnvironment {
    bool present;
    std::string old;
    explicit RidepodEnvironment(bool enabled=true) {
        const char *v=std::getenv("DC_IXION_RIDEPOD"); present=v!=nullptr; if(v) old=v;
        RidepodSetEnvironment(enabled ? "1" : "0");
    }
    ~RidepodEnvironment() {
        RidepodSetEnvironment(present ? old.c_str() : nullptr);
    }
};
struct RidepodScene {
    RidepodEnvironment env;
    std::unique_ptr<CMonstorUnit> unit=std::make_unique<CMonstorUnit>();
    std::unique_ptr<CDngStatusData> status=std::make_unique<CDngStatusData>();
    std::unique_ptr<CDungeonMap> map=std::make_unique<CDungeonMap>();
    CCollisionData pool{};
    CHitValue values[32]{};
    CUserStatus *old_status=UserStatus;
    CCollisionData *old_pool=NowColData;
    CMonstorUnit *old_monsters=NowMonstorUnit;
    CDungeonMap *old_map=NowDngMap;
    CHitValue *old_values=NowHitValue;
    int old_dungeon=selectMapNo;
    RidepodScene() {
        std::memset(unit->monster,0,sizeof(unit->monster));
        std::memset(unit->effect,0,sizeof(unit->effect));
        std::memset(unit->effect2,0,sizeof(unit->effect2));
        std::memset(unit->guard,0,sizeof(unit->guard));
        std::memset(unit->script,0,sizeof(unit->script));
        UserStatus=status.get(); NowColData=&pool; NowMonstorUnit=unit.get();
        NowDngMap=map.get(); NowHitValue=values; selectMapNo=DUNGEON_DIVINE_BEAST_CAVE;
        status->cur_chara=CHARA_TOAN; status->cur_floor=0;
        unit->current_monster=0;
        auto &m=unit->monster[0]; m.hp=100; m.state=2; m.defense=7; m.steal_item=-1;
        auto &hurt=unit->effect[0]; hurt.timer[0]=1; hurt.radius[0]=3; hurt.position[0][3]=1;
        RidepodCombatState()={}; RidepodCombatMaintainOwnership(true);
        auto &r=RidepodCombatState(); r.initialized=true; r.arm_whp=40; r.gun_whp=62;
    }
    ~RidepodScene() {
        RidepodCombatRetireScene(); RidepodCombatState()={};
        UserStatus=old_status; NowColData=old_pool; NowMonstorUnit=old_monsters;
        NowDngMap=old_map; NowHitValue=old_values; selectMapNo=old_dungeon;
    }
    int Punch(int attack=0) {
        float point[4]{0,0,0,1};
        auto &r=RidepodCombatState();
        r.punch_record=RidepodRecord(point,40,2,2,attack); r.punch_attack=attack;
        r.punch_token=RidepodRecordToken(&pool,r.punch_record); r.consumed=false;
        r.donor.program=1; r.pending_confirm=true; r.locked_target=0;
        r.locked_frame=unit->chara[0][0].frame; r.locked_model=unit->monster[0].base_model;
        return r.punch_record;
    }
};
}
TEST(FirstPersonWalk, RidepodDonorVelocityStopsAtDC1WallAndLandsWithDonorGravity) {
    auto map = std::make_unique<CDungeonMap>();
    map->map_type = 1;
    for (auto &cell : map->cells) cell.parts_no = -1;
    map->cells[0].parts_no = 0;
    map->cells[0].direction = 0;
    CFrame frame;
    CCollisionMDT collision;
    std::array<CCPolyBox, 3> mesh{};
    mesh[0].poly = Poly({-100, 0, -100}, {-100, 0, 100}, {100, 0, -100}, {0, 1, 0});
    mesh[1].poly = Poly({100, 0, -100}, {-100, 0, 100}, {100, 0, 100}, {0, 1, 0});
    mesh[2].poly = Poly({-100, -100, 10}, {0, 100, 10}, {100, -100, 10}, {0, 0, -1});
    for (int j = 0; j < 3; ++j) {
        collision.min[j] = 1000;
        collision.max[j] = -1000;
        for (auto &m : mesh) {
            m.box.min[j] = std::min({m.poly.vertex[0][j], m.poly.vertex[1][j], m.poly.vertex[2][j]});
            m.box.max[j] = std::max({m.poly.vertex[0][j], m.poly.vertex[1][j], m.poly.vertex[2][j]});
            collision.min[j] = std::min(collision.min[j], m.box.min[j]);
            collision.max[j] = std::max(collision.max[j], m.box.max[j]);
        }
    }
    collision.mesh = mesh.data();
    collision.mesh_count = 3;
    frame.flags = 1;
    frame.collision = &collision;
    map->parts[0].collision = &frame;
    sceVu0FVECTOR ray_from{0,10,0,1},ray_to{0,10,50,1},endpoint{};
    ASSERT_TRUE(RidepodDungeonTrace(*map,ray_from,ray_to,endpoint));
    EXPECT_NEAR(endpoint[2],10,1e-5); // wall clips the shot
    ray_to[2]=-50;
    ASSERT_TRUE(RidepodDungeonTrace(*map,ray_from,ray_to,endpoint));
    EXPECT_NEAR(endpoint[2],-50,1e-5); // unobstructed ray keeps its endpoint
    sceVu0FVECTOR pos{0, 0, 0, 1}, velocity{};
    ixdc2_robo_state state{};
    ixdc2_robo_output output{};
    ixdc2_robo_input input{0, 1, 0, 0, 2, 4, 0};
    MoveCheckInfo info{};
    for (int tick = 0; tick < 12; ++tick) {
        ASSERT_EQ(ixdc2_robo_walk_step(&input, &state, &output), 1);
        ASSERT_FLOAT_EQ(state.velocity[2], 4);
        for (int i = 0; i < 3; ++i) velocity[i] = state.velocity[i];
        ASSERT_TRUE(FirstPersonDungeonStep(*map, nullptr, pos, velocity, info, 1));
        EXPECT_NEAR(pos[1], 0, 1e-5);
        EXPECT_TRUE(info.landed);
        EXPECT_LE(pos[2], 5.001f);
        state.velocity[1] = ixdc2_robo_gravity(velocity[1], info.landed);
    }
    EXPECT_GT(pos[2], 4);
    float wall_z = pos[2];
    EXPECT_FLOAT_EQ(state.velocity[1], 0);
    // Same field/part collection and retail collision solver, no donor MoveCheck.
    pos[1] = 50;
    input.stick_y = 0;
    state.velocity[1] = 0;
    for (int tick = 0; tick < 20; ++tick) {
        ASSERT_EQ(ixdc2_robo_walk_step(&input, &state, &output), 1);
        for (int i = 0; i < 3; ++i) velocity[i] = state.velocity[i];
        ASSERT_TRUE(FirstPersonDungeonStep(*map, nullptr, pos, velocity, info, 1));
        state.velocity[1] = ixdc2_robo_gravity(velocity[1], info.landed);
        EXPECT_GE(state.velocity[1], -5);
    }
    EXPECT_TRUE(info.landed);
    EXPECT_NEAR(pos[1], 0, 1e-5);
    EXPECT_NEAR(pos[2], wall_z, 1e-5);
    EXPECT_FLOAT_EQ(state.velocity[1], 0);

    // R2: the cadence/scale adapter reaches the same actual DC1 wall with
    // 25 donor ticks in 60 host frames, and still stops immediately on a lock.
    pos[0] = pos[1] = pos[2] = 0;
    state = {};
    ixdc2_robo_clock clock{};
    BT_ACT_STATUS status{};
    status.can_act = 1;
    input.stick_y = 1;
    for (int frame_no = 0; frame_no < 60; ++frame_no) {
        ASSERT_TRUE(RidepodDungeonHostVelocity(input,state,output,clock,60,velocity,
            status,false,true,false));
        EXPECT_NEAR(velocity[2],4*IXDC2_ROBO_DC1_SCALE*25/60,1e-6);
        ASSERT_TRUE(FirstPersonDungeonStep(*map,nullptr,pos,velocity,info,1));
        EXPECT_LE(pos[2],5.001f);
        EXPECT_TRUE(info.landed);
        state.velocity[1] = 0;
    }
    EXPECT_EQ(clock.steps,25u);
    EXPECT_NEAR(pos[2],wall_z,1e-5);
    status.movement_locked = 1;
    EXPECT_FALSE(RidepodDungeonHostVelocity(input,state,output,clock,60,velocity,
        status,false,true,false));
    EXPECT_FLOAT_EQ(velocity[2],0);
    EXPECT_EQ(output.motion,IXDC2_ROBO_STAND);
}

TEST(FirstPersonWalk, RidepodAdapterRespectsScriptActionDeathHoldAndPadLocks) {
    PrepareInput();
    BT_ACT_STATUS status{};
    status.can_act = 1;
    ixdc2_robo_input input{0, 1, 0, 0, 2, 4, 0};
    ixdc2_robo_state state{0, {0, -3, 0}, 0, 0};
    ixdc2_robo_output output{};
    ASSERT_TRUE(RidepodDungeonVelocity(input, state, output, status, false, true, false));
    EXPECT_FLOAT_EQ(state.velocity[2], 4);
    GamePad.KeyLock(1);
    EXPECT_TRUE(PadMovementLocked());
    EXPECT_FALSE(RidepodDungeonVelocity(input, state, output, status, false, true, PadMovementLocked()));
    EXPECT_FLOAT_EQ(state.velocity[2], 0);
    GamePad.KeyLock(0);
    EXPECT_FALSE(PadMovementLocked());
    for (int block = 0; block < 7; ++block) {
        status = {};
        status.can_act = block != 0;
        status.movement_locked = block == 1;
        status.action_on = block == 2;
        status.action_busy = block == 3;
        EXPECT_FALSE(RidepodDungeonVelocity(input, state, output, status, block == 4, block != 5, block == 6));
        EXPECT_FLOAT_EQ(state.velocity[0], 0);
        EXPECT_FLOAT_EQ(state.velocity[2], 0);
        EXPECT_FLOAT_EQ(state.velocity[1], -3);
        EXPECT_EQ(output.motion, IXDC2_ROBO_STAND);
    }
}

TEST(FirstPersonWalk, RidepodEyeTaskKeepsRecoilAndInvincibilityTimersAdvancing) {
    BT_ACT_STATUS status{};
    status.recoil_frames = 2;
    status.invincible_frames = 3;
    for (int i = 1; i <= 4; ++i) {
        RidepodDungeonUpkeep(status);
        EXPECT_EQ(status.recoil_frames, std::max(0, 2 - i));
        EXPECT_EQ(status.invincible_frames, std::max(0, 3 - i));
    }
}

TEST(FirstPersonWalk, RidepodPrivateOwnerUsesNativeHurtSpheresDefenseAndNoSwordWear) {
    RidepodEnvironment env;
    auto unit=std::make_unique<CMonstorUnit>();
    // The retail class constructor initializes characters, not combat records.
    std::memset(unit->monster,0,sizeof(unit->monster));
    std::memset(unit->effect,0,sizeof(unit->effect));
    std::memset(unit->effect2,0,sizeof(unit->effect2));
    std::memset(unit->guard,0,sizeof(unit->guard));
    CCollisionData collision{};
    auto status=std::make_unique<CDngStatusData>();
    auto *old_status=UserStatus; auto *old_col=NowColData; auto *old_values=NowHitValue;
    CHitValue values[32]{};
    UserStatus=status.get(); NowColData=&collision; NowHitValue=values;
    unit->current_monster=0;
    auto &m=unit->monster[0]; m.hp=100; m.defense=7; m.steal_item=-1;
    auto &hurt=unit->effect[0];
    hurt.position[0][3]=1; hurt.radius[0]=3;
    // If owner 6 were routed to a native multiplier it would read past all six
    // slots. All six are zero, so a character owner would do zero damage here.
    sceVu0FVECTOR point{0,0,0,1};
    int hit=RidepodRecord(point,40,2,2,0);
    EXPECT_EQ(unit->CheckDmg(),MONSTER_DMG_NONE); EXPECT_EQ(m.hp,100);
    hurt.timer[0]=1;
    EXPECT_EQ(unit->CheckDmg(),MONSTER_DMG_HIT); EXPECT_EQ(m.hp,67);
    EXPECT_EQ(m.last_hit_id,60);
    auto *old_monsters=NowMonstorUnit; NowMonstorUnit=unit.get();
    RS_STACKDATA script_value{}, script_argument{};
    script_argument.type=RS_PTR; script_argument.p=&script_value;
    EXPECT_EQ(_STATUS_GET_DMG_ID(&script_argument,1),1);
    EXPECT_EQ(script_value.i,60);
    NowMonstorUnit=old_monsters;
    EXPECT_EQ(collision.active[hit],0);
    // The live NowWeaponHave is null in this headless fixture: a call into
    // SwordDmgCheck1 would dereference it. A gun uses the same native seam.
    hit=RidepodRecord(point,61,2,2,3);
    EXPECT_EQ(unit->CheckDmg(),MONSTER_DMG_HIT); EXPECT_EQ(m.hp,13);
    EXPECT_EQ(m.last_hit_id,63);
    NowMonstorUnit=unit.get();
    _STATUS_GET_DMG_ID(&script_argument,1); EXPECT_EQ(script_value.i,63);
    NowMonstorUnit=old_monsters;
    for (int attack=1; attack<=2; ++attack) {
        m.hp=100;
        hit=RidepodRecord(point,40,2,2,attack);
        EXPECT_EQ(unit->CheckDmg(),MONSTER_DMG_HIT);
        EXPECT_EQ(m.hp,67); EXPECT_EQ(m.last_hit_id,60+attack);
        NowMonstorUnit=unit.get();
        _STATUS_GET_DMG_ID(&script_argument,1);
        EXPECT_EQ(script_value.i,60+attack);
        NowMonstorUnit=old_monsters;
    }
    RidepodRecordsRetire(&collision);
    UserStatus=old_status; NowColData=old_col; NowHitValue=old_values;
}

TEST(FirstPersonWalk, RidepodGunRejectsBehindMuzzleAndSelectsForwardSphere) {
    const float muzzle[3]{0,0,0}, delta[3]{0,0,300};
    const float behind[3]{0,0,-20}, front[3]{0,0,100};
    float t=2, best=2;
    int victim=-1;
    if (RidepodSphereEntry(muzzle,delta,behind,5,t) && t<best) {best=t; victim=0;}
    if (RidepodSphereEntry(muzzle,delta,front,5,t) && t<best) {best=t; victim=1;}
    EXPECT_EQ(victim,1);
    EXPECT_NEAR(best,95.f/300,1e-6);
    EXPECT_FALSE(RidepodSphereEntry(muzzle,delta,behind,5,t));
}

TEST(FirstPersonWalk, RidepodGunAcceptsOriginInsideAndRejectsBeyondEndpoint) {
    const float muzzle[3]{0,0,0}, delta[3]{0,0,300};
    const float inside[3]{0,0,-2}, beyond[3]{0,0,310};
    float t=2;
    ASSERT_TRUE(RidepodSphereEntry(muzzle,delta,inside,5,t));
    EXPECT_FLOAT_EQ(t,0);
    EXPECT_FALSE(RidepodSphereEntry(muzzle,delta,beyond,5,t));
}

TEST(FirstPersonWalk, RidepodRetiresCombatLockAndQueuedInputOnOwnershipLoss) {
    CCollisionData pool{};
    auto *old_pool=NowColData; NowColData=&pool;
    auto &r=RidepodCombatState(); r={};
    r.initialized=true; r.arm_whp=40; r.gun_whp=62;
    r.donor.program=2; r.donor.history=1; r.pending_confirm=true;
    r.locked_target=3; r.punch_record=0; r.punch_attack=0;
    pool.active[0]=1; pool.hit[0].owner=6; pool.hit[0].attack_no=0;
    RidepodCombatMaintainOwnership(false);
    EXPECT_EQ(r.donor.program,0); EXPECT_FALSE(r.pending_confirm);
    EXPECT_EQ(r.locked_target,-1); EXPECT_EQ(r.punch_record,-1);
    EXPECT_FLOAT_EQ(r.arm_whp,40); EXPECT_FLOAT_EQ(r.gun_whp,62);
    r={}; NowColData=old_pool;
}

TEST(FirstPersonWalk, RidepodPunchExpiryDuringPauseDoesNotChargeContact) {
    CCollisionData pool{};
    auto *old_pool=NowColData; NowColData=&pool;
    auto &r=RidepodCombatState(); r={};
    r.initialized=true; r.arm_whp=40; r.punch_record=0; r.punch_attack=0;
    pool.hit[0].owner=6; pool.hit[0].attack_no=0;
    // motionDrive cleared a missed, two-frame hit while simulation was held.
    pool.active[0]=0;
    RidepodCombatObservePunch();
    EXPECT_FLOAT_EQ(r.arm_whp,40);
    EXPECT_FALSE(r.consumed);
    r={}; NowColData=old_pool;
}

TEST(FirstPersonWalk, RidepodPrivatePolicyNeedsOptInAndRegisteredGeneration) {
    RidepodScene scene;
    int hit=scene.Punch();
    ASSERT_TRUE(RidepodRecordOwned(&scene.pool,hit));
    const auto old_token=RidepodRecordToken(&scene.pool,hit);
    RidepodSetEnvironment(nullptr);
    EXPECT_FALSE(RidepodRecordOwned(&scene.pool,hit));
    RidepodSetEnvironment("1");
    scene.pool.active[hit]=0;
    float point[4]{0,0,0,1};
    int reused=scene.pool.Set(point,40,2,2,0,2,2,0,0);
    ASSERT_EQ(reused,hit);
    scene.pool.SetUserID(6,0);
    EXPECT_FALSE(RidepodRecordOwned(&scene.pool,reused));
    RidepodCombatObservePunch();
    EXPECT_FLOAT_EQ(RidepodCombatState().arm_whp,40);
    RidepodRecordCancel(&scene.pool,reused,old_token);
    EXPECT_EQ(scene.pool.active[reused],1); // stale cancellation cannot delete a new hit
}

TEST(FirstPersonWalk, RidepodContactAndGuardChargeOnceButExpiryAndCancelAreFree) {
    RidepodScene scene;
    auto &r=RidepodCombatState();
    int hit=scene.Punch();
    EXPECT_EQ(scene.unit->CheckDmg(),MONSTER_DMG_HIT);
    EXPECT_FLOAT_EQ(r.arm_whp,39); EXPECT_TRUE(r.consumed);
    RidepodCombatContact(&scene.pool,hit);
    RidepodCombatObservePunch(); EXPECT_FLOAT_EQ(r.arm_whp,39);
    scene.unit->monster[0].hp=100;
    hit=scene.Punch(1);
    auto &guard=scene.unit->guard[0]; guard.active[0]=1;
    guard.motion_start[0]=-1; guard.motion_end[0]=1000;
    EXPECT_EQ(scene.unit->CheckDmg(),MONSTER_DMG_NONE);
    EXPECT_EQ(scene.unit->monster[0].hp,100);
    EXPECT_FLOAT_EQ(r.arm_whp,38); EXPECT_EQ(scene.pool.active[hit],0);
    RidepodCombatContact(&scene.pool,hit); EXPECT_FLOAT_EQ(r.arm_whp,38);
    hit=scene.Punch(); scene.pool.active[hit]=0;
    RidepodCombatObservePunch(); EXPECT_FLOAT_EQ(r.arm_whp,38); EXPECT_FALSE(r.consumed);
    hit=scene.Punch(); RidepodCombatRetireScene();
    EXPECT_EQ(scene.pool.active[hit],0); EXPECT_FLOAT_EQ(r.arm_whp,38);
    EXPECT_FLOAT_EQ(r.gun_whp,62);
}

TEST(FirstPersonWalk, RidepodRebindsFloorCharacterPoolAndSameFloorReentry) {
    RidepodScene scene;
    auto &r=RidepodCombatState();
    for (int change=0; change<5; ++change) {
        int hit=scene.Punch();
        if (change==0) ++scene.status->cur_floor;
        if (change==1) scene.status->cur_chara=CHARA_XIAO;
        if (change==2) ++selectMapNo;
        if (change==3) {
            // Real reset barrier, same address, same floor, same monster slot.
            scene.unit->CleanViewMonstor(0);
            scene.unit->monster[0].state=2; scene.unit->monster[0].hp=100;
        }
        if (change==4) {
            CCollisionData replacement{};
            NowColData=&replacement;
            RidepodCombatMaintainOwnership(true);
            EXPECT_EQ(r.punch_record,-1); EXPECT_EQ(r.locked_target,-1);
            EXPECT_FALSE(r.pending_confirm); EXPECT_EQ(r.pool,&replacement);
            NowColData=&scene.pool;
        }
        RidepodCombatMaintainOwnership(true);
        EXPECT_EQ(r.donor.program,0); EXPECT_FALSE(r.pending_confirm);
        EXPECT_EQ(r.locked_target,-1); EXPECT_EQ(r.punch_record,-1);
        if(change!=4) EXPECT_EQ(scene.pool.active[hit],0);
        else scene.pool.active[hit]=0; // old pool was intentionally not dereferenced
        EXPECT_FLOAT_EQ(r.arm_whp,40); EXPECT_FLOAT_EQ(r.gun_whp,62);
    }
}

TEST(FirstPersonWalk, RidepodLockRejectsDeadAndReplacedActor) {
    RidepodScene scene;
    auto &r=RidepodCombatState();
    scene.Punch(); scene.unit->monster[0].hp=0;
    RidepodCombatMaintainOwnership(true); EXPECT_EQ(r.locked_target,-1);
    scene.unit->monster[0].hp=100; scene.Punch();
    ++scene.unit->monster[0].base_model;
    RidepodCombatMaintainOwnership(true); EXPECT_EQ(r.locked_target,-1);
    scene.Punch();
    // A new actor may reuse the same manager, slot, model and frame address.
    RidepodCombatActorSpawn(scene.unit.get(),0);
    RidepodCombatMaintainOwnership(true); EXPECT_EQ(r.locked_target,-1);
}

TEST(FirstPersonWalk, RidepodFrameRetiresOnPauseMenuDeathEventAndLaneLoss) {
    RidepodScene scene;
    PrepareInput();
    const auto old_status=BtActStatus;
    const auto old_task=gameTask;
    const int old_event=BtEventMode, old_drive=driveStepHold;
    const int old_monster_hold=CMonUnitHold, old_effect_hold=CEffectHold;
    BtActStatus={}; BtActStatus.can_act=1;
    BtEventMode=driveStepHold=CMonUnitHold=CEffectHold=0;
    auto &r=RidepodCombatState();
    for(int transition=0;transition<6;++transition) {
        gameTask=GAME_TASK_EYE_CAMERA; scene.status->hp[CHARA_TOAN]=100;
        BtEventMode=0;
        RidepodCombatFrame(true); ASSERT_TRUE(r.owns);
        int hit=scene.Punch();
        bool lane=true;
        if(transition==0) gameTask=GAME_TASK_PAUSE;
        if(transition==1) gameTask=GAME_TASK_MENU_OPEN;
        if(transition==2) scene.status->hp[CHARA_TOAN]=0;
        if(transition==3) BtEventMode=1;
        if(transition==4) lane=false;
        if(transition==5) BtActStatus.action_busy=1;
        RidepodCombatFrame(lane);
        EXPECT_FALSE(r.owns); EXPECT_EQ(r.locked_target,-1);
        EXPECT_EQ(r.punch_record,-1); EXPECT_EQ(scene.pool.active[hit],0);
        EXPECT_FALSE(r.pending_confirm); EXPECT_EQ(r.donor.program,0);
        EXPECT_FLOAT_EQ(r.arm_whp,40); EXPECT_FLOAT_EQ(r.gun_whp,62);
        BtActStatus.action_busy=0;
    }
    BtActStatus=old_status; gameTask=old_task; BtEventMode=old_event;
    driveStepHold=old_drive; CMonUnitHold=old_monster_hold; CEffectHold=old_effect_hold;
}

TEST(FirstPersonWalk, RidepodDefaultDamageRetainsAllSixNativeOwnersAndNoHitSentinel) {
    RidepodScene scene;
    RidepodSetEnvironment(nullptr);
    auto *old_weapon=NowWeaponHave;
    float old_position[4]; sceVu0CopyVector(old_position,CharaMain.pos);
    sceVu0FVECTOR point{0,0,0,1}; sceVu0CopyVector(CharaMain.pos,point);
    for(int owner=0;owner<6;++owner) {
        auto &weapon=scene.status->chara_weapons[owner][0];
        weapon.durability=100; weapon.durability_f=100; weapon.item_no=-1;
        scene.status->equipped_weapon_slot[owner]=0;
        scene.status->cur_chara=owner; NowWeaponHave=&weapon;
        scene.unit->monster[0].hp=100;
        scene.unit->effect[0].parameter[0][owner]=100;
        int hit=scene.pool.Set(point,40,2,2,0,2,2,0,0);
        scene.pool.SetUserID(owner,0); scene.pool.hit[hit].weapon_flags=0;
        scene.unit->CheckDmg();
        EXPECT_EQ(scene.unit->monster[0].last_hit_id,owner*10);
        int damage=owner==3 ? 57 : owner%2 ? 53 : 33;
        EXPECT_EQ(scene.unit->monster[0].hp,100-damage);
    }
    scene.unit->monster[0].hp=100; scene.unit->monster[0].item_damage_rate=100;
    int hit=scene.pool.Set(point,40,2,2,0,2,2,0,0);
    scene.pool.hit[hit].weapon_flags=0;
    scene.unit->CheckDmg();
    EXPECT_EQ(scene.unit->monster[0].last_hit_id,-1); EXPECT_EQ(scene.unit->monster[0].hp,67);
    NowWeaponHave=old_weapon; sceVu0CopyVector(CharaMain.pos,old_position);
}
#endif
