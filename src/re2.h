#pragma once

#include <cstdint>

#pragma pack(push, 1)

struct MATRIX
{
    int16_t m[3][3];
    int16_t field_12;
    int t[3];
};

struct SCE_TASK
{
    uint8_t Routine_0;
    uint8_t Status;
    uint8_t Sub_ctr;
    uint8_t Task_level;
    uint8_t Ifel_ctr[4];
    uint8_t Loop_ctr[4];
    uint8_t Loop_if_class[16];
    uint8_t* Data;
    int32_t Lstack[16];
    int32_t Lbreak[16];
    int16_t Lcnt[16];
    int32_t Stack[32];
    uint32_t* pS_SP;
    int32_t Ret_addr[4];
    int32_t pWork;
    int16_t Spd[3];
    int16_t Dspd[3];
    int16_t Aspd[3];
    int16_t Adspd[3];
    int32_t R_no_bak;
};

struct SCE_AOT
{
    uint8_t Id;
    uint8_t Type;
    uint8_t nFloor;
    uint8_t Super;
    int16_t X;
    int16_t Z;
    uint16_t W;
    uint16_t D;
    int16_t Data;
};

struct IN_DOOR_WORK
{
    int16_t Next_pos_x;
    int16_t Next_pos_y;
    int16_t Next_pos_z;
    int16_t Next_cdir_y;
    uint8_t Next_stage;
    uint8_t Next_room;
    uint8_t Next_cut;
    uint8_t Next_nfloor;
    uint8_t Dtex_type;
    uint8_t Door_type;
    uint8_t Knock_type;
    uint8_t Key_id;
    uint8_t Key_type;
    uint8_t Free;
};

struct SceAotBase
{
    uint8_t Sce;
    uint8_t Sat;
    uint8_t nFloor;
    uint8_t Super;
};

struct SceAot : SceAotBase
{
    int16_t X;
    int16_t Z;
    uint16_t W;
    uint16_t D;
};

struct XZPoint
{
    int16_t X;
    int16_t Z;
};

struct SceAot4p : SceAotBase
{
    XZPoint Points[4];
};

struct SceAotDoor
{
    SceAot Aot;
    IN_DOOR_WORK Door;
};

struct ScdAotSet
{
    uint8_t Opcode;
    uint8_t Id;
    SceAot Aot;
    uint8_t Data[6];
};

struct ScdSceAotDoor
{
    uint8_t Opcode;
    uint8_t Id;
    SceAotDoor Data;
};

struct ScdAotSet4p
{
    uint8_t Opcode;
    uint8_t Id;
    SceAot4p Aot;
    uint8_t Data[6];
};

