#include "enemy.h"
#include "entity.h"
#include "interop.hpp"
#include "openre.h"

namespace openre::enemy
{
    enum
    {
        POSE_IDLE = 0,
        POSE_HOSTILE = 2,
        POSE_EATING = 5,
    };

    enum
    {
        ROUTINE_INIT,
        ROUTINE_NORMAL,
        ROUTINE_HURT,
        ROUTINE_DIE,
        ROUTINE_4,
        ROUTINE_5,
        ROUTINE_6,
        ROUTINE_DEAD,
    };

    static EnemyRoutineFunc _funcs_45E447[] = {
        (EnemyRoutineFunc)0x0045E3C0, (EnemyRoutineFunc)0x0045E460, (EnemyRoutineFunc)0x0045E460, (EnemyRoutineFunc)0x0045E460,
        (EnemyRoutineFunc)0x0045ED50, (EnemyRoutineFunc)0x0045E460, (EnemyRoutineFunc)0x0045E460, (EnemyRoutineFunc)0x0045E460,
        (EnemyRoutineFunc)0x0045E460, (EnemyRoutineFunc)0x0045EB60, (EnemyRoutineFunc)0x0045EBD0, (EnemyRoutineFunc)0x0045EC50,
        (EnemyRoutineFunc)0x0045E460, (EnemyRoutineFunc)0x0045E460, (EnemyRoutineFunc)0x0045ECB0, (EnemyRoutineFunc)0x0045ED50,
        (EnemyRoutineFunc)0x0045EBD0, (EnemyRoutineFunc)0x0045EB60, (EnemyRoutineFunc)0x0045ED50, (EnemyRoutineFunc)0x0045EB60
    };

    static int16_t _baseHpNormal[] = {
        119, 85, 85, 85, 119, 70, 85, 85, 70, 85, 59, 70, 59, 85, 59, 70,
    };

    static int16_t _baseHpEasy[] = {
        57, 57, 82, 65, 82, 65, 57, 59, 65, 57, 30, 57, 30, 57, 30, 69,
    };

    static uint8_t _byte_527588[] = { 0, 1, 2, 4, 10, 12, 14, 13, 15, 0, 0, 0, 0, 0, 0, 0 };
    static uint8_t _byte_527550[] = { 7, 31, 15, 15, 15, 15, 7, 31 };

    static void sub_45F630(EnemyEntity* enemy, int a1, int a2)
    {
        interop::call<void, EnemyEntity*, int, int>(0x0045F630, enemy, a1, a2);
    }

    static void sub_45EDD0(EnemyEntity* enemy, int a1)
    {
        interop::call<void, EnemyEntity*, int>(0x0045EDD0, enemy, a1);
    }

    static void sub_45E680(EnemyEntity* enemy, Emr* emr, Edd* edd)
    {
        auto pl = enemy->var_227 == 0 ? (Entity*)gGameTable.player_work : (Entity*)gGameTable.splayer_work;
        if (enemy->var_220 == 0)
        {
            add_speed_xz(enemy, enemy->timer0);
            sub_45F630(enemy, 10, 0);
        }

        auto result = joint_move(enemy, emr, edd, enemy->timer1);
        if (result == 0)
            return;

        switch (enemy->routine_3)
        {
        case 0:
            enemy->routine_3 = 1;
            enemy->var_223 = (enemy->var_223 & 0xC2) | 2;
            enemy->move_no = 18;
            enemy->move_cnt = 0;
            enemy->hokan_flg = enemy->hokan_flg;
            enemy->mplay_flg = 0;
            break;
        case 1:
            enemy->routine_3 = 2;
            enemy->move_no = 7;
            enemy->move_cnt = 0;
            enemy->hokan_flg = 15;
            enemy->mplay_flg = 0;
            enemy->timer1 = 256;
            break;
        case 2:
            sub_45EDD0(enemy, 1);
            enemy->type &= ~0x2000;
            enemy->var_223 = 0;
            enemy->damage_cnt &= ~0x80;
            if (enemy->var_22D == 0)
            {
                auto r = goto00_ck(enemy, pl->pos.x, pl->pos.z, 512);
                enemy->routine_0 = ROUTINE_NORMAL;
                enemy->routine_1 = r == 0 ? 5 : 6;
                enemy->routine_2 = 0;
                enemy->routine_3 = 0;
            }
            else
            {
                enemy->routine_0 = ROUTINE_NORMAL;
                enemy->routine_1 = 13;
                enemy->routine_2 = 1;
                enemy->routine_3 = 0;
            }
            break;
        }
    }

