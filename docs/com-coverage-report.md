# RE2 (bio2 1.10.exe) — COM Interface × Method Coverage Matrix for the MARNI Renderer

**Target**: original `bio2 1.10.exe`, imagebase `0x400000` (IDA Pro database)
**Scope**: every DirectDraw2 / Direct3D2 COM vtable method called by the MARNI renderer creation/teardown path
**Purpose**: exact input for a front-end COM layer reimplementation

---

## 0. Critical header-identification findings (read this first)

The binary was compiled against **DirectX 3 SDK-era custom headers**, NOT the stock DirectX 2 headers. Three things differ from the standard DX2 layout and **must** be respected by the COM front-end:

1. **`D3DRENDERSTATETYPE` is renumbered** (custom d3dtypes.h). The full 50-value table from the IDB type library (`_D3DRENDERSTATETYPE`, til ordinal 81) is in §9.1. Highlights that contradict the standard DX2 numbering:
   - `17 = D3DRENDERSTATE_TEXTUREMAG`, `18 = D3DRENDERSTATE_TEXTUREMIN` (standard DX2: 17=SRCBLEND, 18=DESTBLEND)
   - `19 = SRCBLEND`, `20 = DESTBLEND`, `21 = TEXTUREMAPBLEND`, `22 = CULLMODE`, `23 = ZFUNC`
   - `27 = ALPHABLENDENABLE` (standard: 27 = FOGCOLOR), `29 = SPECULARENABLE`
   - `34 = FOGCOLOR`, `41 = COLORKEYENABLE`, `47 = ZBIAS`, `49 = ANISOTROPY`
2. **`DDSURFACEDESC` is a 108-byte (0x6C) struct** with `ddpfPixelFormat` at **+0x48 (32 bytes)** and `ddsCaps` at **+0x68** (standard DX2: pixel format at +0x48 but 36 bytes, caps at +0x6C). `dwSize` is always 108.
3. **`DDPIXELFORMAT` is 32 bytes** (8 dwords: dwSize, dwFlags, dwFourCC, dwRGBBitCount union, dwRBitMask, dwGBitMask, dwBBitMask, dwRGBAlphaBitMask).
4. **`IDirect3DDevice2` vtable is the DX3-era layout** (33 entries, 132 bytes) — it includes `SetRenderTarget` (+0x3C), `BeginIndexed`/`Vertex`/`Index` (+0x48/+0x4C/+0x50), `DrawPrimitive` (+0x74), `DrawIndexedPrimitive` (+0x78), `SetTransform` (+0x68), `MultiplyTransform` (+0x70). The D3D device returned by `IDirect3D2::CreateDevice` is this DX3-style device.
5. **`DDCAPS` is 364+ bytes** (game sets `dwSize = 380` for both HAL and HEL caps); `dwZBufferBitDepths` sits at **+0x38**.

---

## 1. 0x00406D90 — `Marni::MD3DCreateDevice` (device creation)

```c
signed int __thiscall Marni::MD3DCreateDevice(MARNI *this)
{
    if (this->DirectDevice2) { this->DirectDevice2->lpVtbl->Release(...); this->DirectDevice2 = 0; }
    if (this->Gpu_flg & 0x2000) return 1;                     // software renderer
    CLSID *iid = dword_543B7C[91*Device_cnt] ? &IID_IDirect3DHALDevice
                                             : &IID_IDirect3DRGBDevice;
    error = pDirect3D2->lpVtbl->CreateDevice(pDirect3D2, iid,
                                             this->Surface0.pDDsurface, &this->DirectDevice2);
    if (error) { Marni::Error(); return 0; }
    if (dword_543B80[91*Device_cnt]) {                        // device supports texture formats
        n = D3DEnumTextureFormats(DirectDevice2, 20, formats); // -> EnumTextureFormats
        for each format:
            DDrawDesc2SurfDesc(&desc, &pSDesc);
            if (pSDesc.r_bitcnt + pSDesc.g_bitcnt + pSDesc.b_bitcnt >= 15
                && pSDesc.a_bitcnt
                && !(desc.ddpfPixelFormat.dwFlags & 0x28)) {  // 0x28 = DDPF_PALETTEINDEXED4(0x8)|DDPF_PALETTEINDEXED8(0x20) (old DX2 header flags)
                copy desc into texture-format table; entry.dwFlags = 4103 (0x1007); entry.caps = 67112960 (0x4000100);
            }
    }
    // then converts all existing surface descs via DDrawDesc2SurfDesc
}
```

**COM calls**
| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirect3DDevice2 | Release | +0x08 | existing device |
| IDirect3D2 | **CreateDevice** | **+0x20** | `(pDirect3D2, IID_IDirect3DHALDevice | IID_IDirect3DRGBDevice, Surface0.pDDsurface (IDirectDrawSurface*), &DirectDevice2)` |
| IDirect3DDevice2 | EnumTextureFormats | +0x24 | via `D3DEnumTextureFormats(dev, max=20, out[20])`; callback `cbEnumTextureFormat` (0x406920), context `{count=0, max=20}` |

**Creates/returns**: the D3D device (HAL or RGB) stored in `this->DirectDevice2`; a texture-format table (`this->field_8C78A8..`, count `field_8C78A0`) of DDSURFACEDESC entries (r+g+b bitcount ≥ 15, has alpha, non-paletted, non-FOURCC), each tagged with MARNI format code 0x1007 and flags 0x4000100.

IID addresses in the binary: `IID_IDirect3DHALDevice` @ 0x517304, `IID_IDirect3DRGBDevice` @ 0x517314.

---

## 2. 0x00407020 — `Marni::MD3DCreateZBuffer` (z-buffer surface)

