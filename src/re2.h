#pragma once

#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)

using ItemType = uint8_t;

struct ItemMixDefinition
{
    uint8_t object_item_id;             // 0x0000
    uint8_t mix_type;                   // 0x0001
    uint8_t result_item;                // 0x0002
    uint8_t mixed_pix_no;               // 0x0003
};
static_assert(sizeof(ItemMixDefinition) == 0x04);

struct ItemTypeDefinition
{
    uint8_t max;                        // 0x0000
    uint8_t var_01;                     // 0x0001
    uint8_t var_02;                     // 0x0002
    uint8_t var_03;                     // 0x0003
    ItemMixDefinition* mix;             // 0x0004
};
static_assert(sizeof(ItemTypeDefinition) == 0x08);

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

struct ItemboxItem
{
    uint8_t Type;                       // 0x0000
    uint8_t Quantity;                   // 0x0001
    uint8_t Part;                       // 0x0002
    uint8_t var_04;                     // 0x0003
};
static_assert(sizeof(ItemboxItem) == 0x04);

struct Vec16
{
    int16_t x;                          // 0x0000
    int16_t y;                          // 0x0002
    int16_t z;                          // 0x0004
};
static_assert(sizeof(Vec16) == 0x06);

struct Vec16p : Vec16
{
    int16_t pad_6;                      // 0x0006
};
static_assert(sizeof(Vec16p) == 0x08);

struct Vec16d : Vec16
{
    int16_t d;                          // 0x0006
};
static_assert(sizeof(Vec16d) == 0x08);

struct Vec32
{
    int32_t x;                          // 0x0000
    int32_t y;                          // 0x0004
    int32_t z;                          // 0x0008
};
static_assert(sizeof(Vec32) == 0x0C);

struct Vec32p : Vec32
{
    int32_t pad_0C;                     // 0x000C
};
static_assert(sizeof(Vec32p) == 0x10);

struct Vec32d : Vec32
{
    int32_t d;                          // 0x000C
};
static_assert(sizeof(Vec32d) == 0x10);

struct Mat16
{
    int16_t m[9];                       // 0x0000
    int16_t field_12;                   // 0x0012
    Vec32 pos;                          // 0x0014
};
static_assert(sizeof(Mat16) == 0x20);

struct VCut
{
    uint8_t be_flg;                     // 0x0000
    uint8_t nFloor;                     // 0x0001
    uint8_t fCut;                       // 0x0002
    uint8_t tCut;                       // 0x0003
    int16_t xz[8];                      // 0x0004
};
static_assert(sizeof(VCut) == 0x14);

struct TmdEntry;

struct Edd;

struct Emr;

struct PartsW
{
    uint32_t Be_flg;                    // 0x0000
    uint8_t Attribute;                  // 0x0004
    uint8_t field_5;                    // 0x0005
    uint8_t field_6;                    // 0x0006
    uint8_t field_7;                    // 0x0007
    TmdEntry* pTmd;                     // 0x0008
    uint32_t pPacket;                   // 0x000C
    TmdEntry* pTmd2;                    // 0x0010
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
    uint32_t super;                     // 0x0074
    uint8_t parts_no;                   // 0x0078
    uint8_t timer1;                     // 0x0079
    uint8_t timer2;                     // 0x007A
    uint8_t sabun_flg;                  // 0x007B
    uint16_t rot_x;                     // 0x007C
    uint16_t rot_y;                     // 0x007E
    uint16_t rot_z;                     // 0x0080
    uint16_t sabun_cnt0;                // 0x0082
    uint16_t timer0;                    // 0x0084
    uint16_t timer3;                    // 0x0086
    uint32_t* psa_dat_head;             // 0x0088
    uint16_t size_x;                    // 0x008C
    uint16_t size_y;                    // 0x008E
    uint16_t size_z;                    // 0x0090
    uint16_t dummy03;                   // 0x0092
    PartsW* oya_parts;                  // 0x0094
    uint16_t free[10];                  // 0x0098
};
static_assert(sizeof(PartsW) == 0xAC);

struct Kage
{
    uint8_t pad_0000[4];                // 0x0000
    uint16_t var_04;                    // 0x0004
    uint16_t var_06;                    // 0x0006
    uint8_t pad_0008[20];               // 0x0008
    uint32_t var_1C;                    // 0x001C
    uint8_t pad_0020[36];               // 0x0020
    uint32_t var_44;                    // 0x0044
};
static_assert(sizeof(Kage) == 0x48);

struct At
{
    Vec32 pos;                          // 0x0000
    int16_t w;                          // 0x000C
    int16_t d;                          // 0x000E
    Vec16 ofs;                          // 0x0010
    int16_t at_w;                       // 0x0016
    int16_t at_d;                       // 0x0018
    int16_t at_h;                       // 0x001A
    int16_t atw_x;                      // 0x001C
    int16_t atw_z;                      // 0x001E
};
static_assert(sizeof(At) == 0x20);

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
    TmdEntry* pTmd;                     // 0x0014
    int32_t pPacket;                    // 0x0018
    TmdEntry* pTmd2;                    // 0x001C
    int32_t pPacket2;                   // 0x0020
    Mat16 m;                            // 0x0024
    Vec16 old_pos;                      // 0x0044
    Vec16 old_pos_2;                    // 0x004A
    int32_t dummy00;                    // 0x0050
    Mat16 workm;                        // 0x0054
    Vec16p cdir;                        // 0x0074
    int32_t poly_rgb;                   // 0x007C
    Mat16* pSuper;                      // 0x0080
    At atd[4];                          // 0x0084
    uint8_t tpage;                      // 0x0104
    uint8_t clut;                       // 0x0105
    uint8_t nFloor;                     // 0x0106
    uint8_t parts_num;                  // 0x0107
    Emr* pKan_t_ptr;                    // 0x0108
    int16_t water;                      // 0x010C
    uint16_t type;                      // 0x010E
};
static_assert(sizeof(Entity) == 0x110);

struct ActorEntity : Entity
{
    int32_t Sca_info;                   // 0x0110
    int32_t field_114;                  // 0x0114
    int16_t sca_old_x;                  // 0x0118
    int16_t sca_old_z;                  // 0x011A
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
    Kage* pKage_work;                   // 0x016C
    int32_t field_170;                  // 0x0170
    int32_t field_174;                  // 0x0174
    uint32_t* pNow_seq;                 // 0x0178
    Edd* pSeq_t_ptr;                    // 0x017C
    Emr* pSub0_kan_t_ptr;               // 0x0180
    Edd* pSub0_seq_t_ptr;               // 0x0184
    Emr* pSub1_kan_t_ptr;               // 0x0188
    Edd* pSub1_seq_t_ptr;               // 0x018C
    int32_t field_190;                  // 0x0190
    int32_t field_194;                  // 0x0194
    PartsW* pSin_parts_ptr;             // 0x0198
    int32_t pParts_tmd;                 // 0x019C
    int32_t pParts_packet;              // 0x01A0
    int32_t pM_uint8_t_ptr;             // 0x01A4
    int32_t pM_option_tmd;              // 0x01A8
    int32_t pM_option_packet;           // 0x01AC
    int32_t pM_Kage_work;               // 0x01B0
    Entity* pEnemy_ptr;                 // 0x01B4
    int32_t pEnemy_neck;                // 0x01B8
    void* pSa_dat;                      // 0x01BC
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
    uint32_t pT_xz;                     // 0x01E0
    uint32_t pOn_om;                    // 0x01E4
    int32_t nOba;                       // 0x01E8
    uint8_t attw_timer;                 // 0x01EC
    uint8_t attw_seq_no;                // 0x01ED
    uint16_t eff_at_r;                  // 0x01EE
    int32_t l_pl;                       // 0x01F0
    int32_t l_spl;                      // 0x01F4
    int16_t dir_spl;                    // 0x01F8
    uint8_t sound_bank;                 // 0x01FA
    uint8_t area_no;                    // 0x01FB
    int32_t tmp_routine;                // 0x01FC
    int32_t pDamage_work;               // 0x0200
    void* pTbefore_func;                // 0x0204
    void* pTafter_func;                 // 0x0208
    Vec16 spd_base;                     // 0x020C
    int16_t kage_ofs;                   // 0x0212
    int16_t poison_timer;               // 0x0214
    uint8_t pison_down;                 // 0x0216
    uint8_t field_217;                  // 0x0217
};
static_assert(sizeof(ActorEntity) == 0x218);

