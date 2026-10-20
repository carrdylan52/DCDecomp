float unitRotation(mgCFrame *frame, float target, float speed) {
    float rot[4];
    float diff;
    float abs_diff;

    frame->GetRotation(rot);
    diff = target - rot[1];
    abs_diff = diff;

    if (diff <= 0.0f) {
        abs_diff = -1.0f * diff;
    }

    if (abs_diff <= PI) {
        if (abs_diff <= PI / speed) {
            diff = 0.0f;
        }
    } else if (TWO_PI - abs_diff <= PI / speed) {
        diff = 0.0f;
    }

    if (!(diff <= 0.0f)) {
        if (diff <= PI) {
            rot[1] += PI / (2.0f * speed);
        } else {
            rot[1] -= PI / speed;
        }
    }

    if (diff < 0.0f) {
        if (!(diff < -PI)) {
            rot[1] -= PI / (2.0f * speed);
        } else {
            rot[1] += PI / speed;
        }
    }

    if (diff == 0.0f) {
        rot[1] = target;
    }

    if (rot[1] <= -PI) {
        rot[1] += TWO_PI;
    }

    if (!(rot[1] < PI)) {
        rot[1] -= TWO_PI;
    }

    return rot[1];
}

int CActionChara::RoboWalkMoveIF(int mode) {
    sceVu0FVECTOR position;
    sceVu0FVECTOR move_velocity;
    sceVu0FVECTOR movement;
    sceVu0FVECTOR target_position;
    sceVu0FVECTOR rotation;
    CActionChara *target;
    CActionChara *arm;
    float         camera_angle;
    float         stick_x;
    float         stick_y;
    float         move_z;
    float         motion_speed;
    float         target_angle;

    GetPosition(position);
    sceVu0CopyVector(move_velocity, velocity);
    camera_angle = action_info.camera->GetAngle();
    stick_x = GamePad__2.GetLXf();
    stick_y = GamePad__2.GetLYf();
    float stick_world_x = stick_x * cosf(camera_angle) + stick_y * sinf(camera_angle);
    float move_x = stick_world_x;
    move_z = -stick_x * sinf(camera_angle) + stick_y * cosf(camera_angle);
    move_x = move_x * (2.0f * mgFrameRate);
    move_z = move_z * (2.0f * mgFrameRate);
    move_velocity[0] = move_x;
    move_velocity[2] = move_z;
    stand_flag = false;

    if (lock_on != 0) {
        if (move_x != 0.0f || move_z != 0.0f) {
            SetRotation(0.0f, unitRotation(CObjectFrame::frame, atan2f(move_x, move_z), 5.0f), 0.0f);
            movement[0] = move_x;
            movement[1] = 0.0f;
            movement[2] = move_z;
            motion_speed = mgDistVector(movement) / max_speed;

            if (motion_speed > 1.0f) {
                motion_speed = 1.0f;
            }

            if (motion_speed >= 0.8f) {
                SetMotion("\x91\x96\x82\xE8-\x91\xAB", 0, mode);
            } else {
                SetMotion("\x95\xE0\x82\xAB-\x91\xAB", 0, mode);
            }
        } else {
            SetMotion("\x97\xA7\x82\xBF-\x91\xAB", 0, mode);
        }

        target = static_cast<CActionChara *>(nowScene__2->GetCharacter(target_no));

        if (target && target->chara_kind == ACTION_KIND_SCRIPT) {
            target->GetPosition(target_position);
            target_angle = atan2f(target_position[0] - position[0], target_position[2] - position[2]);
            GetRotation(rotation);
            target_angle -= rotation[1];

            if (target_angle < -PI) {
                target_angle += TWO_PI;
            }

            if (target_angle > PI) {
                target_angle -= TWO_PI;
            }

            arm = SearchChara("arm");

            if (arm != NULL) {
                arm->SetRotation(0.0f, unitRotation(arm->CObjectFrame::frame, target_angle, 3.0f), 0.0f);
            }
        }
    } else {
        arm = SearchChara("arm");

        if (arm != NULL) {
            arm->SetRotation(0.0f, unitRotation(arm->CObjectFrame::frame, 0.0f, 16.0f), 0.0f);
        }

        if (move_x != 0.0f || move_z != 0.0f) {
            sceVu0FVECTOR movement;
            SetRotation(0.0f, unitRotation(CObjectFrame::frame, atan2f(move_x, move_z), 10.0f), 0.0f);
            movement[0] = move_x;
            movement[1] = 0.0f;
            movement[2] = move_z;
            motion_speed = mgDistVector(movement) / max_speed;

            if (motion_speed > 1.0f) {
                motion_speed = 1.0f;
            }

            if (motion_speed >= 0.8f) {
                SetMotion("\x91\x96\x82\xE8-\x91\xAB", 0, mode);
            } else {
                SetMotion("\x95\xE0\x82\xAB-\x91\xAB", 0, mode);
            }
        } else {
            SetMotion("\x97\xA7\x82\xBF-\x91\xAB", 0, mode);
        }
    }

    sceVu0CopyVector(velocity, move_velocity);
    menu_flag = true;
    RockOn();
    return true;
}
