// Self-contained SDL_GPU 2D sprite renderer for the ECS scene tree.
//
// This is deliberately NOT part of the legacy marni renderer: it owns its
// textures, pipelines and vertex buffers, draws in window pixel space and
// presents directly to the swapchain. The only shared pieces are the engine's
// SDL_GPU device/window (system::gpu) and the embedded shader bytecode
// (shaders/tl_*), which are used the same way the movie overlay in the legacy
// renderer does.

#include "render2d.h"

#include "logger.h"
#include "system_gpu.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace openre::render2d
{
    namespace
    {
        // TL vertex (matches kTLVertexStride in system_gpu.cpp, 32 bytes).
        // Position in pixels (top-left origin, z in [0,1]), colour packed
        // RGBA (memory order B, G, R, A), UVs in [0,1].
        struct Vertex
        {
            float sx;
            float sy;
            float sz;
            float rhw;
            uint8_t b;
            uint8_t g;
            uint8_t r;
            uint8_t a;
            uint8_t pad[4];
            float tu;
            float tv;
        };
        static_assert(sizeof(Vertex) == 0x20, "render2d vertex must be 32 bytes");

        void setColor(Vertex& v, uint32_t packedColor)
        {
            v.b = (uint8_t)(packedColor & 0xFF);
            v.g = (uint8_t)((packedColor >> 8) & 0xFF);
            v.r = (uint8_t)((packedColor >> 16) & 0xFF);
            v.a = (uint8_t)((packedColor >> 24) & 0xFF);
        }

        // Decodes a render2d::Image into 32-bit RGBA (used by texture upload).
        // Mirrors the colour/black-key semantics of the legacy texture loader.
        bool decodeToRgba(const Image& img, std::vector<uint8_t>& rgba)
        {
            rgba.assign((size_t)img.width * img.height * 4, 0);

            if (img.depth == 16)
            {
                const size_t needed = (size_t)img.width * img.height * 2;
                if (img.pixels.size() < needed)
                {
                    logging::logWarning("[render2d] 16bpp image pixels too small ({} < {})", img.pixels.size(), needed);
                    return false;
                }
                const auto* src = (const uint16_t*)img.pixels.data();
                for (int y = 0; y < img.height; y++)
                {
                    const uint16_t* row = src + (size_t)y * img.width;
                    uint8_t* dst = rgba.data() + (size_t)y * img.width * 4;
                    for (int x = 0; x < img.width; x++)
                    {
                        const uint16_t v = row[x];
                        // RE2 PC transparency: pixels with no colour (RGB == 0,
                        // e.g. 0x0000 or 0x8000) are fully transparent.
                        const bool transparent = (v & 0x7FFF) == 0;
                        if (img.psxFormat)
                        {
                            // PSX 555: red low, no alpha.
                            dst[0] = (uint8_t)((v & 0x1F) << 3);
                            dst[1] = (uint8_t)(((v >> 5) & 0x1F) << 3);
                            dst[2] = (uint8_t)(((v >> 10) & 0x1F) << 3);
                        }
                        else
                        {
                            // 555 layout: blue low, alpha in bit 15.
                            dst[0] = (uint8_t)(((v >> 10) & 0x1F) << 3);
                            dst[1] = (uint8_t)(((v >> 5) & 0x1F) << 3);
                            dst[2] = (uint8_t)((v & 0x1F) << 3);
                        }
                        dst[3] = transparent ? 0x00 : 0xFF;
                        dst += 4;
                    }
                }
                return true;
            }

            if (img.depth == 32)
            {
                const size_t needed = (size_t)img.width * img.height * 4;
                if (img.pixels.size() < needed)
                {
                    logging::logWarning("[render2d] 32bpp image pixels too small ({} < {})", img.pixels.size(), needed);
                    return false;
                }
                const auto* src = (const uint32_t*)img.pixels.data();
                for (int y = 0; y < img.height; y++)
                {
                    const uint32_t* row = src + (size_t)y * img.width;
                    uint8_t* dst = rgba.data() + (size_t)y * img.width * 4;
                    for (int x = 0; x < img.width; x++)
                    {
                        const uint32_t v = row[x];
                        dst[0] = (uint8_t)((v >> 16) & 0xFF);
                        dst[1] = (uint8_t)((v >> 8) & 0xFF);
                        dst[2] = (uint8_t)(v & 0xFF);
                        dst[3] = (uint8_t)((v >> 24) & 0xFF);
                        dst += 4;
                    }
                }
                return true;
            }

            if (img.depth == 4 || img.depth == 8)
            {
                if (img.palBpp != 16 || img.palCnt <= 0)
                {
                    logging::logWarning(
                        "[render2d] paletted image without 16bpp palette (palBpp={} palCnt={})", img.palBpp, img.palCnt);
                    return false;
                }
                const int entriesPerPal = img.depth == 4 ? 16 : 256;
                const size_t palNeeded = (size_t)entriesPerPal * img.palCnt * 2;
                if (img.palette.size() < palNeeded)
                {
                    logging::logWarning("[render2d] palette too small ({} < {})", img.palette.size(), palNeeded);
                    return false;
                }
                const auto* pal = (const uint16_t*)img.palette.data();

                uint8_t lut[256 * 4];
                for (int i = 0; i < entriesPerPal; i++)
                {
                    const uint16_t e = pal[i];
                    lut[i * 4 + 0] = (uint8_t)((e & 0x1F) << 3);
                    lut[i * 4 + 1] = (uint8_t)(((e >> 5) & 0x1F) << 3);
                    lut[i * 4 + 2] = (uint8_t)(((e >> 10) & 0x1F) << 3);
                    lut[i * 4 + 3] = ((e & 0x7FFF) == 0) ? 0x00 : 0xFF;
                }

                if (img.depth == 4)
                {
                    const size_t rowBytes = ((size_t)img.width + 1) / 2;
                    if (img.pixels.size() < rowBytes * (size_t)img.height)
                    {
                        logging::logWarning("[render2d] 4bpp image pixels too small");
                        return false;
                    }
                    for (int y = 0; y < img.height; y++)
                    {
                        const uint8_t* src = img.pixels.data() + (size_t)y * rowBytes;
                        uint8_t* dst = rgba.data() + (size_t)y * img.width * 4;
                        for (int x = 0; x < img.width; x++)
                        {
                            const uint8_t nib = (x & 1) ? (src[x / 2] >> 4) : (src[x / 2] & 0xF);
                            std::memcpy(dst + x * 4, lut + (size_t)nib * 4, 4);
                        }
                    }
                }
                else
                {
                    const size_t rowBytes = (size_t)img.width;
                    if (img.pixels.size() < rowBytes * (size_t)img.height)
                    {
                        logging::logWarning("[render2d] 8bpp image pixels too small");
                        return false;
                    }
                    for (int y = 0; y < img.height; y++)
                    {
                        const uint8_t* src = img.pixels.data() + (size_t)y * rowBytes;
                        uint8_t* dst = rgba.data() + (size_t)y * img.width * 4;
                        for (int x = 0; x < img.width; x++)
                        {
                            const uint8_t idx = src[x];
                            std::memcpy(dst + x * 4, lut + (size_t)idx * 4, 4);
                        }
                    }
                }
                return true;
            }

            logging::logWarning("[render2d] unsupported depth {}", img.depth);
            return false;
        }
    } // namespace

    struct SpriteRenderer::Impl
    {
        SDL_GPUShader* vertexShader = nullptr;
        SDL_GPUShader* texturedFrag = nullptr;
        SDL_GPUShader* solidFrag = nullptr;
        SDL_GPUSampler* sampler = nullptr;

        SDL_GPUGraphicsPipeline* texturedPipeline = nullptr;
        SDL_GPUGraphicsPipeline* solidPipeline = nullptr;
        SDL_GPUTextureFormat pipelineFormat = SDL_GPU_TEXTUREFORMAT_INVALID;

        struct TexEntry
        {
            SDL_GPUTexture* texture = nullptr;
            int width = 0;
            int height = 0;
        };
        std::unordered_map<int, TexEntry> textures;
        int nextHandle = 1;

        SDL_GPUBuffer* vertexBuffer = nullptr;
        SDL_GPUTransferBuffer* vertexTransfer = nullptr;
        size_t vertexCapacity = 0;

        FadeState fade;

        bool ensureShadersAndSampler(SDL_GPUDevice* dev)
        {
            if (vertexShader && texturedFrag && solidFrag && sampler)
                return true;

            const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(dev);
            SDL_GPUShaderFormat format;
            const uint8_t* vsCode;
            const uint8_t* texCode;
            const uint8_t* solidCode;
            uint32_t vsSize, texSize, solidSize;
            if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0)
            {
                format = SDL_GPU_SHADERFORMAT_DXIL;
                vsCode = gfx::gTLVertexDxil;
                vsSize = gfx::gTLVertexDxilSize;
                texCode = gfx::gTLTexturedFragDxil;
                texSize = gfx::gTLTexturedFragDxilSize;
                solidCode = gfx::gTLUntexturedFragDxil;
                solidSize = gfx::gTLUntexturedFragDxilSize;
            }
            else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0)
            {
                format = SDL_GPU_SHADERFORMAT_SPIRV;
                vsCode = gfx::gTLVertexSpirv;
                vsSize = gfx::gTLVertexSpirvSize;
                texCode = gfx::gTLTexturedFragSpirv;
                texSize = gfx::gTLTexturedFragSpirvSize;
                solidCode = gfx::gTLUntexturedFragSpirv;
                solidSize = gfx::gTLUntexturedFragSpirvSize;
            }
            else
            {
                logging::logError("[render2d] no supported shader format (got 0x{}); cannot render", (uint32_t)formats);
                return false;
            }

            SDL_GPUShaderCreateInfo vsInfo{};
            vsInfo.code_size = vsSize;
            vsInfo.code = vsCode;
            vsInfo.entrypoint = "main";
            vsInfo.format = format;
            vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
            vsInfo.num_samplers = 0;
            vsInfo.num_uniform_buffers = 1;
            vertexShader = SDL_CreateGPUShader(dev, &vsInfo);
            if (!vertexShader)
            {
                logging::logError("[render2d] SDL_CreateGPUShader(vertex) failed: {}", SDL_GetError());
                return false;
            }

            SDL_GPUShaderCreateInfo texInfo{};
            texInfo.code_size = texSize;
            texInfo.code = texCode;
            texInfo.entrypoint = "main";
            texInfo.format = format;
            texInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
            texInfo.num_samplers = 1;
            texturedFrag = SDL_CreateGPUShader(dev, &texInfo);
            if (!texturedFrag)
            {
                logging::logError("[render2d] SDL_CreateGPUShader(textured frag) failed: {}", SDL_GetError());
                return false;
            }

            SDL_GPUShaderCreateInfo solidInfo{};
            solidInfo.code_size = solidSize;
            solidInfo.code = solidCode;
            solidInfo.entrypoint = "main";
            solidInfo.format = format;
            solidInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
            solidInfo.num_samplers = 0;
            solidFrag = SDL_CreateGPUShader(dev, &solidInfo);
            if (!solidFrag)
            {
                logging::logError("[render2d] SDL_CreateGPUShader(solid frag) failed: {}", SDL_GetError());
                return false;
            }

            SDL_GPUSamplerCreateInfo sci{};
            sci.min_filter = SDL_GPU_FILTER_NEAREST;
            sci.mag_filter = SDL_GPU_FILTER_NEAREST;
            sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            sci.min_lod = 0.0f;
            sci.max_lod = 0.0f;
            sampler = SDL_CreateGPUSampler(dev, &sci);
            if (!sampler)
            {
                logging::logError("[render2d] SDL_CreateGPUSampler failed: {}", SDL_GetError());
                return false;
            }
            return true;
        }

        SDL_GPUGraphicsPipeline* createPipeline(SDL_GPUDevice* dev, SDL_GPUShader* frag, SDL_GPUTextureFormat colorFormat)
        {
            SDL_GPUVertexBufferDescription vbDesc{};
            vbDesc.slot = 0;
            vbDesc.pitch = sizeof(Vertex);
            vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

            SDL_GPUVertexAttribute attrs[3]{};
            attrs[0].location = 0;
            attrs[0].buffer_slot = 0;
            attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
            attrs[0].offset = 0;
            attrs[1].location = 1;
            attrs[1].buffer_slot = 0;
            attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
            attrs[1].offset = 16;
            attrs[2].location = 2;
            attrs[2].buffer_slot = 0;
            attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
            attrs[2].offset = 24;

            SDL_GPUVertexInputState inputState{};
            inputState.vertex_buffer_descriptions = &vbDesc;
            inputState.num_vertex_buffers = 1;
            inputState.vertex_attributes = attrs;
            inputState.num_vertex_attributes = 3;

            SDL_GPURasterizerState raster{};
            raster.fill_mode = SDL_GPU_FILLMODE_FILL;
            raster.cull_mode = SDL_GPU_CULLMODE_NONE;
            raster.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
            raster.enable_depth_clip = true;

            SDL_GPUMultisampleState ms{};
            ms.sample_count = SDL_GPU_SAMPLECOUNT_1;

            SDL_GPUDepthStencilState depthStencil{};

            SDL_GPUColorTargetBlendState bs{};
            bs.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            bs.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            bs.color_blend_op = SDL_GPU_BLENDOP_ADD;
            bs.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            bs.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            bs.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            bs.color_write_mask
                = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
            bs.enable_blend = true;

            SDL_GPUColorTargetDescription target{};
            target.format = colorFormat;
            target.blend_state = bs;

            SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
            targetInfo.color_target_descriptions = &target;
            targetInfo.num_color_targets = 1;
            targetInfo.has_depth_stencil_target = false;

            SDL_GPUGraphicsPipelineCreateInfo ci{};
            ci.vertex_shader = vertexShader;
            ci.fragment_shader = frag;
            ci.vertex_input_state = inputState;
            ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
            ci.rasterizer_state = raster;
            ci.multisample_state = ms;
            ci.depth_stencil_state = depthStencil;
            ci.target_info = targetInfo;

            SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(dev, &ci);
            if (!pipe)
                logging::logError("[render2d] SDL_CreateGPUGraphicsPipeline failed: {}", SDL_GetError());
            return pipe;
        }

        bool ensurePipelines(SDL_GPUDevice* dev)
        {
            if (!ensureShadersAndSampler(dev))
                return false;

            const SDL_GPUTextureFormat format = (SDL_GPUTextureFormat)system::gpu::swapchain_format();
            if (texturedPipeline && solidPipeline && pipelineFormat == format)
                return true;

            if (texturedPipeline)
                SDL_ReleaseGPUGraphicsPipeline(dev, texturedPipeline);
            if (solidPipeline)
                SDL_ReleaseGPUGraphicsPipeline(dev, solidPipeline);
            texturedPipeline = nullptr;
            solidPipeline = nullptr;

            texturedPipeline = createPipeline(dev, texturedFrag, format);
            solidPipeline = createPipeline(dev, solidFrag, format);
            if (!texturedPipeline || !solidPipeline)
                return false;

            pipelineFormat = format;
            return true;
        }

        bool ensureVertexBuffer(SDL_GPUDevice* dev, size_t vertexCount)
        {
            if (vertexBuffer && vertexCapacity >= vertexCount)
                return true;

            const size_t size = vertexCount * sizeof(Vertex);
            SDL_GPUBufferCreateInfo bci{};
            bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bci.size = size;
            SDL_GPUTransferBufferCreateInfo tbci{};
            tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tbci.size = size;

            if (vertexBuffer)
            {
                SDL_ReleaseGPUBuffer(dev, vertexBuffer);
                vertexBuffer = nullptr;
            }
            if (vertexTransfer)
            {
                SDL_ReleaseGPUTransferBuffer(dev, vertexTransfer);
                vertexTransfer = nullptr;
            }

            vertexBuffer = SDL_CreateGPUBuffer(dev, &bci);
            vertexTransfer = SDL_CreateGPUTransferBuffer(dev, &tbci);
            if (!vertexBuffer || !vertexTransfer)
            {
                logging::logError("[render2d] vertex buffer allocation failed: {}", SDL_GetError());
                return false;
            }
            vertexCapacity = vertexCount;
            return true;
        }
    };

    SpriteRenderer::SpriteRenderer()
        : impl(new Impl())
    {
    }

    SpriteRenderer::~SpriteRenderer()
    {
        shutdown();
        delete impl;
        impl = nullptr;
    }

    SpriteRenderer& SpriteRenderer::get()
    {
        static SpriteRenderer instance;
        return instance;
    }

    int SpriteRenderer::loadTexture(const Image& image)
    {
        SDL_GPUDevice* dev = (SDL_GPUDevice*)system::gpu::device();
        if (!dev || image.width <= 0 || image.height <= 0)
            return 0;

        std::vector<uint8_t> rgba;
        if (!decodeToRgba(image, rgba))
            return 0;

        SDL_GPUTextureCreateInfo tci{};
        tci.type = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        tci.width = (Uint32)image.width;
        tci.height = (Uint32)image.height;
        tci.layer_count_or_depth = 1;
        tci.num_levels = 1;
        tci.sample_count = SDL_GPU_SAMPLECOUNT_1;

        SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev, &tci);
        if (!tex)
        {
            logging::logError("[render2d] SDL_CreateGPUTexture failed: {}", SDL_GetError());
            return 0;
        }

        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = (Uint32)rgba.size();
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbci);
        if (!tb)
        {
            logging::logError("[render2d] SDL_CreateGPUTransferBuffer failed: {}", SDL_GetError());
            SDL_ReleaseGPUTexture(dev, tex);
            return 0;
        }
        void* mapped = SDL_MapGPUTransferBuffer(dev, tb, false);
        if (!mapped)
        {
            logging::logError("[render2d] SDL_MapGPUTransferBuffer failed: {}", SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(dev, tb);
            SDL_ReleaseGPUTexture(dev, tex);
            return 0;
        }
        std::memcpy(mapped, rgba.data(), rgba.size());
        SDL_UnmapGPUTransferBuffer(dev, tb);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
        if (!cmd)
        {
            logging::logError("[render2d] SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(dev, tb);
            SDL_ReleaseGPUTexture(dev, tex);
            return 0;
        }
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
        if (cp)
        {
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = tb;
            src.pixels_per_row = (Uint32)image.width;
            src.rows_per_layer = (Uint32)image.height;
            SDL_GPUTextureRegion dst{};
            dst.texture = tex;
            dst.w = (Uint32)image.width;
            dst.h = (Uint32)image.height;
            dst.d = 1;
            SDL_UploadToGPUTexture(cp, &src, &dst, false);
            SDL_EndGPUCopyPass(cp);
        }
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(dev, tb); // deferred until GPU done

        // Allocate a handle (skip slots already in use).
        int handle = 0;
        for (int i = 0; i < 4096; i++)
        {
            if (impl->nextHandle < 1)
                impl->nextHandle = 1;
            const int cand = impl->nextHandle++;
            if (impl->textures.find(cand) == impl->textures.end())
            {
                handle = cand;
                break;
            }
        }
        if (handle == 0)
        {
            logging::logError("[render2d] no free texture handle");
            SDL_ReleaseGPUTexture(dev, tex);
            return 0;
        }

        impl->textures[handle] = { tex, image.width, image.height };
        logging::logInfo("[render2d] loaded texture handle {} ({}x{})", handle, image.width, image.height);
        return handle;
    }

    int SpriteRenderer::createDynamicTexture(int w, int h)
    {
        SDL_GPUDevice* dev = (SDL_GPUDevice*)system::gpu::device();
        if (!dev || w <= 0 || h <= 0)
            return 0;

        // SAMPLER lets present() bind it; the RGBA8 layout matches the decoded
        // video frames. (This SDL build uploads via SDL_UploadToGPUTexture
        // without a separate COPY_DST flag, matching loadTexture above.)
        SDL_GPUTextureCreateInfo tci{};
        tci.type = SDL_GPU_TEXTURETYPE_2D;
        tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        tci.width = (Uint32)w;
        tci.height = (Uint32)h;
        tci.layer_count_or_depth = 1;
        tci.num_levels = 1;
        tci.sample_count = SDL_GPU_SAMPLECOUNT_1;

        SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev, &tci);
        if (!tex)
        {
            logging::logError("[render2d] SDL_CreateGPUTexture(dynamic) failed: {}", SDL_GetError());
            return 0;
        }

        int handle = 0;
        for (int i = 0; i < 4096; i++)
        {
            if (impl->nextHandle < 1)
                impl->nextHandle = 1;
            const int cand = impl->nextHandle++;
            if (impl->textures.find(cand) == impl->textures.end())
            {
                handle = cand;
                break;
            }
        }
        if (handle == 0)
        {
            logging::logError("[render2d] no free dynamic texture handle");
            SDL_ReleaseGPUTexture(dev, tex);
            return 0;
        }

        impl->textures[handle] = { tex, w, h };
        return handle;
    }

    void SpriteRenderer::updateDynamicTexture(int handle, const uint8_t* rgba, int w, int h)
    {
        SDL_GPUDevice* dev = (SDL_GPUDevice*)system::gpu::device();
        auto it = impl->textures.find(handle);
        if (!dev || rgba == nullptr || it == impl->textures.end() || it->second.texture == nullptr)
            return;
        if (w != it->second.width || h != it->second.height)
            return; // size mismatch - the texture was created for another frame

        const Uint32 bytes = (Uint32)w * (Uint32)h * 4;
        SDL_GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size = bytes;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &tbci);
        if (!tb)
            return;
        void* mapped = SDL_MapGPUTransferBuffer(dev, tb, false);
        if (mapped)
        {
            std::memcpy(mapped, rgba, bytes);
            SDL_UnmapGPUTransferBuffer(dev, tb);
        }

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
        if (cmd)
        {
            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
            if (cp)
            {
                SDL_GPUTextureTransferInfo src{};
                src.transfer_buffer = tb;
                src.pixels_per_row = (Uint32)w;
                src.rows_per_layer = (Uint32)h;
                SDL_GPUTextureRegion dst{};
                dst.texture = it->second.texture;
                dst.w = (Uint32)w;
                dst.h = (Uint32)h;
                dst.d = 1;
                SDL_UploadToGPUTexture(cp, &src, &dst, false);
                SDL_EndGPUCopyPass(cp);
            }
            SDL_SubmitGPUCommandBuffer(cmd);
        }
        SDL_ReleaseGPUTransferBuffer(dev, tb); // deferred until GPU done
    }

    void SpriteRenderer::unloadTexture(int handle)
    {
        if (handle <= 0)
            return;
        SDL_GPUDevice* dev = (SDL_GPUDevice*)system::gpu::device();
        auto it = impl->textures.find(handle);
        if (it == impl->textures.end())
            return;
        if (dev && it->second.texture)
            SDL_ReleaseGPUTexture(dev, it->second.texture);
        impl->textures.erase(it);
        logging::logInfo("[render2d] unloaded texture handle {}", handle);
    }

    void SpriteRenderer::present(const SpriteDraw* sprites, size_t count)
    {
        SDL_GPUDevice* dev = (SDL_GPUDevice*)system::gpu::device();
        SDL_Window* win = (SDL_Window*)system::gpu::window();
        if (!dev || !win)
            return;
        if (!impl->ensurePipelines(dev))
            return;

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
        if (!cmd)
        {
            logging::logError("[render2d] present: SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
            return;
        }

        SDL_GPUTexture* swapTex = nullptr;
        Uint32 winW = 0;
        Uint32 winH = 0;
        if (!SDL_AcquireGPUSwapchainTexture(cmd, win, &swapTex, &winW, &winH) || !swapTex)
        {
            SDL_CancelGPUCommandBuffer(cmd);
            return;
        }
        if (winW == 0 || winH == 0)
        {
            SDL_CancelGPUCommandBuffer(cmd);
            return;
        }

        const size_t vertexCount = count * 4;
        if (vertexCount > 0 && !impl->ensureVertexBuffer(dev, vertexCount))
        {
            SDL_CancelGPUCommandBuffer(cmd);
            return;
        }

        SDL_GPUViewport vp{};
        vp.x = 0.0f;
        vp.y = 0.0f;
        vp.w = (float)winW;
        vp.h = (float)winH;
        vp.min_depth = 0.0f;
        vp.max_depth = 1.0f;

        SDL_GPUColorTargetInfo target{};
        target.texture = swapTex;
        target.mip_level = 0;
        target.layer_or_depth_plane = 0;
        target.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
        if (!pass)
        {
            logging::logError("[render2d] present: SDL_BeginGPURenderPass failed: {}", SDL_GetError());
            SDL_CancelGPUCommandBuffer(cmd);
            return;
        }
        SDL_SetGPUViewport(pass, &vp);

        const float viewportData[4] = { 0.0f, 0.0f, (float)winW, (float)winH };
        SDL_PushGPUVertexUniformData(cmd, 0, viewportData, sizeof(viewportData));

        for (size_t i = 0; i < count; i++)
        {
            const SpriteDraw& s = sprites[i];

            const Impl::TexEntry* entry = nullptr;
            SDL_GPUGraphicsPipeline* pipeline = impl->solidPipeline;
            if (s.textureHandle > 0)
            {
                auto it = impl->textures.find(s.textureHandle);
                if (it == impl->textures.end())
                    continue; // texture was released underneath us - skip
                entry = &it->second;
                pipeline = impl->texturedPipeline;
            }

            // Source rect: default to the full texture.
            float sw = s.src.width;
            float sh = s.src.height;
            float sx = s.src.x;
            float sy = s.src.y;
            float texW = entry ? (float)entry->width : 1.0f;
            float texH = entry ? (float)entry->height : 1.0f;
            if (entry)
            {
                if (sw <= 0.0f)
                    sw = texW;
                if (sh <= 0.0f)
                    sh = texH;
            }

            // UV range across the source rect.
            float u0 = texW > 0.0f ? sx / texW : 0.0f;
            float v0 = texH > 0.0f ? sy / texH : 0.0f;
            float uSpan = 0.0f;
            float vSpan = 0.0f;
            if (entry)
            {
                const float srcUW = texW > 0.0f ? sw / texW : 0.0f;
                const float srcVH = texH > 0.0f ? sh / texH : 0.0f;
                if (s.flags & kRepeatX)
                    uSpan = s.dst.width / texW; // tile the texture across the dst
                else
                    uSpan = srcUW; // stretch: map the src region across the dst
                if (s.flags & kRepeatY)
                    vSpan = s.dst.height / texH;
                else
                    vSpan = srcVH;
            }
            const float u1 = u0 + uSpan;
            const float v1 = v0 + vSpan;

            const float x0 = s.dst.x;
            const float y0 = s.dst.y;
            const float x1 = s.dst.x + s.dst.width;
            const float y1 = s.dst.y + s.dst.height;

            Vertex verts[4]{};
            verts[0].sx = x0;
            verts[0].sy = y0;
            verts[0].tu = u0;
            verts[0].tv = v0;
            verts[1].sx = x1;
            verts[1].sy = y0;
            verts[1].tu = u1;
            verts[1].tv = v0;
            verts[2].sx = x0;
            verts[2].sy = y1;
            verts[2].tu = u0;
            verts[2].tv = v1;
            verts[3].sx = x1;
            verts[3].sy = y1;
            verts[3].tu = u1;
            verts[3].tv = v1;
            for (auto& vert : verts)
            {
                vert.sz = 0.5f;
                vert.rhw = 1.0f;
                const uint32_t packed = (uint32_t)s.b | ((uint32_t)s.g << 8) | ((uint32_t)s.r << 16) | ((uint32_t)s.a << 24);
                setColor(vert, packed);
            }

            void* mapped = SDL_MapGPUTransferBuffer(dev, impl->vertexTransfer, true);
            if (!mapped)
            {
                logging::logError("[render2d] SDL_MapGPUTransferBuffer(vertices) failed: {}", SDL_GetError());
                continue;
            }
            std::memcpy(mapped, verts, sizeof(verts));
            SDL_UnmapGPUTransferBuffer(dev, impl->vertexTransfer);

            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
            if (cp)
            {
                SDL_GPUTransferBufferLocation srcLoc{};
                srcLoc.transfer_buffer = impl->vertexTransfer;
                SDL_GPUBufferRegion dstLoc{};
                dstLoc.buffer = impl->vertexBuffer;
                dstLoc.size = sizeof(verts);
                SDL_UploadToGPUBuffer(cp, &srcLoc, &dstLoc, true);
                SDL_EndGPUCopyPass(cp);
            }

            SDL_BindGPUGraphicsPipeline(pass, pipeline);

            if (entry)
            {
                SDL_GPUTextureSamplerBinding sb{};
                sb.texture = entry->texture;
                sb.sampler = impl->sampler;
                SDL_BindGPUFragmentSamplers(pass, 0, &sb, 1);
            }

            SDL_GPUBufferBinding vbBind{};
            vbBind.buffer = impl->vertexBuffer;
            vbBind.offset = 0;
            SDL_BindGPUVertexBuffers(pass, 0, &vbBind, 1);

            SDL_DrawGPUPrimitives(pass, 4, 1, 0, 0);
        }

        SDL_EndGPURenderPass(pass);
        SDL_SubmitGPUCommandBuffer(cmd);
    }

    int SpriteRenderer::screenWidth() const
    {
        SDL_Window* win = (SDL_Window*)system::gpu::window();
        if (!win)
            return 0;
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(win, &w, &h);
        return w;
    }

    int SpriteRenderer::screenHeight() const
    {
        SDL_Window* win = (SDL_Window*)system::gpu::window();
        if (!win)
            return 0;
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(win, &w, &h);
        return h;
    }

    void SpriteRenderer::setFade(const FadeState& state)
    {
        impl->fade = state;
    }

    FadeState SpriteRenderer::takeFade()
    {
        FadeState state = impl->fade;
        impl->fade = FadeState{};
        return state;
    }

    void SpriteRenderer::shutdown()
    {
        SDL_GPUDevice* dev = (SDL_GPUDevice*)system::gpu::device();
        if (!dev)
            return;

        SDL_WaitForGPUIdle(dev);

        for (auto& [handle, entry] : impl->textures)
        {
            (void)handle;
            if (entry.texture)
                SDL_ReleaseGPUTexture(dev, entry.texture);
        }
        impl->textures.clear();

        if (impl->vertexBuffer)
            SDL_ReleaseGPUBuffer(dev, impl->vertexBuffer);
        impl->vertexBuffer = nullptr;
        if (impl->vertexTransfer)
            SDL_ReleaseGPUTransferBuffer(dev, impl->vertexTransfer);
        impl->vertexTransfer = nullptr;

        if (impl->texturedPipeline)
            SDL_ReleaseGPUGraphicsPipeline(dev, impl->texturedPipeline);
        impl->texturedPipeline = nullptr;
        if (impl->solidPipeline)
            SDL_ReleaseGPUGraphicsPipeline(dev, impl->solidPipeline);
        impl->solidPipeline = nullptr;

        if (impl->vertexShader)
            SDL_ReleaseGPUShader(dev, impl->vertexShader);
        impl->vertexShader = nullptr;
        if (impl->texturedFrag)
            SDL_ReleaseGPUShader(dev, impl->texturedFrag);
        impl->texturedFrag = nullptr;
        if (impl->solidFrag)
            SDL_ReleaseGPUShader(dev, impl->solidFrag);
        impl->solidFrag = nullptr;
        if (impl->sampler)
            SDL_ReleaseGPUSampler(dev, impl->sampler);
        impl->sampler = nullptr;
    }
}