```c
signed int __thiscall Marni::MD3DCreateZBuffer(MARNI *this, int width, int height, _DWORD *pDDsurfaceZ)
{
    memset(pdesc, 0, 108);
    pdesc[0] = 108; pdesc[1] = 1;                       // dwSize, dwFlags = DDSD_CAPS
    Surface0.pDDsurface->lpVtbl->GetSurfaceDesc(..., pdesc);   // probe primary
    memset(&desc, 0, sizeof(desc));
    desc.dwSize = 108;
    desc.dwFlags = 71;                                  // 0x47 = DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH|DDSD_PIXELFORMAT
    desc.dwWidth = width; desc.dwHeight = height;
    desc.ddsCaps.dwCaps = pdesc[26] & 0x4800 | 0x20000; // primary's 0x4800 (VIDEOMEM|LOCALVIDMEM) | DDSCAPS_ZBUFFER
    depth = dword_543B10[91*Device_cnt];                // per-driver zbuffer bit-depth flags (from GetZbufferCaps)
    if (depth & 0x100) desc.anonymous_1.dwMipMapCount = 32;   // union field @ +0x18 = dwZBufferBitDepth
    else if (depth & 0x200) ... = 24;
    else if (depth & 0x400) ... = 16;
    else if (depth & 0x800) ... = 8;  else fail;
    error = pDirectDraw->lpVtbl->CreateSurface(pDirectDraw, &desc, (LPDIRECTDRAWSURFACE*)pDDsurfaceZ, 0);
    if (error) { while (depth > 0x10) { 24->16; 32->24; retry CreateSurface; } }   // depth fallback 32->24->16
    error = Surface0.pDDsurface->lpVtbl->AddAttachedSurface(..., *pDDsurfaceZ);
    GetSurfaceDesc(&desc, z);                           // helper 0x40F170
    this->Gpu_flg |= (desc.ddsCaps.dwCaps >> 9) & 0x20;
    if (fail && *pDDsurfaceZ) { (*(+8))(*pDDsurfaceZ); *pDDsurfaceZ = 0; }        // Release
}
```

**COM calls**
| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirectDrawSurface | GetSurfaceDesc | +0x58 | primary surface; desc `{dwSize=108, dwFlags=DDSD_CAPS}` |
| IDirectDraw2 | **CreateSurface** | **+0x18** | `(pDirectDraw, &desc, &zsurf, 0)` — desc: `dwFlags=0x47`, `ddsCaps=0x20000\|(primaryCaps & 0x4800)`, `dwZBufferBitDepth(+0x18) = 32/24/16/8` (note: depth stuffed into the union field even though `DDSD_ZBUFFERBITDEPTH` (0x80) is **not** set in dwFlags) |
| IDirectDrawSurface | AddAttachedSurface | +0x0C | `(primary, zsurf)` |
| IDirectDrawSurface | GetSurfaceDesc | +0x58 | on zsurf (via `GetSurfaceDesc` helper) |
| IDirectDrawSurface | Release | +0x08 | on failure |

**Creates/returns**: a z-buffer `IDirectDrawSurface*` attached to the primary surface; bit depth from driver caps 32→24→16→8 with 32→24→16 retry on CreateSurface failure.

---

## 3. 0x00405DD0 — `Marni::GetZbufferCaps` (caps probe)

```c
int __thiscall Marni::GetZbufferCaps(MARNI *this)
{
    DDCAPS hal; DDCAPS hel;                              // both dwSize = 380 (0x17C)
    memset both 0; hal.dwSize = 380; hel.dwSize = 380;
    error = pDirectDraw2->lpVtbl->GetCaps(pDirectDraw2, &hal, &hel);
    if (!hal.dwZBufferBitDepths) goto z16;               // +0x38
    if (hal.dwZBufferBitDepths & 0x100) this->Zbuffer_depth = 32;
    else if (& 0x200) = 24;
    else if (& 0x400) = 16;
    else if (& 0x800) = 8; else fail;
    this->field_8C7284 = hal[0x5B];                      // dword at offset 0x16C (vendor-specific; beyond IDB 364-byte def)
    return hal[91];                                      // offset 0x16C
}
```

**COM calls**
| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirectDraw2 | GetCaps | +0x2C | `(pDirectDraw2, &halCaps, &helCaps)`, both `dwSize=380` |

**Probes**: `DDCAPS.dwZBufferBitDepths` (+0x38): `0x100`→32-bit, `0x200`→24-bit, `0x400`→16-bit, `0x800`→8-bit. Stores `Zbuffer_depth` used by 0x407020.

---

## 4. 0x00405EC0 — `Marni::CreateTextureHandle` (texture upload dispatcher)

Large dispatcher; no direct COM calls itself, but it orchestrates the upload path:

```c
if (!Is_gpu_active || !pSrcSurface->Is_open) return 0;
if (software path (Gpu_flg & 0x2000) || (mode & 0x4000)) { ... CPU-only work surface ... }
if (pSrcSurface->width > 256 || height > 256) return 0;   // hard 256x256 texture cap
switch (mode & 0xFFFFDFEB) {
  case 1: case 2:            // text / non-paletted 8-bit
  case 0x22|0x41|0x42|0xC1|0xC2:   // paletted, various alpha combos
  case 0x81|0x82|0xA1|0xA2:  // alpha/4bit combos
      tex = SearchTextureObject1(...);
      MarniSurface2::CreateWork(surf2, w, h, bpp, palbpp, palcount);  // CPU work surface
      vTbl->Blt(...)        // CPU copy (slot +4 -> MarniSurfaceX::vBlt -> MarniSurface2::Blt)
      vTbl->vPalBlt(...)    // palette copy (slot +12) for paletted cases
      slot[3099] = palcnt;
}
slot[1536] = mode;
if (mode & 0x2000) return tex;               // deferred reload
if (Marni::ReloadTexture(this, tex)) { Marni::RequestVideoMemory(this); return tex; }
```

**Creates/returns**: a texture object index (slot in `this`); the actual D3D texture surface is created inside `Marni::ReloadTexture` (§5). The CPU work surface is a `MarniSurface2` (system-memory), later uploaded by `IDirect3DTexture2::Load`.

---

## 5. 0x004033F0 — `Marni::ReloadTexture` (re-lock / re-upload after surface loss)

Four switch groups (1/2 text, 0x22/0x41/0x42, 0x81/0x82/0xA1/0xA2, 0xC1/0xC2) all following the same pattern:

1. `Direct3DSurface::CreateWork(this, ...)` → thunk to `0x40FBE0 DirectDrawSurface::CreateWork` (see §17) — creates the DD texture surface + palette, **`caps byte 1 |= 8`** (i.e. `dwCaps |= 0x800`, the game-header DDSCAPS_TEXTURE).
2. `Direct3DSurface::CreateTextureObject(this)` (§13) — `QueryInterface(IID_IDirect3DTexture2)`.
3. `MarniSurfaceX::vBlt` (0x40F370 → `MarniSurface2::Blt`, CPU copy) from the CPU work surface.
4. `MarniSurfaceX::Load(this, pSrc)` (0x40EE30) → **`IDirect3DTexture2::Load(+0x14)`** `(dstTex, srcTex)`.
5. `sub_40ED20(this, surface2)` (0x40ED20) → **`IDirect3DTexture2::GetHandle(+0x0C)`** `(tex, arg, &handle@this+60)` — grabs the D3D texture handle stored at object+0x3C.
6. `Marni::CompressTexture(...)` — tries FOURCC codes 273 (0x111, DXT1), 257 (0x101), 274 (0x112), 258 (0x102).

Surfaces are chained via a doubly-linked list of texture slots (16-bit forward/backward links) so one CPU source can be shared by several textures. Called from `Marni::RestoreSurfaces` (0x402940) for all 256 slots when any surface reports lost.

**COM calls (via helpers)**
| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirectDraw2 | CreateSurface | +0x18 | see §17 |
| IDirectDraw2 | CreatePalette | +0x14 | see §17 |
| IDirectDrawSurface | SetPalette | +0x7C | see §17 |
| IDirectDrawSurface | SetColorKey | +0x74 | see §17 |
| IDirectDrawSurface | QueryInterface | +0x00 | `IID_IDirect3DTexture2` (0x5172F4) |
| IDirect3DTexture2 | Load | +0x14 | `(dstTexture, srcTexture)` |
| IDirect3DTexture2 | GetHandle | +0x0C | `(texture, srcSurf?, &handle)` |

**Creates/returns**: re-uploads the texture (DD surface + IDirect3DTexture2) and returns the D3D texture handle.

---

## 6. 0x00414750 — `MarniSurface2::CreateWork` (CPU work surface)

**No COM calls.** Pure CPU: allocates `pBitmap`/`pPalette` arrays, sets pixel-format masks for 555 (`a=15,r=10,g=5,b=5`) or 8888 (`a=24,r=16,g=8,b=0`). Creates the system-memory surface that later gets uploaded by `Load`.

---

## 7. 0x00412BD0 — `MarniSurface2::vFill` (CPU fill)

**No direct COM.** Locks via `vTbl->Lock` (for DD-backed surfaces this dispatches to `MarniSurfaceX::vLock` = DD `Lock`), writes pixels/memset in the CPU buffer, unlocks. Used by the software renderer and for CPU-side fills.

---

## 8. 0x00412ED0 — `MarniBits::SetAddress`

**No COM.** CPU-only: stores bitmap/palette pointers and pixel-format desc into the surface struct.

---

## 9. 0x00412580 — `MarniSurface2::Blt` (CPU blit)

**No DirectDraw::Blt here.** Pure CPU blit with bilinear-ish scaling (`__ftol` fixed-point); calls `vTbl->Lock`/`vTbl->Unlock` on source and dest surfaces. This is the routine used by the texture-upload path (via `MarniSurfaceX::vBlt` 0x40F370) to copy CPU pixels into the system-memory work surface.

---

## 10. 0x0040F580 — `MarniSurfaceY::vRelease` (DD-backed surface release)

```c
if (Is_open) {
    if (pDDsurface)  { (*(+8))(pDDsurface); pDDsurface = 0; }        // IDirectDrawSurface::Release
    if (pDDpalette) for (i = 0; i < pal_cnt; ++i) {
        if (pDDpalette[i]) { (*(+8))(pDDpalette[i]); pDDpalette[i] = 0; }  // IDirectDrawPalette::Release
    }
    delete pDDpalette array;
}
MarniSurface2::vRelease(this);        // CPU cleanup (0x414A40)
```

**COM calls**
| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirectDrawSurface | Release | +0x08 | the surface (field +0x30) |
| IDirectDrawPalette | Release | +0x08 | each palette in array (field +0x34), `pal_cnt` entries |

**Frees**: DD surface + all palettes, then CPU buffers.

---

## 11. 0x00414A40 — `MarniSurface2::vRelease` (CPU surface release)

**No COM.** If `Is_locked`, calls `vTbl->Unlock(this)` (dispatches to DD Unlock for DD-backed surfaces); frees `pBitmap`/`pPalette`; zeroes the whole descriptor (masks/shifts, pitch, dims, flags).

---

## 12. 0x0040E770 — `Marni::SetFiltering` (texture filtering state)

Disasm-verified — 4 calls to **`IDirect3DDevice2::SetRenderState(+0x5C)`**:

| call | state (custom enum) | value | condition |
|---|---|---|---|
| 1 | 3 = `D3DRENDERSTATE_TEXTUREADDRESS` | 1 | always |
| 2 | 0x11 = 17 = `D3DRENDERSTATE_TEXTUREMAG` | 2 (bilinear) or 1 | `arg0 && marni_config.Bilinear` |
| 3 | 0x12 = 18 = `D3DRENDERSTATE_TEXTUREMIN` | 6 (bilinear) or 1 | same condition (value pushed at 0x40E7A5/0x40E7B9) |
| 4 | 4 = `D3DRENDERSTATE_TEXTUREPERSPECTIVE` | 1 or 0 | `arg0 && marni_config.PersWarp` |

Filter values: 1 = `D3DFILTER_NEAREST`, 2 = `D3DFILTER_LINEAR`, 6 = `D3DFILTER_LINEARMIPLINEAR`.
**Note the custom numbering**: 17/18 are TEXTUREMAG/TEXTUREMIN here (in stock DX2 they would be SRCBLEND/DESTBLEND).

---

## 13. 0x0040ECA0 — `Direct3DSurface::CreateTextureObject` (texture object)

```c
if (!IsOpen) return 0;
if (DDtexture) { DDtexture->lpVtbl->Release(...); DDtexture = 0; }
if (pDDsurface->lpVtbl->QueryInterface(pDDsurface, &IID_IDirect3DTexture2, &this->DDtexture)) {
    Marni::Error(); this->vTbl->Release(this); return 0;
}
return 1;
```

**COM calls**
| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirect3DTexture2 | Release | +0x08 | existing texture |
| IDirectDrawSurface | QueryInterface | +0x00 | `(pDDsurface, IID_IDirect3DTexture2 @0x5172F4, &DDtexture)` |

