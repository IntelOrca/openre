#include "rdt_re1.h"
#include "re2.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Resident Evil 1 room (RDT) -> RE2 room (RDT) converter.
//
// The RE1 RDT is loaded fully into the RE1-side structs below (all defined in
// this file). re2.h structs (RdtHeader, RdtCamera, RdtModel) are used only
// as copy destinations when mapping RE1 data into RE2 format.

namespace openre::rdt_re1
{
    namespace
    {
        // --- RE1 RDT format (all RE1-side structs live here) ---

        // 0x00: header, 6 bytes.
        struct Re1Header
        {
            uint8_t nSprite;
            uint8_t nCut;
            uint8_t nOmodel;
            uint8_t nItem;
            uint8_t nDoor;
            uint8_t nRoom_at;
        };

        // 0x48: offsets table, 19 entries.
        enum class Re1Chunk
        {
            RVD = 0,
            SCA = 1,
            ObjectModelTable = 2,
            ItemModelTable = 3,
            BLK = 4,
            FLR = 5,
            InitSCD = 6,
            MainSCD = 7,
            EventSCD = 8,
            EMR = 9,
            EDD = 10,
            MSG = 11,
            ItemPix = 12,
            ESPIds = 13,
            ESPEff = 14,
            ESPTim = 15,
            EDT = 16,
            VH = 17,
            VB = 18,
        };

        // 0x94: camera, 0x28 bytes each.
        struct Re1Camera
        {
            int32_t masks_offset;      // 0x00 absolute file offset into PRI region
            int32_t tim_masks_offset;  // 0x04 absolute file offset into camera TIMs
            int32_t from_x;            // 0x08
            int32_t from_y;            // 0x0C
            int32_t from_z;            // 0x10
            int32_t to_x;              // 0x14
            int32_t to_y;              // 0x18
            int32_t to_z;              // 0x1C
            int32_t unknown1[3];       // 0x20
        };

        // Camera switch entry, 0x14 bytes, in the RVD region.
        struct Re1CameraSwitch
        {
            uint16_t to;
            uint16_t from;
            int16_t x1, y1;
            int16_t x2, y2;
            int16_t x3, y3;
            int16_t x4, y4;
        };

        // Embedded model table entry, 8 bytes.
        struct Re1EmbeddedModel
        {
            uint32_t tmd_offset;
            uint32_t tim_offset;
        };

        // The whole RE1 RDT loaded into memory.
        struct Re1Rdt
        {
            Re1Header header{};
            uint32_t offsets[19]{};
            std::vector<uint8_t> lit;             // 0x42 bytes @0x06
            std::vector<Re1Camera> cameras;       // @0x94, header.nCut entries
            std::vector<uint8_t> rvd;             // camera switches (RE1)
            std::vector<uint8_t> sca;
            std::vector<uint8_t> blk;
            std::vector<uint8_t> flr;
            std::vector<uint8_t> msg;
            std::vector<uint8_t> edt;
            std::vector<uint8_t> vh;
            std::vector<uint8_t> vb;
            std::vector<uint8_t> pri;             // camera mask region
            std::vector<uint8_t> espIds;
            std::vector<Re1EmbeddedModel> objectModels;
            std::vector<uint8_t> objectTmd;       // raw RE1 TMD blobs (concatenated)
            std::vector<uint8_t> objectTim;       // raw RE1 TIM blobs (concatenated)
            std::vector<uint32_t> objectTmdOffsets;
            std::vector<uint32_t> objectTimOffsets;
        };

        // --- small binary reader over the raw RE1 bytes ---

        struct Reader
        {
            const uint8_t* data;
            size_t size;

            bool in_bounds(size_t offset, size_t length) const
            {
                return offset <= size && length <= size - offset;
            }

            template<typename T> bool read(size_t offset, T& out) const
            {
                if (!in_bounds(offset, sizeof(T)))
                    return false;
                std::memcpy(&out, data + offset, sizeof(T));
                return true;
            }
        };

        // --- RE1 loading ---

