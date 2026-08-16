#pragma once

#include "re2.h"

#include <string>

namespace openre::str
{
    // 0x0050BBB0
    OldStdString* string_ctor_from_cstr(OldStdString* self, const char* s);

    // 0x0050BBF0
    void string_dtor(OldStdString* self);

    // 0x0050BC60
    // Assigns a C string to the OldStdString, freeing any existing buffer.
    OldStdString* string_assign_cstr(OldStdString* self, const char* s);

    // 0x0050C3F0
    const char* string_get_data(const OldStdString* self);

    // 0x0050C400
    // Assigns the contents of another OldStdString to this one.
    OldStdString* string_assign(OldStdString* self, const OldStdString* other);
    // Convenience overload of 0x0050C400 for std::string.
    OldStdString* string_assign(OldStdString* self, const std::string& s);

    // 0x0050BBD0
    // Reinitializes the destination before copying; safe on unconstructed strings.
    OldStdString* string_op_assign(OldStdString* self, const OldStdString* other);

    // 0x0050C420
    // Thin wrapper over 0x0050BC60 that returns `self`.
    OldStdString* string_copy(OldStdString* self, const char* s);

    // 0x0050C4E0
    // Appends the given C string to the OldStdString and returns the object.
    OldStdString* string_append(OldStdString* self, const char* s);

    // 0x0050BD10
    // Returns the length (in characters) of the string, counting Shift-JIS
    // double-byte characters as a single character.
    int string_sjis_len(OldStdString* self);

    // 0x0050BD50
    // Searches the Shift-JIS string for the last occurrence of the needle,
    // returning its character index (double-byte characters count as one) or -1.
    int string_find_last(OldStdString* self, const char* needle);

    // 0x0050BE30
    // Copies a Shift-JIS substring: skips `skipChars` characters from `self`,
    // then copies up to `maxChars` characters into `out`.
    OldStdString* string_sjis_copy(OldStdString* self, OldStdString* out, int skipChars, int maxChars);

    // 0x0050BF30
    // Stores the first `count` Shift-JIS characters of `self` into `out`.
    OldStdString* string_slice(OldStdString* self, OldStdString* out, int count);

    // 0x0050BFF0
    // Stores the last `count` Shift-JIS characters of `self` into `out`.
    OldStdString* string_right(OldStdString* self, OldStdString* out, int count);

    // 0x0050C550
    // Returns true if the two strings are equal.
    bool string_eq(OldStdString* self, const OldStdString* other);

    // Convenience overload of 0x0050C550 for std::string.
    bool string_eq(OldStdString* self, const std::string& s);

    // 0x0050C5C0
    // Returns true if the string equals the given C string.
    bool string_eq_cstr(OldStdString* self, const char* s);

    // 0x0050C630
    // Returns true if the string differs from the given C string.
    bool string_ne_cstr(OldStdString* self, const char* s);

    // 0x00509AF0
    // Returns the length (in characters) of the given C string, counting
    // Shift-JIS double-byte characters as a single character.
    int string_sjis_len_cstr(const char* s);

    // Converts a Shift-JIS (CP932) encoded string to UTF-8. Returns the input
    // unchanged if the conversion fails.
    std::string sjis_to_utf8(const std::string& s);

    // Converts a UTF-8 string to Shift-JIS (CP932). Returns the input
    // unchanged if the conversion fails.
    std::string utf8_to_sjis(const std::string& s);
}
