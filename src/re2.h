#pragma once

#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)

using ItemType = uint8_t;

struct InventoryDef
{
    ItemType Type;                      // 0x0000
    uint8_t Quantity;                   // 0x0001
    uint8_t Part;                       // 0x0002
};
static_assert(sizeof(InventoryDef) == 0x03);

struct InventorySlot : InventoryDef
{
    uint8_t unk_04;                     // 0x0003
};
static_assert(sizeof(InventorySlot) == 0x04);

struct Vec16
{
    int16_t x;                          // 0x0000
    int16_t y;                          // 0x0002
    int16_t z;                          // 0x0004
};
static_assert(sizeof(Vec16) == 0x06);

struct Vec32
{
    int32_t x;                          // 0x0000
    int32_t y;                          // 0x0004
    int32_t z;                          // 0x0008
};
static_assert(sizeof(Vec32) == 0x0C);

struct Mat16
{
    int16_t m[9];                       // 0x0000
    int16_t field_12;                   // 0x0012
    int32_t t[3];                       // 0x0014
};
static_assert(sizeof(Mat16) == 0x20);

struct PartsW
{
    uint32_t Be_flg;                    // 0x0000
    uint8_t Attribute;                  // 0x0004
    uint8_t field_5;                    // 0x0005
    uint8_t field_6;                    // 0x0006
    uint8_t field_7;                    // 0x0007
    uint32_t pTmd;                      // 0x0008
    uint32_t pPacket;                   // 0x000C
    uint32_t pTmd2;                     // 0x0010
    uint32_t pPacket2;                  // 0x0014
    uint16_t M[9];                      // 0x0018
    uint16_t field_2A;                  // 0x002A
    Vec32 pos;                          // 0x002C
    Vec16 old_x;                        // 0x0038
    Vec16 old_x2;                       // 0x003E
    uint16_t dummy00;                   // 0x0044
    uint16_t dm00;                      // 0x0046
    Mat16 workm;                        // 0x0048
    uint16_t cdir_x;                    // 0x0068
    uint16_t cdir_y;                    // 0x006A
    uint16_t cdir_z;                    // 0x006C
    uint16_t dummy01;                   // 0x006E
    uint32_t poly_rgb;                  // 0x0070
    Mat16 super;                        // 0x0074
    uint8_t parts_no;                   // 0x0094
    uint8_t timer1;                     // 0x0095
    uint8_t timer2;                     // 0x0096
    uint8_t sabun_flg;                  // 0x0097
    uint16_t rot_x;                     // 0x0098
    uint16_t rot_y;                     // 0x009A
    uint16_t rot_z;                     // 0x009C
    uint16_t sabun_cnt0;                // 0x009E
    uint16_t timer0;                    // 0x00A0
    uint16_t timer3;                    // 0x00A2
    uint32_t* psa_dat_head;             // 0x00A4
    uint16_t size_x;                    // 0x00A8
    uint16_t size_y;                    // 0x00AA
    uint16_t size_z;                    // 0x00AC
    uint16_t dummy03;                   // 0x00AE
    PartsW* oya_parts;                  // 0x00B0
    uint16_t free[10];                  // 0x00B4
};
static_assert(sizeof(PartsW) == 0xC8);

struct Entity
{
    int32_t be_flg;                     // 0x0000
    uint8_t routine_0;                  // 0x0004
    uint8_t routine_1;                  // 0x0005
    uint8_t routine_2;                  // 0x0006
    uint8_t routine_3;                  // 0x0007
    uint8_t id;                         // 0x0008
    uint8_t sc_id;                      // 0x0009
    uint8_t at_obj_no;                  // 0x000A
    uint8_t at_sce_no;                  // 0x000B
    uint8_t work_no;                    // 0x000C
    uint8_t at_em_no;                   // 0x000D
    int16_t at_em_flg;                  // 0x000E
    int32_t attribute;                  // 0x0010
    int32_t pTmd;                       // 0x0014
    int32_t pPacket;                    // 0x0018
    int32_t pTmd2;                      // 0x001C
    int32_t pPacket2;                   // 0x0020
    uint16_t m[9];                      // 0x0024
    int16_t m_pad;                      // 0x0036
    Vec32 pos;                          // 0x0038
    Vec16 old_pos;                      // 0x0044
    Vec16 old_pos_2;                    // 0x004A
    int32_t dummy00;                    // 0x0050
    Mat16 workm;                        // 0x0054
    Vec16 cdir;                         // 0x0074
    int16_t dummy01;                    // 0x007A
    int32_t poly_rgb;                   // 0x007C
    Mat16* pSuper;                      // 0x0080
    int32_t atd[32];                    // 0x0084
    uint8_t tpage;                      // 0x0104
    uint8_t clut;                       // 0x0105
    uint8_t nFloor;                     // 0x0106
    uint8_t parts_num;                  // 0x0107
    int32_t pKan_t_ptr;                 // 0x0108
    int16_t water;                      // 0x010C
    int16_t type;                       // 0x010E
};
static_assert(sizeof(Entity) == 0x110);

