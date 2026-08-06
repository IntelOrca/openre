# Move the MARNI renderer from DirectDraw/D3D2 to SDL3 + SDL_GPU

## Problem

The game's renderer (marni.cpp) runs on the original DirectDraw2/Direct3D2 COM
pipeline: `surface2` = DirectDraw primary surface on the SDL3 window HWND,
`surface0` = offscreen render target, `surfaceZ` = z-buffer, textures = DirectDraw
surfaces, scene = `DrawPrimitive(D3DPT_TRIANGLELIST/STRIP, D3DVT_TLVERTEX)` with
render states, present = `Blt surface0 → surface2`. About 66 call sites in
marni.cpp are still `interop` wrappers into original game code (primitive
transform/tessellation sub_40xxxx, `create_device`, `create_zbuffer`), so the
renderer is a hybrid of our C++ and original game code.

Goal: eliminate the DirectDraw/D3D2 COM usage (renderer only; movies, audio and
save-menu GDI text are separate later workstreams) using SDL3 + the SDL3 GPU API
(SDL_GPU — D3D12/Vulkan/Metal backends), while always keeping a known-good
reference path and the ability to A/B against it live.

## Approach

**Compat layer with dual backends + live toggle.** The seam is the D3D2-era COM
interfaces: every draw call, whether from our C++ or the still-original game
code, flows through ~10 small COM interfaces (`IDirectDraw2`, `IDirectDrawSurface`,
`IDirect3D2`, `IDirect3DDevice2`, `IDirect3DViewport2`, `IDirect3DMaterial2`,
`IDirect3DTexture2`, `IDirectDrawClipper`). We implement those interfaces as a
front-end that dispatches every call to **two** backends:

```
Marni renderer (our C++ + original game code)
        │  D3D2/DirectDraw COM calls
        ▼
 d3d2 front-end COM objects      (forward-by-default; broadcast to both)
        │
        ├──▶ GfxBackendD3D   → real DirectDraw/D3D2 (today's path, the reference)
        └──▶ GfxBackendGPU   → SDL3 GPU API (the new path, built up piece by piece)
        │
        ▼
 present(activeBackend)     ← hotkey (F6, configurable) selects whose backbuffer shows
```

Both backends render every frame (broadcast) so the toggle is an instant A/B;
later a config flag can disable the D3D reference backend.

Key facts that make this tractable:
- D3D2 API surface is tiny by modern standards and well documented.
- On modern Windows the game already runs on the D3D2 **software (RGB) device**,
  so surfaces are CPU-accessible.
- **We do not need to decompile `create_device`/`create_zbuffer`** — those are
  original code but operate *through* the COM objects we replace (`pDirectDraw2`,
  `pDirect3D2` are set by our reimplemented `init`/`create_d3d` first). Same for
  the still-original surface methods (`surface2_create_work`, `surface2_vfill`,
  ...): they call `Lock`/`Blt`/etc. on our COM surfaces.
- Forward-by-default: front-end objects hold a real DirectDraw/D3D2 object and
  forward anything we don't specially handle (e.g. movie/media-stream queries),
  so nothing outside the renderer breaks.

## Milestones

### M0 — Audit (do first)
Enumerate every DirectDraw/D3D COM method the marni code actually calls —
including the still-original functions (`create_device` 0x00406D90,
`create_zbuffer` 0x00407020, surface methods 0x0040F580/0x00412BD0/0x00414750,
texture creation 0x00405EC0 and friends). Use IDA to disassemble the original
functions and produce an interface × method coverage matrix so the front-end
implements everything the game touches.

### M1 — Seam: front-end COM objects + backend plumbing
- `gfx_backend.h`: small C++ `GfxBackend` interface (init/shutdown, surface
  create/destroy/lock/unlock/fill/blit, begin/end scene, set state, draw
  primitive, clear, present).