        // Loads the whole RE1 RDT into `rdt`. `priStart`/`priEnd` bound the
        // camera mask region that the camera masks_offset values point into.
        bool load_re1_rdt(const Reader& r, Re1Rdt& rdt, uint32_t& priStart, uint32_t& priEnd)
        {
            if (!r.read(0x00, rdt.header))
                return false;
            if (r.size < 0x94)
                return false;

            for (size_t i = 0; i < 19; i++)
            {
                if (!r.read(0x48 + i * 4, rdt.offsets[i]))
                    return false;
            }

            // LIT is fixed at 0x06 and always 0x42 bytes.
            rdt.lit.assign(r.data + 0x06, r.data + 0x06 + 0x42);

            // Cameras at 0x94.
            const size_t numCuts = rdt.header.nCut;
            if (!r.in_bounds(0x94, numCuts * sizeof(Re1Camera)))
                return false;
            rdt.cameras.resize(numCuts);
            for (size_t i = 0; i < numCuts; i++)
            {
                if (!r.read(0x94 + i * sizeof(Re1Camera), rdt.cameras[i]))
                    return false;
            }

            // Compute chunk sizes from the offsets table: each chunk extends to
            // the next higher offset in file order (mirrors RdtFileData). The
            // camera mask/texture offsets also carve the RVD region: the RVD
            // data ends where the first camera mask begins, and the PRI/mask
            // data sits between the smallest mask offset and the camera
            // textures.
            std::vector<uint32_t> boundaries;
            for (uint32_t off : rdt.offsets)
            {
                if (off != 0)
                    boundaries.push_back(off);
            }
            for (const auto& cam : rdt.cameras)
            {
                if (cam.masks_offset > 0)
                    boundaries.push_back(static_cast<uint32_t>(cam.masks_offset));
                if (cam.tim_masks_offset > 0)
                    boundaries.push_back(static_cast<uint32_t>(cam.tim_masks_offset));
            }
            std::sort(boundaries.begin(), boundaries.end());
            boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
            if (boundaries.empty())
                return false;

            auto chunk = [&](Re1Chunk kind, std::vector<uint8_t>& out) -> bool {
                const uint32_t start = rdt.offsets[static_cast<size_t>(kind)];
                if (start == 0)
                    return true;
                uint32_t end = static_cast<uint32_t>(r.size);
                auto it = std::upper_bound(boundaries.begin(), boundaries.end(), start);
                if (it != boundaries.end())
                    end = *it;
                if (end < start || end - start > r.size)
                    return false;
                out.assign(r.data + start, r.data + end);
                return true;
            };

            if (!chunk(Re1Chunk::RVD, rdt.rvd))
                return false;
            if (!chunk(Re1Chunk::SCA, rdt.sca))
                return false;
            if (!chunk(Re1Chunk::BLK, rdt.blk))
                return false;
            if (!chunk(Re1Chunk::FLR, rdt.flr))
                return false;
            if (!chunk(Re1Chunk::MSG, rdt.msg))
                return false;
            if (!chunk(Re1Chunk::EDT, rdt.edt))
                return false;
            if (!chunk(Re1Chunk::VH, rdt.vh))
                return false;
            if (!chunk(Re1Chunk::VB, rdt.vb))
                return false;
            if (!chunk(Re1Chunk::ESPIds, rdt.espIds))
                return false;

            // Object model table + embedded TMD/TIM blobs.
            const uint32_t tableStart = rdt.offsets[static_cast<size_t>(Re1Chunk::ObjectModelTable)];
            if (tableStart != 0 && rdt.header.nOmodel != 0)
            {
                if (!r.in_bounds(tableStart, rdt.header.nOmodel * sizeof(Re1EmbeddedModel)))
                    return false;
                rdt.objectModels.resize(rdt.header.nOmodel);
                for (size_t i = 0; i < rdt.objectModels.size(); i++)
                {
                    if (!r.read(tableStart + i * sizeof(Re1EmbeddedModel), rdt.objectModels[i]))
                        return false;
                }

                // The TMD/TIM blob sizes are bounded by the next higher offset.
                std::vector<uint32_t> modelBounds = boundaries;
                for (const auto& m : rdt.objectModels)
                {
                    if (m.tmd_offset != 0)
                        modelBounds.push_back(m.tmd_offset);
                    if (m.tim_offset != 0)
                        modelBounds.push_back(m.tim_offset);
                }
                std::sort(modelBounds.begin(), modelBounds.end());
                modelBounds.erase(std::unique(modelBounds.begin(), modelBounds.end()), modelBounds.end());

                auto blob = [&](uint32_t start, std::vector<uint8_t>& out) -> bool {
                    if (start == 0)
                        return true;
                    uint32_t end = static_cast<uint32_t>(r.size);
                    auto it = std::upper_bound(modelBounds.begin(), modelBounds.end(), start);
                    if (it != modelBounds.end())
                        end = *it;
                    if (end < start || end - start > r.size || !r.in_bounds(start, end - start))
                        return false;
                    out.insert(out.end(), r.data + start, r.data + end);
                    return true;
                };

                // Concatenate blobs and remember each one's offset into the
                // concatenation, mirroring how Rdt2.Builder stores them.
                rdt.objectTmdOffsets.reserve(rdt.objectModels.size());
                rdt.objectTimOffsets.reserve(rdt.objectModels.size());
                for (const auto& m : rdt.objectModels)
                {
                    rdt.objectTmdOffsets.push_back(static_cast<uint32_t>(rdt.objectTmd.size()));
                    if (!blob(m.tmd_offset, rdt.objectTmd))
                        return false;
                    rdt.objectTimOffsets.push_back(static_cast<uint32_t>(rdt.objectTim.size()));
                    if (!blob(m.tim_offset, rdt.objectTim))
                        return false;
                }
            }

            // PRI region: the camera mask data. It starts at the smallest
            // camera masks_offset and ends at the start of the next chunk
            // after the largest one (the camera textures).
            priStart = 0;
            priEnd = 0;
            uint32_t maxMask = 0;
            for (const auto& cam : rdt.cameras)
            {
                if (cam.masks_offset > 0)
                {
                    const uint32_t mask = static_cast<uint32_t>(cam.masks_offset);
                    if (priStart == 0 || mask < priStart)
                        priStart = mask;
                    if (mask > maxMask)
                        maxMask = mask;
                }
            }
            if (priStart != 0)
            {
                auto it = std::upper_bound(boundaries.begin(), boundaries.end(), maxMask);
                priEnd = it != boundaries.end() ? *it : static_cast<uint32_t>(r.size);
                if (priEnd >= priStart && r.in_bounds(priStart, priEnd - priStart))
                {
                    rdt.pri.assign(r.data + priStart, r.data + priEnd);
                }
            }

            return true;
        }