struct PLAYER_WORK
{
    int Be_flg;
    uint8_t Routine_0;
    uint8_t Routine_1;
    uint8_t Routine_2;
    uint8_t Routine_3;
    uint8_t Id;
    uint8_t Sc_id;
    uint8_t At_obj_no;
    uint8_t At_sce_no;
    uint8_t Work_no;
    uint8_t At_em_no;
    int16_t At_em_flg;
    int Attribute;
    int pTmd;
    int pPacket;
    int pTmd2;
    int pPacket2;
    uint16_t M[3][3];
    int16_t M_pad;
    int Pos_x;
    int Pos_y;
    int Pos_z;
    int16_t Old_x;
    int16_t Old_y;
    int16_t Old_z;
    int16_t Old_x2;
    int16_t Old_y2;
    int16_t Old_z2;
    int dummy00;
    MATRIX Workm;
    int16_t Cdir_x;
    int16_t Cdir_y;
    int16_t Cdir_z;
    int16_t dummy01;
    int Poly_rgb;
    MATRIX* pSuper;
    int Atd[32];
    uint8_t Tpage,
        Clut,
        nFloor,
        Parts_num;
    int pKan_t_ptr;
    int16_t Water;
    int16_t Type;
    int Sca_info;
    int field_114;
    int field_118;
    int field_11C;
    int field_120;
    int field_124;
    int field_128;
    int field_12C;
    int field_130;
    int field_134;
    int field_138;
    int field_13C;
    int field_140;
    int16_t Spd_x;
    int16_t Spd_y;
    int16_t Spd_z;
    uint8_t In_screen;
    uint8_t Model_Tex_type;
    uint8_t Move_no;
    uint8_t Move_cnt;
    uint8_t Hokan_flg;
    uint8_t Mplay_flg;
    uint8_t Root_ck_cnt;
    uint8_t D_life_u;
    uint8_t D_life_c;
    uint8_t D_life_d;
    int16_t Status_flg;
    int16_t Life;
    int16_t Timer0;
    int16_t Timer1;
    int16_t F_pos_x;
    int16_t F_pos_y;
    int16_t F_pos_z;
    int16_t Max_life;
    int16_t Base_pos_x;
    int16_t Base_pos_y;
    int16_t Base_pos_z;
    uint8_t Timer2;
    uint8_t Timer3;
    int pKage_work;
    int field_170;
    int field_174;
    uint32_t* pNow_seq;
    int pSeq_t_ptr;
    int pSub0_kan_t_ptr;
    int pSub0_seq_t_ptr;
    int field_188;
    int field_18C;
    int field_190;
    int field_194;
    int pSin_parts_ptr;
    int pParts_tmd;
    int pParts_packet;
    int pM_uint8_t_ptr;
    int pM_option_tmd;
    int pM_option_packet;
    int pM_Kage_work;
    int pEnemy_ptr;
    int pEnemy_neck;
    int pSa_dat;
    uint8_t Neck_flg;
    uint8_t Neck_no;
    int16_t Ground;
    int16_t Dest_x;
    int16_t Dest_z;
    int16_t Down_cnt;
    uint8_t At_hit_flg;
    uint8_t field_1CB;
    int16_t Sce_flg;
    uint8_t Em_set_flg;
    uint8_t Model_type;
    int field_1D0;
    int field_1D4;
    int field_1D8;
    int field_1DC;
    int field_1E0;
    int field_1E4;
    int field_1E8;
    int field_1EC;
    int field_1F0;
    int field_1F4;
    int field_1F8;
    int field_1FC;
    int field_200;
    void(__cdecl* pTbefore_func)(PLAYER_WORK*);
    void(__cdecl* pTafter_func)(PLAYER_WORK*);
    int field_20C;
    int field_210;
    int16_t Poison_timer;
    uint8_t Pison_down;
    uint8_t field_217;
};

#pragma pack(pop)

enum {
    SCE_AUTO,
    SCE_DOOR,
    SCE_ITEM,
    SCE_NORMAL,
    SCE_MESSAGE,
    SCE_EVENT,
    SCE_FLAG_CHG,
    SCE_WATER,
    SCE_MOVE,
    SCE_SAVE,
    SCE_ITEMBOX,
    SCE_DAMAGE,
    SCE_STATUS,
    SCE_HIKIDASHI,
    SCE_WINDOWS,
};

using ScdOpcode = uint8_t;
using AotId = uint8_t;
using SceKind = uint8_t;

constexpr uint8_t SAT_4P = (1 << 7);

constexpr uint32_t GAME_FLAG_HAS_PARTNER = (1 << 28);
constexpr uint32_t GAME_FLAG_IS_PLAYER_1 = (1 << 31);

constexpr uint32_t MESSAGE_KIND_YOU_USED_KEY_X = 5;
constexpr uint32_t MESSAGE_KIND_YOU_UNLOCKED_IT = 10;
constexpr uint32_t MESSAGE_KIND_LOCKED_FROM_OTHER_SIDE = 11;
constexpr uint32_t MESSAGE_KIND_LEAVE_SHERRY_BEHIND = 8;
