#include "room.h"
#include "audio.h"
#include "camera.h"
#include "enemy.h"
#include "entity.h"
#include "file.h"
#include "interop.hpp"
#include "marni.h"
#include "marni_renderer.h"
#include "openre.h"
#include "player.h"
#include "rdt.h"
#include "re2.h"
#include "sce.h"
#include "scheduler.h"

#include <cstring>
#include <iterator>
#include <memory>

using namespace openre::audio;
using namespace openre::camera;
using namespace openre::file;
using namespace openre::player;
using namespace openre::sce;
using namespace openre::rdt;
using namespace openre::enemy;

namespace openre::room
{
    static const char* font1 = "common\\data\\font1.tim";
    static const char* font2 = "common\\data\\font1.adt";

    // 0x00442EA0
    static void set_registry_flag(int index, int bit)
    {
        auto* flags = reinterpret_cast<uint32_t*>(&gGameTable.pad_68059C);
        flags[index + (bit >> 5)] |= 0x80000000 >> (bit & 0x1F);
    }

    // 0x004DD0C0
    static void psp_init0()
    {
        auto& psp_work = gGameTable.psp_work;
        psp_work = reinterpret_cast<int>(gGameTable.mem_top);
        // Original advances Mem_top as LPVOID*: lea eax, [eax+ecx*4]
        gGameTable.mem_top
            = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(gGameTable.mem_top) + gGameTable.rdt->header.unknown7 * 4);
    }

    // 0x004DD0E0
    static void psp_init1()
    {
        auto& psp_prim_0 = gGameTable.psp_prim_0;
        auto& psp_prim_1 = gGameTable.psp_prim_1;
        auto count = gGameTable.rdt->header.unknown7;
        auto memTop = reinterpret_cast<int>(gGameTable.mem_top);

        psp_prim_0 = memTop;
        // &Mem_top[8 * count] where Mem_top is LPVOID* (sizeof = 4) = memTop + 8 * count * 4
        psp_prim_1 = memTop + 32 * count;
        gGameTable.mem_top = reinterpret_cast<void*>(psp_prim_1 + 32 * count);
    }

    // 0x004FAF80
    // Returns the map-area index for the given stage/room, used to select the
    // map texture file and the fg_map_area flag bit.
    uint32_t get_map_area_index(uint32_t stage, uint32_t room)
    {
        // nFloor is loaded once at entry (mov cl, pPl.pad_40 + 0xC6) and reused.
        const uint8_t nFloor = gGameTable.pl.nFloor;
        switch (stage)
        {
        case 0:
            if (room == 17 || room == 23)
                return 4;
            if (room == 18)
                return (nFloor == 3) ? 4 : 3;
            if (room <= 3)
                return 0;
            if (room <= 7)
                return 1;
            if (room <= 0x15)
                return 3;
            if (room == 22)
                return (nFloor == 4) ? 3 : 2;
            return 0;
        case 1:
            if (room == 27)
                return 3;
            // sbb eax,eax / and al,0FDh / add eax,5: room < 0x11 -> 2, else -> 5
            return (room < 0x11) ? 2 : 5;
        case 2:
            if (room <= 1 || room == 8)
                return 5;
            return 6;
        case 3:
            if (room == 0 || room == 1 || room == 3)
                return (nFloor >= 4) ? 7 : 8;
            if (room == 2 || room == 9 || room == 0xB)
                return 7;
            return 8;
        case 4:
        {
            if (room <= 2 || room == 8)
                return 9;
            // ecx = fg_status & 0x40000000 loaded once and reused (0x4FB080/0x4FB0B9)
            const bool scenario = check_flag(FlagGroup::Status, FG_STATUS_SCENARIO);
            if (scenario && room == 3)
                return 11 - (check_flag(FlagGroup::Common, 0x62) ? 1 : 0);
            if (room == 5)
            {
                if (scenario)
                {
                    if (check_flag(FlagGroup::Common, 0xBE))
                        return 13;
                }
                else if (check_flag(FlagGroup::Common, 0xBE))
                {
                    return 16;
                }
                if (check_flag(FlagGroup::Common, 0x63))
                    return 12;
                return 10;
            }
            if (room <= 5)
                return 10;
            return (room == 9) ? 12 : 11;
        }
        case 5:
            if (room == 1)
                return (gGameTable.last_cut != 0x602) ? 16 : 13;
            if (room == 3)
                return (nFloor < 3) ? 14 : 13;
            if (room == 0xE)
                return (gGameTable.byte_692FAC == 1) ? 17 : 16;
            if (room <= 4)
                return 13;
            if (room == 5)
                return 15;
            if (room > 0x11)
                return 17;
            return 16;
        case 6: return 18 + (check_flag(FlagGroup::Common, 0x89) ? 1 : 0);
        default: return 0;
        }
    }

    // 0x005023D0
    // Marks the current room as visited on the in-game map. The first flag is
    // bit [stageBase + room] of fg_map; the second is the floor bit used by the
    // map's up/down-floor navigation. Returns a pointer in the original, which
    // is ignored by all callers.
    static void st_room_set()
    {
        // First fg_map bit index for each stage (cumulative room counts). The
        // original defaults the base to 0 for stages outside 1..6, and the
        // fg_map flag is always set.
        static constexpr uint8_t kMapFlagBase[7] = { 0, 30, 58, 72, 89, 99, 123 };
        const auto stage = gGameTable.current_stage;
        const auto base = (stage < std::size(kMapFlagBase)) ? kMapFlagBase[stage] : 0;
        bitarray_set(gGameTable.fg_map, base + gGameTable.current_room);
        bitarray_set(&gGameTable.fg_map_area, get_map_area_index(gGameTable.current_stage, gGameTable.current_room));
    }

    // ESP work slot pool (96 slots of 0x7C bytes each). Only the 'used'
    // flag at +0x18 is meaningful here; the rest of the slot is opaque.
    constexpr int ESP_ROOM_SLOT_START = 8;
    constexpr int ESP_ROOM_SLOT_COUNT = 8;

    // ESP effect loader globals. espdat page-count lookup tables (constant
    // data from the binary's .data segment). Each byte packs two nibbles:
    // high nibble = pages for even rooms, low nibble = pages for odd rooms.
    // Indexed as [16 * stage + room / 2].
    static constexpr uint8_t kEspdatPageCounts0[112] = {
        0x32, 0x31, 0x31, 0x11, 0x53, 0x32, 0x00, 0x01, 0x21, 0x21, 0x11, 0x01, 0x11, 0x13, 0x13, 0x00, 0x12, 0x10, 0x22,
        0x12, 0x01, 0x11, 0x22, 0x21, 0x12, 0x11, 0x01, 0x10, 0x01, 0x10, 0x00, 0x00, 0x00, 0x21, 0x22, 0x10, 0x01, 0x21,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x21, 0x02, 0x03, 0x12, 0x20, 0x11, 0x10, 0x20,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x11, 0x11, 0x10, 0x01, 0x32, 0x11, 0x11, 0x20, 0x11, 0x11, 0x21, 0x00, 0x00,
        0x00, 0x00, 0x11, 0x11, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    static constexpr uint8_t kEspdatPageCounts1[112] = {
        0x00, 0x00, 0x01, 0x11, 0x11, 0x21, 0x11, 0x11, 0x10, 0x11, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11,
        0x11, 0x11, 0x11, 0x11, 0x12, 0x11, 0x11, 0x11, 0x10, 0x01, 0x11, 0x00, 0x00, 0x12, 0x21, 0x20, 0x01, 0x11, 0x11,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0x21, 0x12, 0x03, 0x12, 0x20, 0x00, 0x10, 0x20,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x11, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x11, 0x11, 0x10, 0x11, 0x31, 0x11, 0x11, 0x10, 0x11, 0x11, 0x10, 0x00, 0x00,
        0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    static const char* kEspdatFile1 = "common\\bin\\espdat1.bin";
    static const char* kEspdatFile2 = "common\\bin\\espdat2.bin";

    // Returns the high 16 bits of a packed 32-bit value. The TIM descriptor
    // extracted by esp_tim_info packs the clut/bitmap sizes into the high
    // words of the pointer fields, which is how the original reads them.
    static uint32_t hiword(const void* value)
    {
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value) >> 16);
    }

    // 0x004415E0
    // Extracts the TIM header fields (image and palette pointers) into a
    // MARNI_SURFACE2 used as a scratch structure. Returns 0 when the buffer
    // is not a TIM; the caller only uses the surfaced pointer values.
    static int esp_tim_info(uint32_t* timPtr, MarniSurface2* surface)
    {
        return interop::call<int, uint32_t*, MarniSurface2*>(0x004415E0, timPtr, surface);
    }

    // 0x00509540
    // Reads `size` bytes at `pos` from the given file into `buffer` (opens a
    // handle, seeks, then reads). Returns the number of bytes read on success
    // or 0 on failure.
    static int bg_cache_roomptr(const char* filename, void* buffer, int pos, uint32_t size, int mode)
    {
        return interop::call<int, const char*, void*, int, uint32_t, int>(0x00509540, filename, buffer, pos, size, mode);
    }

    // 0x004B8100
    // Registers the room's ESP effects: copies up to ESP_ROOM_SLOT_COUNT effect
    // ids from idList into the room-held esp_id slots starting at startSlot,
    // stopping at the 0xFF sentinel, then links each effect's data and move
    // tables. The offsets are read from the end of offsetTable backwards (the
    // table's last entry belongs to the first effect id), so each data pointer
    // is base + offset. p_espdt[id] points at the effect data; p_espmv[id]
    // points at its frame/move data, which sits 2 * count + 2 + hiword dwords
    // past the data start, where count/hiword are the low/high words of the
    // effect header dword at the data pointer.
    static void esp_data_set0(uint8_t* idList, uint32_t* offsetTable, uintptr_t base, int startSlot)
    {
        for (uint32_t i = 0; i < ESP_ROOM_SLOT_COUNT; i++)
        {
            const uint8_t id = idList[i];
            gGameTable.esp_id[startSlot + i] = id;
            if (id == 0xFF)
            {
                break;
            }

            auto* const data = reinterpret_cast<uint32_t*>(base + *offsetTable--);
            gGameTable.p_espdt[id] = reinterpret_cast<int32_t>(data);

            const auto header = data[0];
            gGameTable.p_espmv[id] = reinterpret_cast<int32_t>(&data[2 * (header & 0xFFFF) + 2 + (header >> 16)]);
        }
    }

    // Loads the current room's effect texture pages from the espdat bin file
    // into `buffer`. Texture counts come from the per-stage nibble tables
    // indexed by stage/room; fg_system bit 0x1000000 selects the mirror
    // (B-scenario) table and offsets the file position by 7 stages. Returns
    // false when any file read fails.
    static bool esp_load_room_pages(uint8_t* buffer, int mode)
    {
        const char* path = (mode == 1) ? kEspdatFile1 : kEspdatFile2;

        const auto room = gGameTable.current_room;
        const auto stage = gGameTable.current_stage;
        const auto tableIndex = 16 * stage + room / 2;
        auto fileStage = stage;
        int entry;
        if ((gGameTable.fg_system & 0x1000000) != 0)
        {
            fileStage += 7;
            entry = kEspdatPageCounts1[tableIndex];
        }
        else
        {
            entry = kEspdatPageCounts0[tableIndex];
        }
        const auto pos = 32 * (room + 32 * fileStage);
        const auto count = ((room & 1) != 0) ? (entry & 0x0F) : (entry >> 4);

        // Fetch the room's list of espdat file offsets (one dword per page).
        uint32_t pageOffsets[ESP_ROOM_SLOT_COUNT] = {};
        if (!bg_cache_roomptr(path, pageOffsets, pos, 0x20, 4))
        {
            return false;
        }

        auto* dstPtr = gGameTable.esp_anim_data;
        for (int page = 0; page < count; page++)
        {
            if (!load_adt_sub(path, buffer, pageOffsets[page], 4))
            {
                return false;
            }
            if (mode == 2)
            {
                // The 8-bit mode keeps a copy of the effect animation header
                // stored at a fixed offset inside each loaded page.
                std::memcpy(dstPtr, buffer + 0x20014, 0x5C);
                dstPtr += 0x60;
            }
            tim_buffer_to_surface(reinterpret_cast<int*>(buffer), page, 0);
        }
        return true;
    }

    // 0x004B8353
    // Links the 8 room-held ESP effect slots to their texture pages. With
    // Graphicsprdata the pages are assigned directly (mode 0, each effect
    // takes the next page) or via the TIM header sizes (modes 1/2, effects
    // wrap to a fresh page when the shared 256-entry clut would overflow).
    // Each effect's animation data is patched with the running clut offset,
    // bitmap offset, and page number.
    static void esp_link_slots()
    {
        int paletteOffset = 0;    // running clut offset (added to each animation)
        int32_t bitmapOffset = 0; // running bitmap offset (stored as 16-bit)
        uint8_t page = 0;         // current texture page

        for (int i = 0; i < ESP_ROOM_SLOT_COUNT; i++)
        {
            const auto slotId = gGameTable.esp_id[i + ESP_ROOM_SLOT_START];
            if (slotId == 0xFF)
            {
                return;
            }
            auto* const data = reinterpret_cast<uint8_t*>(gGameTable.p_espdt[slotId]);

            const auto mode = static_cast<int8_t>(gGameTable.graphics_ptr_data);
            if (mode == 0)
            {
                // Direct assignment: every effect uses the next page.
                tim_buffer_to_surface(reinterpret_cast<int*>(gGameTable.esp_tim_ptrs[i]), page, 0);
                data[6] = page;
                *reinterpret_cast<uint16_t*>(data + 4) = 0;
                ++page;
                continue;
            }
            if (mode != 1 && mode != 2)
            {
                // Unknown graphics mode leaves the slot untouched.
                continue;
            }

            MarniSurface2 timInfo = {};
            esp_tim_info(static_cast<uint32_t*>(gGameTable.esp_tim_ptrs[i]), &timInfo);
            const auto paletteSize = static_cast<int>(hiword(timInfo.pPalette));
            const auto bitmapSize = static_cast<int>(hiword(timInfo.pBitmap));
            if (paletteOffset + paletteSize > 0x100)
            {
                // The clut would overflow its 256-entry limit; wrap to a new
                // texture page.
                paletteOffset = 0;
                bitmapOffset = 0;
                ++page;
            }

            const auto slotCount = *reinterpret_cast<uint16_t*>(data);
            *reinterpret_cast<uint16_t*>(data + 4) = static_cast<uint16_t>(bitmapOffset);
            data[6] = page;

            // Patch each animation's clut offset; they are stored at a 4-byte
            // stride past the effect's animation headers.
            const auto animCount = *reinterpret_cast<uint16_t*>(data + 2);
            auto* blendPtr = data + 8 * slotCount + 9;
            for (int k = 0; k < animCount; k++)
            {
                *blendPtr = static_cast<uint8_t>(*blendPtr + static_cast<uint8_t>(paletteOffset));
                blendPtr += 4;
            }
            paletteOffset += paletteSize;
            bitmapOffset += bitmapSize;
        }
    }

    // 0x004B8170
    // Loads the room's ESP (effect) texture data. Converts the 8 model
    // texture offsets into absolute effect TIM pointers, unloads the shared
    // texture pages, uploads the per-room effect pages from the espdat bin
    // file, then links every effect to its texture page.
    static void esp_data_set1(void* tim, void* type)
    {
        // The model-texture offsets are stored backwards in the RDT, so walk
        // the list from the end, rebasing each offset onto the ESP data block.
        for (int i = 0; i < ESP_ROOM_SLOT_COUNT; i++)
        {
            tim = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(tim) - 4);
            const auto offset = *reinterpret_cast<uint32_t*>(tim);
            gGameTable.esp_tim_ptrs[i] = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(type) + offset);
        }

        for (int page = 0; page < ESP_ROOM_SLOT_COUNT; page++)
        {
            marni::unload_texture_page(page);
        }

        auto buffer = std::make_unique<uint8_t[]>(0x21000);

        // Only the 16-bit (1) and 8-bit (2) graphics modes upload their pages
        // from the espdat bin; other modes link the effects directly.
        const auto mode = static_cast<int8_t>(gGameTable.graphics_ptr_data);
        if ((mode == 1 || mode == 2) && !esp_load_room_pages(buffer.get(), mode))
        {
            file_error();
            return;
        }

        esp_link_slots();
    }

    // 0x004B8080
    // Resets the room ESP (effect) system: clears the 'used' flag of every ESP
    // work slot, releases the room-held effect slots (8..15, stopping at the
    // first free slot), then links the room's ESP effect table if it has one.
    static void esp_init_r()
    {
        // Clear the 'used' flag of every ESP work slot.
        for (auto& slot : gGameTable.esp_work)
        {
            slot.used = 0;
        }

        // Release the room-held ESP slots (8..15), breaking on the first
        // unused slot.
        for (uint32_t i = ESP_ROOM_SLOT_START; i < ESP_ROOM_SLOT_START + ESP_ROOM_SLOT_COUNT; i++)
        {
            const auto id = gGameTable.esp_id[i];
            if (id == 0xFF)
            {
                break;
            }
            gGameTable.esp_id[i] = 0xFF;
            gGameTable.p_espdt[id] = -1;
            gGameTable.p_espmv[id] = -1;
        }

        // Link the room's ESP effect table if it has any effects.
        auto* espIds = rdt_get_offset<uint8_t>(RdtOffsetKind::ESP_IDS);
        if (espIds != nullptr && *reinterpret_cast<int32_t*>(espIds) != -1)
        {
            esp_data_set0(
                espIds,
                rdt_get_offset<uint32_t>(RdtOffsetKind::ESP_EFF_TABLE),
                reinterpret_cast<uintptr_t>(espIds),
                ESP_ROOM_SLOT_START);
            esp_data_set1(rdt_get_offset<void*>(RdtOffsetKind::MODEL_TEXTURES), rdt_get_offset<void*>(RdtOffsetKind::EFF));
        }
    }

    static void memset32(void* dest, uint32_t value, size_t count)
    {
        auto* ptr = static_cast<uint32_t*>(dest);
        for (size_t i = 0; i < count; i++)
        {
            ptr[i] = value;
        }
    }

    // 0x004DE7B0
    void room_set()
    {
        auto& ctcb = *gGameTable.ctcb;

        while (true)
        {
            switch (ctcb.var_0D)
            {
            case 0:
            {
                strcpy(gGameTable.room_path, "Pl0\\Rdt\\room1000.rdt");
                if (gGameTable.graphics_ptr_data == 1)
                {
                    std::memcpy(gGameTable.stage_font_name, font1, 22);
                }
                else
                {
                    std::memcpy(gGameTable.stage_font_name, font2, 22);
                }
                if (check_flag(FlagGroup::Status, FG_STATUS_PLAYER))
                {
                    ++gGameTable.room_path[2];
                    ++gGameTable.room_path[15];
                }
                gGameTable.p_em = &gGameTable.pl;
                gGameTable.room_path[12] = gStageSymbols[(gGameTable.byte_98E798) + gGameTable.current_stage];
                gGameTable.room_path[13] += gGameTable.current_room / 16;
                const auto mod = gGameTable.current_room % 16;
                if (mod >= 10)
                {
                    gGameTable.room_path[14] = mod + 87;
                }
                else
                {
                    gGameTable.room_path[14] = mod + 48;
                }

                switch (gGameTable.current_stage)
                {
                case 0:
                {
                    if (gGameTable.current_room == 22)
                    {
                        set_registry_flag(0, 56);
                    }
                    break;
                }
                case 1:
                {
                    switch (gGameTable.current_room)
                    {
                    case 0: set_registry_flag(0, 48); break;
                    case 2: set_registry_flag(0, 49); break;
                    case 4: set_registry_flag(0, 51); break;
                    case 7: set_registry_flag(0, 50); break;
                    case 8: set_registry_flag(0, 53); break;
                    case 10: set_registry_flag(0, 52); break;
                    case 11: set_registry_flag(0, 55); [[fallthrough]];
                    case 27: set_registry_flag(0, 54); break;
                    default: goto LABEL_34;
                    }
                }
                case 2:
                {
                    if (gGameTable.current_room == 5)
                    {
                        set_registry_flag(0, 57);
                    }
                    break;
                }
                case 3:
                {
                    if (gGameTable.current_room == 5)
                    {
                        set_registry_flag(0, 59);
                    }
                    break;
                }
                case 4:
                {
                    if (gGameTable.current_room == 0)
                    {
                        set_registry_flag(0, 58);
                    }
                    break;
                }
                }
            LABEL_34:
                auto* pEm = gGameTable.p_em;
                gGameTable.word_98E78C = 0;
                gGameTable.fg_rbj_set = 0;
                gGameTable.fg_status &= 0xFFF04000;
                pEm->pOn_om = 0;
                pEm->status_flg = (pEm->status_flg & 0xf900) << 8 | (pEm->status_flg & 0xff);
                gGameTable.mem_top = reinterpret_cast<void*>(gGameTable.dword_988620);
                gGameTable.rdt = reinterpret_cast<Rdt*>(gGameTable.dword_988620);
                ctcb.var_0D = 10;
                goto LABEL_35;
            }
            case 1: goto LABEL_62;
            case 2: goto LABEL_63;
            case 3:
            {
                if (gGameTable.current_stage == gGameTable.byte_989E7D)
                {
                    ctcb.var_0D = 5;
                    continue;
                }

                gGameTable.byte_989E7D = gGameTable.current_stage & 0xff;
                if (gGameTable.stage_bk == gGameTable.current_stage)
                {
                    ctcb.var_0D = 4;
                    task_sleep(1);
                    return;
                }
                void* fBuff = file_alloc(0x20014);
                gGameTable.stage_font_name[16] += gGameTable.byte_989E7D;
                switch (gGameTable.graphics_ptr_data)
                {
                case 1:
                {
                    if (!read_file_into_buffer(gGameTable.stage_font_name, fBuff, 4))
                    {
                        file_error();
                        return;
                    }
                    break;
                }
                case 0:
                case 2:
                {
                    if (!load_adt(gGameTable.stage_font_name, fBuff, 4))
                    {
                        file_error();
                        return;
                    }
                    break;
                }
                }
                tim_buffer_to_surface(reinterpret_cast<int*>(fBuff), 9, 1);
                file_alloc(0);
                gGameTable.stage_bk = gGameTable.byte_989E7D;
                ctcb.var_0D = 4;
                task_sleep(1);
                return;
            }
            case 4:
            case 5:
            {
                gGameTable.word_989EE8 = 3333;
                osp_read();
                gGameTable.byte_689C64 = 1;
                gGameTable.rdt_size = read_file_into_buffer(gGameTable.room_path, gGameTable.rdt, 8);
                if (!gGameTable.rdt_size)
                {
                    file_error();
                    return;
                }
                for (int i = 0; i < 23; i++)
                {
                    auto baseRdt = (uint32_t)&(*gGameTable.rdt);

                    if (gGameTable.rdt->offsets[i])
                    {
                        auto offset = rdt_get_offset<uintptr_t>(static_cast<RdtOffsetKind>(i));
                        gGameTable.rdt->offsets[i] = (void*)(baseRdt + (uint32_t)offset);
                    }
                }

                gGameTable.rdt_count = 0;
                if (gGameTable.rdt->header.num_cuts)
                {
                    auto baseRdt = (uint32_t)&(*gGameTable.rdt);
                    auto cameras = rdt_get_offset<RdtCamera>(RdtOffsetKind::RID);
                    for (int i = 0; i < gGameTable.rdt->header.num_cuts; i++)
                    {
                        cameras[i].offset += baseRdt;
                        gGameTable.rdt_count++;
                    }
                }

                gGameTable.mem_top = reinterpret_cast<void*>((uintptr_t)gGameTable.mem_top + gGameTable.rdt_size);
                gGameTable.rdt_count = 0;
                if (gGameTable.rdt->header.num_models)
                {
                    auto baseRdt = (uint32_t)&(*gGameTable.rdt);
                    auto models = rdt_get_offset<RdtModel>(RdtOffsetKind::MODELS);
                    for (int i = 0; i < gGameTable.rdt->header.num_models; i++)
                    {
                        models[i].texture_offset += baseRdt;
                        models[i].model_offset += baseRdt;
                        gGameTable.rdt_count++;
                    }
                }

                cut_change(gGameTable.current_cut & 0xff);
                esp_init_r();
                ctcb.var_0D = 6;
            LABEL_84:
                snd_room_load();
                if (!ctcb.var_13)
                {
                    marni::unloadTexturePage(17);
                    sce_model_init();
                    snd_bgm_play_ck();
                    if (rdt_get_offset<void*>(RdtOffsetKind::VB))
                    {
                        gGameTable.mem_top = rdt_get_offset<void*>(RdtOffsetKind::VB);
                    }
                    gGameTable.actor_entity = &gGameTable.pl;
                    gGameTable.pl.routine_0 = 0;
                    gGameTable.pl.routine_1 = 0;
                    gGameTable.pl.routine_2 = 0;
                    gGameTable.pl.routine_3 = 0;
                    player_move(&gGameTable.pl);
                    sce_scheduler_set();
                    ctcb.var_0D = 7;
                LABEL_88:
                    snd_load_enemy();
                    if (!ctcb.var_13)
                    {
                        rbj_set();
                        psp_init0();
                        ctcb.var_0D = 8;
                    LABEL_90:
                        marni::out();
                        if (!ctcb.var_13)
                        {
                            em_init_move();
                            psp_init1();
                            ctcb.var_0D = 9;
                        LABEL_92:
                            if (gGameTable.byte_99270F)
                            {
                                task_sleep(1);
                            }
                            else
                            {
                                st_room_set();
                                marni::flush_surfaces();
                                ctcb.var_0D = 0;
                            }
                        }
                    }
                }
                return;
            }
            case 6: goto LABEL_84;
            case 7: goto LABEL_88;
            case 8: goto LABEL_90;
            case 9: goto LABEL_92;
            case 10:
            {
            LABEL_35:
                snd_bgm_set();
                if (ctcb.var_13)
                {
                    return;
                }
                marni::unload_register_surfaces(0);
                gGameTable.dword_98862C = &gGameTable.enemies;
                gGameTable.enemy_count = 0;
                memset32(&gGameTable.splayer_work, 0x0098E544, 33);
                gGameTable.enemy_init_entries[0].enabled = 0;
                gGameTable.enemy_init_entries[1].enabled = 0;
                marni::release_object_textures();

                gGameTable.obj_ptr = gGameTable.pOm;
                gGameTable.rdt_count = 32;
                gGameTable.pOm->be_flg = 0;
                if (gGameTable.p_em->id == (gGameTable.next_pld & 0xff))
                {
                    ctcb.var_0D = 2;
                    continue;
                }
                if (gGameTable.next_pld < 12)
                {
                    if (gGameTable.next_pld & 1)
                    {
                        if (check_flag(FlagGroup::Zapping, FG_ZAPPING_6))
                        {
                            gGameTable.next_pld = 9;
                        }
                    }
                    else
                    {
                        if (check_flag(FlagGroup::Zapping, FG_ZAPPING_5))
                        {
                            gGameTable.next_pld = 8;
                        }
                        if (check_flag(FlagGroup::Zapping, FG_ZAPPING_15))
                        {
                            gGameTable.next_pld = 10;
                        }
                    }
                }
                gGameTable.dword_689C1C = gGameTable.p_em->id;
                gGameTable.p_em->id = static_cast<uint8_t>(gGameTable.next_pld);
                partner_switch(static_cast<uint8_t>(gGameTable.next_pld));
                player_set(gGameTable.p_em);

                if (!ctcb.var_13)
                {
                    gGameTable.p_em->routine_0 = 0;
                    gGameTable.p_em->routine_1 = 0;
                    gGameTable.p_em->routine_2 = 0;
                    gGameTable.p_em->routine_3 = 0;

                    if (gGameTable.next_pld == 14 || gGameTable.next_pld == 15)
                    {
                        gGameTable.word_98E9B6 = gGameTable.pl.life;
                        gGameTable.byte_98E9AB = gGameTable.poison_timer;
                        gGameTable.word_98E9AC = gGameTable.poison_status;
                        gGameTable.poison_timer = 0;
                        gGameTable.poison_status = 0;
                        gGameTable.pl.life = gGameTable.pl.max_life;
                    }
                    else if ((gGameTable.dword_689C1C & 0xff) >= 12)
                    {
                        gGameTable.poison_timer = gGameTable.byte_98E9AB;
                        gGameTable.poison_status = gGameTable.word_98E9AC;
                        gGameTable.pl.life = gGameTable.word_98E9B6;
                    }
                }

                gGameTable.byte_691F7B = 1;
                ctcb.var_0D = 1;
            LABEL_62:
                snd_load_core(gGameTable.next_pld & 0xff, 1);
                if (!ctcb.var_13)
                {
                LABEL_63:
                    gGameTable.byte_99270F = 0;
                    ctcb.var_0D = 3;
                    task_sleep(1);
                }
            }
                return;
            }
        }
    }
}
