# RE1 RDT -> RE2 RDT mapping

Ground truth: `biohazard-utils` (`Rdt1.cs`, `Rdt2.cs`, `Rdt2.Builder.cs`, the
`MeshConverter` TMD->MD1 path) plus the RE1 Ghidra DB (`re1-ghidra` MCP) for
anything ambiguous. Status: room shell + embedded object models verified
byte-identical against biohazard-utils; scripts/enemies/ESP deferred.

## Containers

| | RE1 (`Rdt1`) | RE2 (`Rdt2`/engine) |
|---|---|---|
| Header | 6 bytes @0x00 (`nSprite,nCut,nOmodel,nItem,nDoor,nRoom_at`) | 8 bytes @0x00 (`RdtHeader`) |
| LIT | @0x06, 0x42 bytes | offset slot 9 |
| Offset table | 19 x u32 @0x48 | 23 x u32 @0x08 |
| Cameras (RID) | @0x94, `Rdt1Camera` 0x28 ea | offset slot 7, `RdtCamera` 0x20 ea |

## RE1 offset slots

| # | RE1 chunk | RE2 slot | Status |
|---|---|---|---|
| 0 | zone.rvd (RVD) | 8 | converted: camera switches -> VCuts |
| 1 | collision.sca (SCA) | 6 | copied |
| 2 | embedded object model table | 10 (MODELS) | converted: TMD -> MD1 |
| 3 | embedded item model table | - | skipped (out of scope) |
| 4 | block.blk (BLK) | 12 | copied |
| 5 | floor.flr (FLR) | 11 | copied |
| 6 | init.scd | 16 (SCD_INIT) | stub (empty proc list) |
| 7 | main.scd | 17 (SCD_MAIN) | stub |
| 8 | events.scd | - | skipped |
| 9 | animation.emr | - | skipped |
| 10 | animation.emd | - | skipped |
| 11 | message.msg (MSG) | 13 (MSG_JA) | copied |
| 12 | embedded item PIX table | - | skipped |
| 13 | ESP IDs | 18 | skipped (empty) |
| 14 | embedded ESP EFF table | 19 | skipped |
| 15 | embedded ESP TIM table | - | skipped |
| 16 | sound/snd.edt (EDT) | 0 | copied (identical format) |
| 17 | sound/snd.vh (VH) | 1 | copied (identical format) |
| 18 | sound/snd.vb (VB) | 2 | copied (identical format) |

## Confirmed (byte inspection of ROOM1000/1010/1050 both games)

- VH/VB/EDT formats are IDENTICAL: both start with the same 4-byte header
  (`70 42 41 56`), same field layout; only data differs. Copied verbatim.
- Camera structs differ:
  - RE1 `Rdt1Camera` (0x28): `masks_offset`, `tim_masks_offset`, from/to xyz,
    unknown1[3].
  - RE2 `RdtCamera` (0x20): `flags(u16)`, `perspective(u16)`, pos/target xyz,
    `offset(u32)`.
  - RE1 `masks_offset`/`tim_masks_offset` are ABSOLUTE file offsets into the
    PRI / camera-TIM region that physically sits between RVD and SCA.
- Camera switches (RVD): RE1 `Rdt1CameraSwitch` (0x14) `{to, from, 4 points}`
  map to RE2 `VCut` (0x14) `{be_flg, nFloor, fCut, tCut, a, b, c, d}` with
  `fCut=from`, `tCut=to`, `be_flg=1`, `nFloor=0xFF`. Verified.
- PRI (camera masks): copied verbatim; camera `offset` rebased by the
  relocation delta. Verified data lands correctly.
- Embedded object models: RE1 TMD (`{Length, ?, NumObjects, descriptors}` with
  interleaved texture data) -> RE2 MD1 (separate triangle/quad descriptors +
  separate texture data). Conversion follows biohazard-utils `MeshConverter`
  (`Md1.Builder.ToMesh`) and is byte-identical for all tested models:
  - descriptor table at 12, 2 descriptors per part (triangle + empty quad)
  - positions/normals/triangles serialised, Length patched before textures
  - page = `0x80 | (page & 0x0F)`, clut = `0x7800 | (page & 0x0F) << 6`

## Conversion strategy

Copy the RVD->SCA physical region verbatim (it holds RVD + PRI + camera TIMs)
into the RE2 RVD slot; rebase RE1 camera `masks_offset`/`tim_masks_offset` by
the relocation delta. Embedded models: wrap each RE1 TMD as RE2 MD1 as above.
SCD/EMR/EDD/items/ESP stubbed or skipped for now.

## Open questions

- ESP / effects / item models / SCD conversion (all deferred).
- RE1 `Rdt1Camera` flags/perspective fields: set to 0 (RE1 has no direct
  equivalent exposed; RE2 main hall uses 0x683c perspective constant).
- Whether the engine needs `num_items`/item model table for room rendering.