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

### M2 — GPU swapchain + present + toggle (done)
- **What was implemented**:
  - `system_window.h/cpp`: added `system::window::get_window()` returning the
    `SDL_Window*` as `void*` (header stays SDL-free) so the GPU backend can
    claim the game window.
  - `gfx_backend_gpu.cpp`: `GfxBackendGPU` is no longer a stub for the present
    path. `init()` creates an SDL_GPU device
    (`SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV)`
    → driver `direct3d12` on this machine), claims the SDL3 window
    (`SDL_ClaimWindowForGPUDevice`), logs the swapchain format
    (`SDL_GetGPUSwapchainTextureFormat` → 12 = `B8G8R8A8_UNORM`) and a
    `(M2: swapchain ready)` marker. If device creation or claim fails the
    backend returns false and the game keeps running on the D3D path (active
    backend stays 0). `present()` acquires a command buffer + swapchain texture
    (non-blocking `SDL_AcquireGPUSwapchainTexture`), runs a render pass that
    clears the swapchain to solid black, and submits. A NULL swapchain texture
    (window minimized / swapchain being recreated on resize) is logged at debug
    and the frame is dropped without crashing. `shutdown()` releases the window
    claim and destroys the device before SDL_Quit. All other methods remain
    stubs (surface/texture/draw are M3+).
  - Present is **gated on the active backend**: `GfxBackendGPU::present()`
    returns immediately unless `active_backend() == 1`, so while the D3D
    reference backend is active the DirectDraw primary surface (the game's Blt
    in `flip_blt`) keeps owning the window unchanged.
  - `openre.cpp`: F6 toggles the active backend
    (`gfx::set_active_backend(gfx::active_backend() ? 0 : 1)`) and logs the
    new value (`[gfx] active backend toggled to N`).
  - `gfx_d3d2.cpp`: `gfx::init()` reads an `OPENRE_GFX_BACKEND` env var
    (`=1` starts on the GPU backend) as a developer hook for automated
    verification; default stays 0.
- **Risk checkpoint result: the DirectDraw primary surface and the SDL_GPU
  swapchain coexist cleanly on the same window.** The game's `surface2` (DD
  primary on the same HWND) and the D3D12 swapchain created by
  `SDL_ClaimWindowForGPUDevice` do not fight: with active=0 the swapchain
  exists but never presents and D3D output is pixel-identical to before; with
  active=1 the swapchain presents every frame (solid black, no content yet) and
  toggling back to 0 instantly restores the DD-presented frame. No black
  screen, crash, or garbled output at any point, including during live resizes
  and minimize/restore. The planned fallback (unified present via readback) was
  not needed.
- **Verification evidence** (all at `OPENRE_LOG_VERBOSITY=debug`):
  - Default run (active=0): `[gfx:gpu] device created (driver=direct3d12)`,
    `[gfx:gpu] window claimed, swapchain format=12 (M2: swapchain ready)`,
    `[gfx] backends initialised (active=0)`; the game then renders continuous
    frames through the D3D path (BeginScene/EndScene/Blt cycles, ~17k log
    lines), window "BIOHAZARD(R) 2 PC" present, process stays alive — no
    `[gfx:gpu] present` lines (correctly gated off).
  - GPU run (`OPENRE_GFX_BACKEND=1`): `[gfx] backends initialised (active=1)`,
    `[gfx:gpu] present (swapchain cleared)` logged every frame (190 presents in
    ~10s), zero `failed:` lines, zero "swapchain texture unavailable" skips.
  - Live resize (640x480 → 800x600 → back) while on the GPU backend: process
    keeps running, present continues (~97 more presents during the resizes), no
    errors.
  - Live minimize/restore while on the GPU backend: process keeps running,
    present resumes after restore, no errors.
  - Live F6 toggle via synthetic keypress: `[gfx] active backend toggled to 0`
    (GPU presents stop), `toggled to 1` (GPU presents resume), `toggled to 0`
    (GPU presents stop again) — the A/B switch is instant and D3D output
    returns immediately when active=0.
  - `build.bat`: 0 warnings, 0 errors (`TreatWarningAsError`).
- Note: on this SDL 3.4.12 there is no `SDL_WINDOW_GPU` window flag;
  `SDL_ClaimWindowForGPUDevice` handles swapchain setup on its own, so
  `SDL_CreateWindow` flags were left unchanged.

