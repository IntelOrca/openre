#include "font.h"
#include "shell.h"

#include <algorithm>
#include <optional>
#include <stack>
#include <string>
#include <tuple>

using namespace openre::shellextensions;

namespace openre::graphics
{
    FontData FontData::fromBuffer(DataBlock input)
    {
        FontData font;
        auto src = static_cast<const uint32_t*>(input.data);
        src++; // magic
        src++; // version
        font.width = *src++;
        font.height = *src++;
        src++; // reserved
        src++; // reserved
        src++; // reserved
        auto numChars = *src++;
        for (size_t i = 0; i < numChars; i++)
        {
            auto& c = font.chars.emplace_back();
            c.codepoint = *src++;
            c.left = *src++;
            c.top = *src++;
            c.right = *src++;
            c.bottom = *src++;
            c.advanceX = *src++;
            src++; // reserved
            src++; // reserved

            c.s0 = c.left / (float)font.width;
            c.t0 = c.top / (float)font.height;
            c.s1 = c.right / (float)font.width;
            c.t1 = c.bottom / (float)font.height;
        }
        return font;
    }

    class FontResource : public Resource
    {
    public:
        ResourceCookie textureCookie{};
        FontData fontData;

        const char* getName() const override
        {
            return "font";
        }
    };

    static FontData loadFontData(OpenREShell& shell, std::string_view path);

    ResourceCookie loadBuiltInFont(OpenREShell& shell)
    {
        auto builtInFont = getBuiltInFont();
        auto fontData = FontData::fromBuffer(builtInFont.data);
        auto fontTexture = webp2TextureBuffer(builtInFont.texture);

        auto& resourceManager = shell.getResourceManager();
        auto fontCookie = resourceManager.addFirstRef<FontResource>("", std::make_unique<FontResource>());

        auto fontResource = resourceManager.fromCookie<FontResource>(fontCookie);
        fontResource->textureCookie = shell.loadTexture(fontTexture);
        fontResource->fontData = fontData;
        return fontCookie;
    }

    ResourceCookie loadFont(OpenREShell& shell, std::string_view path)
    {
        auto& resourceManager = shell.getResourceManager();
        auto fontCookie = resourceManager.addRef<FontResource>(path);
        if (fontCookie)
            return fontCookie;

        fontCookie = resourceManager.addFirstRef(path, std::make_unique<FontResource>());

        auto fontResource = resourceManager.fromCookie<FontResource>(fontCookie);
        fontResource->textureCookie = shell.loadTexture(path, 256, 256);
        fontResource->fontData = loadFontData(shell, path);
        return fontCookie;
    }

    static FontData loadFontData(OpenREShell& shell, std::string_view path)
    {
        auto loadResult = loadFile(shell, path, { ".dat" });
        if (!loadResult.success)
            return {};

        return FontData::fromBuffer(loadResult.buffer);
    }

    class TextParser
    {
    private:
        std::string_view text;
        size_t index{};
        std::stack<TextFormatting> formatStack;
        std::string_view nextSpan;
        bool error{};

    public:
        TextParser(std::string_view text)
            : text(text)
        {
            formatStack.emplace();
        }

        bool parse()
        {
            while (true)
            {
                if (!parseTags())
                {
                    if (error)
                        break;

                    nextSpan = parseTagless();
                    return !nextSpan.empty();
                }
            }
            return false;
        }

        std::string_view getNextSpan() const
        {
            return this->nextSpan;
        }

        const TextFormatting& getFormatting() const
        {
            return this->formatStack.top();
        }

    private:
        bool parseTags()
        {
            auto tagOpen = parseTagOpenBegin();
            if (tagOpen.empty())
            {
                auto tagClose = parseTagClose();
                if (tagClose.empty())
                {
                    return false;
                }
                else
                {
                    this->formatStack.pop();
                    return true;
                }
            }

            this->formatStack.push(this->formatStack.top());
            auto& fmt = this->formatStack.top();
            while (true)
            {
                auto attr = parseAttribute();
                if (attr)
                {
                    auto [key, value] = *attr;
                    if (key == "color")
                    {
                        fmt.color = parseColor(value);
                    }
                    else if (key == "scale")
                    {
                        fmt.scale = std::stof(std::string(value));
                    }
                    else if (key == "halign")
                    {
                        fmt.halign = parseAlignment(value);
                    }
                    else if (key == "valign")
                    {
                        fmt.valign = parseAlignment(value);
                    }
                }
                else
                {
                    break;
                }
            }

            if (!parseTagOpenEnd())
            {
                error = true;
                return false;
            }

            return true;
        }