struct PlayerEntity : Entity
{
    int32_t Sca_info;                   // 0x0110
    int32_t field_114;                  // 0x0114
    int32_t field_118;                  // 0x0118
    int32_t field_11C;                  // 0x011C
    int32_t field_120;                  // 0x0120
    int32_t field_124;                  // 0x0124
    int32_t field_128;                  // 0x0128
    int32_t field_12C;                  // 0x012C
    int32_t field_130;                  // 0x0130
    int32_t field_134;                  // 0x0134
    int32_t field_138;                  // 0x0138
    int32_t field_13C;                  // 0x013C
    int32_t field_140;                  // 0x0140
    Vec16 spd;                          // 0x0144
    uint8_t in_screen;                  // 0x014A
    uint8_t model_Tex_type;             // 0x014B
    uint8_t move_no;                    // 0x014C
    uint8_t move_cnt;                   // 0x014D
    uint8_t hokan_flg;                  // 0x014E
    uint8_t mplay_flg;                  // 0x014F
    uint8_t root_ck_cnt;                // 0x0150
    uint8_t d_life_u;                   // 0x0151
    uint8_t d_life_c;                   // 0x0152
    uint8_t d_life_d;                   // 0x0153
    int16_t status_flg;                 // 0x0154
    int16_t life;                       // 0x0156
    int16_t timer0;                     // 0x0158
    int16_t timer1;                     // 0x015A
    Vec16 f_pos;                        // 0x015C
    int16_t max_life;                   // 0x0162
    Vec16 base_pos;                     // 0x0164
    uint8_t timer2;                     // 0x016A
    uint8_t timer3;                     // 0x016B
    int32_t pKage_work;                 // 0x016C
    int32_t field_170;                  // 0x0170
    int32_t field_174;                  // 0x0174
    uint32_t* pNow_seq;                 // 0x0178
    int32_t pSeq_t_ptr;                 // 0x017C
    int32_t pSub0_kan_t_ptr;            // 0x0180
    int32_t pSub0_seq_t_ptr;            // 0x0184
    int32_t field_188;                  // 0x0188
    int32_t field_18C;                  // 0x018C
    int32_t field_190;                  // 0x0190
    int32_t field_194;                  // 0x0194
    int32_t pSin_parts_ptr;             // 0x0198
    int32_t pParts_tmd;                 // 0x019C
    int32_t pParts_packet;              // 0x01A0
    int32_t pM_uint8_t_ptr;             // 0x01A4
    int32_t pM_option_tmd;              // 0x01A8
    int32_t pM_option_packet;           // 0x01AC
    int32_t pM_Kage_work;               // 0x01B0
    int32_t pEnemy_ptr;                 // 0x01B4
    int32_t pEnemy_neck;                // 0x01B8
    int32_t pSa_dat;                    // 0x01BC
    uint8_t neck_flg;                   // 0x01C0
    uint8_t neck_no;                    // 0x01C1
    int16_t ground;                     // 0x01C2
    int16_t dest_x;                     // 0x01C4
    int16_t dest_z;                     // 0x01C6
    int16_t down_cnt;                   // 0x01C8
    uint8_t at_hit_flg;                 // 0x01CA
    uint8_t field_1CB;                  // 0x01CB
    int16_t sce_flg;                    // 0x01CC
    uint8_t em_set_flg;                 // 0x01CE
    uint8_t model_type;                 // 0x01CF
    uint16_t damage_flg;                // 0x01D0
    uint8_t damage_no;                  // 0x01D2
    uint8_t damage_cnt;                 // 0x01D3
    uint16_t sce_free0;                 // 0x01D4
    uint16_t sce_free1;                 // 0x01D6
    uint16_t sce_free2;                 // 0x01D8
    uint16_t sce_free3;                 // 0x01DA
    uint16_t spl_flg;                   // 0x01DC
    uint16_t parts0_pos_y;              // 0x01DE
    int32_t pOn_om;                     // 0x01E0
    int32_t field_1E8;                  // 0x01E4
    int32_t field_1EC;                  // 0x01E8
    int32_t field_1F0;                  // 0x01EC
    int32_t field_1F4;                  // 0x01F0
    int32_t field_1F8;                  // 0x01F4
    int32_t field_1FC;                  // 0x01F8
    int32_t field_200;                  // 0x01FC
    void* pTbefore_func;                // 0x0200
    void* pTafter_func;                 // 0x0204
    int32_t field_20C;                  // 0x0208
    int32_t field_210;                  // 0x020C
    int16_t poison_timer;               // 0x0210
    uint8_t pison_down;                 // 0x0212
    uint8_t field_217;                  // 0x0213
};
static_assert(sizeof(PlayerEntity) == 0x214);