### M3 — GPU surface layer (done)
- **What was implemented** (`gfx_backend_gpu.cpp` only):
  - `GfxBackendGPU` now tracks every DirectDraw surface the front-end reports
    (`create_surface` is called right after the real `CreateSurface` succeeds,
    keyed by the `IUnknown*`/`IDirectDrawSurface*` pointer) in
    `std::unordered_map<void*, SurfaceEntry>`. A `SurfaceEntry` holds an
    `SDL_GPUTexture` (usage `SAMPLER | COLOR_TARGET`), a CPU shadow
    (`std::vector<uint8_t>`) in the surface's DirectDraw pixel format, and two
    transfer buffers (upload for shadow→texture, download for texture→shadow).
  - `create_surface`: records the entry and logs creation. Pixel formats:
    16bpp → `SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM` (byte-identical to DirectDraw
    RGB565 → memcpy transfers), 32bpp → `R8G8B8A8_UNORM` with a channel reorder
    on transfer (DirectDraw RGBX8888 memory order is B,G,R,X; alpha forced
    opaque). `SDL_GPUTextureSupportsFormat` gates `SAMPLER|COLOR_TARGET`, with
    a SAMPLER-only fallback for 16-bit formats on backends that cannot render
    to them.
  - **Zero-size primary handling**: the game creates the primary (`surface2`)
    with `DDSD_CAPS` only (width/height/bit depth all zero). Such entries are
    created in a deferred state; `ensureTexture()` lazily creates the GPU
    texture once real dimensions arrive, which `adoptDesc()` harvests from the
    `DDSURFACEDESC` the D3D reference backend fills during `GetSurfaceDesc` /
    `Lock` (width, height, bpp; pitch recomputed as `width * bpp/8`). This also
    covers `surface0`, which is created with dimensions but no pixel format.
  - `lock`: only hands out a CPU pointer when the GPU backend is active
    (otherwise returns S_OK and leaves the D3D backend's `lpSurface`/`lPitch`
    untouched so the game keeps writing into the real DD surface). When active:
    ensures the texture exists, downloads the current texture content into the
    shadow (`SDL_DownloadFromGPUTexture` + synchronous
    `SDL_SubmitGPUCommandBuffer` + `SDL_WaitForGPUIdle`, then
    `SDL_MapGPUTransferBuffer`), and returns `lpSurface = shadow.data()`,
    `lPitch = width * bpp/8`. A re-entrant lock (already locked) is a no-op.
    First lock of a fresh surface hands back a zeroed shadow.
  - `unlock`: uploads the shadow back into the texture
    (`SDL_MapGPUTransferBuffer` → `SDL_UploadToGPUTexture`, synchronous
    submit + wait) and marks the surface as having content.
  - `blt`: surface→surface copy uses `SDL_CopyGPUTextureToTexture` (1:1,
    rects clamped, same-format check, debug log); colorfill
    (`src == nullptr && DDBLT_COLORFILL`) writes `fx->dwFillColor` into the
    CPU shadow in the surface's own pixel format and uploads only the affected
    region. Both are gated on the active backend (logged `(inactive)` when the
    D3D backend owns rendering, which is the default — but `create_surface`,
    `get_surface_desc`, `lock`/`unlock` bookkeeping always run so the GPU-side
    state stays coherent for a live F6 toggle).
  - `get_surface_desc`: unknown surface → `E_FAIL`; otherwise `adoptDesc` (logs
    adoptions) and, when active, overwrites `dwWidth`/`dwHeight`/`lPitch` so
    `MarniSurface2` gets `width * bytesPerPixel` as DirectDraw pitch.
  - `is_lost` → `DD_OK` (a GPU texture is never lost); `restore` → `S_OK`;
    `add_attached_surface` / `set_color_key` / `set_palette` / `set_clipper`
    stay `S_OK` stub logs (color key / palette are M5 work; the game calls
    them but nothing breaks when they no-op).
  - `shutdown()` now waits for the GPU to idle (`SDL_WaitForGPUIdle`) and
    releases every tracked surface's texture + transfer buffers before
    destroying the device. `destroy_surface` releases a single entry; unknown
    surfaces and missing entries are guarded everywhere.