        std::string_view parseTagOpenBegin()
        {
            if (this->peekChar() == '<' && this->peekChar2() != '/')
            {
                this->readChar();
                auto start = this->index;
                while (true)
                {
                    auto ch = this->peekChar();
                    if (ch == '\0' || ch == ' ' || ch == '>')
                        break;
                    this->readChar();
                }
                return this->getView(start);
            }
            return {};
        }

        bool parseTagOpenEnd()
        {
            this->skipWhitespace();
            if (this->peekChar() == '>')
            {
                this->readChar();
                return true;
            }
            return false;
        }

        std::string_view parseTagless()
        {
            auto start = this->index;
            while (true)
            {
                auto ch = this->peekChar();
                if (ch == '\0' || ch == '<')
                    break;
                this->readChar();
            }
            return getView(start);
        }

        std::optional<std::pair<std::string_view, std::string_view>> parseAttribute()
        {
            skipWhitespace();

            if (this->peekChar() == '>')
                return std::nullopt;

            auto start = this->index;
            while (true)
            {
                auto ch = this->peekChar();
                if (ch == '\0' || ch == '=')
                    break;
                this->readChar();
            }
            auto key = getView(start);
            if (this->readChar() != '=')
            {
                error = true;
                return std::nullopt;
            }
            if (this->readChar() != '\"')
            {
                error = true;
                return std::nullopt;
            }

            start = this->index;
            while (true)
            {
                auto ch = this->peekChar();
                if (ch == '\0' || ch == '\"')
                    break;
                this->readChar();
            }
            auto value = getView(start);

            if (this->readChar() != '\"')
            {
                error = true;
                return std::nullopt;
            }
            return std::make_pair(key, value);
        }

        std::string_view parseTagClose()
        {
            if (this->peekChar() == '<' && this->peekChar2() == '/')
            {
                this->readChar();
                this->readChar();
                auto start = this->index;
                while (true)
                {
                    auto ch = this->peekChar();
                    if (ch == '\0' || ch == '>')
                        break;
                    this->readChar();
                }
                auto result = getView(start);
                if (this->peekChar() == '>')
                    this->readChar();
                else
                    error = true;
                return result;
            }
            return {};
        }

        uint32_t peekChar()
        {
            if (this->index >= this->text.size())
                return '\0';
            return this->text[this->index];
        }

        uint32_t peekChar2()
        {
            if (this->index + 1 >= this->text.size())
                return '\0';
            return this->text[this->index + 1];
        }

        uint32_t readChar()
        {
            if (this->index >= this->text.size())
                return '\0';
            return this->text[this->index++];
        }

        std::string_view getView(size_t start)
        {
            return this->text.substr(start, index - start);
        }

        void skipWhitespace()
        {
            while (this->peekChar() == ' ')
            {
                this->readChar();
            }
        }

        static Color4f parseColor(std::string_view str)
        {
            if ((str.substr(0, 5) != "rgba(" && str.substr(0, 4) != "rgb(") || str.back() != ')')
            {
                return {};
            }

            size_t startIndex = (str.substr(0, 5) == "rgba(") ? 5 : 4;
            str.remove_prefix(startIndex);
            str.remove_suffix(1);

            float values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            size_t index = 0;
            size_t start = 0;

            while (index < 4)
            {
                size_t end = str.find(',', start);
                if (end == std::string_view::npos)
                {
                    end = str.length();
                }

                try
                {
                    values[index++] = std::stof(std::string(str.substr(start, end - start)));
                }
                catch (...)
                {
                    return {};
                }

                start = end + 1;
                if (start >= str.length())
                {
                    break;
                }
            }

            if (index < 3)
            {
                return {};
            }
            return { values[0] / 255.0f, values[1] / 255.0f, values[2] / 255.0f, values[3] };
        }

        static uint8_t parseAlignment(std::string_view str)
        {
            if (str == "left" || str == "top")
                return HALIGN_LEFT;
            if (str == "center")
                return HALIGN_CENTER;
            if (str == "right" || str == "bottom")
                return HALIGN_RIGHT;
            return HALIGN_LEFT;
        }
    };

