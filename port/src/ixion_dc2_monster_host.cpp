// Retail lifecycle with donor-only hooks; ps2/src/monstorunit.cpp:1847,2310.
#include "monstorunit.hpp"
#include "ridepod_combat.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "btactstatus.hpp"
#include "collisiondata.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dngmessageman.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "dungeonparts.hpp"
#include "frame.hpp"
#include "framevu1.hpp"
#include "gameutil.hpp"
#include "hitmark.hpp"
#include "hitvalue.hpp"
#include "itemdata.hpp"
#include "mathutil.hpp"
#include "menu_inventory.hpp"
#include "mglib.hpp"
#include "nowload.hpp"
#include "randomitem.hpp"
#include "rect.hpp"
#include "runscript_opcodes.hpp"
#include "savedata.hpp"
#include "shot_effect_pack.hpp"
#include "snd.hpp"
#include "stealitem.hpp"
#include "texture.hpp"
#include "userstatus.hpp"
#include "weaponelement.hpp"
#include "gameloop.hpp"
#include "ixion_dc2_monster.hpp"

PORT_OVERRIDE void CMonstorUnit::Step(int pause) {
    sceVu0FVECTOR position, destination, hit;
    CBoxVu0       box;
    sceVu0FVECTOR other_position, width_start, width_hit;
    sceVu0FVECTOR turn_position, rotation, direction;
    sceVu0FVECTOR drop_position, key_position, attachment_position, money_position;
    CheckViewLevel();

    if (pause != 0) {
        paused = true;
        return;
    }

    if (paused != 0) {
        for (int i = 0; i < 16; i++) {
            if (monster[i].state == 2) {
                monster[i].requested_motion_flags &= ~4;

                if (monster[i].requested_motion_flags == 2) {
                    chara[i][0].SetMotion(monster[i].requested_motion, 2);

                    if (!(monster[i].requested_motion_speed < 0.0f)) {
                        chara[i][0].SetMotionSpeed(monster[i].requested_motion_speed);
                    }

                    for (int j = 0; j < monster[i].attachment_count; j++) {
                        chara[i][j + 1].SetMotion(monster[i].requested_motion, 2);

                        if (!(monster[i].requested_motion_speed < 0.0f)) {
                            chara[i][j + 1].SetMotionSpeed(monster[i].requested_motion_speed);
                        }
                    }
                } else {
                    chara[i][0].SetMotion(monster[i].requested_motion, 0);

                    if (!(monster[i].requested_motion_speed < 0.0f)) {
                        chara[i][0].SetMotionSpeed(monster[i].requested_motion_speed);
                    }

                    for (int j = 0; j < monster[i].attachment_count; j++) {
                        chara[i][j + 1].SetMotion(monster[i].requested_motion, 0);

                        if (!(monster[i].requested_motion_speed < 0.0f)) {
                            chara[i][j + 1].SetMotionSpeed(monster[i].requested_motion_speed);
                        }
                    }
                }
            }
        }

        paused = false;
    }

    BtActStatus.monstor_target = -1;

    for (int i = 0; i < 16; i++) {
        if (monster[i].state == 2) {
            current_monster = i;
            ixdc2::dc1mon::Install(this,current_monster);

            if (monster[current_monster].motion_reset_pending != 0) {
                monster[current_monster].requested_motion_flags &= ~4;

                if (monster[current_monster].requested_motion_flags == 2) {
                    chara[current_monster][0].SetMotion(monster[current_monster].requested_motion, 2);

                    if (!(monster[current_monster].requested_motion_speed < 0.0f)) {
                        chara[current_monster][0].SetMotionSpeed(monster[current_monster].requested_motion_speed);
                    }

                    for (int j = 0; j < monster[current_monster].attachment_count; j++) {
                        chara[current_monster][j + 1].SetMotion(monster[current_monster].requested_motion, 2);

                        if (!(monster[current_monster].requested_motion_speed < 0.0f)) {
                            chara[current_monster][j + 1].SetMotionSpeed(monster[current_monster].requested_motion_speed);
                        }
                    }
                } else {
                    chara[current_monster][0].SetMotion(monster[current_monster].requested_motion, 0);

                    for (int j = 0; j < monster[current_monster].attachment_count; j++) {
                        chara[current_monster][j + 1].SetMotion(monster[current_monster].requested_motion, 0);

                        if (!(monster[current_monster].requested_motion_speed < 0.0f)) {
                            chara[current_monster][j + 1].SetMotionSpeed(monster[current_monster].requested_motion_speed);
                        }
                    }
                }

                monster[current_monster].motion_reset_pending = false;
            }

            if (monster[current_monster].revealed != 0) {
                if (back_dungeon != 0) {
                    monster[current_monster].anger_timer = 180;
                }

                WorkBuffer__2->Reset();
                monster[current_monster].collision_poly = (CCPoly *) WorkBuffer__2->Alloc(2000);
                chara[current_monster][0].GetPosition(position);
                monster[current_monster].collision_poly_count = setCollisionData(NowDngMap, monster[current_monster].collision_poly, position, 30.0f, 5.0f);
                int original_count = monster[current_monster].collision_poly_count;
                box.max[0] = 30.0f + position[0];
                box.max[1] = 80.0f + position[1];
                box.max[2] = 30.0f + position[2];
                box.min[0] = position[0] - 30.0f;
                box.min[1] = position[1] - 80.0f;
                box.min[2] = position[2] - 30.0f;

                for (int j = 0; j < 16; j++) {
                    if (j != current_monster && monster[j].state == 2) {
                        chara[j][0].GetPosition(other_position);
                        collision->SetPosition(other_position);
                        float scale = 2.0f * (0.1f * monster[j].collision_radius);
                        collision->SetScale(scale, scale, scale);
                        monster[current_monster].collision_poly_count += collision->PickUpNearPoly(monster[current_monster].collision_poly + monster[current_monster].collision_poly_count, box);
                    }
                }

                if (monster[current_monster].collision_poly_count >= 400) {
                    printf("err %d\n", monster[current_monster].collision_poly_count);
                }

                ixdc2::dc1mon::BeforeDamage(this,current_monster);
                int dc2_damage = CheckDmg();
                ixdc2::dc1mon::AfterDamage(this,current_monster,dc2_damage);
                switch (dc2_damage) {
                    case MONSTER_DMG_NONE:
                        break;
                    case MONSTER_DMG_HIT:
                        interpreter[current_monster].run(ixdc2::dc1mon::IsDonor(this,current_monster)?500:110);
                        script_state[current_monster] = 1;
                        break;
                    case MONSTER_DMG_KILLED:
                        interpreter[current_monster].run(ixdc2::dc1mon::IsDonor(this,current_monster)?1000:120);
                        script_state[current_monster] = 1;
                        break;
                }

                if (monster[current_monster].stop_timer > 0) {
                    PalletStep();

                    if (monster[current_monster].invincible_timer > 0) {
                        monster[current_monster].invincible_timer--;
                    }
                } else {
                    if (ixdc2::dc1mon::Tick(this,current_monster)) {
                    if (script_state[current_monster] == 0) {
                        if (monster[current_monster].revealed == 1) {
                            interpreter[current_monster].run(50);
                            monster[current_monster].revealed = -1;
                        } else {
                            interpreter[current_monster].run(ixdc2::dc1mon::IsDonor(this,current_monster)?200:100);
                        }

                        script_state[current_monster] = 1;
                    } else {
                        interpreter[current_monster].resume();

                        if (interpreter[current_monster].IsEnd() != 0) {
                            script_state[current_monster] = 0;
                        }
                    }

                    } // donor interpreter resumes at 25 Hz; native cadence is unchanged
                    chara[current_monster][0].GetPosition(position);

                    if (!(monster[current_monster].knockback[0] <= 0.0f)) {
                        sceVu0CopyVector(monster[current_monster].movement, monster[current_monster].knockback_direction);
                        monster[current_monster].movement_speed = monster[current_monster].knockback[0];
                        monster[current_monster].knockback[0] -= monster[current_monster].knockback[1];

                        if (monster[current_monster].knockback[0] <= 0.0f) {
                            monster[current_monster].knockback[0] = 0.0f;
                            monster[current_monster].movement_speed = 0.0f;
                        }
                    }

                    if (monster[current_monster].collision_off_timer <= 0) {
                        MoveCheck2();
                    }

                    if (monster[current_monster].collision_off_timer <= 0) {
                        MoveChecMonster();
                    }

                    monster[current_monster].collision_poly_count = original_count;

                    if (monster[current_monster].movement_speed != 0.0f || (monster[current_monster].falls != 0 && monster[current_monster].movement_speed == 0.0f)) {
                        destination[0] = position[0] + 10.0f * monster[current_monster].movement[0];
                        destination[1] = position[1] + 10.0f * monster[current_monster].movement[1];
                        destination[2] = position[2] + 10.0f * monster[current_monster].movement[2];
                        position[1] += 5.0f;
                        destination[1] += 5.0f;

                        if (CheckHit(monster[current_monster].collision_poly, monster[current_monster].collision_poly_count, position, destination, hit, 0, 0) >= 0 && monster[current_monster].collision_off_timer <= 0) {
                            position[1] -= 5.0f;
                            monster[current_monster].movement_speed = 0.0f;
                            chara[current_monster][0].SetPosition(position);
                        } else {
                            position[1] -= 5.0f;

                            if (monster[current_monster].falls != 0 && !(monster[current_monster].ground_distance <= 0.0001f)) {
                                destination[0] = position[0] + monster[current_monster].movement[0] * monster[current_monster].movement_speed;
                                destination[1] = position[1] + (monster[current_monster].movement[1] * monster[current_monster].movement_speed - 0.2f);
                                destination[2] = position[2] + monster[current_monster].movement[2] * monster[current_monster].movement_speed;
                            } else {
                                destination[0] = position[0] + monster[current_monster].movement[0] * monster[current_monster].movement_speed;
                                destination[1] = position[1] + monster[current_monster].movement[1] * monster[current_monster].movement_speed;
                                destination[2] = position[2] + monster[current_monster].movement[2] * monster[current_monster].movement_speed;
                            }

                            chara[current_monster][0].SetPosition(destination);

                            if (monster[current_monster].falls != 0 && !(monster[current_monster].ground_distance <= 0.0001f)) {
                                monster[current_monster].movement_speed = DistVector(destination, position);
                                destination[0] -= position[0];
                                destination[1] -= position[1];
                                destination[2] -= position[2];
                                sceVu0Normalize(monster[current_monster].movement, destination);
                            }
                        }

                        chara[current_monster][0].GetPosition(width_start);
                        width_start[1] += 5.0f;

                        if (CheckWidth(monster[current_monster].collision_poly, monster[current_monster].collision_poly_count, width_start, monster[current_monster].body_radius, width_hit, 0) != 0) {
                            width_hit[1] -= 5.0f;
                            chara[current_monster][0].SetPosition(width_hit);
                        }
                    }

                    monster[current_monster].ground_distance = 0.0f;
                    chara[current_monster][0].GetPosition(position);
                    position[1] += 10.0f;
                    float ground_depth = -130.0f;

                    if (CheckHitVertical(monster[current_monster].collision_poly, monster[current_monster].collision_poly_count, position, ground_depth, hit, 0) >= 0) {
                        position[1] -= 10.0f;

                        if (position[1] < hit[1]) {
                            position[1] = hit[1];

                            if (monster[current_monster].falls != 0) {
                                monster[current_monster].movement[1] *= -1.0f;

                                if (!(monster[current_monster].movement[1] <= 0.0f)) {
                                    monster[current_monster].movement_speed *= 0.5f * (2.0f - monster[current_monster].movement[1]);

                                    if (monster[current_monster].movement_speed < 0.2f) {
                                        monster[current_monster].movement_speed = 0.0f;
                                    }
                                }
                            }
                        }

                        monster[current_monster].ground_distance = position[1] - hit[1];
                        monster[current_monster].ground_y = hit[1];

                        if (monster[current_monster].falls != 0) {
                            chara[current_monster][0].SetPosition(position);

                            if (monster[current_monster].free_fall == 0) {
                                chara[current_monster][0].SetPosition(hit);
                            }
                        }
                    }

                    if (monster[current_monster].turn_speed != 0.0f) {
                        chara[current_monster][0].GetPosition(turn_position);
                        chara[current_monster][0].GetRotation(rotation);
                        sceVu0SubVector(direction, monster[current_monster].turn_target, turn_position);
                        float angle = atan2f(direction[0], direction[2]);
                        rotation[1] = AngleInterpolate(rotation[1], angle, monster[current_monster].turn_speed, INTERPOLATE_STEP);
                        chara[current_monster][0].SetRotation(rotation);

                        if (AngleCmp(rotation[1], angle, 0.052359879f) == 0) {
                            monster[current_monster].turn_speed = 0.0f;
                        }
                    }

                    if (monster[current_monster].invincible_timer > 0) {
                        monster[current_monster].invincible_timer--;
                    }

                    if (monster[current_monster].collision_off_timer > 0) {
                        monster[current_monster].collision_off_timer--;
                    }

                    if (event[current_monster].timer == 2) {
                        NowShotEffect->Set(monster[current_monster].shot_effect, event[current_monster].position, event[current_monster].local_position);
                        NowShotEffect->SetUserID2(current_monster);

                        if (event[current_monster].damage_override != -1) {
                            NowShotEffect->SetDmg(event[current_monster].damage_override);
                        }

                        event[current_monster].timer = 0;
                    }

                    if (event2[current_monster].timer == 2) {
                        NowShotEffect->Set(monster[current_monster].shot_effect2, event2[current_monster].position, event2[current_monster].local_position);
                        NowShotEffect->SetUserID2(current_monster);

                        if (event2[current_monster].damage_override != -1) {
                            NowShotEffect->SetDmg(event2[current_monster].damage_override);
                        }

                        event2[current_monster].timer = 0;
                    }

                    PalletStep();
                    SoundCheck();

                    ixdc2::dc1mon::AfterStep(this,current_monster);
                    if (monster[current_monster].state == -1) {
                        int dc2_xp_before=ixdc2::dc1mon::IsDonor(this,current_monster)?ixdc2::dc1mon::WeaponXp():0;
                        CDngStatusData *status = (CDngStatusData *) UserStatus;
                        int             current_chara = UserStatus->cur_chara;
                        int             no_exp;
                        WEAPON_HAVE    *weapon = &status->chara_weapons[current_chara][status->equipped_weapon_slot[current_chara]];
                        no_exp = 0;

                        if (status->CheckDefaultWeapon(current_chara) == 0) {
                            no_exp = 1;
                        }

                        if (weapon->item_no == ITEM_WEAPON_SERPENT_SWORD && SaveData->GetGameFlag(0x30) == 0) {
                            no_exp = 1;
                        }

                        if (monster[current_monster].last_attacker == current_chara && no_exp == 0) {
                            int max_exp = GetWeaponMaxExp(weapon);
                            int exp = monster[current_monster].exp;

                            if (back_dungeon != 0) {
                                exp *= 2;
                            }

                            if (effect[current_monster].hit_attributes & 0x2000) {
                                exp *= 1.2f;
                            }

                            if (UserStatus->res_limit_zone_current == RES_LIMIT_ZONE_NO_WEAPON_CHANGE) {
                                weapon->experience -= exp;

                                if (weapon->experience <= 0) {
                                    weapon->experience = 0;
                                }
                            } else if (weapon->experience < max_exp) {
                                int updated = weapon->experience + exp;

                                if (updated >= max_exp) {
                                    weapon->experience = max_exp;
                                    DngMessMan.insert_mes_1 = GetCommonItemDataSystemMsg(weapon->item_no);
                                    DngMessMan.insert_value_1 = weapon->level;
                                    DngMessMan.message = 150;
                                    DngMessMan.timer = 480;
                                    DngMessMan.steev_window = false;
                                    weapon->experience = max_exp;
                                } else {
                                    weapon->experience = updated;
                                }
                            }
                        }

                        if (effect[current_monster].hit_attributes & 0x10) {
                            UserStatus->AddDrink(current_chara, 10, 255.0f);
                        }

                        if (monster[current_monster].stolen_money > 0) {
                            chara[current_monster][0].GetPosition(drop_position);
                            drop_position[0] += 1.5f;
                            drop_position[1] -= monster[current_monster].ground_distance;
                            drop_position[0] += 1.5f;
                            RandomItem->Set(drop_position, 1, monster[current_monster].stolen_money, -1);
                            monster[current_monster].money_chance = 0;
                        }

                        if (monster[current_monster].kind != MONSTER_KIND_NO_LOCK_ON && monster[current_monster].drops_items != 0) {
                            printf("************ item = %d ************ \n", monster[current_monster].drop_item);

                            if (monster[current_monster].drop_item == -1 && monster[current_monster].rare_item != -1 && (100.0f * (float) rand() / 2147483648.0f) < 10.0f) {
                                monster[current_monster].drop_item = monster[current_monster].rare_item;
                            }

                            if (monster[current_monster].drop_item != -1) {
                                if (SetGateKeyStack(monster[current_monster].drop_item) != 0) {
                                    chara[current_monster][0].GetPosition(key_position);
                                    key_position[1] -= monster[current_monster].ground_distance;
                                    RandomItem->Set(key_position, 1, -1, monster[current_monster].drop_item);
                                }
                            } else {
                                int attachment = SelectAttachi();

                                if (monster[current_monster].last_attacker == -1 && attachment != -1) {
                                    chara[current_monster][0].GetPosition(attachment_position);
                                    attachment_position[1] -= monster[current_monster].ground_distance;
                                    RandomItem->Set(attachment_position, 1, -1, attachment);
                                    monster[current_monster].drop_item = attachment;
                                } else {
                                    monster[current_monster].drop_item = -1;
                                }
                            }

                            if (monster[current_monster].drop_item == -1) {
                                if ((int) (100.0f * (float) rand() / 2147483648.0f) < monster[current_monster].money_chance) {
                                    int index = current_monster;
                                    int base = monster[index].money;
                                    int money = base + (int) (((float) base * (float) rand() / 2.0f) / 2147483648.0f);
                                    int attributes = effect[index].hit_attributes;

                                    if (attributes & 2) {
                                        money *= 2;
                                    }

                                    if ((attributes & 4) && money >= 2) {
                                        money *= 0.5;
                                    }

                                    chara[index][0].GetPosition(money_position);
                                    money_position[1] -= monster[current_monster].ground_distance;
                                    RandomItem->Set(money_position, 1, money, -1);
                                }
                            }
                        }
                        ixdc2::dc1mon::Reward(this,current_monster,dc2_xp_before);
                    }
                }
            }
        }
    }
}

