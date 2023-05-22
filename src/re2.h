#pragma once

#include <cstdint>

using ScdOpcode = uint8_t;
using AotId = uint8_t;
using SceKind = uint8_t;
using ItemType = uint8_t;
using PldType = uint8_t;
using HudKind = uint8_t;

using Action = void (*)();

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

struct SceAotMessageData
{
    uint16_t var_00;
    uint16_t var_02;
    uint16_t var_04;
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

struct SceAotDoorData
{
    int16_t TargetX;
    int16_t TargetY;
    int16_t TargetZ;
    int16_t TargetD;
    uint8_t TargetStage;
    uint8_t TargetRoom;
    uint8_t TargetCut;
    uint8_t TargetFloor;
    uint8_t Texture;
    uint8_t DoorType;
    uint8_t KnockType;
    uint8_t LockId;
    uint8_t KeyType;
    uint8_t Free;
};

struct SceAotDoor
{
    SceAot Aot;
    SceAotDoorData Door;
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

struct SceAotItemData
{
    uint8_t type;
    uint8_t pad_01;
    uint16_t amount;
    uint16_t flag;
    uint8_t md1;
    uint8_t action;
};

struct SceAotItem
{
    SceAot Aot;
    SceAotItemData Item;
};

struct ScdSceAotItem
{
    uint8_t Opcode;
    uint8_t Id;
    SceAotItem Data;
};

struct ScdSceBgmControl
{
    uint8_t Opcode;
    uint8_t var_01;
    uint8_t var_02;
    uint8_t var_03;
    uint8_t var_04;
    uint8_t var_05;
};

struct ScdSceBgmTblSet
{
    uint8_t Opcode;
    uint8_t pad_01;
    uint16_t roomstage;
    uint16_t var_04;
    uint16_t var_06;
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

struct InventoryDef
{
    ItemType Type;
    uint8_t Quantity;
    uint8_t Part;
};

struct InventorySlot : InventoryDef
{
    uint8_t unk_04;
};

struct Unknown68A204
{
    uint8_t pad_00[0x09 - 0x00];
    uint8_t var_09;
    uint8_t pad_0A[0x0D - 0x0A];
    uint8_t var_0D;
    uint8_t pad_0E[0x13 - 0x0E];
    uint8_t var_13;
};

struct Unknown689C60
{
    uint8_t pad_000[0x008];
    uint8_t var_008;
    uint8_t pad_009[0x155 - 0x008];
    uint8_t var_155;
    uint8_t pad_156[0x1E4 - 0x155];
    uint32_t var_1E4;
};

struct Unknown6949F8
{
    uint8_t pad_00[0x0C];
    uint8_t var_0C;
    uint8_t pad_0D;
    uint8_t var_0E;
};

struct Unknown988628
{
    uint8_t pad_000[0x10C];
    uint16_t var_10C;
};

struct Unknown689CA8
{
    uint8_t pad_00[0x78];
    int16_t var_78;
    uint8_t pad_7A[4];
};
static_assert(sizeof(Unknown689CA8) == 0x7E);

struct HudInfo
{
    uint8_t routine;
    uint8_t var_01;
    uint8_t pad_02[0x0C - 0x02];
    uint8_t var_0C;
    uint8_t pad_0D[0x24 - 0x0D];
    uint8_t var_24;
    uint8_t var_25;
};

struct Unknown98A720
{
    uint8_t var_00;
    uint8_t pad_01[503];
};
static_assert(sizeof(Unknown98A720) == 504);

// 0x98E79C - 0x98EF34
struct GameTable
{
    uint8_t pad_000[0x98E9A4 - 0x98E79C]; // 0x0098E79C
    uint8_t inventory_size;               // 0x0098E9A4
    uint8_t pad_209[0x98E9BC - 0x98E9A5];
    uint16_t num_saves;                   // 0x0098E9BC
    uint8_t pad_222[0x98E9C8 - 0x98E9BE];
    uint16_t bgm_table[142];              // 0x0098E9C8
    uint8_t pad_348[0x98EB14 - 0x98EAE4];
    uint16_t current_stage;               // 0x0098EB14
    uint16_t current_room;                // 0x0098EB16
    uint16_t current_cut;                 // 0x0098EB18
    uint16_t last_cut;                    // 0x0098EB1A
    uint8_t pad_380[0x98ED2C - 0x98EB1C];
    uint32_t door_locks[2];               // 0x0098ED2C
    InventorySlot inventory[11];          // 0x0098ED34
    uint8_t pad_005[0x98EF34 - 0x98ED60];
};
static_assert(sizeof(GameTable) == 1944);

#pragma pack(pop)

enum
{
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

enum
{
    ITEM_TYPE_NONE = 0,
    ITEM_TYPE_KNIFE = 1,
    ITEM_TYPE_HANDGUN_LEON = 2,
    ITEM_TYPE_HANDGUN_CLAIRE = 3,
    ITEM_TYPE_HANDGUN_COLT_SAA = 13,
    ITEM_TYPE_AMMO_HANDGUN = 20,
    ITEM_TYPE_AMMO_SHOTGUN = 21,
    ITEM_TYPE_AMMO_MAGNUM = 22,
    ITEM_TYPE_AMMO_FLAMETHROWER = 23,
    ITEM_TYPE_AMMO_EXPLOSIVE_ROUNDS = 24,
    ITEM_TYPE_AMMO_FLAME_ROUNDS = 25,
    ITEM_TYPE_AMMO_ACID_ROUNDS = 26,
    ITEM_TYPE_AMMO_SMG = 27,
    ITEM_TYPE_AMMO_SPARKSHOT = 28,
    ITEM_TYPE_AMMO_BOWGUN = 29,
    ITEM_TYPE_INK_RIBBON = 30,
    ITEM_TYPE_FIRST_AID_SPRAY = 35,
    ITEM_TYPE_HERB_GGB = 45,
    ITEM_TYPE_LIGHTER = 47,
    ITEM_TYPE_LOCKPICK = 48,
    ITEM_TYPE_PHOTO_SHERRY = 49,
    ITEM_TYPE_PHOTO_ADA = 87,
};

enum
{
    PLD_LEON_0,
    PLD_CLAIRE_0,
    PLD_LEON_1,
    PLD_CLAIRE_1,
    PLD_LEON_2,
    PLD_CLAIRE_2,
    PLD_LEON_3,
    PLD_CLAIRE_3,
    PLD_LEON_4,
    PLD_CLAIRE_4,
    PLD_LEON_5,
    PLD_CHRIS,
    PLD_HUNK,
    PLD_TOFU,
    PLD_ADA,
    PLD_SHERRY
};

enum
{
    HUD_MODE_INVENTORY,
    HUD_MODE_ITEM_BOX,
    HUD_MODE_PICKUP_ITEM,
    HUD_MODE_MAP_1,
    HUD_MODE_MAP_2,
};

constexpr uint8_t SAT_4P = (1 << 7);

constexpr uint32_t GAME_FLAG_15 = (1 << 15);
constexpr uint32_t GAME_FLAG_HAS_PARTNER = (1 << 28);
constexpr uint32_t GAME_FLAG_IS_PLAYER_1 = (1 << 31);

constexpr uint32_t MESSAGE_KIND_INK_RIBBON_REQUIRED_TO_SAVE = 0;
constexpr uint32_t MESSAGE_KIND_WILL_YOU_USE_USE_INK_RIBBON = 1;
constexpr uint32_t MESSAGE_KIND_YOU_USED_KEY_X = 5;
constexpr uint32_t MESSAGE_KIND_YOU_UNLOCKED_IT = 10;
constexpr uint32_t MESSAGE_KIND_LOCKED_FROM_OTHER_SIDE = 11;
constexpr uint32_t MESSAGE_KIND_LEAVE_SHERRY_BEHIND = 8;

constexpr uint8_t INVENTORY_INDEX_SPECIAL = 10;
constexpr uint8_t FULL_INVENTORY_SIZE = 11;