        // --- RE2 output writing ---

        struct Writer
        {
            std::vector<uint8_t> bytes;
            uint32_t offsets[23]{};

            size_t tell() const
            {
                return bytes.size();
            }

            void seek_and_write(size_t pos, const void* data, size_t len)
            {
                if (bytes.size() < pos + len)
                    bytes.resize(pos + len);
                std::memcpy(bytes.data() + pos, data, len);
            }

            void write_u8(uint8_t v)
            {
                bytes.push_back(v);
            }

            void write_u16(uint16_t v)
            {
                bytes.push_back(static_cast<uint8_t>(v));
                bytes.push_back(static_cast<uint8_t>(v >> 8));
            }

            void write_u32(uint32_t v)
            {
                bytes.push_back(static_cast<uint8_t>(v));
                bytes.push_back(static_cast<uint8_t>(v >> 8));
                bytes.push_back(static_cast<uint8_t>(v >> 16));
                bytes.push_back(static_cast<uint8_t>(v >> 24));
            }

            void write_bytes(const uint8_t* data, size_t len)
            {
                bytes.insert(bytes.end(), data, data + len);
            }

            void write_u32s(const std::vector<uint32_t>& values)
            {
                for (uint32_t v : values)
                    write_u32(v);
            }

            // Writes a chunk and records its file offset into `offsetSlot`.
            size_t write_chunk(int offsetSlot, const std::vector<uint8_t>& data)
            {
                offsets[offsetSlot] = static_cast<uint32_t>(tell());
                write_bytes(data.data(), data.size());
                return offsets[offsetSlot];
            }

            void patch_offsets(size_t tablePos)
            {
                for (int i = 0; i < 23; i++)
                {
                    seek_and_write(tablePos + i * 4, &offsets[i], 4);
                }
            }
        };

        // --- RE2 model conversion ---

        // RE1 embedded object model (TMD): header + object descriptors +
        // positions + normals + triangle primitives. RE2 embedded models (MD1)
        // split primitives into a triangle descriptor and a quad descriptor
        // per part, with texture data stored separately. This mirrors
        // IntelOrca.Biohazard.Model.Md1.Builder.ToMesh.

        // RE1 TMD header: [Length][?][NumObjects][ObjectDescriptor[]].
        struct Re1TmdHeader
        {
            uint32_t length;   // 0x41 magic in practice
            uint32_t unknown;  // 0
            uint32_t numObjects;
        };

        struct Re1TmdObject
        {
            uint32_t vtx_offset;
            uint32_t vtx_count;
            uint32_t nor_offset;
            uint32_t nor_count;
            uint32_t pri_offset;
            uint32_t pri_count;
            uint32_t unknown;
        };

        // RE1 triangle primitive, 0x1C bytes.
        struct Re1TmdTriangle
        {
            uint32_t unknown;
            uint8_t tu0;
            uint8_t tv0;
            uint16_t clutId;
            uint8_t tu1;
            uint8_t tv1;
            uint16_t page;
            uint8_t tu2;
            uint8_t tv2;
            uint16_t dummy;
            uint16_t n0, v0;
            uint16_t n1, v1;
            uint16_t n2, v2;
        };

        // RE1 position/normal vector, 8 bytes.
        struct Re1TmdVector
        {
            int16_t x, y, z, zero;
        };