**Creates/returns**: `IDirect3DTexture2*` from the surface.

---

## 14. 0x00411360 — `ScreenFont::Trans` (font raster on a surface)

**No direct COM.** CPU font blit: `pSurface->vTbl->Lock(surface,0,0)` / `vTbl->Unlock(surface)` around 8×8 glyph bit-blits into the surface's locked buffer (`MarniSurface::SetCurrentColor` per pixel). For DD-backed surfaces the Lock/Unlock dispatch to `MarniSurfaceX::vLock`/`vUnlock` (DD Lock/Unlock + palette sync).

---

## 15. 0x0040F0F0 — `Marni::EnumDisplayMode`

```c
ctx = { max, res }; ctx.count = *cnt_found;
if (a1->lpVtbl->EnumDisplayModes(a1, 0, 0, &ctx, cbEnumDispModes)) { MarniOut(); *cnt_found = 0; Marni::Error(); }
*cnt_found = ctx.count;
```

**COM calls**
| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirectDraw2 | **EnumDisplayModes** | **+0x20** | `(dd2, dwFlags=0, lpDDSurfaceDesc=NULL, context={count, max, res[]}, cbEnumDispModes @0x40F090)` |

Callback copies each mode's width/height/bpp into `MARNI_RES` until `max`.

---

## 16. 0x0040F170 — `GetSurfaceDesc` (helper)

```c
memset(a1, 0, 108); a1->dwSize = 108;
return a2->lpVtbl->GetSurfaceDesc(a2, a1);
```

| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirectDrawSurface | GetSurfaceDesc | +0x58 | `(surface, desc)` with `dwSize=108` |

---

## 17. 0x0040FBE0 — `DirectDrawSurface::CreateWork` (real DD surface creator)

Called via thunk `0x40EC90 Direct3DSurface::CreateWork`. This is where the COM surface creation actually happens:

```c
int __thiscall DirectDrawSurface::CreateWork(MARNI_SURFACEX *this, IDirectDraw *pDDsurf, DDSURFACEDESC *pDesc, int a4)
{
    this->vTbl->Release(this);
    // round w/h up to 8/16/32/64/128/256
    dwWidth  = pDesc->dwWidth  -> 8,16,32,64,128,256 (nearest power-of-2 band, 256 cap)
    dwHeight = pDesc->dwHeight -> same
    memcpy(desc, pDesc, 108); desc.dwHeight = rounded; desc.dwWidth = rounded;
    if (pDDsurf->lpVtbl->CreateSurface(pDDsurf, &desc, &this->pDDsurface, 0)) { fail }
    GetSurfaceDesc(&a1, pDDsurface);
    this->Is_vmem = (BYTE1(a1.ddsCaps.dwCaps) & 0x40);      // caps & 0x4000 = DDSCAPS_VIDEOMEMORY
    colorkey = {0, 0};
    if (pDDsurface->lpVtbl->SetColorKey(pDDsurface, 8 /*DDCKEY_SRCBLT*/, &colorkey)) { fail }
    this->bpp = a1.ddpfPixelFormat.dwRGBBitCount;
    if (pf.dwFlags & 8)  { Is_paletted=1; palbpp=32; v13 = 1; }              // old-header DDPF_PALETTEINDEXED4 (0x8) -> 8-bit-cap palette (1)
    if (pf.dwFlags & 0x20) { Is_paletted=1; palbpp=32; v13 = 68; }           // DDPF_PALETTEINDEXED8 (0x20) -> 0x44 = 8BIT|ALPHA palettes
    if (!Is_paletted) { DDrawDesc2SurfDesc(...); }
    else {
        pDDpalette = new IDirectDrawPalette*[field_20];
        for (i = 0; i < field_20; ++i)
            pDDsurf->lpVtbl->CreatePalette(pDDsurf, v13, (LPPALETTEENTRY)v22 /*all-zero 256-entry table*/,
                                           &pDDpalette[i], 0);
        pDDsurface->lpVtbl->SetPalette(pDDsurface, pDDpalette[0]);
        this->desc = 8888 (a24 r16 g8 b0, all masks 0xFFFFFFFF);
    }
    width/pitch/height from desc; IsOpen = 1;
    sub_40FFD0(this);        // transparency probe: GetSurfaceDesc(DDSD_PIXELFORMAT) + Lock/Unlock,
                             // sets field_2C 'has_transparency' if any non-zero alpha bits
}
```

**COM calls**
| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirectDraw2 | **CreateSurface** | **+0x18** | `(pDDsurf, &desc, &pDDsurface, 0)` — desc from caller (108-byte, w/h rounded up to 8..256) |
| IDirectDrawSurface | GetSurfaceDesc | +0x58 | to read back pixel format / caps / pitch |
| IDirectDrawSurface | **SetColorKey** | **+0x74** | `(surface, dwFlags=8 /*DDCKEY_SRCBLT*/, &{0,0})` |
| IDirectDraw2 | **CreatePalette** | **+0x14** | `(pDDsurf, dwFlags, paletteEntries(zeros), &pal, 0)` — flags: `1` (DDPCAPS_8BIT), `0x44` (8-bit+alpha), or `0x20` (DDPCAPS_4BIT); one palette per `field_20` slot |
| IDirectDrawSurface | **SetPalette** | **+0x7C** | `(surface, pDDpalette[0])` |
| IDirectDrawSurface | GetSurfaceDesc / Lock / Unlock | +0x58/+0x64/+0x80 | in sub_40FFD0 transparency probe |

**Creates/returns**: DD surface (texture), 0..N palettes, pixel-format descriptor, transparency flag.

---

## 18. Lock / Unlock paths (the 0x51737C and 0x5173B0 vtable entries)

The MARNI internal surface vtable slot order is: `[0]=Fill, [1]=Blt, [2]=Null, [3]=PalBlt, [4]=Lock, [5]=Unlock, [6]=PalLock, [7]=PalUnlock, [8]=Release`.

**0x51737C — `MarniSurfaceX/Y` vtable (DD-backed surfaces)** (user asked for 0x51737C; the table *starts* here, 0x517380 is the `Blt` slot):

