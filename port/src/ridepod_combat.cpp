#include "ridepod_combat.hpp"
#ifdef IXION_HAS_DC2_RIDEPOD
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "ridepod_visual.hpp"
#include "ridepod_records.hpp"
#include "ridepod_hitscan.hpp"
#include "gameloop.hpp"
#include "first_person_walk.hpp"
#include "collisiondata.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "gamepad.hpp"
#include "monstorunit.hpp"
#include "platform/clock.hpp"
#include "userstatus.hpp"
#include "btactstatus.hpp"
#include "gamepad_port.hpp"
extern int lockOnTargetNo;
extern int gameTask;

namespace {
RidepodCombatRuntime runtime;
auto &state = runtime.donor;
auto &output = runtime.output;
auto &initialized = runtime.initialized;
auto &pending_confirm = runtime.pending_confirm;
auto &consumed = runtime.consumed;
auto &arm_whp = runtime.arm_whp;
auto &gun_whp = runtime.gun_whp;
auto &punch_record = runtime.punch_record;
auto &punch_attack = runtime.punch_attack;
auto &locked_target = runtime.locked_target;
auto &punch_serial = runtime.punch_serial;
int Record(float *point, int damage, float radius, int life, int attack) {
    return RidepodRecord(point,damage,radius,life,attack);
}
void Gun(float *muzzle, float yaw, int damage, int target) {
    sceVu0FVECTOR end = {muzzle[0]+sinf(yaw)*300,muzzle[1],muzzle[2]+cosf(yaw)*300,1};
    if (target >= 0 && target < 16 && NowMonstorUnit->monster[target].hp > 0) {
        // Native lock-on identity, aim at its first live hurt sphere.
        for (int s=0;s<16;++s) if (NowMonstorUnit->effect[target].timer[s]) {
            sceVu0CopyVector(end,NowMonstorUnit->effect[target].position[s]); break;
        }
    }
    sceVu0FVECTOR clipped;
    if (!RidepodDungeonTrace(*NowDngMap,muzzle,end,clipped)) return;
    float d[3], length2=0;
    for (int j=0;j<3;++j) { d[j]=clipped[j]-muzzle[j]; length2+=d[j]*d[j]; }
    float best=2;
    int victim=-1;
    if (length2>1e-6f) for (int m=0;m<16;++m) {
        if (NowMonstorUnit->monster[m].state != 2 || NowMonstorUnit->monster[m].hp <= 0) continue;
        auto &hurt=NowMonstorUnit->effect[m];
        for (int s=0;s<16;++s) if (hurt.timer[s]) {
            float r=hurt.radius[s]+2*IXDC2_ROBO_DC1_SCALE;
            float t;
            if (RidepodSphereEntry(muzzle,d,hurt.position[s],r,t) && t<best) { best=t; victim=m; }
        }
    }
    int record=-1;
    if (victim>=0) {
        sceVu0FVECTOR point;
        for (int j=0;j<3;++j) point[j]=muzzle[j]+d[j]*std::min(1.f,best+.0001f);
        point[3]=1;
        record=Record(point,damage,2*IXDC2_ROBO_DC1_SCALE,2,3);
    }
    std::fprintf(stderr,"ixdc2 shot frame=%lld serial=%llu muzzle=(%.4f,%.4f,%.4f) lock=%d victim=%d record=%d raw=%d gun_whp=%.1f\n",
        static_cast<long long>(GameFrameCount()),static_cast<unsigned long long>(state.serial),
        muzzle[0],muzzle[1],muzzle[2],target,victim,record,damage,gun_whp);
}
}
RidepodCombatRuntime &RidepodCombatState() { return runtime; }
void RidepodCombatRetireScene() {
    if (runtime.owns && (locked_target>=0 || punch_record>=0 || state.program || pending_confirm))
        std::fprintf(stderr,"ixdc2 retire frame=%lld lock=%d punch=%d program=%d pending=%d arm_whp=%.1f gun_whp=%.1f\n",
            static_cast<long long>(GameFrameCount()),locked_target,punch_record,state.program,
            pending_confirm,arm_whp,gun_whp);
    RidepodRecordsRetire(NowColData);
    const bool had_weapons=initialized;
    const float arm=arm_whp, gun=gun_whp;
    runtime={};
    // Durability belongs to the vehicle, transient combat belongs to this scene.
    initialized=had_weapons; arm_whp=arm; gun_whp=gun;
    RidepodVisualCombatPose(-1,0,false);
}
void RidepodCombatMaintainOwnership(bool owns) {
    owns = owns && RidepodEnabled() && NowColData && NowMonstorUnit && NowDngMap && UserStatus;
    if (!owns) { RidepodCombatRetireScene(); return; }
    if (!runtime.owns || runtime.pool!=NowColData || runtime.monsters!=NowMonstorUnit ||
        runtime.map!=NowDngMap || runtime.dungeon!=selectMapNo ||
        runtime.floor!=UserStatus->cur_floor || runtime.character!=UserStatus->cur_chara) {
        RidepodCombatRetireScene();
        runtime.owns=true; runtime.pool=NowColData; runtime.monsters=NowMonstorUnit;
        runtime.map=NowDngMap; runtime.dungeon=selectMapNo;
        runtime.floor=UserStatus->cur_floor; runtime.character=UserStatus->cur_chara;
    }
    if (locked_target>=0) {
        auto &target=NowMonstorUnit->monster[locked_target];
        if (target.state!=2 || target.hp<=0 || target.base_model!=runtime.locked_model ||
            NowMonstorUnit->chara[locked_target][0].frame!=runtime.locked_frame)
            locked_target=-1;
    }
}
void RidepodCombatFrame(bool lane_active) {
    RidepodCombatMaintainOwnership(lane_active && UserStatus && BtEventMode == 0 &&
        gameTask == GAME_TASK_EYE_CAMERA && !driveStepHold && !CMonUnitHold &&
        !CEffectHold && !PadMovementLocked() && !BtActStatus.movement_locked &&
        BtActStatus.can_act && !BtActStatus.action_on && !BtActStatus.action_busy &&
        UserStatus->CheckLife() > 0);
}
void RidepodCombatActorSpawn(CMonstorUnit *unit, int slot) {
    // SetupViewMonstor can reuse a model/frame without a floor reset. The old
    // actor's lock is never retained across that successful allocation.
    if (runtime.monsters==unit && locked_target==slot) {
        locked_target=-1; runtime.locked_frame=nullptr; runtime.locked_model=-1;
    }
}
void RidepodCombatContact(CCollisionData *pool, int record) {
    if (!RidepodRecordOwned(pool,record)) return;
    const uint64_t token=RidepodRecordToken(pool,record);
    if (pool==runtime.pool && record==punch_record && token==runtime.punch_token && !consumed) {
        arm_whp=std::max(0.f,arm_whp-1);
        consumed=true; punch_record=-1; runtime.punch_token=0;
        std::fprintf(stderr,"ixdc2 contact frame=%lld token=%llu attack=%d arm_whp=%.1f\n",
            static_cast<long long>(GameFrameCount()),static_cast<unsigned long long>(token),
            pool->hit[record].attack_no,arm_whp);
    }
    // Guard contact retires the same hit too; a second hurt sphere cannot charge it.
    RidepodRecordCancel(pool,record,token);
}
void RidepodCombatObservePunch() {
    // Expiry, cancellation and reuse are not contact. Set invalidates generations.
    if (punch_record>=0 && (!runtime.punch_token ||
        RidepodRecordToken(NowColData,punch_record)!=runtime.punch_token)) {
        RidepodRecordCancel(NowColData,punch_record,runtime.punch_token);
        punch_record=-1; runtime.punch_token=0;
    }
}
void RidepodCombatStep(const float position[4], float yaw, int motion, bool tick, bool allowed) {
    RidepodCombatMaintainOwnership(allowed);
    if (!runtime.owns) return;
    if (!RidepodVisualReady()) RidepodVisualStep(position,yaw,motion);
    ixdc2_robo_combat_input input{};
    if (!RidepodVisualCombatInput(input)) { RidepodCombatRetireScene(); return; }
    if (!initialized) { arm_whp=input.arm_whp; gun_whp=input.gun_whp; initialized=true; }
    pending_confirm |= allowed && GamePad.Down(PadInput_OK);
    bool fire = allowed && GamePad.On(PAD_SQUARE);
    if (allowed && GamePad.Down(PAD_R1)) {
        if (locked_target>=0) { LockOffTargte(); locked_target=-1; }
        else {
            // The eye-task bridge lacks PLAY's target-facing update. Face the
            // nearest eligible actor, then let native range/cone rules choose.
            int nearest=-1; float best=1e20f;
            for(int m=0;m<16;++m) {
                auto &candidate=NowMonstorUnit->monster[m];
                if(candidate.state!=2 || candidate.hp<=0 || !candidate.revealed || !candidate.lockon_enabled) continue;
                float p[4]; NowMonstorUnit->chara[m][0].GetPosition(p);
                float d=(p[0]-position[0])*(p[0]-position[0])+(p[2]-position[2])*(p[2]-position[2]);
                if(d<best){best=d;nearest=m;}
            }
            if(nearest>=0) {
                float p[4]; NowMonstorUnit->chara[nearest][0].GetPosition(p);
                yaw=std::atan2(p[0]-position[0],p[2]-position[2]);
                CharaMain.SetRotation(0,yaw,0);
            }
            SetNearLockOnTarget(0,1); locked_target=lockOnTargetNo;
            if (locked_target>=0) {
                runtime.locked_frame=NowMonstorUnit->chara[locked_target][0].frame;
                runtime.locked_model=NowMonstorUnit->monster[locked_target].base_model;
            }
            std::fprintf(stderr,"ixdc2 lock frame=%lld nearest=%d native_target=%d yaw=%.4f\n",
                static_cast<long long>(GameFrameCount()),nearest,lockOnTargetNo,yaw);
            // Keep native target eligibility/identity without its human-sized
            // lock camera, which clips through the imported vehicle.
            LockOffTargte();
        }
    }
    if (locked_target>=0) {
        auto &target=NowMonstorUnit->monster[locked_target];
        if (target.state!=2 || target.hp<=0) locked_target=-1;
    }
    // Retire expired/reused records; only the damage seam reports contact.
    RidepodCombatObservePunch();
    if (!allowed) {
        pending_confirm=false; state={}; output={}; output.attack=-1;
    } else if (tick) {
        input.confirm=pending_confirm; pending_confirm=false;
        input.fire=fire; input.attack_type=fire ? 30 : 0;
        input.arm_whp=arm_whp; input.gun_whp=gun_whp;
        ixdc2_robo_combat_step(&input,&state,&output);
    }
    RidepodVisualCombatPose(output.attack,output.frame,fire && state.program==0);
    RidepodVisualStep(position,yaw,motion);
    if(locked_target>=0) {
        auto &hurt=NowMonstorUnit->effect[locked_target];
        for(int s=0;s<16;++s) if(hurt.timer[s]) {
            RidepodVisualAim(hurt.position[s],output.attack==1); break;
        }
    }
    if (!allowed || !output.punch || punch_serial!=state.serial) {
        if (punch_record>=0) RidepodRecordCancel(NowColData,punch_record,runtime.punch_token);
        punch_record=-1;
        runtime.punch_token=0;
        if (punch_serial!=state.serial) { consumed=false; punch_serial=state.serial; }
    }
    if (allowed && output.punch && !consumed) {
        sceVu0FVECTOR point;
        if (RidepodVisualMuzzle(output.attack==1,point)) {
            if (punch_record<0) {
                punch_attack=output.attack;
                punch_record=Record(point,int(std::ceil(output.damage)),output.radius*IXDC2_ROBO_DC1_SCALE,2,punch_attack);
                runtime.punch_token=RidepodRecordToken(NowColData,punch_record);
                std::fprintf(stderr,"ixdc2 punch frame=%lld serial=%llu attack=%d key=%.3f record=%d raw=%.1f point=(%.3f,%.3f,%.3f) arm_whp=%.1f\n",
                    static_cast<long long>(GameFrameCount()),static_cast<unsigned long long>(state.serial),
                    output.attack,output.frame,punch_record,output.damage,point[0],point[1],point[2],arm_whp);
                for(int m=0;m<16;++m) if(NowMonstorUnit->monster[m].state==2) {
                    auto &hurt=NowMonstorUnit->effect[m];
                    for(int s=0;s<16;++s) if(hurt.timer[s]) {
                        std::fprintf(stderr,"ixdc2 target monster=%d hp=%d sphere=(%.3f,%.3f,%.3f) radius=%.3f lock=%d\n",
                            m,NowMonstorUnit->monster[m].hp,hurt.position[s][0],hurt.position[s][1],hurt.position[s][2],hurt.radius[s],locked_target);
                        break;
                    }
                }
            } else {
                sceVu0CopyVector(NowColData->hit[punch_record].pos,point);
                NowColData->hit[punch_record].life=2;
            }
        }
    }
    if (allowed && tick && output.shot) {
        sceVu0FVECTOR muzzle;
        if (RidepodVisualMuzzle(output.muzzle,muzzle)) {
            gun_whp=std::max(0.f,gun_whp-1);
            Gun(muzzle,yaw,int(std::ceil(output.damage)),locked_target);
        }
    }
}
#endif