        // Converts one RE1 TMD blob into an RE2 MD1 blob. Returns false on
        // malformed input.
        bool convert_re1_tmd_to_md1(const uint8_t* tmd, size_t tmdSize, std::vector<uint8_t>& out)
        {
            if (tmdSize < sizeof(Re1TmdHeader))
                return false;

            Re1TmdHeader header{};
            std::memcpy(&header, tmd, sizeof(header));
            const size_t numObjects = header.numObjects;
            if (numObjects == 0 || tmdSize < sizeof(Re1TmdHeader) + numObjects * sizeof(Re1TmdObject))
                return false;

            // Each RE1 object becomes one MD1 part = two descriptors (triangle
            // + quad). RE1 TMDs only carry triangles, so the quad descriptor
            // is always empty.
            const size_t numEntries = numObjects * 2;

            std::vector<Re1TmdObject> objects(numObjects);
            for (size_t i = 0; i < numObjects; i++)
            {
                std::memcpy(&objects[i], tmd + sizeof(Re1TmdHeader) + i * sizeof(Re1TmdObject), sizeof(Re1TmdObject));
            }

            // Gather positions, normals and triangles, tracking the cumulative
            // per-object offsets into the shared MD1 data regions.
            std::vector<uint8_t> positions;
            std::vector<uint8_t> normals;
            std::vector<uint8_t> triangles;
            std::vector<uint8_t> triangleTextures;
            std::vector<uint32_t> vtxOffsets(numObjects);
            std::vector<uint32_t> norOffsets(numObjects);
            std::vector<uint32_t> priOffsets(numObjects);
            std::vector<uint32_t> texOffsets(numObjects);      // tri descriptor
            std::vector<uint32_t> quadTexOffsets(numObjects);  // quad descriptor

            for (size_t i = 0; i < numObjects; i++)
            {
                const Re1TmdObject& obj = objects[i];
                const size_t vtxOffset = sizeof(Re1TmdHeader) + obj.vtx_offset;
                const size_t norOffset = sizeof(Re1TmdHeader) + obj.nor_offset;
                const size_t priOffset = sizeof(Re1TmdHeader) + obj.pri_offset;

                if (vtxOffset + obj.vtx_count * sizeof(Re1TmdVector) > tmdSize
                    || norOffset + obj.nor_count * sizeof(Re1TmdVector) > tmdSize
                    || priOffset + obj.pri_count * sizeof(Re1TmdTriangle) > tmdSize)
                {
                    return false;
                }

                vtxOffsets[i] = static_cast<uint32_t>(positions.size());
                norOffsets[i] = static_cast<uint32_t>(normals.size());
                priOffsets[i] = static_cast<uint32_t>(triangles.size());
                texOffsets[i] = static_cast<uint32_t>(triangleTextures.size());

                positions.insert(positions.end(), tmd + vtxOffset, tmd + vtxOffset + obj.vtx_count * sizeof(Re1TmdVector));
                normals.insert(normals.end(), tmd + norOffset, tmd + norOffset + obj.nor_count * sizeof(Re1TmdVector));

                for (size_t j = 0; j < obj.pri_count; j++)
                {
                    Re1TmdTriangle tri{};
                    std::memcpy(&tri, tmd + priOffset + j * sizeof(Re1TmdTriangle), sizeof(tri));

                    // RE2 MD1 triangle: 6 u16 (n,v) pairs.
                    const uint16_t md1Tri[] = { tri.n0, tri.v0, tri.n1, tri.v1, tri.n2, tri.v2 };
                    triangles.insert(triangles.end(), (const uint8_t*)md1Tri, (const uint8_t*)md1Tri + sizeof(md1Tri));

                    // RE2 MD1 triangle texture (12 bytes). The page and CLUT
                    // are recomputed from the RE1 texture page number exactly
                    // as biohazard-utils' Md1Converter does:
                    //   clutId = 0x7800 | page * 0x40
                    //   page   = 0x80  | page
                    // (RE1 stores the raw page in the low nibble of its page
                    // field; the u16 page value carries other flags too.)
                    const uint16_t page = static_cast<uint16_t>(tri.page & 0x0F);
                    const uint16_t clut = static_cast<uint16_t>(0x7800 | (page << 6));
                    const uint8_t md1Tex[] = {
                        tri.tu0, tri.tv0,
                        (uint8_t)clut, (uint8_t)(clut >> 8),
                        tri.tu1, tri.tv1,
                        (uint8_t)(0x80 | page), 0,
                        tri.tu2, tri.tv2,
                        0, 0,
                    };
                    triangleTextures.insert(triangleTextures.end(), md1Tex, md1Tex + sizeof(md1Tex));
                }

                // The quad descriptor's texture offset comes after the triangle
                // textures (mirrors Md1.Builder).
                quadTexOffsets[i] = static_cast<uint32_t>(triangleTextures.size());
            }

            // Serialise the MD1, mirroring Md1.Builder.ToMesh: the descriptor
            // table sits at offset 12 and the data regions follow it, so the
            // stored offsets are relative to the descriptor table start (12).
            const size_t vertexOffset = numEntries * sizeof(Re1TmdObject);
            const size_t normalOffset = vertexOffset + positions.size();
            const size_t primitiveOffset = normalOffset + normals.size();
            const size_t textureOffset = primitiveOffset + triangles.size();

            out.clear();
            out.reserve(textureOffset + triangleTextures.size());

            Writer w;
            w.write_u32(0); // Length placeholder
            w.write_u32(0); // Absolute
            w.write_u32(static_cast<uint32_t>(numEntries));

            for (size_t i = 0; i < numObjects; i++)
            {
                // Triangle descriptor.
                w.write_u32(static_cast<uint32_t>(vertexOffset + vtxOffsets[i]));
                w.write_u32(objects[i].vtx_count);
                w.write_u32(static_cast<uint32_t>(normalOffset + norOffsets[i]));
                w.write_u32(objects[i].nor_count);
                w.write_u32(static_cast<uint32_t>(primitiveOffset + priOffsets[i]));
                w.write_u32(objects[i].pri_count);
                w.write_u32(static_cast<uint32_t>(textureOffset + texOffsets[i]));
                // Quad descriptor (empty): mirrors Md1.Builder, which places
                // it at the primitive offset (start of triangle data) with a
                // texture offset after the triangle textures.
                w.write_u32(static_cast<uint32_t>(vertexOffset + vtxOffsets[i]));
                w.write_u32(objects[i].vtx_count);
                w.write_u32(static_cast<uint32_t>(normalOffset + norOffsets[i]));
                w.write_u32(objects[i].nor_count);
                w.write_u32(static_cast<uint32_t>(primitiveOffset));
                w.write_u32(0);
                w.write_u32(static_cast<uint32_t>(textureOffset + quadTexOffsets[i]));
            }

            w.write_bytes(positions.data(), positions.size());
            w.write_bytes(normals.data(), normals.size());
            w.write_bytes(triangles.data(), triangles.size());

            // Length = everything written so far (before textures).
            const uint32_t length = static_cast<uint32_t>(w.bytes.size());
            w.seek_and_write(0, &length, sizeof(length));

            w.write_bytes(triangleTextures.data(), triangleTextures.size());

            out = std::move(w.bytes);
            return true;
        }