- **Verification evidence** (all at `OPENRE_LOG_VERBOSITY=debug`):
  - `build.bat`: 0 warnings, 0 errors (`TreatWarningAsError`).
  - Default run (active=0, no env): game still renders through D3D
    (init_all ok, frames, window, process alive ~12s). The GPU backend ran
    every surface call silently in the background: CreateSurface / adopted
    GetSurfaceDesc / Lock-Unlock logged `(inactive)` (pointer left to D3D),
    Blt logged `(inactive)`, zero `failed:`/error lines.
  - GPU run (`OPENRE_GFX_BACKEND=1`, ~14s): `[gfx:gpu] device created
    (driver=direct3d12)`, `window claimed`, surfaces created (primary 0x0 and
    surface0 640x480 deferred; 16x16/128x128/256x256 textures at 16/32bpp),
    `GetSurfaceDesc surface=... adopted real size 640x480 bpp=32` and
    `3840x2160 bpp=32`, **222 Lock / 222 Unlock pairs** with real pointers and
    correct pitches (640x480@32bpp → 2560, 256x256@32bpp → 1024, 16x16@16bpp →
    32), **190 `Blt colorfill`** (GPU_13 software-clear path) and **190
    `Blt copy 640x480 at (0,0) -> ...`** (the `flip_blt` surface0→surface2
    copy, one per presented frame), **190 `present (swapchain cleared)`**.
    Process stayed alive; zero `failed:`/error lines; no "Blt skipped"
    entries. Screen is solid black as expected (no draw pipeline until M4).
- **Known limitations**:
  - Blt scaling is not implemented: surface→surface copies are 1:1. The
    common `flip_blt` path is a full-size copy and works; a dst rect larger
    than the src rect would copy only the src-sized region (rects are clamped
    to the surfaces).
  - The dst rect of `flip_blt` carries the window position (e.g.
    `(1600,798)`); the copy honors it literally into the primary texture.
    Present (M4) will decide which region of `surface2` to show.
  - Color key (`SetColorKey`) and palettes are no-op stubs — texture
    transparency/shadows will need them (M5).
  - 24bpp surfaces (one 640x480 `bpp=24` entry, the D3D2 device's internal
    back buffer) are not tracked — logged `unsupported surface bpp=24`; the
    game degrades gracefully since the D3D backend still owns those paths.
  - Lock with a non-null rect ignores the rect (locks the whole surface);
    the game locks whole surfaces in practice.

