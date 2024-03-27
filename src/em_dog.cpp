#include "enemy.h"
#include "interop.hpp"
#include "openre.h"

namespace openre::enemy
{
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

    static void sub_45E680(EnemyEntity* enemy, void* a1, int a2)
    {
        interop::call<void, EnemyEntity*, void*, int>(0x0045E680, enemy, a1, a2);
    }

    static void sub_45EAF0(EnemyEntity* enemy, void* a1, int a2)
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
        sub_45E680(enemy, a1, a2);
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

    // 0x0045E430
    static void em_20_hurt(EnemyEntity* enemy, void* a1, int a2)
    {
        auto cl = enemy->var_223;
        if (cl & 0x80)
        {
            // While in hurt state
            _funcs_45E8B5[enemy->routine_2](enemy, a1, a2);
            sub_45F9A0(enemy, rnd() & 15, 0);
            sub_45F9A0(enemy, rnd() & 15, 0);
            return sub_45F9A0(enemy, rnd() & 0xF, 5);
        }
        else
        {
            // While in normal/other state
            return _funcs_45E447[enemy->routine_1](enemy, a1, a2);
        }
    }

    // 0x0045F470
    static void em_20_dead(EnemyEntity* enemy, void*, int)
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

    static EnemyRoutineFunc _routines[] = { (EnemyRoutineFunc)0x0045C140,
                                            (EnemyRoutineFunc)0x0045C420,
                                            em_20_hurt,
                                            (EnemyRoutineFunc)0x0045EE20,
                                            (EnemyRoutineFunc)0x00492990,
                                            nullptr,
                                            nullptr,
                                            em_20_dead

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
