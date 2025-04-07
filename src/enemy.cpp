#include "enemy.h"
#include "file.h"
#include "interop.hpp"
#include "marni.h"
#include "model.h"
#include "openre.h"
#include "rdt.h"
#include <array>
#include <cstring>

using namespace openre::file;
using namespace openre::rdt;

namespace openre::enemy
{
    struct UnknownA3
    {
        uint8_t pad_00[0xA0 - 0x00];
        uint16_t var_A0;
    };

#pragma pack(push, 1)
    struct EmdFileHeader
    {
        uint32_t Footer;
    };

    struct EmdFileFooter
    {
        uint32_t Dat;
        uint32_t Edd0;
        uint32_t Emr0;
        uint32_t Edd1;
        uint32_t Emr1;
        uint32_t Edd2;
        uint32_t Emr2;
        uint32_t Md1;
    };

    struct EmdFile
    {
        EmdFileHeader Header;

        void* GetDat() const
        {
            return GetPointer<void>(GetFooter()->Dat);
        }

        Edd* GetEdd(int index) const
        {
            if (index == 0)
                return GetPointer<Edd>(GetFooter()->Edd0);
            if (index == 1)
                return GetPointer<Edd>(GetFooter()->Edd1);
            if (index == 2)
                return GetPointer<Edd>(GetFooter()->Edd2);
            return nullptr;
        }

        Emr* GetEmr(int index) const
        {
            if (index == 0)
                return GetPointer<Emr>(GetFooter()->Emr0);
            if (index == 1)
                return GetPointer<Emr>(GetFooter()->Emr1);
            if (index == 2)
                return GetPointer<Emr>(GetFooter()->Emr2);
            return nullptr;
        }

        Md1* GetMd1() const
        {
            return GetPointer<Md1>(GetFooter()->Md1);
        }

    private:
        EmdFileFooter* GetFooter() const
        {
            return GetPointer<EmdFileFooter>(Header.Footer);
        }

        template<typename T> T* GetPointer(uint32_t offset) const
        {
            return (T*)((uintptr_t)this + offset);
        }
    };
#pragma pack(pop)

    static void em_zombie(EnemyEntity* enemy);
    static void em_21(EnemyEntity* enemy);
    static void em_22(EnemyEntity* enemy);
    static void em_23(EnemyEntity* enemy);
    static void em_24(EnemyEntity* enemy);
    static void em_25(EnemyEntity* enemy);
    static void em_26(EnemyEntity* enemy);
    static void em_27(EnemyEntity* enemy);
    static void em_28(EnemyEntity* enemy);
    static void em_29(EnemyEntity* enemy);
    static void em_2A(EnemyEntity* enemy);
    static void em_2B(EnemyEntity* enemy);
    static void em_2C(EnemyEntity* enemy);
    static void em_2D(EnemyEntity* enemy);
    static void em_2E(EnemyEntity* enemy);
    static void em_2F(EnemyEntity* enemy);
    static void em_30(EnemyEntity* enemy);
    static void em_31(EnemyEntity* enemy);
    static void em_33(EnemyEntity* enemy);
    static void em_34(EnemyEntity* enemy);
    static void em_36(EnemyEntity* enemy);
    static void em_37(EnemyEntity* enemy);
    static void em_38(EnemyEntity* enemy);
    static void em_3A(EnemyEntity* enemy);
    static void em_3B(EnemyEntity* enemy);
    static void em_3E(EnemyEntity* enemy);
    static void em_3F(EnemyEntity* enemy);

    static int16_t word_52DA48[] = { 30,   59,   87,   114,  140,  165,  189,  212,  234,  255,  275,  294,  312,  329,  345,
                                     360,  374,  387,  399,  410,  420,  429,  437,  444,  450,  455,  459,  462,  464,  465,
                                     180,  354,  522,  684,  840,  990,  1134, 1272, 1404, 1530, 1650, 1764, 1872, 1974, 2070,
                                     2160, 2244, 2322, 2394, 2460, 2520, 2574, 2622, 2664, 2700, 2730, 2754, 2772, 2784, 2790 };

