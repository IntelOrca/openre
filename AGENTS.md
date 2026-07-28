# AGENTS.md
This is the OpenRE project, an open-source reimplementation of Resident Evil 2.

## RE2 addresses
* If a function is a direct implementation of an original function in RE2, the original function address is written above it as a comment.
* Avoid directly using `interop::call` in a function and instead create a shared wrapper for it, e.g.
```
    // 0x00432080
    static void rsrc_release()
    {
        interop::call(0x00432080);
    }
```
If you can't figure out what the function does, just name it, e.g. `sub_432080`.

## RE2 memory model
* re2.h is generated and must not be edited manually. Instead edit `address_map.txt` and run:
  `dotnet run --project tools\interopgen address_map.txt > src\re2.h`
* Avoid adding raw addresses to the code, instead update `GameTable` via `address_map.txt`
* The exception is constant/immutable data which can be added directly to the source file or best suited place.
* Helpers are often used to get certain things rather than directly accessing GameTable, look for one, potentially add one.

## RE2 flags (check_flag / set_flag)
* `check_flag(group, index)` / `set_flag(group, index, value)` use **MSB-first** bit numbering via
  `bitarray_get`/`bitarray_set` (sce.cpp): index N tests mask `0x80000000 >> N`.
  This is NOT the usual LSB convention (`1 << N`). `index = 31 - lsb_bit_position`.
* Examples: index 0 = `0x80000000`, index 18 = `0x00002000`, index 31 = `0x00000001`.
  A conversion table is written as a comment above each `FG_*` enum in `openre.h`.
* When decompiling original code that uses raw masks, convert mask → index, never assume index = LSB bit position:
  * `fg_system & 0x2000` → LSB bit 13 → index 18 → `FG_SYSTEM_DEMO` (NOT `FG_SYSTEM_13`; that is mask `0x40000`)
  * `fg_system & 0x40000000` → index 1 → `FG_SYSTEM_1`
  * `fg_system |= 0x80000000` / `fg_system < 0` → index 0 → `FG_SYSTEM_0`
  * `fg_system & 1` → index 31 → `FG_SYSTEM_31`
* After touching flag code, sanity-check the mask: compute `0x80000000 >> index` and compare against the original assembly.
* Indexes ≥ 32 address subsequent dwords of the flag group (index 32 = `0x80000000` of `dword[1]`).

## RE2 hooks
* It is possible to force original code to call our new implementation using hooks.
  Typically each module defines its hooks at the bottom of the source file like:
    void file_init_hooks()
    {
        interop::writeJmp(0x004DD360, &osp_read);
        interop::writeJmp(0x00509780, &file_read_save);
        interop::writeJmp(0x005097E0, &file_write_save);
    }
  And this is called from openre.cpp onAttach.
* Once we know all callers are also implemented (check IDA), we can remove the hook.

## Other instructions
* When adding new source files, update `src\openre.vcxproj`.
* Avoid labels as much as possible, extract small sections of code to functions if that helps avoid them. Use [[fallthrough]] in switch blocks.

## Building
* Run `format.bat` to format all code in the repo
* Run `build.bat`

## Running
* Run `run.bat`
