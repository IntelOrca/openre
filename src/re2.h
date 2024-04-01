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
    int32_t t[3];                       // 0x0014
};
static_assert(sizeof(Mat16) == 0x20);

struct VCut
{
    int8_t be_flg;                      // 0x0000
    uint8_t nFloor;                     // 0x0001
    uint8_t fCut;                       // 0x0002
    uint8_t tCut;                       // 0x0003
    uint16_t xz[8];                     // 0x0004
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
    int32_t pSin_parts_ptr;             // 0x0198
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
    int32_t pOn_om;                     // 0x01E4
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
    uint8_t pad_0219[7];                // 0x0219
    uint8_t var_220;                    // 0x0220
    uint8_t var_221;                    // 0x0221
    uint8_t pad_0222[1];                // 0x0222
    uint8_t var_223;                    // 0x0223
    uint8_t pad_0224[3];                // 0x0224
    uint8_t var_227;                    // 0x0227
    uint8_t pad_0228[5];                // 0x0228
    uint8_t var_22D;                    // 0x022D
    uint8_t var_22E;                    // 0x022E
    uint8_t pad_022F[1];                // 0x022F
    uint8_t var_230;                    // 0x0230
    uint8_t pad_0231[1];                // 0x0231
    uint8_t var_232;                    // 0x0232
    uint8_t pad_0233[20];               // 0x0233
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

struct Unknown68A204
{
    uint8_t pad_0000[8];                // 0x0000
    uint8_t var_08;                     // 0x0008
    uint8_t var_09;                     // 0x0009
    uint8_t pad_000A[3];                // 0x000A
    uint8_t var_0D;                     // 0x000D
    uint8_t var_0E;                     // 0x000E
    uint8_t pad_000F[4];                // 0x000F
    uint8_t var_13;                     // 0x0013
};
static_assert(sizeof(Unknown68A204) == 0x14);

struct EnemyInitEntry
{
    uint16_t type;                      // 0x0000
    uint16_t enabled;                   // 0x0002
};
static_assert(sizeof(EnemyInitEntry) == 0x04);

struct GameTable
{
    uint8_t pad_0000[5394102];          // 0x0000
    bool enable_dsound;                 // 0x524EB6
    uint8_t pad_524EB7[5041];           // 0x524EB7
    int32_t global_prj;                 // 0x526268
    uint8_t pad_52626C[25156];          // 0x52626C
    void* pGG;                          // 0x52C4B0
    uint8_t pad_52C4B4[60772];          // 0x52C4B4
    uint32_t* flag_groups[35];          // 0x53B218
    uint8_t pad_53B2A4[4916];           // 0x53B2A4
    BgmTableEntry byte_53C5D8[146];     // 0x53C5D8
    uint8_t pad_53C78E[1];              // 0x53C78E
    uint8_t byte_53C78F[70];            // 0x53C78F
    uint8_t pad_53C7D5[5023];           // 0x53C7D5
    uint8_t* byte_53DB74;               // 0x53DB74
    uint8_t pad_53DB78[1243164];        // 0x53DB78
    uint32_t dword_66D394;              // 0x66D394
    uint8_t pad_66D398[63128];          // 0x66D398
    uint8_t input_mapping[31];          // 0x67CA30
    uint8_t pad_67CA4F[5];              // 0x67CA4F
    uint32_t dword_67CA54;              // 0x67CA54
    uint8_t pad_67CA58[464];            // 0x67CA58
    uint32_t dword_67CC28;              // 0x67CC28
    uint32_t dword_67CC2C;              // 0x67CC2C
    uint8_t pad_67CC30[464];            // 0x67CC30
    uint32_t dword_67CE00;              // 0x67CE00
    uint8_t pad_67CE04[14160];          // 0x67CE04
    uint32_t dword_680554;              // 0x680554
    uint32_t input_keyboard;            // 0x680558
    uint8_t pad_68055C[49];             // 0x68055C
    uint16_t can_draw;                  // 0x68058D
    uint8_t pad_68058F[11];             // 0x68058F
    uint8_t blood_censor;               // 0x68059A
    uint8_t pad_68059B[22];             // 0x68059B
    uint8_t hard_mode;                  // 0x6805B1
    uint8_t pad_6805B2[36414];          // 0x6805B2
    uint32_t dword_6893F0;              // 0x6893F0
    uint32_t door_state;                // 0x6893F4
    uint8_t pad_6893F8[8];              // 0x6893F8
    DoorInfo* door;                     // 0x689400
    uint8_t pad_689404[908];            // 0x689404
    uint32_t idd;                       // 0x689790
    uint8_t pad_689794[1096];           // 0x689794
    uint32_t dword_689BDC;              // 0x689BDC
    uint8_t pad_689BE0[536];            // 0x689BE0
    uint32_t dword_689DF8;              // 0x689DF8
    uint8_t pad_689DFC[1032];           // 0x689DFC
    Unknown68A204* ctcb;                // 0x68A204
    uint8_t pad_68A208[32089];          // 0x68A208
    uint8_t _st;                        // 0x691F61
    uint8_t itembox_state;              // 0x691F62
    uint8_t byte_691F63;                // 0x691F63
    uint8_t pad_691F64[8];              // 0x691F64
    uint8_t byte_691F6C;                // 0x691F6C
    uint8_t pad_691F6D[3];              // 0x691F6D
    uint8_t byte_691F70;                // 0x691F70
    uint8_t pad_691F71[5];              // 0x691F71
    uint8_t byte_691F76;                // 0x691F76
    uint8_t pad_691F77[13];             // 0x691F77
    uint8_t itembox_slot_id;            // 0x691F84
    uint8_t byte_691F85;                // 0x691F85
    uint8_t pad_691F86[42];             // 0x691F86
    uint16_t word_691FB0;               // 0x691FB0
    uint16_t word_691FB2;               // 0x691FB2
    uint8_t pad_691FB4[4106];           // 0x691FB4
    uint16_t word_692FBE;               // 0x692FBE
    uint16_t word_692FC0;               // 0x692FC0
    uint8_t pad_692FC2[51];             // 0x692FC2
    uint8_t vab_id[16];                 // 0x692FF5
    uint8_t pad_693005[1199];           // 0x693005
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
    uint8_t pad_693813[1081];           // 0x693813
    uint32_t dword_693C4C;              // 0x693C4C
    uint8_t pad_693C50[564];            // 0x693C50
    uint8_t* current_bgm_address;       // 0x693E84
    uint32_t cd_vol_0;                  // 0x693E88
    uint8_t pad_693E8C[20];             // 0x693E8C
    char ss_name_sbgm[260];             // 0x693EA0
    uint8_t byte_693FA4;                // 0x693FA4
    uint8_t pad_693FA5[7859];           // 0x693FA5
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
    uint8_t pad_695E80[267404];         // 0x695E80
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
    uint8_t pad_8E37C4[643900];         // 0x8E37C4
    CCWork cc_work;                     // 0x980B00
    uint8_t pad_9813EC[15456];          // 0x9813EC
    uint16_t fg_message;                // 0x98504C
    uint8_t pad_98504E[13738];          // 0x98504E
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
    uint8_t pad_988620[4];              // 0x988620
    void* mem_top;                      // 0x988624
    uint8_t pad_988628[4];              // 0x988628
    void* dword_98862C;                 // 0x98862C
    VCut* vcut_data[2];                 // 0x988630
    void* em_damage_table_16[48];       // 0x988638
    void* em_die_table[84];             // 0x9886F8
    void* door_aot_data;                // 0x988848
    uint8_t pad_98884C[4];              // 0x98884C
    void* aot_table[32];                // 0x988850
    uint8_t pad_9888D0[5532];           // 0x9888D0
    uint32_t fg_system;                 // 0x989E6C
    uint8_t pad_989E70[96];             // 0x989E70
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
    ObjectEntity pOm;                   // 0x98A61C
    uint8_t pad_98A814[15636];          // 0x98A814
    uint8_t aot_count;                  // 0x98E528
    uint8_t pad_98E529[627];            // 0x98E529
    uint8_t table_start;                // 0x98E79C
    uint8_t pad_98E79D[519];            // 0x98E79D
    uint8_t inventory_size;             // 0x98E9A4
    uint8_t pad_98E9A5[23];             // 0x98E9A5
    uint16_t num_saves;                 // 0x98E9BC
    uint8_t pad_98E9BE[10];             // 0x98E9BE
    uint16_t bgm_table[142];            // 0x98E9C8
    uint16_t scd_variables_00;          // 0x98EAE4
    uint16_t word_98EAE6;               // 0x98EAE6
    uint16_t last_used_item;            // 0x98EAE8
    uint16_t word_98EAEA;               // 0x98EAEA
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
    uint16_t word_98EB22;               // 0x98EB22
    uint16_t next_pld;                  // 0x98EB24
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
    uint8_t pad_98EBD4[48];             // 0x98EBD4
    uint32_t fg_map[5];                 // 0x98EC04
    uint8_t pad_98EC18[8];              // 0x98EC18
    uint32_t pri_be_flg[64];            // 0x98EC20
    uint8_t pad_98ED20[12];             // 0x98ED20
    uint32_t door_locks[2];             // 0x98ED2C
    InventorySlot inventory[11];        // 0x98ED34
    ItemboxItem itembox[64];            // 0x98ED60
    uint8_t pad_98EE60[538];            // 0x98EE60
    uint8_t byte_98F07A;                // 0x98F07A
    uint8_t byte_98F07B;                // 0x98F07B
    uint8_t pad_98F07C[12036];          // 0x98F07C
    uint8_t byte_991F80;                // 0x991F80
    uint8_t pad_991F81[67];             // 0x991F81
    uint32_t dword_991FC4;              // 0x991FC4
    uint8_t pad_991FC8[44760];          // 0x991FC8
    DoorEntity* doors[9];               // 0x99CEA0
    uint32_t dword_99CEC4;              // 0x99CEC4
    uint8_t pad_99CEC8[156];            // 0x99CEC8
    uint32_t dword_99CF64;              // 0x99CF64
    uint8_t pad_99CF68[8];              // 0x99CF68
    uint32_t dword_99CF70;              // 0x99CF70
};
static_assert(sizeof(GameTable) == 0x99CF74);

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

