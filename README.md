# OpenRE
Open source clone of the original Resident Evil 2.

## Building

### Windows
#### Dependencies
* Visual Studio 2026
  * Desktop development with C++ workload

Download the third-party libraries (SDL3, Lua) into `lib`:
```
getdeps.bat
```

Using a Visual Studio 2026 development prompt:
```
msbuild openre.sln
```

### Linux
#### Dependencies
* cmake
* g++-mingw-w64

```
cmake -B out
cmake --build out
```

Copy `out/openre.dll` and `dist/openre.exe` to your RE 2 directory and run `openre.exe`.

## Running

Set `OPENRE_LOG_VERBOSITY` to `debug`, `warning`, `error`, or `info`.
Set `OPENRE_RE2_DATA` to the data directory containing `common`, `gallery`, `movie`, etc.