    static void sub_45EAF0(EnemyEntity* enemy, Emr* kan, Edd* seq)
    {
        if (enemy->timer2 == 0)
        {
            enemy->timer2 = 1;
            if (enemy->routine_1 != 12)
            {
                enemy->spd.x = 40;
                add_speed_xz(enemy, enemy->timer0);
                enemy->cdir.y += rnd() - 128;
            }
        }
        sub_45E680(enemy, kan, seq);
        if (enemy->routine_3 > 1)
            enemy->var_223 &= ~0x40;
    }

    static EnemyRoutineFunc _funcs_45E8B5[]
        = { (EnemyRoutineFunc)0x0045E900, (EnemyRoutineFunc)0x0045E9F0, (EnemyRoutineFunc)0x0045EA50, sub_45EAF0 };

    // 0x0045F9A0
    static void sub_45F9A0(EnemyEntity* enemy, uint8_t a1, int a2)
    {
        interop::call<void, EnemyEntity*, uint8_t, int>(0x0045F9A0, enemy, a1, a2);
    }

    // 0x0045F530
    static int em_dog_pl_die(PlayerEntity* pl, Emr* emr, Edd* edd)
    {
        return interop::call<int, PlayerEntity*, Emr*, Edd*>(0x0045F530, pl, emr, edd);
    }

    // 0x0045C140
    static void em_dog_init(EnemyEntity* enemy, Emr* emr, Edd* seq)
    {
        enemy->routine_0 = ROUTINE_NORMAL;
        enemy->damage_flg = 0;
        enemy->sce_flg = 0;
        enemy->down_cnt = 0;
        std::memset(&enemy->var_218, 0, 48); // Zero enemy portion of struct

        auto baseLife = check_flag(FlagGroup::System, FG_SYSTEM_EASY) ? _baseHpEasy[rnd() % std::size(_baseHpEasy)]
                                                                      : _baseHpNormal[rnd() % std::size(_baseHpNormal)];
        enemy->life = baseLife + (rnd() & 3);
        enemy->d_life_d = 13;
        enemy->d_life_c = 13;
        enemy->d_life_u = 13;
        kage_work_set(&enemy->pKage_work, 0, 0x025804B0, 0x00000000, &enemy->pos);
        enemy->nOba = 1;
        enemy->atd[0].ofs.x = 500;
        enemy->atd[0].ofs.y = -1000;
        enemy->atd[0].ofs.z = 0;
        enemy->atd[1].at_w = 600;
        enemy->atd[0].at_d = 600;
        enemy->atd[0].at_h = 1000;
        enemy->atd[0].w = 600;
        enemy->atd[0].d = 600;
        enemy->eff_at_r = 600;
        enemy->pEnemy_ptr = &gGameTable.pl;
        enemy->neck_flg = 0;
        enemy->neck_no = 2;
        for (auto i = 2; i < 4; i++)
        {
            auto v6 = &((PartsW*)enemy->pSin_parts_ptr)[i];
            v6->free[0] = 0;
            v6->free[1] = 0;
            v6->free[2] = 32;
            v6->free[3] = 0;
            v6->free[4] = 384;
            v6->free[5] = 128;
        }
        enemy->spd.x = 0;
        enemy->spd.y = 0;
        enemy->spd.z = 0;
        if (gGameTable.hard_mode != 0)
            enemy->life *= 2;

        auto pose = static_cast<uint8_t>(enemy->type & 0x7F);
        enemy->routine_1 = pose < std::size(_byte_527588) ? _byte_527588[pose] : 0;

        auto type = enemy->type;
        if (type > 0x2003)
        {
            enemy->be_flg |= 0xC000000;
        }
        else if (type == 0x2003)
        {
            enemy->move_no = 6;
            enemy->move_cnt = 0;
            enemy->hokan_flg = 15;
            enemy->mplay_flg = 0;
            enemy->be_flg |= 0x4000000;
        }
        else
        {
            switch (type)
            {
            case 0:
            case 1:
            case 4:
                enemy->move_no = 1;
                enemy->move_cnt = rnd() & 0x3F;
                enemy->hokan_flg = 15;
                enemy->mplay_flg = 0;
                break;
            case 2:
            case 6:
                enemy->move_no = 2;
                enemy->move_cnt = rnd() & 0xF;
                enemy->hokan_flg = 15;
                enemy->mplay_flg = 0;
                break;
            case 7:
                enemy->move_no = 11;
                enemy->move_cnt = rnd() & 0x1F;
                enemy->hokan_flg = 15;
                enemy->mplay_flg = 0;
                enemy->timer2 = (rnd() & 3) + 1;
                enemy->neck_flg &= ~0x02;
                enemy->var_22E = 1;
                enemy->routine_3 = 1;
                break;
            case 8:
                enemy->move_no = 0;
                enemy->move_cnt = 0;
                enemy->hokan_flg = 15;
                enemy->mplay_flg = 0;
                enemy->var_22E = 1;
                enemy->be_flg |= 0x8000000;
                break;
            case 9:
                enemy->move_no = 1;
                enemy->move_cnt = rnd() & 0x3F;
                enemy->hokan_flg = 15;
                enemy->mplay_flg = 0;
                enemy->var_22E = 1;
                enemy->be_flg |= 0xC000000;
                break;
            default: enemy->be_flg |= 0xC000000; break;
            }
        }
        joint_move(enemy, emr, seq, 256);
        gGameTable.em_die_table[enemy->id] = em_dog_pl_die;
    }