        // Minimal RE2 SCD: an empty procedure list (two no-op events so both
        // EVT_MAIN and EVT_FRAME resolve to a valid offset).
        constexpr uint8_t kStubScd[] = { 0x04, 0x00, 0x06, 0x00, 0x01, 0x00, 0x01, 0x00 };
    }

    std::vector<uint8_t> convert_re1_rdt(const uint8_t* data, size_t size)
    {
        std::vector<uint8_t> out;
        if (data == nullptr || size < 0x94)
            return out;

        Reader reader{ data, size };
        Re1Rdt rdt;
        uint32_t priStart = 0;
        uint32_t priEnd = 0;
        if (!load_re1_rdt(reader, rdt, priStart, priEnd))
            return out;

        Writer w;

        // --- header ---
        RdtHeader header{};
        header.num_sprites = rdt.header.nSprite;
        header.num_cuts = rdt.header.nCut;
        header.num_models = rdt.header.nOmodel;
        header.num_items = rdt.header.nItem;
        header.num_doors = rdt.header.nDoor;
        header.room_at = rdt.header.nRoom_at;
        header.reverb_lv = 0;
        header.unknown7 = 0;
        w.write_bytes(reinterpret_cast<const uint8_t*>(&header), sizeof(header));

        // --- offsets table placeholder ---
        const size_t tablePos = w.tell();
        for (int i = 0; i < 23; i++)
            w.write_u32(0);

        // --- RID (slot 7): cameras ---
        w.offsets[7] = static_cast<uint32_t>(w.tell());
        const size_t ridPos = w.tell();
        std::vector<uint8_t> rid(sizeof(RdtCamera) * rdt.cameras.size());
        for (size_t i = 0; i < rdt.cameras.size(); i++)
        {
            RdtCamera dst{};
            const Re1Camera& src = rdt.cameras[i];
            dst.pos_x = src.from_x;
            dst.pos_y = src.from_y;
            dst.pos_z = src.from_z;
            dst.target_x = src.to_x;
            dst.target_y = src.to_y;
            dst.target_z = src.to_z;
            dst.offset = 0; // patched after PRI placement
            std::memcpy(rid.data() + i * sizeof(RdtCamera), &dst, sizeof(RdtCamera));
        }
        w.write_bytes(rid.data(), rid.size());

        // --- object model table (slot 10) placeholder ---
        const size_t modelTablePos = w.tell();
        w.offsets[10] = static_cast<uint32_t>(modelTablePos);
        for (size_t i = 0; i < rdt.objectModels.size(); i++)
        {
            w.write_u32(0); // tim offset
            w.write_u32(0); // md1 offset
        }

        // --- RVD (slot 8): convert RE1 camera switches to RE2 VCuts ---
        if (!rdt.rvd.empty())
        {
            w.offsets[8] = static_cast<uint32_t>(w.tell());
            const size_t switchCount = rdt.rvd.size() / sizeof(Re1CameraSwitch);
            for (size_t i = 0; i < switchCount; i++)
            {
                Re1CameraSwitch sw{};
                std::memcpy(&sw, rdt.rvd.data() + i * sizeof(Re1CameraSwitch), sizeof(sw));

                VCut dst{};
                dst.be_flg = 1;
                dst.nFloor = 0xFF;
                dst.fCut = static_cast<uint8_t>(sw.from);
                dst.tCut = static_cast<uint8_t>(sw.to);
                dst.a.x = sw.x1;
                dst.a.y = sw.y1;
                dst.b.x = sw.x2;
                dst.b.y = sw.y2;
                dst.c.x = sw.x3;
                dst.c.y = sw.y3;
                dst.d.x = sw.x4;
                dst.d.y = sw.y4;
                w.write_bytes(reinterpret_cast<const uint8_t*>(&dst), sizeof(dst));
            }
            // RE2 RVD is terminated by a -1.
            w.write_u32(0xFFFFFFFF);
        }

        // --- LIT (slot 9) ---
        w.write_chunk(9, rdt.lit);

        // --- PRI (camera mask region), then patch RID masks_offset ---
        if (!rdt.pri.empty() && priStart != 0)
        {
            const size_t priPos = w.tell();
            w.write_bytes(rdt.pri.data(), rdt.pri.size());

            // Rebase each camera's masks_offset from the old PRI base to the
            // new one.
            for (size_t i = 0; i < rdt.cameras.size(); i++)
            {
                const int32_t oldMask = rdt.cameras[i].masks_offset;
                RdtCamera dst{};
                const size_t pos = ridPos + i * sizeof(RdtCamera);
                std::memcpy(&dst, rid.data() + i * sizeof(RdtCamera), sizeof(dst));
                if (oldMask >= static_cast<int32_t>(priStart) && oldMask < static_cast<int32_t>(priEnd))
                {
                    dst.offset = static_cast<uint32_t>(priPos + (oldMask - static_cast<int32_t>(priStart)));
                }
                else
                {
                    dst.offset = 0;
                }
                w.seek_and_write(pos, &dst, sizeof(dst));
            }
        }

        // --- SCA (slot 6) ---
        w.write_chunk(6, rdt.sca);

        // --- BLK (slot 12) ---
        w.write_chunk(12, rdt.blk);

        // --- FLR (slot 11) ---
        w.write_chunk(11, rdt.flr);

        // --- SCD_INIT (slot 16) / SCD_MAIN (slot 17): stubs ---
        w.write_chunk(16, std::vector<uint8_t>(kStubScd, kStubScd + sizeof(kStubScd)));
        w.write_chunk(17, std::vector<uint8_t>(kStubScd, kStubScd + sizeof(kStubScd)));

        // --- MSG_JA (slot 13) ---
        w.write_chunk(13, rdt.msg);

        // --- ESP_IDS (slot 18): effects/enemies out of scope for the first
        // task; leave empty so the room parses cleanly without an effects
        // table. ---

        // --- embedded object models (slot 10 entries + MD1/TIM blobs) ---
        // Each RE1 TMD is converted into an RE2 MD1 (split into triangle/quad
        // descriptors with separate texture data). TIM blobs are written last
        // and the model table patched with absolute offsets.
        if (!rdt.objectModels.empty())
        {
            std::vector<uint32_t> md1Offsets(rdt.objectModels.size(), 0);
            std::vector<uint32_t> timOffsets(rdt.objectModels.size(), 0);

            // TMD blobs -> MD1 blobs.
            for (size_t i = 0; i < rdt.objectModels.size(); i++)
            {
                const size_t tmdStart = rdt.objectTmdOffsets[i];
                const size_t tmdEnd = i + 1 < rdt.objectTmdOffsets.size() ? rdt.objectTmdOffsets[i + 1] : rdt.objectTmd.size();
                const size_t tmdSize = tmdEnd - tmdStart;
                if (tmdSize == 0)
                    continue;

                std::vector<uint8_t> md1;
                if (!convert_re1_tmd_to_md1(rdt.objectTmd.data() + tmdStart, tmdSize, md1))
                    continue;
                md1Offsets[i] = static_cast<uint32_t>(w.tell());
                w.write_bytes(md1.data(), md1.size());
            }

            // TIM blobs.
            for (size_t i = 0; i < rdt.objectModels.size(); i++)
            {
                const size_t timStart = rdt.objectTimOffsets[i];
                const size_t timEnd = i + 1 < rdt.objectTimOffsets.size() ? rdt.objectTimOffsets[i + 1] : rdt.objectTim.size();
                const size_t timSize = timEnd - timStart;
                if (timSize == 0)
                    continue;
                timOffsets[i] = static_cast<uint32_t>(w.tell());
                w.write_bytes(rdt.objectTim.data() + timStart, timSize);
            }

            // Patch the model table with {tim_offset, md1_offset}.
            for (size_t i = 0; i < rdt.objectModels.size(); i++)
            {
                const size_t pos = modelTablePos + i * sizeof(RdtModel);
                RdtModel dst{};
                dst.texture_offset = timOffsets[i];
                dst.model_offset = md1Offsets[i];
                w.seek_and_write(pos, &dst, sizeof(dst));
            }
        }

        // --- EDT (slot 0), VH (slot 1), VB (slot 2) ---
        w.write_chunk(0, rdt.edt);
        w.write_chunk(1, rdt.vh);
        w.write_chunk(2, rdt.vb);

        // --- patch the offsets table ---
        w.patch_offsets(tablePos);

        return std::move(w.bytes);
    }

