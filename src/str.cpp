#include "str.h"

#include "interop.hpp"
#include "re2.h"

#include <windows.h>

#include <cstring>
#include <string>

namespace openre::str
{
    // 0x0050BBB0
    OldStdString* string_ctor_from_cstr(OldStdString* self, const char* s)
    {
        return interop::thiscall<OldStdString*, void*, const char*>(0x50BBB0, self, s);
    }

    // 0x0050BBF0
    void string_dtor(OldStdString* self)
    {
        interop::thiscall<void, void*>(0x50BBF0, self);
    }

    // 0x0050BC60
    OldStdString* string_assign_cstr(OldStdString* self, const char* s)
    {
        return interop::thiscall<OldStdString*, void*, const char*>(0x50BC60, self, s);
    }

    // 0x0050C3F0
    const char* string_get_data(const OldStdString* self)
    {
        return self->data;
    }

    // 0x0050C400
    OldStdString* string_assign(OldStdString* self, const OldStdString* other)
    {
        return interop::thiscall<OldStdString*, void*, const OldStdString*>(0x50C400, self, other);
    }

    // 0x0050BBD0
    // Reinitializes the destination (zeroes it) before copying, so it is safe
    // to use on a string that has not been constructed yet.
    OldStdString* string_op_assign(OldStdString* self, const OldStdString* other)
    {
        return interop::thiscall<OldStdString*, void*, const OldStdString*>(0x50BBD0, self, other);
    }

    OldStdString* string_assign(OldStdString* self, const std::string& s)
    {
        string_assign_cstr(self, s.c_str());
        return self;
    }

    // 0x0050C420
    OldStdString* string_copy(OldStdString* self, const char* s)
    {
        string_assign_cstr(self, s);
        return self;
    }

    // 0x0050C4E0
    OldStdString* string_append(OldStdString* self, const char* s)
    {
        return interop::thiscall<OldStdString*, OldStdString*, const char*>(0x50C4E0, self, s);
    }

    // 0x0050BD10
    int string_sjis_len(OldStdString* self)
    {
        return interop::thiscall<int, OldStdString*>(0x50BD10, self);
    }

    // 0x0050BD50
    int string_find_last(OldStdString* self, const char* needle)
    {
        return interop::thiscall<int, OldStdString*, const char*>(0x50BD50, self, needle);
    }

    // 0x0050BE30
    OldStdString* string_sjis_copy(OldStdString* self, OldStdString* out, int skipChars, int maxChars)
    {
        return interop::thiscall<OldStdString*, void*, void*, int, int>(0x50BE30, self, out, skipChars, maxChars);
    }

    // 0x0050BF30
    OldStdString* string_slice(OldStdString* self, OldStdString* out, int count)
    {
        return interop::thiscall<OldStdString*, void*, OldStdString*, int>(0x50BF30, self, out, count);
    }

    // 0x0050BFF0
    OldStdString* string_right(OldStdString* self, OldStdString* out, int count)
    {
        return interop::thiscall<OldStdString*, void*, OldStdString*, int>(0x50BFF0, self, out, count);
    }

    // 0x0050C550
    bool string_eq(OldStdString* self, const OldStdString* other)
    {
        return interop::thiscall<bool, void*, const OldStdString*>(0x50C550, self, other) != 0;
    }

    // 0x0050C550
    bool string_eq(OldStdString* self, const std::string& s)
    {
        if (self->length - 1 != s.size())
            return false;
        return std::memcmp(self->data, s.c_str(), s.size()) == 0;
    }

    // 0x0050C5C0
    bool string_eq_cstr(OldStdString* self, const char* s)
    {
        return interop::thiscall<bool, void*, const char*>(0x50C5C0, self, s) != 0;
    }

    // 0x0050C630
    bool string_ne_cstr(OldStdString* self, const char* s)
    {
        return interop::thiscall<bool, void*, const char*>(0x50C630, self, s) != 0;
    }

    // 0x00509AF0
    int string_sjis_len_cstr(const char* s)
    {
        OldStdString temp;
        string_ctor_from_cstr(&temp, s);
        int len = string_sjis_len(&temp);
        string_dtor(&temp);
        return len;
    }

    std::string sjis_to_utf8(const std::string& s)
    {
        if (s.empty())
            return {};

        auto wideLength = MultiByteToWideChar(932, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (wideLength <= 0)
            return s;

        std::wstring wide(wideLength, L'\0');
        MultiByteToWideChar(932, 0, s.c_str(), (int)s.size(), wide.data(), wideLength);

        auto utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLength, nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0)
            return s;

        std::string utf8(utf8Length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), wideLength, utf8.data(), utf8Length, nullptr, nullptr);
        return utf8;
    }

    std::string utf8_to_sjis(const std::string& s)
    {
        if (s.empty())
            return {};

        auto wideLength = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (wideLength <= 0)
            return s;

        std::wstring wide(wideLength, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), wide.data(), wideLength);

        auto sjisLength = WideCharToMultiByte(932, 0, wide.c_str(), wideLength, nullptr, 0, nullptr, nullptr);
        if (sjisLength <= 0)
            return s;

        std::string sjis(sjisLength, '\0');
        WideCharToMultiByte(932, 0, wide.c_str(), wideLength, sjis.data(), sjisLength, nullptr, nullptr);
        return sjis;
    }
}