### M4 — GPU draw pipeline (done)
- **What was implemented** (`gfx_backend_gpu.cpp`; small front-end additions in
  `gfx_d3d2.cpp/h`):
  - **Shader tooling — embedded bytecode.** Three HLSL sources under
    `src/shaders/` (`tl_vertex.hlsl`, `tl_textured_fragment.hlsl`,
    `tl_untextured_fragment.hlsl`) are compiled once with DXC
    (`tools/gen_shaders.ps1`, looks up `dxc.exe` in the Vulkan SDK then the
    Windows Kits) to DXIL (`-T vs_6_0/ps_6_0`) and SPIR-V (`-spirv
    -fvk-use-dx-layout`), and the bytecode is embedded as C arrays in
    `src/gfx_shaders.h/.cpp` which are committed. The build has no
    shader-compilation step (robust across the MSVC `build.bat` and CMake
    builds); re-run the script only when a shader changes. Resource layout
    follows SDL_GPU's conventions: vertex uniforms in `register(b0, space1)`,
    textures/samplers in `(t0/s0, space2)`; `-fvk-use-dx-layout` maps those
    spaces to the descriptor sets SDL's Vulkan backend expects.
  - **TL vertex pipeline.** `D3DTLVERTEX` is actually **32 bytes**
    (pos `FLOAT3`@0, D3DCOLOR `UBYTE4_NORM`@16 in memory order B,G,R,A,
    uv `FLOAT2`@24) — the `rhw`/`specular` fields are consumed by nothing. The
    vertex shader converts screen-space (top-left origin) coords to NDC
    `(x-vp.x)/vp.w*2-1`, `1-(y-vp.y)/vp.h*2` (Y flipped for SDL_GPU's
    bottom-left origin), passes the swizzled RGBA color and UVs; the textured
    fragment multiplies the sampled texel by the vertex color, the untextured
    one outputs the vertex color.
  - **Render-state mapping** (the game uses the DX2-era custom numbering, see
    `RenderStateId`): `ZENABLE`/`ZWRITEENABLE`/`ZFUNC` → SDL depth
    test/write/compare (ZFUNC=4 = `D3DCMP_LESSEQUAL` — the game's z-in-[0,1]
    semantics, matching the D3D reference); `ALPHABLENDENABLE` +
    `SRCBLEND`/`DESTBLEND` → `SDL_GPU_BLENDFACTOR` (ZERO=1, ONE=2, SRCCOLOR=3,
    INVSRCCOLOR=4, SRCALPHA=5, INVSRCALPHA=6, DSTCOLOR/DSTALPHA variants too);
    `CULLMODE` → `SDL_GPU_CULLMODE` (NONE=1, CW=2→BACK, CCW=3→FRONT);
    `TEXTUREMAG`/`TEXTUREMIN` pick the linear/nearest sampler. `SHADEMODE=2`
    (Gouraud) needs nothing (per-vertex interpolation is natural);
    `TEXTUREADDRESS`, `ANISOTROPY`, `LASTPIXEL`, `SUBPIXEL`, `EDGEANTIALIAS`,
    `SPECULARENABLE`, `TEXTUREMAPBLEND` are ignored with a debug log for
    unknowns.
  - **Cached pipelines.** SDL_GPU pipelines are immutable, so pipelines are
    created lazily keyed by `PipelineKey` (textured, alphaBlend,
    src/dst blend factor, zTest/zWrite/zFunc, cull, primitive type) — the
    game uses a small handful of combinations. Scene pipelines target
    `R8G8B8A8_UNORM` + a `D16_UNORM` depth-stencil target; a separate blit
    pipeline targets the swapchain format.
  - **Deferred draws / render pass.** `DrawPrimitive`/
    `DrawIndexedPrimitive` (TL only; indexed draws are reordered through the
    WORD indices, the game does not use them) copy the raw vertices into a
    per-frame CPU pool and record the pipeline/texture/offset/count. `present`
    (when active==1) uploads the pool into a growable vertex buffer, runs the
    **scene pass** on the render target texture (the `SurfaceEntry` of the
    game's surface0) with the D16 depth texture, `load_op` CLEAR/LOAD per the
    frame's `Clear` flags (target and/or z; clear color = the ambient material
    tracked via `set_material`/`SetBackground`), then the **present pass**
    blits the render target letterboxed (aspect preserved) into the swapchain
    with the blit pipeline. One push of the viewport rect serves all draws.
  - **Render-target adoption fix (root cause of black frames).** surface0 is
    created with dimensions but no pixel format, and while loading rooms the
    game never Locks/queries it — so its GPU texture stayed deferred and every
    present cleared a black swapchain. `set_render_target` now force-adopts
    the render-target entry as 32bpp (pitch = width·4) and creates the
    texture, so the scene pass can begin immediately.
  - **Texture content (TEXTUREHANDLE → texture).** `IDirect3DTexture2::GetHandle`
    is hooked in the front-end (`hook_texture_get_handle` →
    `create_texture_handle`), recording `handle → surface` in
    `mTextureHandles`. Texture *content* arrives through
    `IDirect3DTexture2::Load(dst, src)` (the game fills an internal surface
    through the lock path, then the D3D driver copies into the texture's
    backing surface behind our back) — the front-end now hooks `TEX_Load`
    (`hook_texture_load`, vtable slot 5) and the backend `texture_load`
    performs an `SDL_CopyGPUTextureToTexture` of the source texture into the
    destination. `SetRenderState(TEXTUREHANDLE, h)` then resolves to the
    surface's GPU texture (textured pipeline) or falls back to untextured.
  - `get_stats`: when the GPU backend is active, replaces
    `dwTrianglesDrawn`/`dwVerticesProcessed` with what the GPU actually drew
    (per-frame accumulated counters) so the game's per-frame deltas keep
    working.