    // 0x004B1DD0
    void em_move_tbl_set()
    {
        gGameTable.enemy_init_entries[0].type = EM_NONE;
        gGameTable.enemy_init_entries[1].type = EM_NONE;

        gGameTable.enemy_init_table[EM_ZOMBIE_COP] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_BRAD] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_GUY1] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_GIRL] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_14] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_TESTSUBJECT] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_SCIENTIST] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_NAKED] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_GUY2] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_19] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_1A] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_1B] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_1C] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_1D] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_GUY3] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_RANDOM] = em_zombie;
        gGameTable.enemy_init_table[EM_ZOMBIE_DOG] = em_dog;
        gGameTable.enemy_init_table[EM_CROW] = em_21;
        gGameTable.enemy_init_table[EM_LICKER_RED] = em_22;
        gGameTable.enemy_init_table[EM_ALLIGATOR] = em_23;
        gGameTable.enemy_init_table[EM_LICKER_GREY] = em_24;
        gGameTable.enemy_init_table[EM_SPIDER] = em_spider;
        gGameTable.enemy_init_table[EM_SPIDER_BABY] = em_26;
        gGameTable.enemy_init_table[EM_G_EMBRYO] = em_27;
        gGameTable.enemy_init_table[EM_G_ADULT] = em_28;
        gGameTable.enemy_init_table[EM_COCKROACH] = em_29;
        gGameTable.enemy_init_table[EM_TYRANT_1] = em_2A;
        gGameTable.enemy_init_table[EM_TYRANT_2] = em_2B;
        gGameTable.enemy_init_table[EM_2C] = em_2C;
        gGameTable.enemy_init_table[EM_ZOMBIE_ARMS] = em_2D;
        gGameTable.enemy_init_table[EM_IVY] = em_2E;
        gGameTable.enemy_init_table[EM_VINES] = em_2F;
        gGameTable.enemy_init_table[EM_BIRKIN_1] = em_30;
        gGameTable.enemy_init_table[EM_BIRKIN_2] = em_31;

        gGameTable.enemy_init_table[EM_BIRKIN_4] = em_33;
        gGameTable.enemy_init_table[EM_BIRKIN_5] = em_34;

        gGameTable.enemy_init_table[EM_36] = em_36;
        gGameTable.enemy_init_table[EM_37] = em_37;
        gGameTable.enemy_init_table[EM_38] = em_38;
        gGameTable.enemy_init_table[EM_IVY_POISON] = em_2E;
        gGameTable.enemy_init_table[EM_MOTH] = em_3A;
        gGameTable.enemy_init_table[EM_MAGGOTS] = em_3B;

        gGameTable.enemy_init_table[EM_3E] = em_3E;
        gGameTable.enemy_init_table[EM_3F] = em_3F;

        for (auto i = 112; i < 159; i++)
        {
            gGameTable.enemy_init_table[i] = gGameTable.enemy_init_table[i - 96];
        }
    }

    [[nodiscard]] bool is_enemy_dead(uint8_t globalId)
    {
        auto fgEnemy = FlagGroup::Enemy2;
        if (gGameTable.current_stage < 3 && !check_flag(FlagGroup::Status, FG_STATUS_BONUS))
        {
            fgEnemy = FlagGroup::Enemy;
        }
        return globalId != 0xFF && check_flag(fgEnemy, globalId);
    }

    [[nodiscard]] static EnemyFunc get_enemy_init_func(uint8_t type, int slot)
    {
        auto index = (slot * 96) + type;
        return reinterpret_cast<EnemyFunc>(gGameTable.enemy_init_table[index]);
    }

    [[nodiscard]] static bool is_zombie(uint8_t type)
    {
        return type >= EM_ZOMBIE_COP && type <= EM_ZOMBIE_RANDOM;
    }

    // 0x004B20B0
    static void em_bin_load(uint8_t type)
    {
        auto func = get_enemy_init_func(type, 0);
        if (func == nullptr)
            return;

        // Share same init function for all zombies
        auto sharedType = is_zombie(type) ? EM_ZOMBIE_COP : type;

        auto entries = gGameTable.enemy_init_entries;
        int slot;
        for (slot = 0; slot < 2; slot++)
        {
            if (entries[slot].type == sharedType || entries[slot].enabled == 0)
                break;
        }

        entries[slot].type = sharedType;
        entries[slot].enabled = 1;
        gGameTable.enemy_init_map[type] = get_enemy_init_func(type, slot);
    }

    // 0x004E3AB0
    [[nodiscard]] static uint8_t em_kind_search(uint8_t id)
    {
        return interop::call<uint8_t, uint8_t>(0x004E3AB0, id);
    }

    // 0x00442F50
    static void em_set_flags(int id)
    {
        return interop::call<void, int>(0x00442F50, id);
    }

    // 0x00443F70
    static void pl_load_texture(int workNo, void* pTim, int tpage, int clut, int id)
    {
        return interop::call<void, int, void*, int, int, int>(0x00443F70, workNo, pTim, tpage, clut, id);
    }

    static int get_customised_emd_id(int id)
    {
        if (id >= EM_LEON_RPD && id <= EM_LEON_LEATHER)
        {
            if (id & 1)
            {
                if (check_flag(FlagGroup::Zapping, FG_ZAPPING_6))
                    return EM_CLAIRE_COWGIRL;
            }
            else
            {
                if (check_flag(FlagGroup::Zapping, FG_ZAPPING_5))
                    return EM_LEON_TANKTOP;
                if (check_flag(FlagGroup::Zapping, FG_ZAPPING_15))
                    return EM_LEON_LEATHER;
            }
        }
        return id;
    }

    static const char* get_emd_path(char* buffer, size_t bufferLen, const char* extension, int id)
    {
        auto player = check_flag(FlagGroup::Status, FG_STATUS_PLAYER) ? 1 : 0;
        snprintf(buffer, bufferLen, "pl%d\\emd%d\\em%d%02X%s", player, player, player, id, extension);
        return buffer;
    }

    // 0x004B1660
    static void* emd_load(int id, EnemyEntity* entity, void* buffer)
    {
        char filename[64];
        auto g = (uint8_t*)gGameTable.pGG;

        // Load texture
        if (gGameTable.ctcb->var_0E != 0)
        {
            if (gGameTable.ctcb->var_0E != 1)
                return nullptr;
        }
        else
        {
            em_set_flags(id);
            gGameTable.idd = get_customised_emd_id(entity->id);

            get_emd_path(filename, sizeof(filename), ".tim", gGameTable.idd);
            if (read_file_into_buffer(filename, buffer, 8) == 0)
            {
                file_error();
                return nullptr;
            }

            entity->clut = g[0x3A09];
            entity->tpage = g[0x3A08];
            marni::out();
            pl_load_texture(entity->work_no, buffer, entity->tpage, entity->clut, entity->id);
        }

        // Load mesh and animations
        get_emd_path(filename, sizeof(filename), ".emd", gGameTable.idd);
        auto emdFileSize = read_file_into_buffer(filename, buffer, 8);
        if (emdFileSize == 0)
        {
            file_error();
            return nullptr;
        }

        auto emdFile = reinterpret_cast<EmdFile*>(buffer);
        auto emdFileEnd = (uintptr_t)emdFile + emdFileSize;
        entity->pSa_dat = emdFile->GetDat();
        entity->pSeq_t_ptr = emdFile->GetEdd(0);
        entity->pKan_t_ptr = emdFile->GetEmr(0);
        entity->pSub0_seq_t_ptr = emdFile->GetEdd(1);
        entity->pSub0_kan_t_ptr = emdFile->GetEmr(1);
        entity->pSub1_seq_t_ptr = emdFile->GetEdd(2);
        entity->pSub1_kan_t_ptr = emdFile->GetEmr(2);
        auto md1 = emdFile->GetMd1();
        entity->poly_rgb = 0x808080;

        if (entity->model_type & MODEL_FLAG_7)
        {
            // Remove edd1,emr1,edd2,emr2
            std::memcpy(emdFile->GetEdd(1), emdFile->GetMd1(), emdFileEnd - (uintptr_t)emdFile->GetMd1());
            md1 = (Md1*)entity->pSub0_seq_t_ptr;
        }
        if (entity->model_type & MODEL_FLAG_6)
        {
            // Untested
            // Remove edd0,emr0
            auto shift = emdFileEnd - (uintptr_t)emdFile->GetEdd(1);
            std::memcpy(emdFile->GetEdd(0), emdFile->GetEdd(1), shift);
            entity->pSub0_seq_t_ptr = (Edd*)((uintptr_t)entity->pSub0_seq_t_ptr - shift);
            entity->pSub0_kan_t_ptr = (Emr*)((uintptr_t)entity->pSub0_kan_t_ptr - shift);
            entity->pSub1_seq_t_ptr = (Edd*)((uintptr_t)entity->pSub1_seq_t_ptr - shift);
            entity->pSub1_kan_t_ptr = (Emr*)((uintptr_t)entity->pSub1_kan_t_ptr - shift);
            md1 = (Md1*)((uintptr_t)entity->pTmd - shift);

            entity->pSeq_t_ptr = reinterpret_cast<Edd*>(&g[0x3B8C]);
            entity->pKan_t_ptr = reinterpret_cast<Emr*>(&g[0x3B18]);
        }

        auto md1Len = mapping_tmd(0, md1, entity->tpage, entity->clut);
        marni::mapping_tmd(entity->work_no, md1, entity->id);
        entity->pTmd = &md1->Data.Entries[0];
        entity->pTmd2 = &md1->Data.Entries[1];
        gGameTable.ctcb->var_0E = 0;

        auto result = reinterpret_cast<void*>((uintptr_t)md1 + md1Len);
        return align(result);
    }

    // 0x004C1270
    void* partswork_set(Entity* entity, void* parts)
    {
        return interop::call<void*, Entity*, void*>(0x004C1270, entity, parts);
    }

    // 0x004C12B0
    void* partswork_link(Entity* entity, void* packetTop, void* kan, int mode)
    {
        return interop::call<void*, Entity*, void*, void*, int>(0x004C12B0, entity, packetTop, kan, mode);
    }

    // 0x004DFD20
    void* sa_dat_set(Entity* entity, void* arg1)
    {
        return interop::call<void*, Entity*, void*>(0x004DFD20, entity, arg1);
    }

    // 0x004C52A0
    void* mirror_model_cp(Entity* entity, void* arg1)
    {
        using sig = void* (*)(Entity*, void*);
        auto p = (sig)0x004C52A0;
        return p(entity, arg1);
    }

    // 0x00443F40
    int* mem_ck_parts_work(int workNo, int id)
    {
        return interop::call<int*, int, int>(0x00443F40, workNo, id);
    }

    // 0x004CC680
    void oba_ck_em(EnemyEntity* enemy)
    {
        interop::call<void, EnemyEntity*>(0x004CC680, enemy);
    }

    // 0x004CC730
    void sca_ck_em(EnemyEntity* enemy, int a1)
    {
        interop::call<void, EnemyEntity*, int>(0x004CC730, enemy, a1);
    }

    // 0x00445840
    void sub_445840(void* a0, int a1)
    {
        interop::call<void, void*, int>(0x00445840, a0, a1);
    }

    // 0x004C0E40
    static void ko_joint_trans2(Entity* entity, PartsW* part, uint16_t beFlg, Mat16* workm)
    {
        interop::call<void, Entity*, PartsW*, uint16_t, Mat16*>(0x004C0E40, entity, part, beFlg, workm);
    }

    // 0x004C5230
    static void bomb_parts_sort_gt(void* a0, void* a1, UnknownA3* a2)
    {
        auto a = a2->var_A0 & 0x7FFF;
        auto b = (a2->var_A0 >> 15) * 30;
        auto c = word_52DA48[a + b];
        sub_445840(a1, c);
    }

    // 0x004DF320
    int root_ck(Entity* entity, int a1, int a2, int a3)
    {
        return interop::call<int, Entity*, int, int, int>(0x004DF320, entity, a1, a2, a3);
    }

    // 0x004B23C0
    void goto00(Entity* entity, int x, int z, int t)
    {
        return interop::call<void, Entity*, int, int, int>(0x004B23C0, entity, x, z, t);
    }

    // 0x004B3990
    void rot_neck_em(Entity* entity, int d)
    {
        return interop::call<void, Entity*, int>(0x004B3990, entity, d);
    }

    // 0x004EDE30
    void snd_se_enem(uint8_t id, EnemyEntity* enemy)
    {
        interop::call<void, uint8_t, void*>(0x004EDE30, id, enemy);
    }

    // part of 0x004E77D0
    bool spawn_enemy(const EnemySpawnInfo& info)
    {
        em_bin_load(info.Type);
        auto ctcb = gGameTable.ctcb;
        if (ctcb->var_13 != 0)
            return false;

        auto em = work_alloc<EnemyEntity>();
        if (info.Id != 0xFF)
        {
            gGameTable.enemy_count++;
            em->work_no = 2 + info.Id;
            gGameTable.enemies[info.Id] = em;
            auto& nextEntry = gGameTable.enemies[info.Id + 1];
            if (gGameTable.dword_98862C < &nextEntry)
                gGameTable.dword_98862C = &nextEntry;
        }
        else
        {
            em->work_no = 1;
            gGameTable.splayer_work = em;
            auto& nextEntry = gGameTable.enemies[0];
            if (gGameTable.dword_98862C < &nextEntry)
                gGameTable.dword_98862C = &nextEntry;
        }

        em->be_flg = 1;
        em->sound_bank = info.SoundBank;
        if (gGameTable.se_tmp0 != info.SoundBank)
        {
            if (gGameTable.se_tmp0 == 0)
                gGameTable.se_tmp0 = info.SoundBank;
            else
                gGameTable.byte_695E71 = info.SoundBank;
        }

        em->m.pos.x = info.Position.x;
        em->m.pos.y = info.Position.y;
        em->m.pos.z = info.Position.z;
        em->old_pos.x = info.Position.x;
        em->old_pos.y = info.Position.y;
        em->old_pos.z = info.Position.z;

        em->sca_old_x = info.Position.x;
        em->sca_old_z = info.Position.z;

        em->cdir.x = 0;
        em->cdir.y = info.Position.d;
        em->cdir.z = 0;

        em->id = info.Type;
        em->type = info.Pose | (info.Behaviour << 8);
        em->em_set_flg = info.GlobalId;
        em->model_type = info.Texture;
        em->nFloor = info.Floor;
        em->routine_0 = 0;
        em->routine_1 = 0;
        em->routine_2 = 0;
        em->routine_3 = 0;
        em->damage_flg = 0;
        em->damage_cnt = 0;
        em->sce_free0 = 0;
        em->status_flg = 0;
        em->in_screen = 0;
        em->sce_flg = 0;
        em->parts0_pos_y = 0x1000;
        em->ground = info.Floor * -1800;
        em->sce_free1 = 0;
        em->sce_free2 = 0;
        em->sce_free3 = 0;
        em->pTbefore_func = nullptr;
        em->pTafter_func = nullptr;
        em->pOn_om = 0;
        em->kage_ofs = 0;
        em->water = 0;
        em->l_pl = 0;
        em->l_spl = 0;
        if (em->id >= 0x40)
            em->sc_id = 0x80;
        else
            em->sc_id = 0x04;
        em->nOba = 0;

        uint16_t* atd = (uint16_t*)&em->atd;
        atd[0x06] = 450;
        atd[0x07] = 450;
        atd[0x08] = 0;
        atd[0x09] = 0;
        atd[0x0A] = -1530;
        atd[0x0B] = 450;
        atd[0x0C] = 450;
        atd[0x0D] = 1530;

        em->root_ck_cnt = em->work_no;

        auto lastEnemy = static_cast<EnemyEntity*>(gGameTable.c_em);
        if (em->id == gGameTable.c_id)
        {
            em->pKan_t_ptr = lastEnemy->pKan_t_ptr;
            em->pSeq_t_ptr = lastEnemy->pSeq_t_ptr;
            em->pTmd = lastEnemy->pTmd;
            em->pTmd2 = lastEnemy->pTmd2;
            em->pSub0_kan_t_ptr = lastEnemy->pSub0_kan_t_ptr;
            em->pSub0_seq_t_ptr = lastEnemy->pSub0_seq_t_ptr;
            em->pSub1_kan_t_ptr = lastEnemy->pSub1_kan_t_ptr;
            em->pSub1_seq_t_ptr = lastEnemy->pSub1_seq_t_ptr;
            em->pSa_dat = lastEnemy->pSa_dat;
            em->tpage = lastEnemy->tpage;
            em->clut = lastEnemy->clut;
            mem_ck_parts_work(em->work_no, em->id);
        }
        else
        {
            gGameTable.c_id = em->id;
            gGameTable.c_model_type = em->model_type;
            gGameTable.c_em = em;
            auto kind = em_kind_search(em->id);
            if (kind != gGameTable.c_kind)
                em->model_type &= ~0x80;
            gGameTable.c_kind = kind;

            auto emdResult = emd_load(em->id, em, gGameTable.mem_top);
            if (gGameTable.ctcb->var_13 != 0)
                return false;

            gGameTable.mem_top = emdResult;
            gGameTable.sce_type = 0;
            gGameTable.scd = rdt_get_offset<uint8_t>(RdtOffsetKind::SCD_MAIN);
            if (em->model_type & 0x80)
            {
                em->pSub0_kan_t_ptr = lastEnemy->pSub0_kan_t_ptr;
                em->pSub0_seq_t_ptr = lastEnemy->pSub0_seq_t_ptr;
                em->pSub1_kan_t_ptr = lastEnemy->pSub1_kan_t_ptr;
                em->pSub1_seq_t_ptr = lastEnemy->pSub1_seq_t_ptr;
            }
        }

        auto parts = partswork_set(em, gGameTable.mem_top);
        gGameTable.mem_top = partswork_link(em, parts, em->pKan_t_ptr, 0);
        sa_dat_set(em, em->pSa_dat);
        if (check_flag(FlagGroup::Status, FG_STATUS_MIRROR))
            gGameTable.mem_top = mirror_model_cp(em, gGameTable.mem_top);

        em->move_no = 0;
        em->timer0 = info.Animation;
        em->timer1 = info.Unknown;
        if (em->type & 0x4000)
            em->neck_flg = 0x92;

        return true;
    }

    // 0x004517F0
    static void em_zombie(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x004517F0)(enemy);
    }

    // 0x0045FC10
    static void em_21(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x0045FC10)(enemy);
    }

    // 0x00464630
    static void em_22(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x00464630)(enemy);
    }

    // 0x00468760
    static void em_23(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x00468760)(enemy);
    }

    // 0x0046CA80
    static void em_24(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x0046CA80)(enemy);
    }

    // 0x0046F000
    static void em_25(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x0046F000)(enemy);
    }

    // 0x00473620
    static void em_26(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x00473620)(enemy);
    }

    // 0x00474210
    static void em_27(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x00474210)(enemy);
    }

    // 0x00476EF0
    static void em_28(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x00476EF0)(enemy);
    }

    // 0x0047BF60
    static void em_29(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x0047BF60)(enemy);
    }

    // 0x0047D330
    static void em_2A(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x0047D330)(enemy);
    }

    // 0x00480900
    static void em_2B(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x00480900)(enemy);
    }

    // 0x00483C90
    static void em_2C(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x00483C90)(enemy);
    }

    // 0x00483E30
    static void em_2D(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x00483E30)(enemy);
    }

    // 0x00484B90
    static void em_2E(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x00484B90)(enemy);
    }

    // 0x004889D0
    static void em_2F(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x004889D0)(enemy);
    }

    // 0x0048A320
    static void em_30(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x0048A320)(enemy);
    }

    // 0x004900E0
    static void em_31(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x004900E0)(enemy);
    }

    // 0x00496930
    static void em_33(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x00496930)(enemy);
    }

    // 0x0049B5D0
    static void em_34(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x0049B5D0)(enemy);
    }

    // 0x004A4560
    static void em_36(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x004A4560)(enemy);
    }

    // 0x004A8770
    static void em_37(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x004A8770)(enemy);
    }

    // 0x004AC8E0
    static void em_38(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x004AC8E0)(enemy);
    }

    // 0x004ACC70
    static void em_3A(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x004ACC70)(enemy);
    }

    // 0x004B0370
    static void em_3B(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x004B0370)(enemy);
    }

    // 0x004B1210
    static void em_3E(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x004B1210)(enemy);
    }

    // 0x004B13D0
    static void em_3F(EnemyEntity* enemy)
    {
        ((EnemyFunc)0x004B13D0)(enemy);
    }

    void enemy_init_hooks()
    {
        interop::writeJmp(0x004B1DD0, em_move_tbl_set);
        interop::writeJmp(0x004C5230, bomb_parts_sort_gt);
    }
}