struct SceTask
{
    uint8_t routine;                    // 0x0000
    uint8_t status;                     // 0x0001
    uint8_t sub_ctr;                    // 0x0002
    uint8_t task_level;                 // 0x0003
    uint8_t ifel_ctr[4];                // 0x0004
    uint8_t loop_ctr[4];                // 0x0008
    uint8_t loop_if_class[16];          // 0x000C
    uint8_t* data;                      // 0x001C
    int32_t lstack[16];                 // 0x0020
    int32_t lbreak[16];                 // 0x0060
    int16_t lcnt[16];                   // 0x00A0
    int32_t stack[32];                  // 0x00C0
    uint8_t** sp;                       // 0x0140
    int32_t ret_addr[4];                // 0x0144
    Entity* work;                       // 0x0154
    int16_t spd[3];                     // 0x0158
    int16_t dspd[3];                    // 0x015E
    int16_t aspd[3];                    // 0x0164
    int16_t adspd[3];                   // 0x016A
    int32_t r_no_bak;                   // 0x0170
};
static_assert(sizeof(SceTask) == 0x174);

struct GameTable
{
    uint8_t pad_0000[10020764];         // 0x0000
    uint8_t table_start;                // 0x98E79C
    uint8_t pad_98E79D[519];            // 0x98E79D
    uint8_t inventory_size;             // 0x98E9A4
    uint8_t pad_98E9A5[23];             // 0x98E9A5
    uint16_t num_saves;                 // 0x98E9BC
    uint8_t pad_98E9BE[10];             // 0x98E9BE
    uint16_t bgm_table[142];            // 0x98E9C8
    uint8_t pad_98EAE4[48];             // 0x98EAE4
    uint16_t current_stage;             // 0x98EB14
    uint16_t current_room;              // 0x98EB16
    uint16_t current_cut;               // 0x98EB18
    uint16_t last_cut;                  // 0x98EB1A
    uint16_t word_98EB1C;               // 0x98EB1C
    uint16_t rng;                       // 0x98EB1E
    uint16_t word_98EB20;               // 0x98EB20
    uint16_t word_98EB22;               // 0x98EB22
    uint16_t next_pld;                  // 0x98EB24
    uint16_t word_98EB26;               // 0x98EB26
    uint16_t word_98EB28;               // 0x98EB28
    uint16_t word_98EB2A;               // 0x98EB2A
    uint32_t fg_scenario[8];            // 0x98EB2C
    uint32_t fg_common[8];              // 0x98EB4C
    uint32_t fg_room;                   // 0x98EB6C
    uint32_t dword_98EB70;              // 0x98EB70
    uint32_t fg_enemy_0[8];             // 0x98EB74
    uint32_t fg_enemy_1[8];             // 0x98EB94
    uint32_t fg_item[7];                // 0x98EBB4
    uint32_t dword_98EBD0;              // 0x98EBD0
    uint8_t pad_98EBD4[344];            // 0x98EBD4
    uint32_t door_locks[2];             // 0x98ED2C
    InventorySlot inventory[11];        // 0x98ED34
};
static_assert(sizeof(GameTable) == 0x98ED60);

struct Unknown68A204
{
    uint8_t pad_0000[9];                // 0x0000
    uint8_t var_09;                     // 0x0009
    uint8_t pad_000A[3];                // 0x000A
    uint8_t var_0D;                     // 0x000D
    uint8_t pad_000E[5];                // 0x000E
    uint8_t var_13;                     // 0x0013
};
static_assert(sizeof(Unknown68A204) == 0x14);

struct Unknown6949F8
{
    uint8_t pad_0000[12];               // 0x0000
    uint8_t var_0C;                     // 0x000C
    uint8_t pad_000D[1];                // 0x000D
    uint8_t var_0E;                     // 0x000E
};
static_assert(sizeof(Unknown6949F8) == 0x0F);

struct Unknown988628
{
    uint8_t pad_0000[268];              // 0x0000
    uint16_t var_10C;                   // 0x010C
};
static_assert(sizeof(Unknown988628) == 0x10E);

struct ObjectEntity : Entity
{
    uint32_t sca_info;                  // 0x0110
    uint32_t sca_hit_data;              // 0x0114
    int16_t sca_old_x;                  // 0x0118
    int16_t sca_old_z;                  // 0x011A
    Mat16 super_matrix;                 // 0x011C
    Vec16 super_vector;                 // 0x013C
    uint8_t push_cnt;                   // 0x0142
    uint8_t free0;                      // 0x0143
    uint8_t free1;                      // 0x0144
    uint8_t free2;                      // 0x0145
    uint32_t sin_parts_ptr;             // 0x0146
    PartsW parts;                       // 0x014A
};
static_assert(sizeof(ObjectEntity) == 0x212);

struct HudInfo
{
    uint8_t routine;                    // 0x0000
    uint8_t var_01;                     // 0x0001
    uint8_t var_0C;                     // 0x0002
    uint8_t var_24;                     // 0x0003
    uint8_t var_25;                     // 0x0004
};
static_assert(sizeof(HudInfo) == 0x05);

#pragma pack(pop)

