#pragma once

#include "data.hpp"
#include "resmgr.h"
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
        float s0{};
        float s1{};
        float t0{};
        float t1{};
        int32_t advanceX{};

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

        static FontData fromBuffer(DataBlock input);
    };

    struct BuiltInFont
    {
        DataBlock data;
        DataBlock texture;
    };

    constexpr uint8_t HALIGN_LEFT = 0;
    constexpr uint8_t HALIGN_CENTER = 1;
    constexpr uint8_t HALIGN_RIGHT = 2;
    constexpr uint8_t VALIGN_TOP = 0;
    constexpr uint8_t VALIGN_CENTER = 1;
    constexpr uint8_t VALIGN_BOTTOM = 2;

    struct TextFormatting
    {
        Color4f color = { 1, 1, 1, 1 };
        float scale = 1;
        uint8_t halign = HALIGN_LEFT;
        uint8_t valign = VALIGN_TOP;
    };

    ResourceCookie loadBuiltInFont(OpenREShell& shell);
    ResourceCookie loadFont(OpenREShell& shell, std::string_view path);
    void drawText(OpenREShell& shell, ResourceCookie font, std::string_view text, float x, float y, float z, float w, float h);
    void drawTextLine(
        OpenREShell& shell, ResourceCookie font, std::string_view text, float x, float y, float z, TextFormatting& formatting);

    BuiltInFont getBuiltInFont();
}