struct EnemyEntity : ActorEntity
{
    uint8_t var_218;                    // 0x0218
    uint8_t var_219;                    // 0x0219
    uint8_t pad_021A[2];                // 0x021A
    uint8_t var_21C;                    // 0x021C
    uint8_t var_21D;                    // 0x021D
    uint8_t var_21E;                    // 0x021E
    uint8_t var_21F;                    // 0x021F
    uint8_t var_220;                    // 0x0220
    uint8_t var_221;                    // 0x0221
    uint8_t var_222;                    // 0x0222
    uint8_t var_223;                    // 0x0223
    uint8_t pad_0224[3];                // 0x0224
    uint8_t var_227;                    // 0x0227
    uint8_t pad_0228[4];                // 0x0228
    uint8_t var_22C;                    // 0x022C
    uint8_t var_22D;                    // 0x022D
    uint8_t var_22E;                    // 0x022E
    uint8_t pad_022F[1];                // 0x022F
    uint8_t var_230;                    // 0x0230
    uint8_t var_231;                    // 0x0231
    uint8_t var_232;                    // 0x0232
    uint8_t var_233;                    // 0x0233
    uint8_t var_234;                    // 0x0234
    uint8_t var_235;                    // 0x0235
    uint8_t var_236;                    // 0x0236
    uint8_t pad_0237[3];                // 0x0237
    int8_t var_23A;                     // 0x023A
    uint8_t pad_023B[1];                // 0x023B
    uint8_t var_23C;                    // 0x023C
    uint8_t var_23D;                    // 0x023D
    uint8_t pad_023E[1];                // 0x023E
    uint8_t var_23F;                    // 0x023F
    uint8_t pad_0240[7];                // 0x0240
    uint8_t pad_247;                    // 0x0247
};
static_assert(sizeof(EnemyEntity) == 0x248);

struct PlayerEntity : ActorEntity
{
};

struct ObjectEntity : Entity
{
    uint32_t sca_info;                  // 0x0110
    uint32_t sca_hit_data;              // 0x0114
    int16_t sca_old_x;                  // 0x0118
    int16_t sca_old_z;                  // 0x011A
    Mat16 super_matrix;                 // 0x011C
    Vec16p super_vector;                // 0x013C
    uint8_t push_cnt;                   // 0x0144
    uint8_t free0;                      // 0x0145
    uint8_t free1;                      // 0x0146
    uint8_t free2;                      // 0x0147
    uint32_t sin_parts_ptr;             // 0x0148
    PartsW parts;                       // 0x014C
};
static_assert(sizeof(ObjectEntity) == 0x1F8);

struct DoorEntity : Entity
{
    uint32_t sca_info;                  // 0x0110
    uint32_t sca_hit_data;              // 0x0114
    int16_t sca_old_x;                  // 0x0118
    int16_t sca_old_z;                  // 0x011A
    Mat16 super_matrix;                 // 0x011C
    Vec16p super_vector;                // 0x013C
    uint8_t attribute_2;                // 0x0144
    uint8_t attribute_3;                // 0x0145
    uint16_t model_no;                  // 0x0146
    uint16_t free0;                     // 0x0148
    uint16_t free2;                     // 0x014A
};
static_assert(sizeof(DoorEntity) == 0x14C);

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

struct BgmTableEntry
{
    uint8_t main;                       // 0x0000
    uint8_t sub0;                       // 0x0001
    uint8_t sub1;                       // 0x0002
};
static_assert(sizeof(BgmTableEntry) == 0x03);

struct RdtHeader
{
    uint8_t num_sprites;                // 0x0000
    uint8_t num_cuts;                   // 0x0001
    uint8_t num_models;                 // 0x0002
    uint8_t num_items;                  // 0x0003
    uint8_t num_doors;                  // 0x0004
    uint8_t room_at;                    // 0x0005
    uint8_t reverb_lv;                  // 0x0006
    uint8_t unknown7;                   // 0x0007
};
static_assert(sizeof(RdtHeader) == 0x08);

struct Rdt
{
    RdtHeader header;                   // 0x0000
    void* offsets[23];                  // 0x0008
};
static_assert(sizeof(Rdt) == 0x64);

struct RdtCamera
{
    uint16_t flags;                     // 0x0000
    uint16_t perspective;               // 0x0002
    int32_t pos_x;                      // 0x0004
    int32_t pos_y;                      // 0x0008
    int32_t pos_z;                      // 0x000C
    int32_t target_x;                   // 0x0010
    int32_t target_y;                   // 0x0014
    int32_t target_z;                   // 0x0018
    uint32_t offset;                    // 0x001C
};
static_assert(sizeof(RdtCamera) == 0x20);

struct RdtModel
{
    uint32_t texture_offset;            // 0x0000
    uint32_t model_offset;              // 0x0004
};
static_assert(sizeof(RdtModel) == 0x08);

struct DoorInfo
{
    void* prepacket;                    // 0x0000
    TmdEntry* tmd_adr;                  // 0x0004
    void* packettop;                    // 0x0008
    int32_t var_0C;                     // 0x000C
    int32_t var_10;                     // 0x0010
    int32_t var_14;                     // 0x0014
    uint8_t pad_0018[524];              // 0x0018
    uint32_t edh_offset;                // 0x0224
    uint32_t vb_offset;                 // 0x0228
    uint16_t ctr1;                      // 0x022C
    uint16_t ctr2;                      // 0x022E
    Vec16p v0;                          // 0x0230
    Vec16p v1;                          // 0x0238
    Vec16p v2;                          // 0x0240
    uint16_t sound_flg;                 // 0x0248
    uint16_t div_max;                   // 0x024A
};
static_assert(sizeof(DoorInfo) == 0x24C);

struct DrMove
{
    uint32_t tag;                       // 0x0000
    uint32_t code[5];                   // 0x0004
};
static_assert(sizeof(DrMove) == 0x18);

struct PsxRect
{
    int16_t x;                          // 0x0000
    int16_t y;                          // 0x0002
    int16_t w;                          // 0x0004
    int16_t h;                          // 0x0006
};
static_assert(sizeof(PsxRect) == 0x08);

struct Tile
{
    uint32_t tag;                       // 0x0000
    uint8_t r;                          // 0x0004
    uint8_t g;                          // 0x0005
    uint8_t b;                          // 0x0006
    uint8_t code;                       // 0x0007
    PsxRect psxRect;                    // 0x0008
};
static_assert(sizeof(Tile) == 0x10);

struct DrMode
{
    uint32_t tag;                       // 0x0000
    uint32_t code[2];                   // 0x0004
};
static_assert(sizeof(DrMode) == 0x0C);

struct Fade
{
    int16_t kido;                       // 0x0000
    int16_t add;                        // 0x0002
    uint8_t hrate;                      // 0x0004
    uint8_t mask_r;                     // 0x0005
    uint8_t mask_g;                     // 0x0006
    uint8_t mask_b;                     // 0x0007
    uint8_t pri;                        // 0x0008
    uint8_t dm00[3];                    // 0x0009
    Tile tiles[2];                      // 0x000C
    DrMode dr_modes[2];                 // 0x002C
    PsxRect psxRect;                    // 0x0044
};
static_assert(sizeof(Fade) == 0x4C);

struct CCPartsWork
{
    uint8_t cc_ctr;                     // 0x0000
    uint8_t cc_cnt;                     // 0x0001
    uint8_t cc_wait;                    // 0x0002
    uint8_t cc_num;                     // 0x0003
    int16_t cc_pos_x;                   // 0x0004
    int16_t cc_pos_y;                   // 0x0006
    DrMove cc_dr_move[2];               // 0x0008
    PsxRect cc_dr_rect[2];              // 0x0038
    ObjectEntity* obj;                  // 0x0048
};
static_assert(sizeof(CCPartsWork) == 0x4C);

struct CCWork
{
    CCPartsWork cc_parts[30];           // 0x0000
    int16_t ccol_old;                   // 0x08E8
    uint8_t ccol_no;                    // 0x08EA
    uint8_t ctex_old;                   // 0x08EB
};
static_assert(sizeof(CCWork) == 0x8EC);

struct EnemyInitEntry
{
    uint16_t type;                      // 0x0000
    uint16_t enabled;                   // 0x0002
};
static_assert(sizeof(EnemyInitEntry) == 0x04);

struct Input
{
    uint8_t mapping[31];                // 0x0000
    uint8_t pad_001F[5];                // 0x001F
    uint32_t keyboard_raw_state;        // 0x0024
    uint8_t pad_0028[464];              // 0x0028
    uint32_t var_1F8;                   // 0x01F8
    uint32_t gamepad_raw_state;         // 0x01FC
    uint8_t pad_0200[464];              // 0x0200
    uint32_t var_3D0;                   // 0x03D0
    uint8_t pad_03D4[14160];            // 0x03D4
    uint32_t var_3B24;                  // 0x3B24
    uint32_t keyboard;                  // 0x3B28
};
static_assert(sizeof(Input) == 0x3B2C);

struct DemoPlayer
{
    uint8_t stage_no;                   // 0x0000
    uint8_t room_no;                    // 0x0001
    uint8_t cut_no;                     // 0x0002
    uint8_t equip_id;                   // 0x0003
    uint8_t equip_no;                   // 0x0004
    uint8_t key_idx;                    // 0x0005
    uint8_t id;                         // 0x0006
    uint8_t pad;                        // 0x0007
    InventorySlot inventory[11];        // 0x0008
    uint16_t frames;                    // 0x0034
    Vec16 pos;                          // 0x0036
    int16_t cdir_y;                     // 0x003C
    uint16_t input[900];                // 0x003E
};
static_assert(sizeof(DemoPlayer) == 0x746);

