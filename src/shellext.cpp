#include "font.h"
#include "gfx.h"
#include "resmgr.h"
#include "shell.h"

using namespace openre::graphics;

namespace openre::shellextensions
{
    OpenREPrim createQuad(float x, float y, float z, float w, float h, float s0, float t0, float s1, float t1)
    {
        OpenREPrim prim{};
        prim.kind = OpenREPrimKind::TextureQuad;
        prim.color = { 1, 1, 1, 1 };
        prim.vertices[0].x = x;
        prim.vertices[0].y = y;
        prim.vertices[0].z = z;
        prim.vertices[0].s = s0;
        prim.vertices[0].t = t0;
        prim.vertices[1].x = x;
        prim.vertices[1].y = y + h;
        prim.vertices[1].z = z;
        prim.vertices[1].s = s0;
        prim.vertices[1].t = t1;
        prim.vertices[2].x = x + w;
        prim.vertices[2].y = y + h;
        prim.vertices[2].z = z;
        prim.vertices[2].s = s1;
        prim.vertices[2].t = t1;
        prim.vertices[3].x = x + w;
        prim.vertices[3].y = y;
        prim.vertices[3].z = z;
        prim.vertices[3].s = s1;
        prim.vertices[3].t = t0;
        return prim;
    }

    LoadFileResult loadFile(OpenREShell& shell, std::string_view path, std::vector<std::string_view> extensions)
    {
        auto streamResult = shell.getStream(path, extensions);
        if (!streamResult.found)
            return {};

        auto& stream = streamResult.stream;
        stream->seek(0, SEEK_END);
        auto length = static_cast<size_t>(stream->tell());
        stream->seek(0, SEEK_SET);
        std::vector<uint8_t> buffer(length);
        stream->read(buffer.data(), length);
        return { true, streamResult.extensionIndex, buffer };
    }

    TextureBuffer loadTextureBuffer(OpenREShell& shell, std::string_view path, uint32_t width, uint32_t height)
    {
        auto result = loadFile(shell, path, { ".adt", ".bmp" });
        TextureBuffer textureBuffer;
        if (result.extensionIndex == 0)
        {
            return adt2TextureBuffer(std::move(result.buffer), width, height);
        }
        else
        {
            return bmp2TextureBuffer(std::move(result.buffer));
        }
    }

    void drawTexture(
        OpenREShell& shell, TextureHandle texture, float x, float y, float z, float w, float h, float s0, float t0, float s1,
        float t1)
    {
        auto prim = createQuad(x, y, z, w, h, s0, t0, s1, t1);
        prim.texture = texture;
        shell.pushPrimitive(prim);
    }

    void drawTexture(OpenREShell& shell, TextureHandle texture, float x, float y, float z, float w, float h)
    {
        drawTexture(shell, texture, x, y, z, w, h, 0, 0, 1, 1);
    }

    void drawMovie(OpenREShell& shell, ResourceCookie movie, float x, float y, float z, float w, float h)
    {
        auto prim = createQuad(x, y, z, w, h);
        prim.kind = OpenREPrimKind::MovieQuad;
        prim.movie = movie;
        shell.pushPrimitive(prim);
    }

    void fade(OpenREShell& shell, float r, float g, float b, float a)
    {
        auto size = shell.getRenderSize();
        auto prim = createQuad(0, 0, 1, (float)size.width, (float)size.height);
        prim.color = { r, g, b, a };
        shell.pushPrimitive(prim);
    }
}
