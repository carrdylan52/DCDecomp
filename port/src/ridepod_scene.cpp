// Retail actor creation with a private lock-identity notification.
#include "monstorunit.hpp"
#include "dataalloc.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "runscript_opcodes.hpp"
#include "userstatus.hpp"
#include "ridepod_combat.hpp"

PORT_OVERRIDE int CMonstorUnit::SetupViewMonstor(int model_no, float *position, int event_flag) {
    current_monster = -1;

    for (int i = 0; i < 16; i++) {
        if (monster[i].state == -1) {
            current_monster = i;
            break;
        }
    }

    if (current_monster == -1) {
        return 0;
    }

    // RIDEPOD_BEGIN
#ifdef IXION_HAS_DC2_RIDEPOD
    RidepodCombatActorSpawn(this,current_monster);
#endif
    // RIDEPOD_END
    script[current_monster]->Reset();
    BtSetEventScript(&interpreter[current_monster], script_data[model_no], script[current_monster]);
    chara[current_monster][0] = base_chara[model_no][0];
    chara[current_monster][0].motion[0] = &chara[current_monster][0].motion_type;
    chara[current_monster][0].SetPosition(position);
    chara[current_monster][0].SetRotation(0.0f, 0.0f, 0.0f);

    if (UserStatus->cur_georama == 3 && UserStatus->cur_floor == 17 && current_monster == 1) {
        InitBee(chara[1][0].frame, 15);
    }

    monster[current_monster].attachment_count = 0;

    for (int j = 0; j < 3; j++) {
        if (model[model_no].model_name[j + 1][0] != 0) {
            chara[current_monster][j + 1] = base_chara[model_no][j + 1];
            chara[current_monster][j + 1].motion[0] = &chara[current_monster][j + 1].motion_type;
            chara[current_monster][j + 1].frame->SetParent(chara[current_monster][0].frame);
            monster[current_monster].attachment_count++;
        }
    }

    monster[current_monster].state = 1;
    monster[current_monster].base_model = model_no;
    monster[current_monster].max_hp = model[model_no].max_hp;
    monster[current_monster].hp = model[model_no].max_hp;
    monster[current_monster].attachment_kind = model[model_no].attachment_kind;

    for (int j = 0; j < 5; j++) {
        monster[current_monster].attachment_weight[j] = model[model_no].attachment_weight[j];
    }

    monster[current_monster].defense = model[model_no].defense;
    monster[current_monster].hardness = model[model_no].hardness;
    monster[current_monster].money = model[model_no].money;
    monster[current_monster].money_chance = model[model_no].money_chance;
    monster[current_monster].kind = model[model_no].kind;
    monster[current_monster].name_no = model[model_no].name_no;
    monster[current_monster].body_radius = model[model_no].collision_radius;
    monster[current_monster].collision_radius = model[model_no].collision_radius;
    monster[current_monster].shot_effect = model[model_no].shot_effect[0];
    monster[current_monster].shot_effect2 = model[model_no].shot_effect[1];
    monster[current_monster].exp = model[model_no].exp;
    monster[current_monster].steal_item = model[model_no].steal_item;
    monster[current_monster].drops_items = model[model_no].drops_items;
    monster[current_monster].item_damage_rate = model[model_no].item_damage_rate;
    monster[current_monster].status_chance = model[model_no].status_chance;
    monster[current_monster].rare_item = model[model_no].rare_item;
    monster[current_monster].event_flag2 = event_flag;
    monster[current_monster].knockback[2] = model[model_no].knockback_scale;

    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 6; j++) {
            effect[current_monster].parameter[i][j] = model[model_no].effect_parameter[j];
        }
    }

    if (model[model_no].attachment_kind == 8) {
        monster[current_monster].revealed = 0;

        switch (model[model_no].kind) {
            case MONSTER_KIND_MIMIC_SMALL:
                NowDngMap->SetMimicEvent(position[0], position[1], position[2], current_monster, TREASURE_BOX_SMALL);
                break;
            case MONSTER_KIND_MIMIC_LARGE:
                NowDngMap->SetMimicEvent(position[0], position[1], position[2], current_monster, TREASURE_BOX_LARGE);
                break;
        }
    } else {
        monster[current_monster].revealed = -1;
    }

    for (int i = 0; i < 3; i++) {
        event_flags[current_monster][i] = 0;
    }

    if (interpreter[current_monster].check_program(1) != 0) {
        interpreter[current_monster].run(1);
    }

    script_state[current_monster] = 0;
    alive_count++;
    return 1;
}