struct Task
{
    uint16_t status;                    // 0x0000
    uint16_t sleep;                     // 0x0002
    void* fn;                           // 0x0004
    uint8_t var_08;                     // 0x0008
    uint8_t var_09;                     // 0x0009
    uint8_t var_0A;                     // 0x000A
    uint8_t var_0B;                     // 0x000B
    uint8_t var_0C;                     // 0x000C
    uint8_t var_0D;                     // 0x000D
    uint8_t var_0E;                     // 0x000E
    uint8_t var_0F;                     // 0x000F
    uint8_t var_10;                     // 0x0010
    uint8_t var_11;                     // 0x0011
    uint8_t var_12;                     // 0x0012
    uint8_t var_13;                     // 0x0013
    uint32_t var_14;                    // 0x0014
    uint32_t var_18;                    // 0x0018
    uint32_t var_1C;                    // 0x001C
    uint32_t var_20;                    // 0x0020
};
static_assert(sizeof(Task) == 0x24);

struct MapData
{
    uint8_t u;                          // 0x0000
    uint8_t v;                          // 0x0001
    uint8_t w;                          // 0x0002
    uint8_t h;                          // 0x0003
    int16_t c_pos_x;                    // 0x0004
    int16_t c_pos_y;                    // 0x0006
    int16_t b_pos_x;                    // 0x0008
    int16_t b_pos_y;                    // 0x000A
    uint8_t flg;                        // 0x000C
    uint8_t change_flg;                 // 0x000D
    uint8_t pl0_flg;                    // 0x000E
    uint8_t pl1_flg;                    // 0x000F
};
static_assert(sizeof(MapData) == 0x10);

struct MapStageInfo
{
    MapData* data;                      // 0x0000
    uint8_t room_num;                   // 0x0004
    uint8_t map;                        // 0x0005
    int16_t file_no;                    // 0x0006
};
static_assert(sizeof(MapStageInfo) == 0x08);

struct Unknown6949F8
{
    uint8_t pad_0000[4];                // 0x0000
    uint16_t var_04;                    // 0x0004
    uint16_t var_06;                    // 0x0006
    uint16_t var_08;                    // 0x0008
    uint16_t var_0A;                    // 0x000A
    uint8_t var_0C;                     // 0x000C
    uint8_t pad_000D[1];                // 0x000D
    uint8_t var_0E;                     // 0x000E
};
static_assert(sizeof(Unknown6949F8) == 0x0F);

struct Prim
{
    Prim* pNext;                        // 0x0000
    int32_t type;                       // 0x0004
};
static_assert(sizeof(Prim) == 0x08);

struct PrimLine : Prim
{
    int16_t x0;                         // 0x0008
    int16_t y0;                         // 0x000A
    int16_t x1;                         // 0x000C
    int16_t y1;                         // 0x000E
    uint32_t color0;                    // 0x0010
};
static_assert(sizeof(PrimLine) == 0x14);

struct PrimLine2 : PrimLine
{
    uint32_t color1;                    // 0x0014
};
static_assert(sizeof(PrimLine2) == 0x18);

struct PrimSprite : Prim
{
    uint32_t texture;                   // 0x0008
    uint32_t var_0C;                    // 0x000C
    int16_t x0;                         // 0x0010
    int16_t y0;                         // 0x0012
    int16_t x1;                         // 0x0014
    int16_t y1;                         // 0x0016
    uint8_t u0;                         // 0x0018
    uint8_t v0;                         // 0x0019
    uint8_t u1;                         // 0x001A
    uint8_t v1;                         // 0x001B
};
static_assert(sizeof(PrimSprite) == 0x1C);

struct PrimScaler : Prim
{
    uint32_t prj;                       // 0x0008
    uint32_t rgb0;                      // 0x000C
    uint32_t rgb1;                      // 0x0010
    uint32_t c_x;                       // 0x0014
    uint32_t c_y;                       // 0x0018
    float rate_x;                       // 0x001C
    float rate_y;                       // 0x0020
    uint32_t var_24;                    // 0x0024
    uint32_t var_28;                    // 0x0028
    uint32_t var_2C;                    // 0x002C
    uint32_t var_30;                    // 0x0030
};
static_assert(sizeof(PrimScaler) == 0x34);

struct MarniFont
{
    uint32_t bitmap;                    // 0x0000
    uint32_t width;                     // 0x0004
    uint32_t height;                    // 0x0008
};
static_assert(sizeof(MarniFont) == 0x0C);

struct MarniRes
{
    uint32_t width;                     // 0x0000
    uint32_t height;                    // 0x0004
    uint32_t depth;                     // 0x0008
    uint32_t fullscreen;                // 0x000C
};
static_assert(sizeof(MarniRes) == 0x10);

struct MarniSurfaceDesc
{
    uint8_t r_shift;                    // 0x0000
    uint8_t r_mask;                     // 0x0001
    uint8_t r_bitcnt;                   // 0x0002
    uint8_t g_shift;                    // 0x0003
    uint8_t g_mask;                     // 0x0004
    uint8_t g_bitcnt;                   // 0x0005
    uint8_t b_shift;                    // 0x0006
    uint8_t b_mask;                     // 0x0007
    uint8_t b_bitcnt;                   // 0x0008
    uint8_t a_shift;                    // 0x0009
    uint8_t a_mask;                     // 0x000A
    uint8_t a_bitcnt;                   // 0x000B
};
static_assert(sizeof(MarniSurfaceDesc) == 0x0C);

struct MarniSurfaceVTBL
{
    uint32_t fill;                      // 0x0000
    uint32_t blt;                       // 0x0004
    uint32_t null_;                     // 0x0008
    uint32_t pal_blt;                   // 0x000C
    void* lock_fn;                      // 0x0010
    void* unlock_fn;                    // 0x0014
    uint32_t pal_lock;                  // 0x0018
    uint32_t pal_unlock;                // 0x001C
    void* release_fn;                   // 0x0020
};
static_assert(sizeof(MarniSurfaceVTBL) == 0x24);

struct MarniSurface2
{
    MarniSurfaceVTBL* vtbl;             // 0x0000
    void* pBitmap;                      // 0x0004
    void* pPalette;                     // 0x0008
    uint8_t bLocked;                    // 0x000C
    uint8_t bPalLocked;                 // 0x000D
    MarniSurfaceDesc desc;              // 0x000E
    int16_t width;                      // 0x001A
    int16_t height;                     // 0x001C
    int16_t pitch;                      // 0x001E
    int16_t pal_cnt;                    // 0x0020
    int16_t var_22;                     // 0x0022
    uint8_t bpp;                        // 0x0024
    uint8_t var_25;                     // 0x0025
    uint8_t bOpen;                      // 0x0026
    uint8_t var_27;                     // 0x0027
    uint8_t var_28;                     // 0x0028
    uint8_t var_29;                     // 0x0029
    uint8_t var_2A;                     // 0x002A
    uint8_t var_2B;                     // 0x002B
    uint8_t var_2C;                     // 0x002C
    uint8_t var_2D;                     // 0x002D
    int16_t var_2E;                     // 0x002E
};
static_assert(sizeof(MarniSurface2) == 0x30);

struct MarniSurface3 : MarniSurface2
{
    void* pDDsurface;                   // 0x0030
    void** pDDpalette;                  // 0x0034
};
static_assert(sizeof(MarniSurface3) == 0x38);

struct MarniSurface : MarniSurface3
{
    uint32_t is_vmem;                   // 0x0038
};
static_assert(sizeof(MarniSurface) == 0x3C);

struct MarniSurfaceX : MarniSurface
{
    uint32_t texture_handle;            // 0x003C
    void* pDDtexture;                   // 0x0040
};
static_assert(sizeof(MarniSurfaceX) == 0x44);

struct MarniSurfaceY : MarniSurface
{
    uint32_t texture_handle;            // 0x003C
    void* pDDtexture;                   // 0x0040
};
static_assert(sizeof(MarniSurfaceY) == 0x44);

struct MarniMovie
{
    uint8_t pad_0000[148];              // 0x0000
    uint32_t flag;                      // 0x0094
    double pos;                         // 0x0098
    int32_t var_A0;                     // 0x00A0
    int32_t var_A4;                     // 0x00A4
};
static_assert(sizeof(MarniMovie) == 0xA8);

struct MarniOt
{
    int32_t zdepth;                     // 0x0000
    Prim* pHead;                        // 0x0004
    int32_t is_valid;                   // 0x0008
    Prim* pCurrent;                     // 0x000C
    uint8_t var_10;                     // 0x0010
    uint8_t var_11;                     // 0x0011
    uint8_t var_12;                     // 0x0012
    uint8_t var_13;                     // 0x0013
};
static_assert(sizeof(MarniOt) == 0x14);

struct MarniLineRecord
{
    void* gourad;                       // 0x0000
    void* flat;                         // 0x0004
};
static_assert(sizeof(MarniLineRecord) == 0x08);