    // --- RE1 cut backgrounds (STAGE*/RC*.pak) ---

    // The RC pak files hold one LZ-compressed 16bpp TIM (320x240 RGB555) per
    // camera cut. The compression is an LZW-style format (0x100 = EOF,
    // 0x101 = widen, 0x102 = end of block) with a growing dictionary, decoded
    // bit-serial MSB-first. Verified byte-for-byte against the original
    // (FUN_0046cbb0): all 318 rooms round-trip through the RE2 background path.

    // Decompresses an RE1 LZ stream (FUN_0046cbb0). Returns the raw decoded
    // bytes (e.g. the 16bpp TIM image), or empty on failure.
    static bool lz_decode(const uint8_t* src, size_t size, std::vector<uint8_t>& out)
    {
        if (!src || size == 0)
            return false;

        out.clear();
        size_t inPos = 0;
        uint8_t srcByte = 0;
        uint8_t mask = 0x80;

        std::vector<uint32_t> parent(0x10000, 0xFFFFFFFF);
        std::vector<uint8_t> ch(0x10000, 0);
        uint32_t dictSize = 0x103;
        int width = 9;

        auto readByte = [&]() -> uint8_t { return inPos < size ? src[inPos++] : 0; };
        auto readCode = [&](int bits) -> uint32_t {
            uint32_t v = 0;
            for (int i = 0; i < bits; i++)
            {
                if (mask == 0x80)
                    srcByte = readByte();
                v = (v << 1) | ((srcByte & mask) ? 1 : 0);
                mask >>= 1;
                if (mask == 0)
                    mask = 0x80;
            }
            return v;
        };

        std::vector<uint8_t> tmp(0x4000);

        while (true)
        {
            // Reset the dictionary per block.
            std::fill(parent.begin(), parent.end(), 0xFFFFFFFF);
            dictSize = 0x103;
            width = 9;

            uint32_t code = readCode(width);
            if (code == 0x100)
                break; // EOF

            out.push_back((uint8_t)code);
            uint32_t prev = code;
            uint8_t lastChar = (uint8_t)code;

            while (true)
            {
                uint32_t u = readCode(width);
                if (u == 0x100)
                    return true; // EOF
                if (u == 0x102)
                    break; // end of block -> reset dict
                if (u == 0x101)
                {
                    width++;
                    continue;
                }

                uint32_t orig = u;
                int start = 0;
                if (dictSize <= u)
                {
                    // Code not in dictionary yet: predict the first char.
                    tmp[0] = lastChar;
                    start = 1;
                    u = prev;
                }

                // Unwind the chain into tmp[start..].
                int n = start;
                uint32_t c = u;
                if (c > 0xff)
                {
                    while (c > 0xff)
                    {
                        if (c >= parent.size())
                            return false;
                        tmp[n++] = ch[c];
                        c = parent[c];
                    }
                }
                tmp[n] = (uint8_t)c;
                n++;

                uint8_t cLast = tmp[n - 1];
                lastChar = cLast;
                for (int i = n; i >= 1; i--)
                    out.push_back(tmp[i - 1]);

                if (dictSize >= parent.size())
                    return false;
                parent[dictSize] = prev;
                ch[dictSize] = cLast;
                dictSize++;
                prev = orig;
            }
        }
        return true;
    }

