# OpenRE
Open source clone of the original Resident Evil 2.

## Building

### Windows
#### Dependencies
* Visual Studio 2022
  * Desktop development with C++ workload

Using a Visual Studio 2022 development prompt:
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