struct WindowRect
{
    int32_t left;                       // 0x0000
    int32_t top;                        // 0x0004
    int32_t right;                      // 0x0008
    int32_t bottom;                     // 0x000C
};
static_assert(sizeof(WindowRect) == 0x10);

struct MarniTextureNode
{
    uint16_t next;                      // 0x0000
    uint8_t pad_0002[6];                // 0x0002
    MarniSurfaceX* surface;             // 0x0008
    uint8_t pad_000C[8];                // 0x000C
    uint32_t var_14;                    // 0x0014
};
static_assert(sizeof(MarniTextureNode) == 0x18);

struct MarniTexture
{
    uint32_t var_00;                    // 0x0000
    MarniSurface2 surface;              // 0x0004
    uint16_t head;                      // 0x0034
    uint16_t var_36;                    // 0x0036
};
static_assert(sizeof(MarniTexture) == 0x38);

struct MarniVertex
{
    uint8_t pad_00[32];                 // 0x0000
};
static_assert(sizeof(MarniVertex) == 0x20);

struct MarniDrawOp
{
    MarniVertex vertices[3];            // 0x0000
    float average_z;                    // 0x0060
    uint8_t filter;                     // 0x0064
    uint8_t pad_0065[3];                // 0x0065
    uint32_t var_68;                    // 0x0068
    uint32_t src_blend;                 // 0x006C
    uint32_t dst_blend;                 // 0x0070
    uint32_t texture_handle;            // 0x0074
    uint32_t shade_mode;                // 0x0078
    uint32_t cull_mode;                 // 0x007C
    uint32_t z_func;                    // 0x0080
    uint8_t z_write_enable;             // 0x0084
    uint8_t specular_enable;            // 0x0085
    uint8_t pad_86[2];                  // 0x0086
};
static_assert(sizeof(MarniDrawOp) == 0x88);

struct PolygonObject
{
    void** vtbl;                        // 0x0000
    uint8_t pad_0004[80];               // 0x0004
    uint32_t var_54;                    // 0x0054
};
static_assert(sizeof(PolygonObject) == 0x58);

struct Unknown8C76A0
{
    uint8_t pad_0000[8];                // 0x0000
    MarniSurface3* surface;             // 0x0008
};
static_assert(sizeof(Unknown8C76A0) == 0x0C);

struct MarniLight
{
    uint8_t pad_0000[12];               // 0x0000
    uint32_t var_0C;                    // 0x000C
    uint32_t var_10;                    // 0x0010
    float var_14;                       // 0x0014
    float var_18;                       // 0x0018
    float var_1C;                       // 0x001C
    float var_20;                       // 0x0020
    uint8_t pad_0024[8];                // 0x0024
    uint32_t var_2C;                    // 0x002C
};
static_assert(sizeof(MarniLight) == 0x30);

struct Marni
{
    MarniTextureNode texture_nodes[256];// 0x0000
    MarniTexture textures[256];         // 0x1800
    uint32_t field_5000;                // 0x5000
    uint32_t field_5004;                // 0x5004
    uint32_t field_5008;                // 0x5008
    uint8_t pad_500C[4];                // 0x500C
    MarniLineRecord field_5010[8];      // 0x5010
    uint8_t pad_5050[8124];             // 0x5050
    int32_t field_700C;                 // 0x700C
    MarniDrawOp draw_ops[65536];        // 0x7010
    MarniDrawOp* draw_op_ptrs[65536];   // 0x887010
    int32_t num_draw_ops;               // 0x8C7010
    uint8_t cutscene_bars;              // 0x8C7014
    uint8_t pad_8C7015[3];              // 0x8C7015
    uint32_t modes;                     // 0x8C7018
    float field_8C701C;                 // 0x8C701C
    uint32_t field_8C7020;              // 0x8C7020
    MarniOt otag[5];                    // 0x8C7024
    uint8_t field_8C7088[252];          // 0x8C7088
    uint8_t field_8C7184[252];          // 0x8C7184
    uint32_t dwVidMemTotal;             // 0x8C7280
    uint8_t pad_8C7284[8];              // 0x8C7284
    uint32_t field_8C728C[256];         // 0x8C728C
    uint8_t pad_8C768C[12];             // 0x8C768C
    PolygonObject** polygons;           // 0x8C7698
    uint32_t polygons_count;            // 0x8C769C
    Unknown8C76A0* var_8C76A0[128];     // 0x8C76A0
    uint8_t pad_8C78A0[1084];           // 0x8C78A0
    WindowRect window_rect;             // 0x8C7CDC
    void* pClipper;                     // 0x8C7CEC
    MarniLight lights[6];               // 0x8C7CF0
    uint32_t field_8C7E10;              // 0x8C7E10
    uint32_t field_8C7E14;              // 0x8C7E14
    float field_8C7E18;                 // 0x8C7E18
    uint32_t field_8C7E1C;              // 0x8C7E1C
    uint32_t field_8C7E20;              // 0x8C7E20
    uint32_t field_8C7E24;              // 0x8C7E24
    float field_8C7E28;                 // 0x8C7E28
    uint32_t field_8C7E2C;              // 0x8C7E2C
    uint32_t field_8C7E30;              // 0x8C7E30
    uint32_t field_8C7E34;              // 0x8C7E34
    float field_8C7E38;                 // 0x8C7E38
    uint32_t field_8C7E3C;              // 0x8C7E3C
    uint32_t field_8C7E40;              // 0x8C7E40
    uint32_t field_8C7E44;              // 0x8C7E44
    uint32_t field_8C7E48;              // 0x8C7E48
    uint32_t field_8C7E4C;              // 0x8C7E4C
    float field_8C7E50;                 // 0x8C7E50
    float field_8C7E54;                 // 0x8C7E54
    float field_8C7E58;                 // 0x8C7E58
    uint32_t field_8C7E5C;              // 0x8C7E5C
    float field_8C7E60;                 // 0x8C7E60
    float field_8C7E64;                 // 0x8C7E64
    float field_8C7E68;                 // 0x8C7E68
    uint32_t field_8C7E6C;              // 0x8C7E6C
    float field_8C7E70;                 // 0x8C7E70
    float field_8C7E74;                 // 0x8C7E74
    float field_8C7E78;                 // 0x8C7E78
    uint32_t field_8C7E7C;              // 0x8C7E7C
    uint32_t field_8C7E80;              // 0x8C7E80
    uint32_t field_8C7E84;              // 0x8C7E84
    uint32_t field_8C7E88;              // 0x8C7E88
    uint32_t field_8C7E8C;              // 0x8C7E8C
    uint32_t field_8C7E90;              // 0x8C7E90
    void* pMaterial;                    // 0x8C7E94
    uint32_t MaterialHandle;            // 0x8C7E98
    void* pDirect3D2;                   // 0x8C7E9C
    void* pDirectDevice2;               // 0x8C7EA0
    void* pViewport;                    // 0x8C7EA4
    void* hWnd;                         // 0x8C7EA8
    int32_t render_w;                   // 0x8C7EAC
    int32_t render_h;                   // 0x8C7EB0
    int32_t xsize_old;                  // 0x8C7EB4
    int32_t ysize_old;                  // 0x8C7EB8
    int32_t bpp_old;                    // 0x8C7EBC
    int32_t fullscreen_old;             // 0x8C7EC0
    int32_t field_8C7EC4;               // 0x8C7EC4
    int32_t field_8C7EC8;               // 0x8C7EC8
    uint8_t pad_8C7ECC[8];              // 0x8C7ECC
    float aspect_x;                     // 0x8C7ED4
    float aspect_y;                     // 0x8C7ED8
    uint32_t field_8C7EDC;              // 0x8C7EDC
    int32_t var_8C7EE0;                 // 0x8C7EE0
    int32_t var_8C7EE4;                 // 0x8C7EE4
    int32_t is_gpu_busy;                // 0x8C7EE8
    int32_t desktop_w;                  // 0x8C7EEC
    int32_t desktop_h;                  // 0x8C7EF0
    int32_t desktop_bpp;                // 0x8C7EF4
    MarniRes resolutions[64];           // 0x8C7EF8
    int32_t res_count;                  // 0x8C82F8
    int32_t field_8C82FC;               // 0x8C82FC
    int32_t field_8C8300;               // 0x8C8300
    uint8_t pad_8C8304[20];             // 0x8C8304
    int32_t var_8C8318;                 // 0x8C8318
    uint8_t ambient_b;                  // 0x8C831C
    uint8_t ambient_g;                  // 0x8C831D
    uint8_t ambient_r;                  // 0x8C831E
    uint8_t ambient_a;                  // 0x8C831F
    void* pDirectDraw;                  // 0x8C8320
    void* pDirectDraw2;                 // 0x8C8324
    MarniMovie* pMovie;                 // 0x8C8328
    int32_t xsize;                      // 0x8C832C
    int32_t ysize;                      // 0x8C8330
    int32_t bpp;                        // 0x8C8334
    int32_t is_gpu_active;              // 0x8C8338
    int32_t dwVidMemFree;               // 0x8C833C
    MarniSurface surface0;              // 0x8C8340
    MarniSurface surfaceZ;              // 0x8C837C
    MarniSurface surface2;              // 0x8C83B8
    uint32_t gpu_flag;                  // 0x8C83F4
    uint32_t vertices_processed;        // 0x8C83F8
    uint32_t triangles_drawn;           // 0x8C83FC
    uint8_t pad_8C8400[16];             // 0x8C8400
    uint32_t field_8C8410;              // 0x8C8410
    uint32_t field_8C8414;              // 0x8C8414
    uint32_t field_8C8418;              // 0x8C8418
    uint32_t field_8C841C;              // 0x8C841C
    uint32_t field_8C8420;              // 0x8C8420
    float field_8C8424;                 // 0x8C8424
    uint32_t field_8C8428;              // 0x8C8428
    int32_t device_cnt;                 // 0x8C842C
    int32_t field_8C8430;               // 0x8C8430
    MarniSurface surface3;              // 0x8C8434
};
static_assert(sizeof(Marni) == 0x8C8470);

