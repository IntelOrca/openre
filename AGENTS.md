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

## New files
* When adding new source files, update `src\openre.vcxproj`.

## Building
* Under a VS2022 prompt: "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
  * E.g. `cmd /c "call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" && msbuild"` in repo root

## Running
* Run `F:\games\openre\openre.exe` with working directory: `F:\games\openre`
