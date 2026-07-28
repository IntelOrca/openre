---
name: decompile
description: 'Decompile / implement an RE2 binary function at a given address into hand-written C++ code, replacing its interop::call wrapper'
argument-hint: 'ADDRESS (e.g., 0x00431000)'
---

# Decompile

Decompile a function from the original RE2 binary by analyzing it with IDA Pro, then writing equivalent C++ in the style of the OpenRE project.

## Description

This skill takes one or more original RE2 function addresses (e.g., `0x00431000`) and replaces the corresponding `interop::call` stubs with hand-written C++ code that behaves identically.

## When to Use

Use this skill when:
- You have identified an `interop::call(0x00XXXXXX)` wrapper that needs to be implemented as actual C++
- You have analyzed a function in IDA Pro and want to write a correct decompilation
- You need to map global variables or string references from the binary

Do NOT use this skill when:
- The function is trivial enough to write without IDA analysis

## Analysis Phase

Start by analyzing the function in IDA Pro:

1. **Decompile the function**: Use `ida-pro-mcp-decompile` with the function address to get pseudocode
2. **Identify the signature**: Determine the calling convention (`__cdecl` is default for x86 in this project), return type, and parameter list
3. **Trace globals**: Check every memory access to a fixed address:
   - Look for addresses in the `0x66XXXX` range (global data segment)
   - Map them to `gGameTable.field` if they appear in the GameTable struct (re2.h)
   - If not in GameTable, note them as standalone globals
4. **Find string references**: Use `ida-pro-mcp-find` with type `"data_ref"` to find strings the function references. Strings at `0x52XXXX` addresses are typically inline string literals
5. **Check callers/callees**: Use `ida-pro-mcp-xrefs_to` to understand how the function is called and what values the caller passes
6. **Name any unknown functions or labels**: Assist future readers by giving meaningful names to any functions or labels that are not already named in the diassembly and decompiled pseudocode.
7. **Add comments to disassembly**: Document any non-obvious behavior and anything else you think will be useful.

## Implementation Phase

Write the C++ implementation in `src/openre.cpp`. Follow these patterns:

### Basic structure

```cpp
// 0x00XXXXXX
static ReturnType function_name(ParamType param1, ParamType param2)
{
    // decompiled body
}
```

Place the implementation immediately after the existing `interop::call` wrapper, replacing it entirely. Keep the original `// 0x00XXXXXX` comment with the address.

### Windows API calls

Map Win32 API calls directly. For example, if the decompiled code calls `CreateFontA`:

```cpp
HFONT hFont = CreateFontA(cHeight, cWidth, 0, 0, cWeight, 0, 0, 0, charset, 0, 0, 0, 0, fontName);
if (hFont)
{
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
    // ...
}
```

Cast return values explicitly when the decompiler shows implicit integer-to-pointer conversions.

### Calling other binary functions

If the function calls another function that hasn't been decompiled yet, call it via `interop::call`:

```cpp
// 0x00432440
void sub_432440(int someParam)
{
    interop::call<void>(0x00432440, someParam);
}
```

### Conditionals and branches

Preserve the decompiled control flow exactly. For patterns like:

```c
if ( is_480p )
    return 24;
else
    return 12;
```

Translate directly to C++ without simplification that changes behavior.

## Global Variables

### GameTable globals

If a global is a field of `GameTable`, access it via `gGameTable.fieldname`:

```cpp
gGameTable.is_480p  // member already defined in re2.h
gGameTable.hFont    // member already defined in re2.h
```

### Standalone globals

For globals not in GameTable, consider adding them unless they map to immutable/constant data, in which case define that data directly in the source file.
Do not use std::string in OG data, use OldStdString instead which is compatible.

## Verification

1. **Control flow**: Ensure every branch, loop, and conditional matches the decompiled pseudocode
2. **Parameter order**: On x86, function arguments are pushed right-to-left. Verify the first parameter in the C++ call matches what's pushed last in the disassembly
3. **Struct offsets**: If accessing struct fields via offsets, verify the offset matches the GameTable layout in re2.h
4. **Return types**: Ensure return values match what the caller expects (especially for HRESULT, HANDLE, and pointer returns)
5. **Build check**: Compile with `msbuild` under a VS2026 prompt to catch type errors
6. **Independent review**: Spin off a `deepseek-v4-pro` sub agent via the `task` tool with `agent_type: "Code Reviewer"` and `model: "deepseek-v4-pro"` to review the implementation.
                           Provide it with both the original IDA Pro pseudocode and the new C++ code, and instruct it to verify correctness independently with a fresh perspective.
                           This catches subtle bugs, calling convention mismatches, and parameter ordering issues that the original author may have overlooked.

## Examples

### Example 1: Font_create (0x00431000)

A simple function with conditional logic and two Win32 API calls:

```cpp
// 0x00431000
static void font_create()
{
    BOOL v1;
    HWND v3;

    v1 = gGameTable.is_480p;
    FontH = (uint32_t)CreateFontA(
        v1 ? 24 : 12,
        v1 ? 12 : 6,
        0, 0,
        v1 ? 500 : 400,
        0, 0, 0,
        SHIFTJIS_CHARSET,
        0, 0, 0, 0,
        (const char*)0x522094);
    v3 = gGameTable.hWnd;
    if ( v3 )
    {
        HDC hdc = GetDC(v3);
        if ( hdc )
        {
            SelectObject(hdc, (HFONT)FontH);
            ReleaseDC(v3, hdc);
        }
    }
}
```

Key points:
- Global `FontH` at `0x662E58` was not in `GameTable` — added as a standalone static ref
- `byte_6634F8` at `0x6634F8` was also referenced — added as standalone ref
- String `(const char*)0x522094` is a Shift-JIS encoded Japanese font name
- `gGameTable.is_480p` was already mapped in GameTable
- `gGameTable.hWnd` was already mapped in GameTable
- `CreateFontA` and `SelectObject` are Win32 GDI functions — linked via `gdi32`

### Example 2: Function with struct access

When a function accesses complex struct fields via offsets, trace each offset to the GameTable. For example, if the decompiled code reads `*(_DWORD *)(this + 0x1234)`, check if offset `0x1234` in GameTable corresponds to a named field like `gGameTable.someField`. If it does not match, add the address as a standalone mapping with a descriptive name.

Add new structs as necessary. If they are to be defined in GameTable, add them to `address_map.txt` and regenerate `re2.h`. If they are standalone, define them in the source file.

---

**Note**: Do not edit `re2.h` manually. If a global needs to be added to GameTable, update `address_map.txt` and regenerate with `dotnet run --project tools\interopgen address_map.txt > src\re2.h`. For standalone globals, add them directly in `src/openre.cpp` near the other static refs.