PORT_OVERRIDE void CMonstorUnit::CleanViewMonstor(int back_floor) {
#ifdef IXION_HAS_DC2_RIDEPOD
    // Runs before replacement/reentry, including the same floor and pool address.
    RidepodCombatRetireScene();
#endif
    ixdc2::dc1mon::Reset(this);
    for (int i = 0; i < 16; i++) {
        monster[i].state = -1;
        monster[i].stop_timer = 0;
        monster[i].poison_timer = 0;
        monster[i].anger_timer = 0;
        monster[i].slow_timer = 0;
        monster[i].collision_poly_count = 0;
        monster[i].movement[2] = 0;
        monster[i].movement[1] = 0;
        monster[i].movement[0] = 0;
        monster[i].turn_target[2] = 0;
        monster[i].turn_target[1] = 0;
        monster[i].turn_target[0] = 0;
        monster[i].movement_speed = 0;
        monster[i].turn_speed = 0;
        monster[i].falls = true;
        monster[i].body_radius = 13.0f;
        monster[i].collision_radius = 13.0f;
        monster[i].unk_094 = 0;
        monster[i].invincible_timer = 0;
        monster[i].drop_item = -1;
        monster[i].clip_distance = 300.0f;
        monster[i].collision_off_timer = 0;
        monster[i].shot_effect = -1;
        monster[i].shot_effect2 = -1;
        monster[i].last_hit_damage = -1;
        monster[i].shadow_length = 1.0f;
        monster[i].lockon_frame = 0;
        monster[i].lockon_scale_x = 1.0f;
        monster[i].lockon_scale_y = 1.0f;
        monster[i].lock_range = 120.0f;
        monster[i].lockon_enabled = true;
        monster[i].revealed = -1;
        monster[i].steal_item = -1;
        monster[i].stolen_money = 0;
        monster[i].event_flag2 = -1;
        monster[i].event_flag2_pending = 0;
        monster[i].palette_alpha = 128.0f;
        monster[i].palette_alpha_step = 0;
        monster[i].palette_delay = 0;
        monster[i].palette_cycles = 0;
        monster[i].free_fall = 0;
        monster[i].palette_override_pending = false;
        monster[i].motion_reset_pending = false;
        monster[i].view_held = false;
        monster[i].attachment_count = 0;
        monster[i].shadow_visible = true;
        monster[i].shadow_enabled = true;
        monster[i].knockback_direction[0] = 0;
        monster[i].knockback_direction[1] = 0;
        monster[i].knockback_direction[2] = 0;
        monster[i].knockback_direction[3] = 1;
        monster[i].knockback[0] = 0;
        monster[i].knockback[1] = 0;
        monster[i].knockback[2] = 1;
        script_state[i] = 0;

        if (script[i] != 0) {
            script[i]->used = 0;
        }

        for (int j = 0; j < 16; j++) {
            effect[i].timer[j] = 0;
            effect[i].motion_start[j] = 0;
        }

        for (int j = 0; j < 16; j++) {
            effect2[i].active[j] = 0;
        }

        for (int j = 0; j < 12; j++) {
            effect3[i].frame[j] = 0;
            effect3[i].timer[j] = 0;
            effect3[i].count = 0;
        }

        for (int j = 0; j < 16; j++) {
            sound[i].id[j] = -1;
            sound[i].cooldown[j] = 0;
        }

        sound[i].sequence_id = -1;
        event[i].frame = 0;
        event[i].timer = 0;
        event2[i].frame = 0;
        event2[i].timer = 0;
    }

    back_dungeon = back_floor;
    alive_count = 0;
}