- `gfx_backend_d3d.cpp`: reference impl delegating to real DirectDraw/D3D2
  (refactor of today's logic, no behavior change).
- `gfx_backend_gpu.cpp`: SDL_GPU backend — stub initially.
- `gfx_d3d2.h/cpp`: the COM front-end objects (broadcast + forward-by-default).
- Route `create_ddraw`/`query_ddraw2`/`create_d3d` (reimplemented, currently call
  `DirectDrawCreate`) to instantiate the front-end instead.
- Result: game renders exactly as today, entirely through the front-end. Both
  backends exist; nothing visible changes.

### M2 — GPU swapchain + present + toggle
- `SDL_CreateGPUDevice`, claim the SDL3 window, acquire swapchain texture.
- `GfxBackendGPU::present` = render-pass blit (letterboxed) to the swapchain.
- F6 (configurable) toggles the active backend: D3D presents via its DD primary
  surface Blt, GPU presents via the swapchain.
- **Risk checkpoint:** validate that a DirectDraw primary surface and an
  SDL_GPU swapchain can coexist on the same window. If they fight over the
  window surface, fallback = read back the D3D reference's pixels and present
  them through the same swapchain (unified present for the D3D reference only),
  or recreate the swapchain on toggle.

### M3 — GPU surface layer
Map surface ops (CreateSurface, Lock/Unlock, fill, Blt, GetSurfaceDesc) to
SDL_GPUTexture + a CPU staging buffer, so all 2D/surface work runs on the GPU
backend (this also covers the GPU_13 no-3D path).

### M4 — GPU draw pipeline
BeginScene/EndScene, viewport Clear (target + z), render states (z-test/write,
alpha blend src/dst incl. SRCALPHA/INVSRCALPHA/SRCCOLOR/ONE, shade mode, cull
none, specular), DrawPrimitive. TL vertices (x,y,z,rhw, color, specular, tu/tv)
→ vertex buffer + a small vertex/fragment shader pair. Two shaders (textured /
untextured). Needs DXC tooling to build HLSL → DXIL (D3D12) + SPIR-V (Vulkan);
MSL later for macOS.

### M5 — GPU textures
RGB555 → RGBA8888 upload (and paletted textures), filtering honoring the game's
filter setting, and TEXTUREHANDLE → GPU texture resolution (the game passes a
DWORD handle via `SetRenderState(D3DRENDERSTATE_TEXTUREHANDLE, h)`).

### M6 — Viewport / material / stats
Viewport size/clip/z-range, ambient material (background clear color), and the
triangles/vertices counters (`GetStats`).

### M7 — Fullscreen & resolution change
`change_mode`/`change_display_mode`/`toggle_fullscreen` recreate surfaces/device
— GPU backend must recreate textures/pipeline/swapchain-sized blit accordingly,
including the borderless-fullscreen letterbox path.

### M8 — Parity sign-off & cleanup
A/B every scene (title, menus, rooms, items, inventory, doors) with the toggle;
fix diffs. Then: config flag to disable the D3D reference backend, compile it
out on non-Windows, and drop the DirectDraw dependencies in vcxproj/CMake.

## Progress

### M0 — Audit (done)
Static audit of every DirectDraw/D3D COM method the MARNI renderer touches.
Deliverable: `docs/com-coverage-report.md` (full interface × method coverage
matrix, decompiled originals for the still-original functions).

Key findings:
- **The game's COM vtables are the standard SDK layouts.** The game's binary was
  compiled against headers whose `IDirectDraw`, `IDirectDraw2`,
  `IDirectDrawSurface`, `IDirect3D2`, `IDirect3DDevice2` (33 entries),
  `IDirect3DViewport2`, `IDirect3DMaterial2` and `IDirect3DTexture2` vtable
  offsets match the modern Windows SDK `ddraw.h`/`d3d.h` exactly (verified
  against SDK headers + IDA disasm), so a front-end can hook standard slots.
- The **D3DRENDERSTATETYPE values** (DX3-era numbering: 17/18 =
  TEXTUREMAG/TEXTUREMIN, 19/20 = SRCBLEND/DESTBLEND, 27 = ALPHABLENDENABLE, 41 =
  COLORKEYENABLE, etc.) also match the SDK `d3d.h` enum. SetRenderState is called
  at 44 sites with these values.
- Interfaces used (deduped): IDirectDraw (QI/GetCaps/Release),
  IDirectDraw2 (QI/CreateSurface/CreatePalette/CreateClipper/EnumDisplayModes/
  GetCaps/SetCooperativeLevel/RestoreDisplayMode), IDirectDrawSurface
  (QI/Release/AddAttachedSurface/Blt/GetSurfaceDesc/IsLost/Lock/Restore/
  SetColorKey/SetPalette/SetClipper/Unlock), IDirectDrawPalette
  (Release/GetEntries/SetEntries), IDirect3D2 (EnumDevices/CreateDevice/
  CreateViewport/CreateMaterial), IDirect3DDevice2 (Begin/End/BeginScene/
  EndScene/SetRenderState/GetStats/DrawPrimitive/DrawIndexedPrimitive/
  SetCurrentViewport/SetRenderTarget/SwapTextureHandles/SetTransform/
  MultiplyTransform/EnumTextureFormats/GetCaps), IDirect3DViewport2
  (Clear/SetViewport2/SetBackground), IDirect3DMaterial2 (GetHandle/SetMaterial),
  IDirect3DTexture2 (GetHandle/Load/Release).
- Most surface method offsets match the standard layout, so both our C++ code and
  the still-original game code hit the same vtable slots on the objects we swap.
- The z-buffer is created at 16-bit via original `create_zbuffer` 0x00407020; the
  D3D device (HAL or RGB) is created by original `create_device` 0x00406D90
  against `surface0`. These need no decompilation because they operate through
  the COM objects we replace.

### M1 — Seam: front-end COM objects + backend plumbing (done)
Implementation approach for the COM front-end (`gfx_d3d2.cpp`):
- **Vtable-swap**: allocate a new vtable array, copy the real object's vtable
  (forward-by-default), override the slots we intercept, and swap the object's
  vtable pointer. `this` in hooks is the real object; a side table maps it to its
  saved original vtable. All objects created after `DirectDrawCreate` (surfaces,
  palettes, clipper, device, viewport, material, textures) get wrapped
  automatically via the QI/Create hooks, so `create_ddraw` is the single explicit
  wrap point.
- Hooks dispatch to `GfxBackendD3D` (reference, calls original vtable entries)
  and `GfxBackendGPU` (stub) for: CreateSurface, Lock/Unlock, Blt,
  GetSurfaceDesc, IsLost, Restore, AddAttachedSurface, SetColorKey, SetPalette,
  SetClipper, CreateDevice, BeginScene/EndScene, SetRenderState, SetTransform,
  MultiplyTransform, DrawPrimitive, DrawIndexedPrimitive, GetStats,
  SetCurrentViewport, SetRenderTarget, SetViewport2, SetBackground, Clear.
  Everything else forwards unchanged.
- New files: `gfx_backend.h`, `gfx_backend_d3d.cpp`, `gfx_backend_gpu.cpp`,
  `gfx_d3d2.h/cpp`.
- **What was implemented in M1**: the five files above were added and wired into
  `marni.cpp` (`gfx::init()` in `init`, `gfx::wrap_ddraw(lpDD)` in `create_ddraw`,
  `gfx::shutdown()` in `dtor`, `gfx::notify_present()` in `flip_blt`) and the
  vcxproj. Slot tables were verified against the Win10 SDK `ddraw.h`/`d3d.h`
  (including the 3-arg `Viewport2::Clear` and `CreateSurface`'s out-param).
- **Active backend selection API**: `gfx::set_active_backend(0|1)` /
  `gfx::active_backend()`; 0 = D3D reference (default), 1 = GPU. Hotkey wired in
  M2.
- **Build/run verification**: `build.bat` succeeds with 0 warnings/0 errors
  (`TreatWarningAsError`); the game launches, logs `[gfx] backends initialised
  (active=0)` + `[gfx:gpu] init (stub)`, reaches the title screen, and runs
  without crashing or visual regression.
- **M1 regression + fix (CreateSurface vtable validation)**: initial runs after
  wrapping crashed during `EnumDevices` (fixed by raising all wrapped vtable
  allocations to 64 slots), then `init_all` failed with
  `get_surface_desc surface0 err=2147942487` (`E_INVALIDARG`) and dropped into
  the blocking `win_exit(13)` message box. Root cause: **ddraw.dll's real
  `CreateSurface` validates `this->lpVtbl`** — when our swapped vtable is in
  place, it creates surfaces that later fail `GetSurfaceDesc` with
  `E_INVALIDARG` (a `DDSURFACEDESC` with `dwSize=108` is correct; size was not
  the issue). Fix: in both `hook_ddraw2_create_surface` (IDirectDraw2) and the
  new `hook_ddraw_create_surface` (IDirectDraw — required because the original
  `create_zbuffer` 0x00407020 calls the IDirectDraw slot directly), the real
  CreateSurface is invoked with the **original vtable temporarily restored**, then
  swapped back and the returned surface is wrapped. Verified: game re-initialises
  (`init_all ok`), creates surfaces/textures (16x16 bpp=16, 256x256 bpp=32),
  renders continuous frames, window "BIOHAZARD(R) 2 PC" present, process stays
  running. All temporary diagnostic trace logging was removed afterwards.

## Out of scope (separate later workstreams)
- Movie playback (DirectShow/DirectDrawMediaStream) → SDL media + decoder.
- Audio (DirectSound8) → SDL3 audio.
- Save-menu GDI text (`GetDC`+`TextOut`) → SDL_ttf.

## Risks / notes
- **Window sharing** (DD primary vs SDL_GPU swapchain) — validated at M2 with a
  documented fallback.
- **Shader tooling**: need DXC in the build (HLSL → DXIL + SPIR-V); SDL 3.4.12
  bundled already supports both formats plus MSL/METALLIB.
- **COM ABI**: front-end vtable layouts must exactly match d3d.h/ddraw.h.
- **Undiscovered COM calls** in original code — mitigated by forward-by-default
  and the M0 audit.
- **Perf**: broadcast double-render during migration; config flag to drop the
  reference later.
- New source files must be added to `src/openre.vcxproj` and `CMakeLists.txt`.