    // 0x0045FB70
    static void sub_45FB70(EnemyEntity* enemy)
    {
        if (check_flag(FlagGroup::RoomEnemy, FG_ROOM_ENEMY_25))
        {
            enemy->routine_1 = 11;
            enemy->routine_2 = 0;
            enemy->routine_3 = 0;
        }
    }

    // 0x0045F5B0
    static void sub_45F5B0(EnemyEntity* enemy)
    {
        enemy->neck_no = 2;
        rot_neck_em(enemy, enemy->cdir.y);
        enemy->neck_no = 3;
        rot_neck_em(enemy, enemy->cdir.y);
        enemy->neck_no = 2;
    }

    // 0x0045C4E0
    static int sub_45C4E0(EnemyEntity* enemy, Emr* emr, Edd* edd)
    {
        auto result = 0;
        switch (enemy->routine_2)
        {
        case 0:
            enemy->routine_2 = 1;
            enemy->spd.x = 30;
            enemy->spd.z = 0;
            enemy->neck_flg |= 2;
            enemy->be_flg |= 0x4000000;
            enemy->be_flg &= ~0x10000000;
            enemy->var_219 = 0;
            enemy->var_22D = 0;
            enemy->damage_cnt &= ~0x80;
            break;
        case 1:
            add_speed_xz(enemy, 0);
            if (enemy->Sca_info & 1)
            {
                enemy->routine_2 = (rnd() & 0x1F) == 0 ? 2 : 3;
                enemy->var_218 = rnd_area();
                if (enemy->routine_2 == 2)
                {
                    enemy->timer1 = 90;
                    enemy->move_no = 0;
                    enemy->move_cnt = 0;
                    enemy->hokan_flg = 15;
                    enemy->mplay_flg = 0;
                }
            }
            break;
        case 2:
            if (--enemy->timer1 == 0)
            {
                enemy->routine_2 = 3;
                enemy->move_no = 1;
                enemy->move_cnt = 0;
                enemy->hokan_flg = 15;
                enemy->mplay_flg = 0;
            }
            break;
        case 3:
            result = root_ck(enemy, 0, enemy->var_218, 1);
            goto00(enemy, enemy->dest_x, enemy->dest_z, 16);
            add_speed_xz(enemy, 0);
            if ((enemy->Sca_info & 1) == 0)
                enemy->routine_2 = 1;
            break;
        }
        joint_move(enemy, emr, edd, 256);
        sub_45F5B0(enemy);
        if (enemy->var_21F == 0 && (rnd() & 0x3F) == 0)
        {
            enemy->routine_3 = _byte_527550[rnd() & 7];
        }
        sub_45F9A0(enemy, 4, 6);
        return result;
    }

    // 0x0045C460
    static void em_dog_normal_00(EnemyEntity* enemy, Emr* emr, Edd* edd)
    {
        auto v3 = sub_45C4E0(enemy, emr, edd);
        if (gGameTable.pl.nFloor == enemy->nFloor)
        {
            if (enemy->l_pl < 4000 || v3 == 1 || check_flag(FlagGroup::RoomEnemy, FG_ROOM_ENEMY_26))
            {
                enemy->routine_1 = 1;
                enemy->routine_2 = 0;
                enemy->routine_3 = 0;
            }
            else if (gGameTable.word_989EEE & 1)
            {
                enemy->routine_1 = 2;
                enemy->routine_2 = 0;
                enemy->routine_3 = 0;
            }
        }
        sub_45FB70(enemy);
    }