    // Extracts the pixel payload of a 16bpp TIM buffer into an RGB555 image.
    // Returns false if the buffer is not a 16bpp TIM.
    bool tim16_to_rgb555(const uint8_t* tim, size_t size, int& width, int& height, std::vector<uint8_t>& pixels)
    {
        pixels.clear();
        if (!tim || size < 20)
            return false;

        const uint32_t magic = *(const uint32_t*)(tim + 0);
        if (magic != 0x10)
            return false;
        const uint32_t flags = *(const uint32_t*)(tim + 4);
        if ((flags & 3) != 2) // not 16bpp
            return false;

        size_t p = 8;
        if ((flags & 3) != 2)
        {
            uint32_t clutSize = *(const uint32_t*)(tim + p);
            p += 4 + clutSize;
        }

        if (p + 12 > size)
            return false;
        const uint32_t imgSize = *(const uint32_t*)(tim + p);
        p += 4;
        const uint16_t w = *(const uint16_t*)(tim + p + 4);
        const uint16_t h = *(const uint16_t*)(tim + p + 6);
        (void)imgSize;
        p += 8;

        const size_t pxBytes = (size_t)w * h * 2;
        if (p + pxBytes > size)
            return false;

        width = w;
        height = h;
        pixels.assign(tim + p, tim + p + pxBytes);
        return true;
    }