    void drawText(OpenREShell& shell, ResourceCookie font, std::string_view text, float x, float y, float z, float w, float h)
    {
        auto& resourceManager = shell.getResourceManager();
        auto fontResource = resourceManager.fromCookie<FontResource>(font);
        if (fontResource == nullptr)
            return;

        auto& fontData = fontResource->fontData;
        auto defaultCharWidth = fontData.getDefaultCharWidth();
        auto lineHeight = fontData.getLineHeight();

        auto halign = HALIGN_LEFT;
        auto valign = VALIGN_TOP;
        auto blkLeft = 0.0f;
        auto blkTop = 0.0f;
        auto blkRight = 0.0f;
        auto blkBottom = 0.0f;
        auto chLeft = 0.0f;
        auto chTop = 0.0f;

        std::vector<OpenREPrim> primitives;
        TextParser parser(text);
        while (parser.parse())
        {
            auto& fmt = parser.getFormatting();
            halign = fmt.halign;
            valign = fmt.valign;

            auto s = std::string(parser.getNextSpan());
            for (auto& ch : s)
            {
                auto advanceX = defaultCharWidth * fmt.scale;
                auto chWidth = defaultCharWidth * fmt.scale;
                auto chHeight = lineHeight * fmt.scale;
                if (ch == '\n')
                {
                    chLeft = 0;
                    chTop += chHeight;
                    continue;
                }
                for (auto& c : fontData.chars)
                {
                    if (c.codepoint == ch)
                    {
                        advanceX = c.advanceX * fmt.scale;
                        chWidth = c.getWidth() * fmt.scale;
                        chHeight = c.getHeight() * fmt.scale;
                        auto chRight = chLeft + chWidth;
                        auto chBottom = chTop + chHeight;
                        blkRight = std::max(blkRight, chRight);
                        blkBottom = std::max(blkBottom, chBottom);
                        auto prim = createQuad(chLeft, chTop, z, chWidth, chHeight, c.s0, c.t0, c.s1, c.t1);
                        prim.texture = fontResource->textureCookie;
                        prim.color = fmt.color;
                        primitives.push_back(prim);
                        break;
                    }
                }
                chLeft += advanceX;
            }
        }

        if (halign == HALIGN_LEFT)
        {
            blkLeft = x;
            blkRight += blkLeft;
        }
        else if (halign == HALIGN_CENTER)
        {
            blkLeft = ((x + w) - blkRight) / 2;
            blkRight += blkLeft;
        }
        else if (halign == HALIGN_RIGHT)
        {
            blkLeft = x + w - blkRight;
            blkRight += blkLeft;
        }
        if (valign == VALIGN_TOP)
        {
            blkTop = y;
            blkBottom += blkTop;
        }
        else if (valign == VALIGN_CENTER)
        {
            blkTop = ((y + h) - blkBottom) / 2;
            blkBottom += blkTop;
        }
        else if (valign == VALIGN_BOTTOM)
        {
            blkTop = y + h - blkBottom;
            blkBottom += blkTop;
        }

        for (auto& p : primitives)
        {
            for (auto& v : p.vertices)
            {
                v.x += blkLeft;
                v.y += blkTop;
            }
            shell.pushPrimitive(p);
        }
    }

    void drawTextLine(
        OpenREShell& shell, ResourceCookie font, std::string_view text, float x, float y, float z, TextFormatting& formatting)
    {
        // TODO make this far more efficient
        char buffer[256];
        std::sprintf(
            buffer,
            "<span color=\"rgba(%d, %d, %d, %f)\" scale=\"%f\">",
            static_cast<uint8_t>(std::clamp<float>(formatting.color.r * 255.0f, 0, 255)),
            static_cast<uint8_t>(std::clamp<float>(formatting.color.g * 255.0f, 0, 255)),
            static_cast<uint8_t>(std::clamp<float>(formatting.color.b * 255.0f, 0, 255)),
            formatting.color.a,
            formatting.scale);

        std::string fullText = buffer;
        fullText.append(text);
        fullText.append("</span>");
        drawText(shell, font, fullText, x, y, z, 10000, 10000);
    }
}
