#include "font.h"
#include "shell.h"

#include <optional>
#include <stack>
#include <string>
#include <tuple>

using namespace openre::shellextensions;

namespace openre::graphics
{
    class FontResource : public Resource
    {
    public:
        TextureHandle textureHandle;
        FontData fontData;

        FontResource(TextureHandle textureHandle, FontData fontData)
            : textureHandle(textureHandle)
            , fontData(fontData)
        {
        }

        const char* getName() const override
        {
            return "font";
        }
    };

    static FontData loadFontData(OpenREShell& shell, std::string_view path);

    ResourceCookie loadFont(OpenREShell& shell, std::string_view path)
    {
        auto& resourceManager = shell.getResourceManager();
        auto result = resourceManager.addRef(path);
        if (result)
            return result;

        auto textureHandle = loadTexture(shell, path, 256, 256);
        auto fontData = loadFontData(shell, path);
        return resourceManager.addFirstRef(path, std::make_unique<FontResource>(textureHandle, fontData));
    }

    static FontData loadFontData(OpenREShell& shell, std::string_view path)
    {
        auto loadResult = loadFile(shell, path, { ".dat" });
        if (!loadResult.success)
            return {};

        FontData font;
        auto src = reinterpret_cast<uint32_t*>(loadResult.buffer.data());
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
            src++; // reserved
            src++; // reserved
            src++; // reserved
        }
        return font;
    }

    struct TextFormatting
    {
        Color4f color = { 1, 1, 1, 1 };
        float scale = 1;
    };

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

        auto chLeft = x;
        auto chTop = y;

        TextParser parser(text);
        while (parser.parse())
        {
            auto& fmt = parser.getFormatting();
            auto s = std::string(parser.getNextSpan());
            for (auto& ch : s)
            {
                auto chWidth = defaultCharWidth * fmt.scale;
                auto chHeight = lineHeight * fmt.scale;
                if (ch == '\n')
                {
                    chLeft = x;
                    chTop += chHeight;
                    continue;
                }
                for (auto& c : fontData.chars)
                {
                    if (c.codepoint == ch)
                    {
                        auto s0 = c.left / (float)fontData.width;
                        auto t0 = c.top / (float)fontData.height;
                        auto s1 = c.right / (float)fontData.width;
                        auto t1 = c.bottom / (float)fontData.height;
                        chWidth = (c.right - c.left) * fmt.scale;
                        chHeight = (c.bottom - c.top) * fmt.scale;
                        auto prim = createQuad(chLeft, chTop, z, chWidth, chHeight, s0, t0, s1, t1);
                        prim.texture = fontResource->textureHandle;
                        prim.color = fmt.color;
                        shell.pushPrimitive(prim);
                        break;
                    }
                }
                chLeft += chWidth;
            }
        }
    }
}