struct OldStdString
{
    char* data;                         // 0x0000
    uint32_t length;                    // 0x0004
};
static_assert(sizeof(OldStdString) == 0x08);

struct MarniConfig
{
    uint32_t key;                       // 0x0000
    uint32_t var_04;                    // 0x0004
    uint32_t var_08;                    // 0x0008
    uint32_t hKey;                      // 0x000C
    uint32_t ex_flag;                   // 0x0010
    uint32_t ex_time[3];                // 0x0014
    uint32_t var_20;                    // 0x0020
    uint32_t var_24;                    // 0x0024
    uint32_t var_28;                    // 0x0028
    uint32_t var_2C;                    // 0x002C
    uint32_t var_30;                    // 0x0030
    uint32_t var_34;                    // 0x0034
    uint32_t data1;                     // 0x0038
    uint32_t data2;                     // 0x003C
    uint32_t data3;                     // 0x0040
    uint32_t data4;                     // 0x0044
    uint8_t perswrap;                   // 0x0048
    uint8_t bilinear;                   // 0x0049
    uint8_t fadeflag;                   // 0x004A
    uint8_t var_4B;                     // 0x004B
    OldStdString device_name;           // 0x004C
    OldStdString display_mode;          // 0x0054
    uint8_t var_5C;                     // 0x005C
};
static_assert(sizeof(MarniConfig) == 0x5D);

struct D3DDeviceInfo
{
    char lpDeviceName[30];              // 0x0000
    char lpDeviceDescription[50];       // 0x001E
    uint8_t desc[252];                  // 0x0050
    uint8_t GUID[16];                   // 0x014C
    uint32_t hwAccelerated;             // 0x015C
    uint32_t supportsFloat;             // 0x0160
    uint32_t supportsZbuffer;           // 0x0164
    uint32_t hwAccelerated2;            // 0x0168
};
static_assert(sizeof(D3DDeviceInfo) == 0x16C);

struct TexturePage
{
    uint32_t handle;                    // 0x0000
    uint32_t var_04;                    // 0x0004
    uint32_t var_08;                    // 0x0008
};
static_assert(sizeof(TexturePage) == 0x0C);