| slot | addr | function |
|---|---|---|
| 0 | 0x51737C | 0x40F380 `MarniSurfaceX::vFill` |
| 1 | 0x517380 | 0x40F370 `MarniSurfaceX::vBlt` → `MarniSurface2::Blt` (CPU) |
| 2 | 0x517384 | 0x4123C0 vNull |
| 3 | 0x517388 | 0x4123D0 vPalBlt |
| 4 | 0x51738C | **0x40F790 `MarniSurfaceX::vLock`** |
| 5 | 0x517390 | **0x40FAD0 `MarniSurfaceX::vUnlock`** |
| 6 | 0x517394 | 0x40F600 vPalLock |
| 7 | 0x517398 | 0x40F9C0 vPalUnlock |
| 8 | 0x51739C | 0x40F580 `MarniSurfaceY::vRelease` |

**0x40F790 `MarniSurfaceX::vLock`** — the real DD lock:
```c
memset(&desc, 0, 108); desc.dwSize = 108;
if (pDDsurface->lpVtbl->Lock(pDDsurface, 0, &desc, 1 /*DDLOCK_WAIT*/, 0)) return 0;
this->pBitmap = desc.lpSurface; width/height/pitch/bpp = desc...;
if (paletted) {
    pPalette = new DWORD[pal_cnt * (1 << bpp)];
    for each pal i:
        pDDpalette[i]->lpVtbl->GetEntries(pDDpalette[i], 0, 0, 1 << bpp, entries);  // flags=0, base=0, count=1<<bpp
        // expand PALETTEENTRY -> 32-bit (bgr -> rgba via masks)
}
```

| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirectDrawSurface | **Lock** | **+0x64** | `(surface, 0 /*rect*/, &desc, 1 /*DDLOCK_WAIT*/, 0 /*hDestSurface*/)` |
| IDirectDrawPalette | GetEntries | +0x10 | `(pal, 0, 0, 1<<bpp, entries[1024])` per palette |

**0x40FAD0 `MarniSurfaceX::vUnlock`**:
```c
pDDsurface->lpVtbl->Unlock(pDDsurface, 0);
if (paletted) for each pal:
    // convert 32-bit pPalette back to PALETTEENTRY
    pDDpalette[i]->lpVtbl->SetEntries(pDDpalette[i], 0, 0, 1 << bpp, entries);
```

| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirectDrawSurface | **Unlock** | **+0x80** | `(surface, 0)` |
| IDirectDrawPalette | **SetEntries** | **+0x18** | `(pal, 0, 0, count, entries)` per palette |

**0x40F380 `MarniSurfaceX::vFill`** (DD color fill):
```c
SetRect(&rc, ...); ddbltfx[0] = 100;                       // DDBLTFX.dwSize = 100 (0x64)
ddbltfx[20] = ((b_mask & (color >> (8 - b_bitcnt))) << b_shift)
            | ((r_mask & (BYTE2(color) >> (8 - r_bitcnt))) << r_shift)
            | ((g_mask & (BYTE1(color) >> (8 - g_bitcnt))) << g_shift);
pDDsurface->lpVtbl->Blt(pDDsurface, &rc, 0, 0, 16778240, &ddbltfx);   // 0x1000200 = DDBLT_WAIT | DDBLT_COLORFILL
```

| Interface | Method | vtable | Args |
|---|---|---|---|
| IDirectDrawSurface | **Blt** | **+0x14** | `(surface, &dstRect, 0, 0, 0x1000200 /*DDBLT_WAIT\|DDBLT_COLORFILL*/, &fx{dwSize=100, fillColor})` |

**0x5173B0 — `MarniSurface2` vtable (CPU surfaces)** (user asked for 0x5173B0; Lock/Unlock here are CPU-only):

| slot | addr | function |
|---|---|---|
| 0 | 0x5173B0 | 0x412BD0 `MarniSurface2::vFill` (CPU fill) |
| 1 | 0x5173B4 | 0x412580 `MarniSurface2::Blt` (CPU blit) |
| 2 | 0x5173B8 | 0x4123C0 vNull |
| 3 | 0x5173BC | 0x4123D0 vPalBlt |
| 4 | 0x5173C0 | 0x412FC0 `MarniSurface2::Lock` |
| 5 | 0x5173C4 | 0x413040 `MarniSurface2::Unlock` |
| 6 | 0x5173C8 | 0x413050 vPalLock |
| 7 | 0x5173CC | 0x4130C0 vPalUnlock |
| 8 | 0x5173D0 | 0x414A40 `MarniSurface2::vRelease` |

**0x412FC0 Lock / 0x413040 Unlock** — **no COM**: hand out `pBitmap`/`pPalette` pointers, set/clear `Is_locked`. (These are the "lock/unlock" entries the audit asked about; the actual DDraw lock lives on the 0x51737C table.)

---

## 19. Reimplemented-but-hooked functions (original behavior)

### 0x0040F1A0 — `Marni::MDDCreateDirect3D` (DirectDraw creation)
```c
if (a1) { DirectDrawEnumerateA(cbEnumDDraw @0x40F250, &lpDD); *ret = 0; }
else    { DirectDrawCreate(NULL, &lpDD, NULL); *ret = 1; }
*ppDD = lpDD;
```
**Not COM** (exported functions): `DirectDrawEnumerateA` (import @0x50C7A6), `DirectDrawCreate` (import @0x50C7A0). `cbEnumDDraw` (0x40F250) creates each enumerated GUID via `DirectDrawCreate(guid, &dd, 0)`, calls **`IDirectDraw::GetCaps(+0x2C)`** `(dd, &caps{dwSize=380})`, checks `caps.dwCaps & 1` (hardware 3D in this header = `DDCAPS_3D`), keeps it if hardware else **`IDirectDraw::Release(+0x08)`**.

### 0x00406860 — `QueryDDraw2`
```c
return pDdraw->lpVtbl->QueryInterface(pDdraw, &IID_IDirectDraw2 @0x517334, (LPVOID*)pDdraw2);
```
**IDirectDraw::QueryInterface (+0x00)** → IDirectDraw2.

### 0x00407440 — `CreateDirect3D`
```c
result = pDirectDraw2->lpVtbl->QueryInterface(pDirectDraw2, &IID_IDirect3D2 @0x517324, &this->pDirect3D2);
```
**IDirectDraw2::QueryInterface (+0x00)** → IDirect3D2.

