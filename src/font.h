#pragma once

#include "shell.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace openre::graphics
{
    struct FontChar
    {
        uint32_t codepoint{};
        uint32_t left{};
        uint32_t top{};
        uint32_t right{};
        uint32_t bottom{};

        inline uint32_t getWidth() const
        {
            return this->right - this->left;
        }

        inline uint32_t getHeight() const
        {
            return this->bottom - this->top;
        }
    };

    struct FontData
    {
        uint32_t width{};
        uint32_t height{};
        std::vector<FontChar> chars;

        inline uint32_t getDefaultCharWidth() const
        {
            return this->chars[0].getWidth();
        }

        inline uint32_t getLineHeight() const
        {
            return this->chars[0].getHeight();
        }
    };

    FontData loadFontData(std::vector<uint8_t> buffer);
    void drawText(OpenREShell& shell, FontHandle font, std::string_view text, float x, float y, float z, float w, float h);
}