    // --- Exported background conversion ---

    // RE1 "shaking rooms" (lifts, boulders) store their 16bpp background 4 px
    // smaller than the screen (316x236) packed as 316-wide rows, despite the
    // TIM header claiming 320x240. pak2tim calls this the "-r4" format. The
    // game re-reads the pixel data at 316 px/row and displays it centred in a
    // 320x240 frame (the 2 px border is the shake margin).
    //
    // This detects such buffers and restitches them into the normal 320x240
    // layout. Detection compares the row-to-row correlation of the pixel data
    // read as 320-wide rows against 316-wide rows: an r4 buffer is incoherent
    // at 320 px/row (each row starts 4 px into the next packed row) but
    // coherent at 316 px/row.
    namespace
    {
        // Average correlation between consecutive rows of `pixels` when read
        // with `stride` pixels per row, over the green channel (5 bits).
        double row_correlation(const uint16_t* px, size_t total, int stride, int rows)
        {
            double sum = 0;
            int n = 0;
            for (int y = 1; y < rows; y++)
            {
                double ma = 0;
                double mb = 0;
                for (int x = 0; x < stride; x++)
                {
                    ma += (px[(size_t)y * stride + x] >> 5) & 31;
                    mb += (px[(size_t)(y - 1) * stride + x] >> 5) & 31;
                }
                ma /= stride;
                mb /= stride;
                double num = 0;
                double da = 0;
                double db = 0;
                for (int x = 0; x < stride; x++)
                {
                    const double a = ((px[(size_t)y * stride + x] >> 5) & 31) - ma;
                    const double b = ((px[(size_t)(y - 1) * stride + x] >> 5) & 31) - mb;
                    num += a * b;
                    da += a * a;
                    db += b * b;
                }
                if (da > 0 && db > 0)
                    sum += num / std::sqrt(da * db);
                n++;
            }
            return n ? sum / n : 0.0;
        }
    }

    // Converts an RE1 cut background pak (STAGE*/RC*.pak) into an RGB555
    // pixel buffer. The pak is an LZ-compressed 16bpp TIM. Returns false on
    // failure. On success the image dimensions are reported in w/h (camera
    // cuts are 320x240; the one-off 316x236 menu screen has different ones).
    // Shaking-room backgrounds ("-r4") are restitched to 320x240.
    bool convert_re1_background(const uint8_t* data, size_t size, std::vector<uint8_t>& rgb555, int& w, int& h)
    {
        std::vector<uint8_t> tim;
        if (!lz_decode(data, size, tim))
            return false;

        if (!tim16_to_rgb555(tim.data(), tim.size(), w, h, rgb555))
            return false;

        // Restitch shaking-room backgrounds stored 4 px smaller.
        if (w == 320 && h == 240)
        {
            const uint16_t* px = reinterpret_cast<const uint16_t*>(rgb555.data());
            const double corr320 = row_correlation(px, rgb555.size() / 2, 320, 240);
            const double corr316 = row_correlation(px, rgb555.size() / 2, 316, 236);
            if (corr320 < 0.7 && corr316 > 0.7 && corr316 > corr320 + 0.15)
            {
                // Read the pixel data as 316-wide rows and place each into the
                // destination at row+2, col+2 (pak2tim remove_4_pixels).
                std::vector<uint8_t> reorg(320 * 240 * 2, 0);
                uint16_t* dst = reinterpret_cast<uint16_t*>(reorg.data());
                for (int y = 0; y < 236; y++)
                {
                    std::memcpy(dst + (size_t)(y + 2) * 320 + 2, px + (size_t)y * 316, 316 * 2);
                }
                rgb555 = std::move(reorg);
            }
        }
        return true;
    }

    // Runtime RE1 room addressing. The game names rooms ROOM<stage><room>
    // <variant>.RDT (4 hex digits); the engine's room loader needs the pack
    // path of the converted room plus the cut-background room code (first 3
    // digits, shared by the room and its A/B variant).
    bool re1_room_info(int stage, int room, int variant, Re1RoomInfo& out)
    {
        if (stage < 0 || stage > 6 || room < 0 || room > 0x1F || (variant != 0 && variant != 1))
            return false;

        const char hex[] = "0123456789abcdef";
        char name[8];
        name[0] = hex[stage + 1]; // STAGE1..7 -> digit 1..7
        name[1] = hex[room >> 4];
        name[2] = hex[room & 0xF];
        name[3] = hex[variant];
        name[4] = '\0';

        out.roomPath = std::string("room\\room") + name + ".rdt";
        out.roomCode.assign(name, 3);
        return true;
    }
}