### 0x00407340 — `MD3DEnumDrivers`
```c
error = pDirect3D2->lpVtbl->EnumDevices(pDirect3D2, cbEnumDriver @0x407290, 0);
// score each driver entry: hardware(caps&1) + bpp-match(D3DIBPPToDDBD(Bpp) & bitDepth) + present flags
// pick highest score -> this->Device_cnt
```
**IDirect3D2::EnumDevices (+0x0C)** with callback context NULL. `cbEnumDriver` fills the per-driver table `dword_543B10` (91 dwords per driver: GUID, description, HAL caps, bpp support, etc.).

---

## 20. Additional render-path functions (SetRenderState / primitive drawing)

These confirm the render-state constants and the primitive-drawing entry points used across the whole renderer:

**`Marni::do_render` (0x404E40)** — frame loop:
- `IDirect3DDevice2::BeginScene (+0x28)`, `EndScene (+0x2C)`
- `SetRenderState`: `ANISOTROPY(49)=0`, `EDGEANTIALIAS(40)=0`, `ANTIALIAS(2)=0`, `SUBPIXEL(31)=0`, `LASTPIXEL(16)=1`
- `IDirect3DDevice2::GetStats (+0x14)` with `D3DSTATS {dwSize=24, dwTrianglesDrawn(+4), dwLinesDrawn(+8)}` — frame stats.

**`Marni::TransSprPoly` (0x40DF70)**:
- `SetCurrentViewport (+0x34)` with `pViewport`
- `SetRenderState`: `TEXTUREHANDLE(1)=texHandle` (from texture object +0x3C), `ZWRITEENABLE(14)=0/1`, `ZENABLE(7)=1`, `ZFUNC(23)=4` (D3DCMPFUNC_GREATEREQUAL), `COLORKEYENABLE(41)=0`, `SHADEMODE(9)=2`, `CULLMODE(22)=1` (NONE), `SPECULARENABLE(29)=0/1`, `ALPHABLENDENABLE(27)=0`, `TEXTUREMAPBLEND(21)=4` (MODULATEALPHA)
- `Marni::SetFiltering(this, 0)`
- `DrawPrimitive (+0x74)`: `(dev, D3DPT_TRIANGLESTRIP=5, D3DVT_TLVERTEX=3, verts, count, flags=1)`

**`Marni::TransObjectNgTin3_VinsNins` (0x407690)**:
- `SetCurrentViewport (+0x34)`
- `SetRenderState`: `TEXTUREPERSPECTIVE(4)=1`, `ZWRITEENABLE(14)=1`, `SHADEMODE(9)=2`, `COLORKEYENABLE(41)=0`, `SPECULARENABLE(29)=0`, `ZENABLE(7)=1`, `ZFUNC(23)=4`, `TEXTUREHANDLE(1)=handle`, `CULLMODE(22)=((~flags & 0x40000000 | 0x20000000) >> 29)` → 1 (NONE) or 3 (CCW), `ALPHABLENDENABLE(27)=0`
- `Marni::SetFiltering(this, 1)`
- `DrawPrimitive (+0x74)`: `(dev, D3DPT_TRIANGLELIST=4, D3DVT_TLVERTEX=3, verts, count, flags=1)`

**`sub_40EAF0` (sprite setup)**:
- `SetCurrentViewport (+0x34)`
- `SetRenderState`: `ALPHABLENDENABLE(27)=1`, `ZENABLE(7)=1`, `COLORKEYENABLE(41)=0`, `TEXTUREMAPBLEND(21)=4`, `TEXTUREHANDLE(1)=obj+116`, `ZWRITEENABLE(14)=0`, `ZFUNC(23)=obj+128`, `SHADEMODE(9)=obj+120`, `CULLMODE(22)=obj+124`, `SPECULARENABLE(29)=0`, `SRCBLEND(19)=obj+108`, `DESTBLEND(20)=obj+112`
- `DrawPrimitive (+0x74)`: `(dev, 4 /*TRIANGLELIST*/, 3 /*TLVERTEX*/, obj, 3, 1)`

**Other device methods seen elsewhere** (for coverage): `SetRenderTarget (+0x3C)` (MarniMovie::Open 0x414D8E/0x414DA1, SsSetVol 0x434CB6), `SwapTextureHandles (+0x10)` (31 sites incl. software renderer emulation 0x409000-0x40C000 region — texture-animation trick), `Begin (+0x44)` / `BeginIndexed (+0x48)` / `Vertex (+0x4C)` / `Index (+0x50)` / `End (+0x54)` (legacy immediate-mode path, e.g. 0x404071, 0x406665, DDSetCoopLevel), `SetTransform (+0x68)` / `MultiplyTransform (+0x70)` (Marni::InitAll 0x404361/0x40460E, MergeSurfaceGDI 0x431452).

**`Marni::RestoreSurfaces` (0x402940)**:
- `IDirectDrawSurface::IsLost (+0x60)` on Surface2/Surface0/SurfaceZBuffer; if result == `-2005532222` (`0x88890002` = DDERR_SURFACELOST) → `IDirectDrawSurface::Restore (+0x6C)`.
- Scans all 256 texture slots; for each, `pDDsurface->IsLost()`; if any lost → `Marni::ReloadTexture` for each slot (re-upload via Load/GetHandle).

**`Marni::Clear` (0x00404D20)** (user list):
- `Marni::RestoreSurfaces(this)`
- `D3DRECT {0, 0, XSize, YSize}` (v7[0..3])
- Hardware: `IDirect3DViewport2::Clear (+0x30)` `(pViewport, 1 /*count*/, &rect, 2 /*D3DCLEAR_ZBUFFER*/)` or `(…, 3 /*D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER*/)` — flags 3 when `Gpu_flg & 8` (backbuffer clear), else 2.
- Software (`Gpu_flg & 0x2000`): `Surface0.vTbl->Fill(0, ambientColor, 0)`.

---

## 21. Summary — COM method coverage per interface (deduped)