    static EnemyRoutineFunc _routineTableNormal[] = {
        em_dog_normal_00,
        (EnemyRoutineFunc)0x0045C680,
        (EnemyRoutineFunc)0x0045C8A0,
        (EnemyRoutineFunc)0x0045CDA0,
        (EnemyRoutineFunc)0x0045D200,
        (EnemyRoutineFunc)0x0045D3D0,
        (EnemyRoutineFunc)0x0045D470,
        (EnemyRoutineFunc)0x0045D520,
        (EnemyRoutineFunc)0x0045D860,
        (EnemyRoutineFunc)0x0045D940,
        (EnemyRoutineFunc)0x0045D9B0,
        (EnemyRoutineFunc)0x0045DA10,
        (EnemyRoutineFunc)0x0045DB20,
        (EnemyRoutineFunc)0x0045DCD0,
        (EnemyRoutineFunc)0x0045DF80,
        (EnemyRoutineFunc)0x0045E110,
        (EnemyRoutineFunc)0x0045E300,
    };

    // 0x0045C420
    static void em_dog_normal(EnemyEntity* enemy, Emr* emr, Edd* edd)
    {
        _routineTableNormal[enemy->routine_1](enemy, emr, edd);
        if (*enemy->pNow_seq & 0x40000)
            snd_se_enem(8, enemy);
    }

    // 0x0045E430
    static void em_dog_hurt(EnemyEntity* enemy, Emr* emr, Edd* seq)
    {
        auto cl = enemy->var_223;
        if (cl & 0x80)
        {
            // While in hurt state
            _funcs_45E8B5[enemy->routine_2](enemy, emr, seq);
            sub_45F9A0(enemy, rnd() & 15, 0);
            sub_45F9A0(enemy, rnd() & 15, 0);
            return sub_45F9A0(enemy, rnd() & 0xF, 5);
        }
        else
        {
            // While in normal/other state
            return _funcs_45E447[enemy->routine_1](enemy, emr, seq);
        }
    }

    // 0x0045F470
    static void em_dog_dead(EnemyEntity* enemy, Emr*, Edd*)
    {
        if (enemy->routine_1 == 0)
        {
            enemy->be_flg |= 2;
            enemy->routine_1 = 1;
        }

        auto kage = enemy->pKage_work;
        if (enemy->var_221 == 0)
        {
            enemy->var_221 = 1;
            auto colour = gGameTable.blood_censor ? 0xBF10BF : 0xBFBF10;
            if (kage != nullptr)
            {
                kage->var_1C = (kage->var_1C & 0xFF000000) | colour;
                kage->var_44 = (kage->var_44 & 0xFF000000) | colour;
            }
            enemy->timer3 = 90;
        }
        else if (enemy->var_221 != 1)
        {
            return;
        }

        if (kage != nullptr)
        {
            kage->var_04 += 8;
            kage->var_06 += 8;
        }

        enemy->timer3--;
        if (enemy->timer3 == 0)
            enemy->var_221 = 2;
    }

    static EnemyRoutineFunc _routines[]
        = { em_dog_init, em_dog_normal, em_dog_hurt, (EnemyRoutineFunc)0x0045EE20, (EnemyRoutineFunc)0x00492990,
            nullptr,     nullptr,       em_dog_dead

          };

    // 0x0045C0A0
    void em_dog(EnemyEntity* enemy)
    {
        if (check_flag(FlagGroup::Stop, FG_STOP_02))
            return;

        if ((enemy->damage_cnt & 0x7F) != 0)
            enemy->damage_cnt--;

        if (enemy->var_232 != 0)
            enemy->var_232--;

        if (enemy->routine_0 < std::size(_routines))
            _routines[enemy->routine_0](enemy, enemy->pKan_t_ptr, enemy->pSeq_t_ptr);
        oba_ck_em(enemy);
        sca_ck_em(enemy, 1024);

        if ((enemy->pos.x == enemy->old_pos.x && enemy->pos.z == enemy->old_pos.z) || enemy->at_em_no != 0xFF)
            enemy->var_230++;
        else
            enemy->var_230 = 0;
    }
}