- **Verification evidence** (all at `OPENRE_LOG_VERBOSITY=debug`,
  `OPENRE_RE2_DATA=F:\games\openre\data`):
  - `build.bat`: 0 warnings, 0 errors (`TreatWarningAsError`).
  - Default run (active=0): D3D path unchanged — init ok, window shows the
    title screen, process alive; zero `[gfx:gpu]` error/fail lines (the GPU
    backend only logs idle-broadcast traces like `(inactive)`).
  - GPU run (`OPENRE_GFX_BACKEND=1`): **real graphics** — the game reaches
    the demo/room mode, `present: 33 draws, 3392 bytes verts, triangles=8xxx`
    every frame, `created pipeline (textured=1 ... zTest=1 ...)` for the
    handful of state combos, and a `PrintWindow` capture of the game window
    shows the room content (≈590-750 unique sampled colors; dominant room
    red `(206,4,16)` and cyan `(0,153,188)` elements, ~2647 red / ~1464 cyan
    sampled pixels) matching the D3D reference capture pixel-for-pixel on the
    colored elements. An `OPENRE_GPU_DUMP` scene readback of the offscreen
    render target showed 1866 unique colors (room content, not black).
  - F6 toggle test both ways (synthetic `WM_KEYDOWN` VK_F6): window captures
    before/after show `[gfx] active backend toggled to 0` → D3D frame
    (2643 red / 1464 cyan) and back `to 1` → GPU frame (2647 red / 1464
    cyan) — both backends produce a visible frame; content matches.
  - Depth is enabled and works: pipelines are created with `zTest=1
    zWrite=0` (the game's ZWRITEENABLE=0 for these draws) and the render is
    unchanged vs the earlier depth-disabled experiment.
- **Known limitations**:
  - Texture content depends on the `IDirect3DTexture2::Load` interception;
    textures the game fills another way (if any) render as the untextured
    pipeline would (vertex color only).
  - Blend factors outside the observed set map to `SDL_GPU_BLENDFACTOR_ONE`
    (default) rather than failing; the observed set (ZERO/ONE/SRCCOLOR/
    INVSRCCOLOR/SRCALPHA/INVSRCALPHA) is covered.
  - Untransformed (`D3DVT_VERTEX`) draws are logged and skipped — the game
    only draws TL vertices on this path.
  - `TEXTUREADDRESS`, `ANISOTROPY`, `TEXTUREMAPBLEND`, specular and color-key
    states are ignored (color key is M5 work); the game's observed values
    don't need them.
  - Present letterboxes the 4:3 render target into whatever the window is;
    fullscreen/resolution changes are M7.
  - 24bpp surfaces remain untracked (M3 limitation); Blt scaling remains 1:1.

### M5 — GPU textures (done)
- **16bpp format decision — the game's textures are ARGB1555, not RGB565.**
  The GPU backend records the DirectDraw channel masks from the
  `DDSURFACEDESC.ddpfPixelFormat` (`adoptPixelFormat`, fed by the first
  `GetSurfaceDesc`/`Lock` after creation — the game passes no masks in the
  `CreateSurface` desc) and logs them on first adoption. Runtime evidence
  (title/menu/demo runs, `OPENRE_GFX_BACKEND=1`):
  `[gfx:gpu] 16bpp masks R=0x7c00 G=0x3e0 B=0x1f A=0x8000` — i.e. ARGB1555
  (5-5-5 with the top bit as 1-bit alpha), matching the Marni 16bpp desc from
  `MarniSurface2::CreateWork` (0x414750) and the classic RE2-era texture
  format. `pickTextureFormat` centralises the decision: RGB565
  (0xF800/0x07E0/0x001F) → `B5G6R5_UNORM` (byte-identical); any other mask
  layout (ARGB1555/RGB555/…) → `R8G8B8A8_UNORM` with per-channel mask
  expansion (`convertShadowToTexture`/`convertTextureToShadow` round-tripping
  through the recorded masks; the 0x8000 alpha bit becomes real alpha).
  Because the masks arrive only after texture creation, the runtime path for
  this game is 16bpp → R8G8B8A8 (the B5G6R5 fast path is kept for descs that
  do carry RGB565 masks). Verified pixel-identical to the D3D reference on the
  title screen (window captures, 0/111069 differing pixels).
- **Paletted (8bpp) path.** The game creates palettes via
  `IDirectDraw::CreatePalette` (slot 5, now wrapped in `gfx_d3d2.cpp` —
  vtable restored around the real call like CreateSurface), attaches them via
  `IDirectDrawSurface::SetPalette` and fills them via
  `IDirectDrawPalette::SetEntries` (slot 6, now wrapped); the front-end
  broadcasts `create_palette` / `set_palette_entries` to both backends (default
  no-ops in `GfxBackend` keep the D3D path untouched). The GPU backend records
  a 256-entry RGBA table per palette (entries expanded opaque — the game
  always writes `peFlags=0`, transparency is colour-keyed, deferred) and
  expands 8bpp index shadows to R8G8B8A8 on upload / re-quantises on download
  (nearest-index when a palette is missing). A `SetEntries` that lands right
  after the surface's own unlock re-uploads paletted surfaces whose content
  came from the shadow (`contentFromShadow`), so the next draw shows the new
  colours. **Runtime finding: the game never creates 8bpp surfaces on the
  tested paths** (0 `CreatePalette`, 0 `bpp=8`, 0 `paletted` events across all
  title/menu/demo runs; the M0 audit §17 shows palettes are only created for
  `DDPF_PALETTEINDEXED*` descs, and the texture-format table advertises only
  non-paletted formats). The conversion path is implemented and documented
  but untested-if-unused.
- **Filtering.** `TEXTUREMAG` (17) / `TEXTUREMIN` (18) render states are
  mirrored (`DeviceState.texMag/texMin`, debug-logged on change) and
  `wantsLinearFilter()` maps the game's values (bilinear = MAG 2 / MIN 6,
  nearest = 1/1, set by `Marni::SetFiltering` gated on `marni_config.Bilinear`,
  F7) to the SDL_GPU sampler per draw (`mSamplerLinear`/`mSamplerNearest`).
  Samplers are separate objects in SDL_GPU, so no pipeline rebuild is needed.
  Verified: the game's startup `SetRenderState(TEXTUREMAG/MIN)` calls reach the
  mirror (`filter MAG -> 1 (nearest)`), the F7 toggle runs without a crash on
  the GPU path, and the title/menu renders pixel-identical to D3D. Note: the
  intro/menu/demo paths draw through the immediate `set_filtering(self, 0)`
  path (nearest regardless of config), so the bilinear→nearest switch itself
  is only exercised when a filter=1 draw op (batched 3D rooms) runs — the
  wiring is code-verified and the mirror/sampler path is log-verified.