struct GameTable
{
    uint8_t pad_0000[5393927];          // 0x0000
    uint8_t byte_524E07;                // 0x524E07
    uint8_t pad_524E08[172];            // 0x524E08
    uint8_t cheat0;                     // 0x524EB4
    uint8_t cheat1;                     // 0x524EB5
    bool enable_dsound;                 // 0x524EB6
    uint8_t pad_524EB7[2];              // 0x524EB7
    uint8_t graphics_ptr_data;          // 0x524EB9
    uint8_t pad_524EBA[638];            // 0x524EBA
    char* mutex_name;                   // 0x525138
    uint8_t pad_52513C[3616];           // 0x52513C
    Mat16 door_ll;                      // 0x525F5C
    Mat16 door_lc;                      // 0x525F7C
    uint8_t pad_525F9C[716];            // 0x525F9C
    int32_t global_prj;                 // 0x526268
    uint8_t pad_52626C[4];              // 0x52626C
    int16_t math_const_table[1224];     // 0x526270
    uint8_t pad_526C00[6900];           // 0x526C00
    uint8_t byte_5286F4[8];             // 0x5286F4
    uint8_t pad_5286FC[40];             // 0x5286FC
    uint8_t byte_528724[4];             // 0x528724
    uint8_t pad_528728[15752];          // 0x528728
    void* pGG;                          // 0x52C4B0
    uint8_t pad_52C4B4[5068];           // 0x52C4B4
    Mat16 g_identity_mat;               // 0x52D880
    uint8_t pad_52D8A0[64];             // 0x52D8A0
    uint8_t curtain2[7];                // 0x52D8E0
    uint8_t byte_52D8E7[25];            // 0x52D8E7
    uint8_t pad_52D900[51717];          // 0x52D900
    uint8_t byte_53A305[63];            // 0x53A305
    uint8_t pad_53A344[3796];           // 0x53A344
    uint32_t* flag_groups[35];          // 0x53B218
    uint8_t pad_53B2A4[4916];           // 0x53B2A4
    BgmTableEntry byte_53C5D8[146];     // 0x53C5D8
    uint8_t pad_53C78E[1];              // 0x53C78E
    uint8_t byte_53C78F[70];            // 0x53C78F
    uint8_t pad_53C7D5[4739];           // 0x53C7D5
    char common_file_map_a[21];         // 0x53DA58
    uint8_t pad_53DA6D[263];            // 0x53DA6D
    uint8_t* byte_53DB74;               // 0x53DB74
    uint8_t pad_53DB78[688];            // 0x53DB78
    ItemTypeDefinition item_def_tbl[64];// 0x53DE28
    uint8_t pad_53E028[388];            // 0x53E028
    uint16_t word_53E1AC;               // 0x53E1AC
    uint8_t pad_53E1AE[2];              // 0x53E1AE
    uint16_t word_53E1B0;               // 0x53E1B0
    uint8_t pad_53E1B2[2374];           // 0x53E1B2
    MapStageInfo map_info0[20];         // 0x53EAF8
    MapStageInfo map_info1[20];         // 0x53EB98
    uint8_t pad_53EC38[6984];           // 0x53EC38
    uint8_t byte_540780[16];            // 0x540780
    uint8_t pad_540790[12932];          // 0x540790
    void* dword_543A14;                 // 0x543A14
    uint32_t error;                     // 0x543A18
    uint8_t pad_543A1C[4];              // 0x543A1C
    D3DDeviceInfo d3d_devices[5];       // 0x543A20
    int32_t dword_54413C;               // 0x54413C
    uint32_t d3d_vertices_processed;    // 0x544140
    uint32_t d3d_triangles_drawn;       // 0x544144
    uint8_t unk_544148[1050736];        // 0x544148
    int32_t d3d_device_count;           // 0x6449B8
    int32_t dword_6449BC;               // 0x6449BC
    uint8_t pad_6449C0[151860];         // 0x6449C0
    void* hFont;                        // 0x669AF4
    uint8_t pad_669AF8[4];              // 0x669AF8
    uint8_t is_480p;                    // 0x669AFC
    uint8_t pad_669AFD[14487];          // 0x669AFD
    uint32_t dword_66D394;              // 0x66D394
    uint8_t pad_66D398[62528];          // 0x66D398
    TexturePage texture_pages[41];      // 0x67C7D8
    uint32_t global_cx;                 // 0x67C9C4
    uint32_t global_cy;                 // 0x67C9C8
    uint8_t pad_67C9CC[4];              // 0x67C9CC
    void* file_buffer;                  // 0x67C9D0
    uint8_t pad_67C9D4[8];              // 0x67C9D4
    uint8_t movie_playing;              // 0x67C9DC
    uint8_t pad_67C9DD[7];              // 0x67C9DD
    uint8_t timer_r0;                   // 0x67C9E4
    uint8_t pad_67C9E5[15];             // 0x67C9E5
    uint32_t timer_last;                // 0x67C9F4
    uint8_t vk_press;                   // 0x67C9F8
    uint8_t pad_67C9F9[3];              // 0x67C9F9
    PrimScaler scaler;                  // 0x67C9FC
    Input input;                        // 0x67CA30
    uint32_t timer_10;                  // 0x68055C
    MarniFont marni_font;               // 0x680560
    void* hwnd;                         // 0x68056C
    uint32_t timer_current;             // 0x680570
    uint8_t pad_680574[4];              // 0x680574
    void* hInstance;                    // 0x680578
    uint8_t pad_68057C[4];              // 0x68057C
    uint32_t error_no;                  // 0x680580
    uint8_t vsync_rate;                 // 0x680584
    uint8_t timer_r1;                   // 0x680585
    uint8_t timer_r2;                   // 0x680586
    uint8_t timer_frame;                // 0x680587
    uint32_t game_seconds;              // 0x680588
    uint8_t frame_current;              // 0x68058C
    uint8_t can_draw;                   // 0x68058D
    uint8_t movie_r0;                   // 0x68058E
    uint8_t reset_r0;                   // 0x68058F
    uint8_t byte_680590;                // 0x680590
    uint8_t byte_680591;                // 0x680591
    uint8_t byte_680592;                // 0x680592
    uint8_t byte_680593;                // 0x680593
    uint8_t pad_680594[3];              // 0x680594
    uint8_t byte_680597;                // 0x680597
    uint8_t byte_680598;                // 0x680598
    uint8_t ushinabe;                   // 0x680599
    uint8_t blood_censor;               // 0x68059A
    uint8_t byte_68059B;                // 0x68059B
    uint8_t pad_68059C[20];             // 0x68059C
    uint8_t super_hard_switch;          // 0x6805B0
    uint8_t hard_mode;                  // 0x6805B1
    uint8_t pad_6805B2[1];              // 0x6805B2
    uint8_t censorship_off;             // 0x6805B3
    uint8_t byte_6805B4;                // 0x6805B4
    uint8_t pause_game;                 // 0x6805B5
    uint8_t pad_6805B6[2];              // 0x6805B6
    Marni* pMarni;                      // 0x6805B8
    uint32_t global_rgb;                // 0x6805BC
    uint32_t movie_idx;                 // 0x6805C0
    uint32_t dword_6805C4;              // 0x6805C4
    void* hMutex;                       // 0x6805C8
    uint8_t pad_6805CC[36388];          // 0x6805CC
    uint32_t dword_6893F0;              // 0x6893F0
    uint32_t door_state;                // 0x6893F4
    uint8_t pad_6893F8[8];              // 0x6893F8
    DoorInfo* door;                     // 0x689400
    uint8_t pad_689404[60];             // 0x689404
    uint32_t dword_689440;              // 0x689440
    uint32_t dword_689444;              // 0x689444
    uint32_t dword_689448;              // 0x689448
    uint8_t pad_68944C[836];            // 0x68944C
    uint32_t idd;                       // 0x689790
    uint8_t pad_689794[108];            // 0x689794
    uint32_t dword_689800;              // 0x689800
    uint8_t pad_689804[69];             // 0x689804
    uint8_t stage_bk;                   // 0x689849
    uint8_t byte_68984A;                // 0x68984A
    uint8_t pad_68984B[1];              // 0x68984B
    uint32_t systemInitialized;         // 0x68984C
    uint8_t pad_689850[620];            // 0x689850
    uint8_t byte_689ABC;                // 0x689ABC
    uint8_t pad_689ABD[287];            // 0x689ABD
    uint32_t dword_689BDC;              // 0x689BDC
    uint8_t pad_689BE0[48];             // 0x689BE0
    uint32_t rdt_count;                 // 0x689C10
    void* rdt_top_ptr;                  // 0x689C14
    uint32_t rdt_size;                  // 0x689C18
    uint32_t dword_689C1C;              // 0x689C1C
    char room_path[32];                 // 0x689C20
    char stage_font_name[32];           // 0x689C40
    PlayerEntity* p_em;                 // 0x689C60
    uint8_t byte_689C64;                // 0x689C64
    uint8_t pad_689C65[43];             // 0x689C65
    int16_t itembox_speed;              // 0x689C90
    uint8_t pad_689C92[6];              // 0x689C92
    int16_t itembox_acceleration;       // 0x689C98
    uint8_t pad_689C9A[6];              // 0x689C9A
    uint32_t dword_689CA0;              // 0x689CA0
    uint8_t pad_689CA4[4];              // 0x689CA4
    ObjectEntity* itembox_obj;          // 0x689CA8
    uint8_t pad_689CAC[328];            // 0x689CAC
    uint32_t dword_689DF4;              // 0x689DF4
    uint32_t dword_689DF8;              // 0x689DF8
    uint8_t pad_689DFC[176];            // 0x689DFC
    uint32_t st_code_pos;               // 0x689EAC
    uint8_t pad_689EB0[112];            // 0x689EB0
    uint32_t dword_689F20;              // 0x689F20
    uint8_t byte_689F24;                // 0x689F24
    uint8_t pad_689F25[7];              // 0x689F25
    uint32_t task_disable;              // 0x689F2C
    uint32_t task_no;                   // 0x689F30
    uint8_t pad_689F34[28];             // 0x689F34
    MarniConfig marni_config;           // 0x689F50
    uint8_t pad_689FAD[599];            // 0x689FAD
    Task* ctcb;                         // 0x68A204
    uint8_t pad_68A208[24];             // 0x68A208
    Task tasks[8];                      // 0x68A220
    uint8_t pad_68A340[30784];          // 0x68A340
    uint8_t title_mv_state;             // 0x691B80
    uint8_t title_disp_add;             // 0x691B81
    uint8_t byte_691B82;                // 0x691B82
    uint8_t byte_691B83;                // 0x691B83
    uint8_t pad_691B84[1];              // 0x691B84
    uint8_t byte_691B85;                // 0x691B85
    uint8_t title_mode;                 // 0x691B86
    uint8_t title_cursor;               // 0x691B87
    uint8_t byte_691B88;                // 0x691B88
    uint8_t byte_691B89;                // 0x691B89
    uint8_t pad_691B8A[3];              // 0x691B8A
    uint8_t byte_691B8D;                // 0x691B8D
    uint8_t byte_691B8E;                // 0x691B8E
    uint8_t pad_691B8F[1];              // 0x691B8F
    uint16_t demo_countdown;            // 0x691B90
    int16_t ti_kido;                    // 0x691B92
    int16_t ti_add;                     // 0x691B94
    int16_t word_691B96;                // 0x691B96
    int16_t word_691B98;                // 0x691B98
    uint8_t pad_691B9A[2];              // 0x691B9A
    int32_t dword_691B9C;               // 0x691B9C
    uint8_t pad_691BA0[472];            // 0x691BA0
    int16_t word_691D78;                // 0x691D78
    int16_t word_691D7A;                // 0x691D7A
    uint8_t pad_691D7C[18];             // 0x691D7C
    int16_t word_691D8E;                // 0x691D8E
    uint8_t pad_691D90[18];             // 0x691D90
    int16_t word_691DA2;                // 0x691DA2
    uint8_t pad_691DA4[335];            // 0x691DA4
    uint8_t byte_691EF3;                // 0x691EF3
    uint8_t pad_691EF4[109];            // 0x691EF4
    uint8_t _st;                        // 0x691F61
    uint8_t itembox_state;              // 0x691F62
    uint8_t byte_691F63;                // 0x691F63
    uint8_t byte_691F64;                // 0x691F64
    uint8_t byte_691F65;                // 0x691F65
    uint8_t byte_691F66;                // 0x691F66
    uint8_t byte_691F67;                // 0x691F67
    uint8_t byte_691F68;                // 0x691F68
    uint8_t byte_691F69;                // 0x691F69
    uint8_t byte_691F6A;                // 0x691F6A
    uint8_t pad_691F6B[1];              // 0x691F6B
    uint8_t inventory_cursor;           // 0x691F6C
    uint8_t inventory_cursor_2;         // 0x691F6D
    uint8_t inventory_cursor_3;         // 0x691F6E
    uint8_t byte_691F6F;                // 0x691F6F
    uint8_t hud_mode;                   // 0x691F70
    uint8_t pad_691F71[3];              // 0x691F71
    uint8_t byte_691F74;                // 0x691F74
    uint8_t pad_691F75[1];              // 0x691F75
    uint8_t byte_691F76;                // 0x691F76
    uint8_t pad_691F77[3];              // 0x691F77
    uint8_t byte_691F7A;                // 0x691F7A
    uint8_t byte_691F7B;                // 0x691F7B
    uint8_t byte_691F7C;                // 0x691F7C
    uint8_t byte_691F7D;                // 0x691F7D
    uint8_t byte_691F7E;                // 0x691F7E
    uint8_t byte_691F7F;                // 0x691F7F
    uint8_t byte_691F80;                // 0x691F80
    uint8_t byte_691F81;                // 0x691F81
    uint8_t byte_691F82;                // 0x691F82
    uint8_t byte_691F83;                // 0x691F83
    uint8_t itembox_slot_id;            // 0x691F84
    uint8_t byte_691F85;                // 0x691F85
    uint8_t byte_691F86;                // 0x691F86
    uint8_t byte_691F87;                // 0x691F87
    uint32_t dword_691F88;              // 0x691F88
    uint8_t pad_691F8C[8];              // 0x691F8C
    uint16_t word_691F94;               // 0x691F94
    uint8_t pad_691F96[4];              // 0x691F96
    uint16_t word_691F9A;               // 0x691F9A
    uint32_t dword_691F9C;              // 0x691F9C
    uint8_t pad_691FA0[6];              // 0x691FA0
    uint16_t word_691FA6;               // 0x691FA6
    int16_t word_691FA8;                // 0x691FA8
    uint8_t pad_691FAA[6];              // 0x691FAA
    uint16_t word_691FB0;               // 0x691FB0
    uint16_t word_691FB2;               // 0x691FB2
    uint8_t pad_691FB4[4];              // 0x691FB4
    uint16_t word_691FB8;               // 0x691FB8
    uint16_t word_691FBA;               // 0x691FBA
    uint8_t pad_691FBC[3496];           // 0x691FBC
    uint8_t byte_692D64;                // 0x692D64
    uint8_t pad_692D65[601];            // 0x692D65
    uint16_t word_692FBE;               // 0x692FBE
    uint16_t word_692FC0;               // 0x692FC0
    uint8_t pad_692FC2[51];             // 0x692FC2
    uint8_t vab_id[16];                 // 0x692FF5
    uint8_t pad_693005[1132];           // 0x693005
    uint8_t bgm_vol;                    // 0x693471
    uint8_t pad_693472[66];             // 0x693472
    uint8_t* dword_6934B4;              // 0x6934B4
    uint8_t pad_6934B8[40];             // 0x6934B8
    char ss_name_bgm[260];              // 0x6934E0
    uint8_t pad_6935E4[540];            // 0x6935E4
    int8_t seq_ctr[3];                  // 0x693800
    uint8_t pad_693803[1];              // 0x693803
    uint32_t dword_693804;              // 0x693804
    uint8_t byte_693808;                // 0x693808
    uint8_t pad_693809[1];              // 0x693809
    int8_t byte_69380A;                 // 0x69380A
    uint8_t pad_69380B[5];              // 0x69380B
    uint8_t byte_693810;                // 0x693810
    uint8_t pad_693811[1];              // 0x693811
    uint8_t byte_693812;                // 0x693812
    uint8_t pad_693813[1077];           // 0x693813
    uint8_t sfx_vol;                    // 0x693C48
    uint8_t pad_693C49[3];              // 0x693C49
    uint32_t dword_693C4C;              // 0x693C4C
    uint8_t pad_693C50[564];            // 0x693C50
    uint8_t* current_bgm_address;       // 0x693E84
    uint32_t cd_vol_0;                  // 0x693E88
    uint8_t pad_693E8C[20];             // 0x693E8C
    char ss_name_sbgm[260];             // 0x693EA0
    uint8_t byte_693FA4;                // 0x693FA4
    uint8_t pad_693FA5[2635];           // 0x693FA5
    uint16_t word_6949F0;               // 0x6949F0
    uint8_t pad_6949F2[2];              // 0x6949F2
    uint16_t word_6949F4;               // 0x6949F4
    uint8_t pad_6949F6[2];              // 0x6949F6
    Unknown6949F8* dword_6949F8;        // 0x6949F8
    uint8_t pad_6949FC[5212];           // 0x6949FC
    uint32_t random_base;               // 0x695E58
    uint8_t* scd;                       // 0x695E5C
    Entity* c_em;                       // 0x695E60
    uint32_t mizu_div;                  // 0x695E64
    uint8_t sce_type;                   // 0x695E68
    uint8_t cut_old;                    // 0x695E69
    uint8_t c_id;                       // 0x695E6A
    uint8_t c_model_type;               // 0x695E6B
    uint8_t c_kind;                     // 0x695E6C
    uint8_t mizu_div_max;               // 0x695E6D
    uint8_t mizu_div_ctr;               // 0x695E6E
    uint8_t rbj_reset_flg;              // 0x695E6F
    uint8_t se_tmp0;                    // 0x695E70
    uint8_t byte_695E71;                // 0x695E71
    uint8_t byte_695E72;                // 0x695E72
    uint8_t pad_695E73[9];              // 0x695E73
    uint32_t dword_695E7C;              // 0x695E7C
    uint8_t pad_695E80[10688];          // 0x695E80
    uint8_t* psp_lookup;                // 0x698840
    uint8_t pad_698844[4220];           // 0x698844
    uint8_t osp_mask_flag;              // 0x6998C0
    uint8_t pad_6998C1[252491];         // 0x6998C1
    uint8_t byte_6D730C[24592];         // 0x6D730C
    uint8_t pad_6DD31C[1983844];        // 0x6DD31C
    uint8_t* bg_buffer;                 // 0x8C1880
    uint8_t pad_8C1884[20476];          // 0x8C1884
    TmdEntry* tmd;                      // 0x8C6880
    void* door_tim;                     // 0x8C6884
    uint8_t byte_8C6888[1928];          // 0x8C6888
    uint8_t pad_8C7010[112752];         // 0x8C7010
    DoorInfo door_info;                 // 0x8E2880
    DoorEntity door_data[10];           // 0x8E2ACC
    uint8_t pad_8E37C4[114908];         // 0x8E37C4
    uint8_t work_buffer[163840];        // 0x8FF8A0
    uint8_t work_buffer_2[329712];      // 0x9278A0
    uint8_t pad_978090[35440];          // 0x978090
    CCWork cc_work;                     // 0x980B00
    uint8_t pad_9813EC[178];            // 0x9813EC
    uint8_t door_trans_mv;              // 0x98149E
    uint8_t pad_98149F[2840];           // 0x98149F
    uint8_t byte_981FB7;                // 0x981FB7
    uint8_t pad_981FB8[3];              // 0x981FB8
    uint8_t byte_981FBB;                // 0x981FBB
    uint8_t pad_981FBC[12432];          // 0x981FBC
    int8_t fg_message;                  // 0x98504C
    uint8_t pad_98504D[2];              // 0x98504D
    uint8_t pickup_item_type;           // 0x98504F
    uint8_t pad_985050[5264];           // 0x985050
    uint8_t* g_table;                   // 0x9864E0
    uint8_t pad_9864E4[60];             // 0x9864E4
    uint32_t dword_986520;              // 0x986520
    uint8_t* byte_986524;               // 0x986524
    uint8_t pad_986528[8188];           // 0x986528
    uint32_t dword_988524;              // 0x988524
    uint8_t* byte_988528;               // 0x988528
    uint8_t pad_98852C[124];            // 0x98852C
    uint32_t dword_9885A8;              // 0x9885A8
    uint32_t dword_9885AC;              // 0x9885AC
    uint8_t pad_9885B0[32];             // 0x9885B0
    uint32_t dword_9885D0;              // 0x9885D0
    uint8_t pad_9885D4[36];             // 0x9885D4
    uint32_t dword_9885F8;              // 0x9885F8
    uint16_t word_9885FC;               // 0x9885FC
    uint32_t dword_9885FE;              // 0x9885FE
    uint8_t pad_988602[2];              // 0x988602
    uint32_t g_key;                     // 0x988604
    uint32_t key_trg;                   // 0x988608
    uint8_t pad_98860C[4];              // 0x98860C
    uint32_t dword_988610;              // 0x988610
    uint8_t pad_988614[8];              // 0x988614
    Rdt* rdt;                           // 0x98861C
    uint32_t dword_988620;              // 0x988620
    void* mem_top;                      // 0x988624
    ActorEntity* actor_entity;          // 0x988628
    void* dword_98862C;                 // 0x98862C
    VCut* vcut_data[2];                 // 0x988630
    void* em_damage_table_16[48];       // 0x988638
    void* em_die_table[81];             // 0x9886F8
    uint16_t* dword_98883C;             // 0x98883C
    uint8_t pad_988840[8];              // 0x988840
    void* door_aot_data;                // 0x988848
    uint8_t pad_98884C[4];              // 0x98884C
    void* aot_table[32];                // 0x988850
    Unknown6949F8* dword_9888D0;        // 0x9888D0
    uint8_t pad_9888D4[4];              // 0x9888D4
    uint8_t byte_9888D8;                // 0x9888D8
    uint8_t byte_9888D9;                // 0x9888D9
    uint8_t pad_9888DA[5518];           // 0x9888DA
    uint8_t fg_rbj_set;                 // 0x989E68
    uint8_t pad_989E69[3];              // 0x989E69
    uint32_t fg_system;                 // 0x989E6C
    uint8_t pad_989E70[5];              // 0x989E70
    uint8_t byte_989E75;                // 0x989E75
    uint16_t word_989E76;               // 0x989E76
    uint16_t word_989E78;               // 0x989E78
    uint16_t word_989E7A;               // 0x989E7A
    uint8_t pad_989E7C[1];              // 0x989E7C
    uint8_t byte_989E7D;                // 0x989E7D
    uint8_t byte_989E7E;                // 0x989E7E
    uint8_t pad_989E7F[17];             // 0x989E7F
    uint8_t byte_989E90;                // 0x989E90
    uint8_t byte_989E91;                // 0x989E91
    uint8_t pad_989E92[2];              // 0x989E92
    uint32_t dword_989E94;              // 0x989E94
    uint8_t pad_989E98[28];             // 0x989E98
    uint16_t word_989EB4;               // 0x989EB4
    uint8_t pad_989EB6[26];             // 0x989EB6
    uint32_t fg_status;                 // 0x989ED0
    uint32_t fg_stop;                   // 0x989ED4
    uint32_t fg_use;                    // 0x989ED8
    uint32_t dword_989EDC;              // 0x989EDC
    uint32_t dword_989EE0;              // 0x989EE0
    uint32_t dword_989EE4;              // 0x989EE4
    int16_t word_989EE8;                // 0x989EE8
    uint8_t byte_989EEA;                // 0x989EEA
    uint8_t enemy_count;                // 0x989EEB
    uint16_t fg_room_enemy;             // 0x989EEC
    uint16_t word_989EEE;               // 0x989EEE
    PlayerEntity pl;                    // 0x989EF0
    uint16_t poison_status;             // 0x98A108
    uint8_t poison_timer;               // 0x98A10A
    uint8_t pad_98A10B[1];              // 0x98A10B
    PlayerEntity* player_work;          // 0x98A10C
    EnemyEntity* splayer_work;          // 0x98A110
    EnemyEntity* enemies[16];           // 0x98A114
    void* enemy_init_map[96];           // 0x98A154
    void* enemy_init_table[192];        // 0x98A2D4
    uint8_t pad_98A5D4[64];             // 0x98A5D4
    EnemyInitEntry enemy_init_entries[2];// 0x98A614
    ObjectEntity pOm[32];               // 0x98A61C
    ObjectEntity* obj_ptr;              // 0x98E51C
    uint8_t pad_98E520[8];              // 0x98E520
    uint8_t aot_count;                  // 0x98E528
    uint8_t pickup_item;                // 0x98E529
    uint8_t pad_98E52A[9];              // 0x98E52A
    uint8_t itembox_obj_index;          // 0x98E533
    uint8_t pad_98E534[13];             // 0x98E534
    uint8_t byte_98E541;                // 0x98E541
    uint8_t question_state;             // 0x98E542
    uint8_t pad_98E543[1];              // 0x98E543
    uint32_t dword_98E544;              // 0x98E544
    uint8_t pad_98E548[580];            // 0x98E548
    int16_t word_98E78C;                // 0x98E78C
    uint8_t pad_98E78E[2];              // 0x98E78E
    uint32_t dword_98E790;              // 0x98E790
    void* action_fn;                    // 0x98E794
    uint8_t byte_98E798;                // 0x98E798
    uint8_t pad_98E799[3];              // 0x98E799
    uint8_t table_start;                // 0x98E79C
    uint8_t pad_98E79D[511];            // 0x98E79D
    uint32_t dword_98E99C;              // 0x98E99C
    uint8_t pad_98E9A0[4];              // 0x98E9A0
    uint8_t inventory_size;             // 0x98E9A4
    uint8_t byte_98E9A5;                // 0x98E9A5
    uint8_t byte_98E9A6;                // 0x98E9A6
    uint8_t byte_98E9A7;                // 0x98E9A7
    uint8_t pad_98E9A8[2];              // 0x98E9A8
    uint8_t byte_98E9AA;                // 0x98E9AA
    uint8_t byte_98E9AB;                // 0x98E9AB
    int16_t word_98E9AC;                // 0x98E9AC
    uint8_t pad_98E9AE[8];              // 0x98E9AE
    int16_t word_98E9B6;                // 0x98E9B6
    uint8_t pad_98E9B8[4];              // 0x98E9B8
    int16_t word_98E9BC;                // 0x98E9BC
    int16_t word_98E9BE;                // 0x98E9BE
    int16_t word_98E9C0;                // 0x98E9C0
    int16_t word_98E9C2;                // 0x98E9C2
    uint32_t dword_98E9C4;              // 0x98E9C4
    uint16_t bgm_table[142];            // 0x98E9C8
    uint16_t scd_variables_00;          // 0x98EAE4
    uint16_t word_98EAE6;               // 0x98EAE6
    uint16_t last_used_item;            // 0x98EAE8
    int16_t word_98EAEA;                // 0x98EAEA
    uint8_t pad_98EAEC[16];             // 0x98EAEC
    int16_t word_98EAFC;                // 0x98EAFC
    int16_t word_98EAFE;                // 0x98EAFE
    int16_t word_98EB00;                // 0x98EB00
    uint16_t scd_var_temp;              // 0x98EB02
    uint8_t pad_98EB04[16];             // 0x98EB04
    uint16_t current_stage;             // 0x98EB14
    uint16_t current_room;              // 0x98EB16
    uint16_t current_cut;               // 0x98EB18
    uint16_t last_cut;                  // 0x98EB1A
    uint16_t word_98EB1C;               // 0x98EB1C
    uint16_t rng;                       // 0x98EB1E
    uint16_t word_98EB20;               // 0x98EB20
    uint16_t ex_battle_mode;            // 0x98EB22
    int16_t next_pld;                   // 0x98EB24
    uint16_t word_98EB26;               // 0x98EB26
    uint16_t word_98EB28;               // 0x98EB28
    uint16_t word_98EB2A;               // 0x98EB2A
    uint32_t fg_scenario[8];            // 0x98EB2C
    uint32_t fg_common[8];              // 0x98EB4C
    uint32_t fg_room;                   // 0x98EB6C
    uint32_t fg_tick;                   // 0x98EB70
    uint32_t fg_enemy_0[8];             // 0x98EB74
    uint32_t fg_enemy_1[8];             // 0x98EB94
    uint32_t fg_item[7];                // 0x98EBB4
    uint32_t dword_98EBD0;              // 0x98EBD0
    uint8_t pad_98EBD4[44];             // 0x98EBD4
    uint32_t dword_98EC00;              // 0x98EC00
    uint32_t fg_map[5];                 // 0x98EC04
    uint8_t pad_98EC18[8];              // 0x98EC18
    uint32_t pri_be_flg[64];            // 0x98EC20
    uint8_t pad_98ED20[12];             // 0x98ED20
    uint32_t door_locks;                // 0x98ED2C
    InventorySlot item_twork;           // 0x98ED30
    InventorySlot inventory[11];        // 0x98ED34
    ItemboxItem itembox[64];            // 0x98ED60
    uint8_t inventory_files[24];        // 0x98EE60
    int16_t word_98EE78;                // 0x98EE78
    uint8_t pad_98EE7A[1];              // 0x98EE7A
    uint8_t byte_98EE7B;                // 0x98EE7B
    int16_t saved_splayer_health;       // 0x98EE7C
    uint16_t word_98EE7E;               // 0x98EE7E
    uint8_t pad_98EE80[112];            // 0x98EE80
    uint32_t dword_98EEF0;              // 0x98EEF0
    uint8_t pad_98EEF4[56];             // 0x98EEF4
    uint8_t byte_98EF2C;                // 0x98EF2C
    uint8_t byte_98EF2D;                // 0x98EF2D
    uint8_t pad_98EF2E[18];             // 0x98EF2E
    uint8_t* main;                      // 0x98EF40
    uint8_t pad_98EF44[300];            // 0x98EF44
    uint32_t dword_98F070;              // 0x98F070
    uint32_t dword_98F074;              // 0x98F074
    uint16_t word_98F078;               // 0x98F078
    uint8_t byte_98F07A;                // 0x98F07A
    uint8_t byte_98F07B;                // 0x98F07B
    Fade fade_table[4];                 // 0x98F07C
    uint32_t dword_98F1AC;              // 0x98F1AC
    uint32_t dword_98F1B0;              // 0x98F1B0
    uint8_t pad_98F1B4[2];              // 0x98F1B4
    uint8_t byte_98F1B6;                // 0x98F1B6
    uint8_t byte_98F1B7;                // 0x98F1B7
    uint8_t byte_98F1B8;                // 0x98F1B8
    uint8_t byte_98F1B9;                // 0x98F1B9
    uint8_t pause;                      // 0x98F1BA
    uint8_t byte_98F1BB;                // 0x98F1BB
    uint8_t pad_98F1BC[2003];           // 0x98F1BC
    uint8_t byte_98F98F[4];             // 0x98F98F
    uint8_t pad_98F993[9693];           // 0x98F993
    uint32_t dword_991F70;              // 0x991F70
    uint32_t dword_991F74;              // 0x991F74
    uint8_t pad_991F78[8];              // 0x991F78
    uint8_t byte_991F80;                // 0x991F80
    uint8_t pad_991F81[67];             // 0x991F81
    uint32_t dword_991FC4;              // 0x991FC4
    DemoPlayer pdemo;                   // 0x991FC8
    uint8_t byte_99270E;                // 0x99270E
    uint8_t byte_99270F;                // 0x99270F
    uint8_t pad_992710[42800];          // 0x992710
    Mat16 ll_matrix;                    // 0x99CE40
    Mat16 lc_matrix;                    // 0x99CE60
    Mat16 rc_matrix;                    // 0x99CE80
    DoorEntity* doors[9];               // 0x99CEA0
    uint32_t dword_99CEC4;              // 0x99CEC4
    uint8_t pad_99CEC8[156];            // 0x99CEC8
    uint32_t dword_99CF64;              // 0x99CF64
    uint8_t pad_99CF68[4];              // 0x99CF68
    uint32_t dword_99CF6C;              // 0x99CF6C
    uint32_t dword_99CF70;              // 0x99CF70
};
static_assert(sizeof(GameTable) == 0x99CF74);

struct Unknown988628
{
    uint8_t pad_0000[268];              // 0x0000
    uint16_t var_10C;                   // 0x010C
};
static_assert(sizeof(Unknown988628) == 0x10E);

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

