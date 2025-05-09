#include "gfx.h"
#include "shell.h"

using namespace openre::graphics;

namespace openre::shellextensions
{
    static void pushQuad(
        OpenREShell& shell, TextureHandle texture, Color4f color, float x, float y, float z, float w, float h, float s0 = 0,
        float t0 = 0, float s1 = 1, float t1 = 1)
    {
        OpenREPrim prim;
        prim.kind = OpenREPrimKind::TextureQuad;
        prim.texture = texture;
        prim.color = color;
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
        shell.pushPrimitive(prim);
    }

    TextureHandle loadTexture(OpenREShell& shell, std::string_view path, uint32_t width, uint32_t height)
    {
        std::vector<std::string_view> extensions = { ".adt", ".bmp" };
        auto streamResult = shell.getStream(path, extensions);
        if (!streamResult.found)
            return {};

        auto& stream = streamResult.stream;
        stream->seek(0, SEEK_END);
        auto length = static_cast<size_t>(stream->tell());
        stream->seek(0, SEEK_SET);
        std::vector<uint8_t> buffer(length);
        stream->read(buffer.data(), length);

        TextureBuffer textureBuffer;
        if (streamResult.extensionIndex == 0)
        {
            textureBuffer = adt2TextureBuffer(std::move(buffer), width, height);
        }
        else
        {
            textureBuffer = bmp2TextureBuffer(std::move(buffer));
        }

        return shell.loadTexture(textureBuffer);
    }

    void drawTexture(
        OpenREShell& shell, TextureHandle texture, float x, float y, float z, float w, float h, float s0, float t0, float s1,
        float t1)
    {
        pushQuad(shell, texture, { 1, 1, 1, 1 }, x, y, z, w, h, s0, t0, s1, t1);
    }

    void drawTexture(OpenREShell& shell, TextureHandle texture, float x, float y, float z, float w, float h)
    {
        drawTexture(shell, texture, x, y, z, w, h, 0, 0, 1, 1);
    }

    void fade(OpenREShell& shell, float r, float g, float b, float a)
    {
        auto size = shell.getRenderSize();
        pushQuad(shell, 0, { r, g, b, a }, 0, 0, 1, (float)size.width, (float)size.height);
    }
}