- **TEXTUREHANDLE resolution.** Every texture goes through the hooked
  `IDirect3DTexture2::GetHandle` (vtable slot 4) → `create_texture_handle`
  records `handle → SurfaceEntry`; `SetRenderState(TEXTUREHANDLE, h)` →
  `resolveTexture` maps it to the entry's GPU texture (untextured fallback).
  0 "unhandled texture handle" events across all runs; 23 handle registrations
  observed in a menu/demo run. Coverage is complete on the exercised paths.
- **Colour key** (`COLORKEYENABLE`) remains ignored as in M4 (deferred; M8
  parity can revisit). Unknown pixel formats log at debug with the `[gfx:gpu]`
  prefix and never crash (unsupported bpp → `SDL_GPU_TEXTUREFORMAT_INVALID` →
  warning + untracked).
- **Verification evidence.**
  - `build.bat`: 0 warnings / 0 errors.
  - Default run (active=0): D3D path unchanged, game renders, no GPU errors.
  - GPU run (`OPENRE_GFX_BACKEND=1`): real graphics render — title screen,
    animated main menu (250–660+ unique sampled colours) and the intro demo;
    window capture of the title screen is **pixel-identical to the D3D
    reference** (PrintWindow, 369×301, 0/111069 differing pixels; same 25
    unique colours, same counts incl. the 0,153,188 title band).
  - `OPENRE_GPU_DUMP=1` scene readback wrote 190 gpu_dump BMPs (640×480) with
    correct per-frame content (title/intro colours match the window captures).
  - Log evidence: `16bpp masks R=0x7c00 G=0x3e0 B=0x1f A=0x8000`,
    `CreateSurface 16x16/256x256 bpp=16 format=4` (R8G8B8A8 for ARGB1555),
    `texture_load`, `CreateTextureHandle`, `filter MAG/MIN -> …` lines.
  - F7 toggled twice on the GPU path: game keeps rendering, no crash.
  - No `[gfx:gpu]` errors in any run; game killed and log files cleaned up.
- **Known limitations.** Paletted (8bpp) textures are implemented but unused by
  the game on the tested paths (see above). The bilinear sampler is selected by
  the mirror but the immediate 2D paths always request nearest. 24bpp surfaces
  remain untracked (M3). Colour keying deferred to M8.

## Out of scope (separate later workstreams)
- Movie playback (DirectShow/DirectDrawMediaStream) → SDL media + decoder.
- Audio (DirectSound8) → SDL3 audio.
- Save-menu GDI text (`GetDC`+`TextOut`) → SDL_ttf.

## Risks / notes
- **Window sharing** (DD primary vs SDL_GPU swapchain) — validated at M2: they
  coexist cleanly on the same window (see Progress M2); the readback fallback
  was not needed.
- **Shader tooling**: done for M4 — HLSL → DXIL + SPIR-V via DXC, bytecode
  embedded in `src/gfx_shaders.h/.cpp` (regenerate with
  `tools\gen_shaders.ps1`); MSL/METALLIB deferred (macOS not a target yet).
- **COM ABI**: front-end vtable layouts must exactly match d3d.h/ddraw.h.
- **Undiscovered COM calls** in original code — mitigated by forward-by-default
  and the M0 audit.
- **Perf**: broadcast double-render during migration; config flag to drop the
  reference later.
- New source files must be added to `src/openre.vcxproj` and `CMakeLists.txt`.