**IDirectDraw** (created via `DirectDrawCreate`, QI'd to IDirectDraw2)
| Method | vtable | Sites |
|---|---|---|
| QueryInterface | +0x00 | QueryDDraw2 (0x406860) |
| GetCaps | +0x2C | cbEnumDDraw (0x40F250), inside MDDCreateDirect3D |
| Release | +0x08 | cbEnumDDraw (0x40F250) |

**IDirectDraw2**
| Method | vtable | Sites |
|---|---|---|
| QueryInterface | +0x00 | CreateDirect3D (0x407440) |
| CreateSurface | +0x18 | MD3DCreateZBuffer (0x407020), DirectDrawSurface::CreateWork (0x40FBE0), ReloadTexture path |
| CreatePalette | +0x14 | DirectDrawSurface::CreateWork (0x40FBE0) |
| EnumDisplayModes | +0x20 | EnumDisplayMode (0x40F0F0) |
| GetCaps | +0x2C | GetZbufferCaps (0x405DD0) |

**IDirectDrawSurface**
| Method | vtable | Sites |
|---|---|---|
| QueryInterface | +0x00 | CreateTextureObject (0x40ECA0) — IID_IDirect3DTexture2 |
| Release | +0x08 | MarniSurfaceY::vRelease (0x40F580), MD3DCreateZBuffer fail path |
| AddAttachedSurface | +0x0C | MD3DCreateZBuffer (0x407020) |
| Blt | +0x14 | MarniSurfaceX::vFill (0x40F380) — `0x1000200` (DDBLT_WAIT\|DDBLT_COLORFILL), fx.dwSize=100 |
| GetSurfaceDesc | +0x58 | MD3DCreateZBuffer, DirectDrawSurface::CreateWork, GetSurfaceDesc helper (0x40F170), sub_40FFD0 |
| IsLost | +0x60 | RestoreSurfaces (0x402940) — `== 0x88890002` |
| Lock | +0x64 | MarniSurfaceX::vLock (0x40F790) — flags `1` (DDLOCK_WAIT), rect 0, hDest 0 |
| Restore | +0x6C | RestoreSurfaces (0x402940) |
| SetColorKey | +0x74 | DirectDrawSurface::CreateWork — flags `8` (DDCKEY_SRCBLT), key `{0,0}` |
| SetPalette | +0x7C | DirectDrawSurface::CreateWork |
| Unlock | +0x80 | MarniSurfaceX::vUnlock (0x40FAD0) — arg 0 |

**IDirectDrawPalette**
| Method | vtable | Sites |
|---|---|---|
| Release | +0x08 | MarniSurfaceY::vRelease (0x40F580) |
| GetEntries | +0x10 | MarniSurfaceX::vLock (0x40F790) — `(flags=0, base=0, count=1<<bpp)` |
| SetEntries | +0x18 | MarniSurfaceX::vUnlock (0x40FAD0) — `(flags=0, base=0, count=1<<bpp)` |

**IDirect3D2**
| Method | vtable | Sites |
|---|---|---|
| EnumDevices | +0x0C | MD3DEnumDrivers (0x407340), cbEnumDriver (0x407290) |
| CreateDevice | +0x20 | MD3DCreateDevice (0x406D90) — IID_IDirect3DHALDevice / IID_IDirect3DRGBDevice, render-target = primary surface |

**IDirect3DDevice2** (DX3-era layout, 33 entries)
| Method | vtable | Sites |
|---|---|---|
| Release | +0x08 | MD3DCreateDevice (0x406D90) |
| SwapTextureHandles | +0x10 | software-emulation texture animation (0x409219, 0x40960B, 0x409CA3, 0x40A273, 0x40C726, …) |
| GetStats | +0x14 | do_render (0x404E45) — D3DSTATS dwSize=24 |
| EnumTextureFormats | +0x24 | D3DEnumTextureFormats (0x406880), cbEnumTextureFormat (0x406920) |
| BeginScene | +0x28 | do_render (0x404E7C), MarniMovie::UpdateWindow/Seek, Marni::PrepareVideo, Marni::Init |
| EndScene | +0x2C | do_render, Marni::Flip, Marni::Init, Marni::RequestVideoMemory, GetZbufferCaps (0x405E1B), cbEnumDDraw, SsCreateBuffer |
| SetCurrentViewport | +0x34 | TransSprPoly, TransObjectNgTin3_VinsNins, sub_40EAF0 — `(dev, pViewport /*IDirect3DViewport2* */)` |
| SetRenderTarget | +0x3C | MarniMovie::Open (0x414D8E, 0x414DA1), SsSetVol (0x434CB6) — `(dev, surface, flags)` |
| Begin | +0x44 | Marni::InitAll (0x404071) |
| BeginIndexed | +0x48 | SsPlay/SsStopAll (movie audio path) |
| Vertex | +0x4C | immediate-mode path |
| Index | +0x50 | immediate-mode path |
| End | +0x54 | Marni::InitAll (0x404071), Marni::Resize (0x406665) |
| GetRenderState | +0x58 | (see GetStats note: +0x58 surface GetSurfaceDesc sites are the surface interface, not the device) |
| **SetRenderState** | **+0x5C** | **44 call sites** — full constant map in §9.1 |
| SetLightState | +0x64 | (MarniSurfaceX::vLock 0x40F825 and sub_40FFD0 0x410053 hit a +0x64 slot on the **surface** interface — these are `IDirectDrawSurface::Lock`, slot +0x64, not device SetLightState) |
| SetTransform | +0x68 | Marni::InitAll (0x404361), MergeSurfaceGDI (0x431452) |
| MultiplyTransform | +0x70 | Marni::InitAll (0x404361, 0x40460E) |
| DrawPrimitive | +0x74 | TransObjectNgTin3_VinsNins (0x4080D5), TransSprPoly (0x40E659), sub_40EAF0 (0x40EBFE), MarniMovie::Open (0x414EC9) — `(dev, D3DPT_TRIANGLELIST=4 | D3DPT_TRIANGLESTRIP=5, D3DVT_TLVERTEX=3, verts, count, flags=1 /*D3DDP_DONOTCLIP*/)` |
| DrawIndexedPrimitive | +0x78 | MarniMovie::UpdateWindow (0x414BCC) |

**IDirect3DTexture2**
| Method | vtable | Sites |
|---|---|---|
| Release | +0x08 | CreateTextureObject (0x40ECA0) |
| GetHandle | +0x0C | sub_40ED20 (ReloadTexture path) |
| Load | +0x14 | MarniSurfaceX::Load (0x40EE30) — `(dst, src)` |

**IDirect3DViewport2**
| Method | vtable | Sites |
|---|---|---|
| Clear | +0x30 | Marni::Clear (0x404D20) — `(viewport, 1, &D3DRECT{0,0,XSize,YSize}, 2 | 3)` |

**Non-COM exports** used: `DirectDrawCreate`, `DirectDrawEnumerateA`.

---

## 9.1 Appendix — custom `_D3DRENDERSTATETYPE` (from IDB til, ordinal 81)

| # | Name | # | Name | # | Name |
|---|---|---|---|---|---|
| 1 | TEXTUREHANDLE | 19 | SRCBLEND | 39 | STIPPLEENABLE |
| 2 | ANTIALIAS | 20 | DESTBLEND | 40 | EDGEANTIALIAS |
| 3 | TEXTUREADDRESS | 21 | TEXTUREMAPBLEND | 41 | COLORKEYENABLE |
| 4 | TEXTUREPERSPECTIVE | 22 | CULLMODE | 43 | BORDERCOLOR |
| 5 | WRAPU | 23 | ZFUNC | 44 | TEXTUREADDRESSU |
| 6 | WRAPV | 24 | ALPHAREF | 45 | TEXTUREADDRESSV |
| 7 | ZENABLE | 25 | ALPHAFUNC | 46 | MIPMAPLODBIAS |
| 8 | FILLMODE | 26 | DITHERENABLE | 47 | ZBIAS |
| 9 | SHADEMODE | 27 | ALPHABLENDENABLE | 48 | RANGEFOGENABLE |
| 10 | LINEPATTERN | 28 | FOGENABLE | 49 | ANISOTROPY |
| 11 | MONOENABLE | 29 | SPECULARENABLE | 50 | FLUSHBATCH |
| 12 | ROP2 | 30 | ZVISIBLE | 64..95 | STIPPLEPATTERN00..31 |
| 13 | PLANEMASK | 34 | FOGCOLOR | | |
| 14 | ZWRITEENABLE | 35 | FOGTABLEMODE | | |
| 15 | ALPHATESTENABLE | 36 | FOGTABLESTART | | |
| 16 | LASTPIXEL | 37 | FOGTABLEEND | | |
| 17 | **TEXTUREMAG** | 38 | FOGTABLEDENSITY | | |
| 18 | **TEXTUREMIN** | | | | |

Values seen in the audited functions: `1` (TEXTUREHANDLE), `2` (ANTIALIAS=0), `3` (TEXTUREADDRESS=1), `4` (TEXTUREPERSPECTIVE=1/0), `7` (ZENABLE=1), `9` (SHADEMODE=2), `14` (ZWRITEENABLE=0/1), `16` (LASTPIXEL=1), `17` (TEXTUREMAG=2/1), `18` (TEXTUREMIN=6/1), `19` (SRCBLEND=obj), `20` (DESTBLEND=obj), `21` (TEXTUREMAPBLEND=4), `22` (CULLMODE=1/3/obj), `23` (ZFUNC=4), `27` (ALPHABLENDENABLE=0/1), `29` (SPECULARENABLE=0/1), `31` (SUBPIXEL=0), `40` (EDGEANTIALIAS=0), `41` (COLORKEYENABLE=0), `49` (ANISOTROPY=0).

### Appendix — constants reference
- **DDSD** flags: `1`=CAPS, `2`=HEIGHT, `4`=WIDTH, `0x40`=PIXELFORMAT, `0x80`=ZBUFFERBITDEPTH, `0x1000`=PITCH. zbuffer desc uses `0x47`.
- **DDSCAPS** (custom header, raw values from code): `0x800`=TEXTURE (set via `caps byte1 |= 8` in ReloadTexture), `0x2000`=COMPLEX?, `0x4000`=VIDEOMEMORY (Is_vmem probe), `0x800`/`0x4000`=LOCALVIDMEM/VIDEOMEMORY in zbuffer caps mask `0x4800`, `0x20000`=ZBUFFER.
- **DDPIXELFORMAT flags** (old DX2 header): `0x1`=ALPHAPIXELS, `0x8`=PALETTEINDEXED4, `0x20`=PALETTEINDEXED8, `0x40`=RGB. (texture-format filter mask `0x28` = 4-bit + 8-bit paletted.)
- **DDLOCK**: `1` = DDLOCK_WAIT.
- **DDBLT**: `0x1000200` = DDBLT_WAIT | DDBLT_COLORFILL; DDBLTFX.dwSize = `100` (0x64), fill color at fx+0x50.
- **DDCKEY**: `8` = DDCKEY_SRCBLT.
- **DDPCAPS**: `1` = DDPCAPS_8BIT, `0x20` = DDPCAPS_4BIT, `0x44` = 8-bit + alpha variant.
- **D3DCLEAR**: `1`=TARGET, `2`=ZBUFFER, `4`=STENCIL; game uses `2`/`3`.
- **D3DPT**: `4`=TRIANGLELIST, `5`=TRIANGLESTRIP. **D3DVT**: `3` = D3DVT_TLVERTEX. **D3DDP**: `1` = D3DDP_DONOTCLIP (last DrawPrimitive arg).
- **D3DCMPFUNC**: 4 = GREATEREQUAL (ZFUNC value used everywhere).
- **D3DFILTER**: 1=NEAREST, 2=LINEAR, 6=LINEARMIPLINEAR (bilinear SetFiltering).
- **D3DTBLEND (TEXTUREMAPBLEND)**: 4 = MODULATEALPHA (used by sprites).
- **Error sentinel**: `0x88890002` = DDERR_SURFACELOST (IsLost check).

### Appendix — struct sizes/offsets used by the game headers
- `DDSURFACEDESC`: total **108 bytes**; dwFlags +4, dwHeight +8, dwWidth +0xC, lPitch +0x10, dwZBufferBitDepth/dwMipMapCount +0x18, lpSurface +0x24, ddpfPixelFormat +0x48 (32 bytes), ddsCaps +0x68.
- `DDCAPS`: `dwSize=380`; `dwZBufferBitDepths` +0x38.
- `D3DSTATS`: dwSize=24; dwTrianglesDrawn +4, dwLinesDrawn +8.
- `DDPIXELFORMAT`: 32 bytes; dwFlags +4, dwFourCC +8, dwRGBBitCount +0xC, RGB masks +0x10/+0x14/+0x18, alpha mask +0x1C.
