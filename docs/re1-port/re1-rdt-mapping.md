# RE1 RDT -> RE2 RDT mapping

Ground truth: `biohazard-utils` (`Rdt1.cs`, `Rdt2.cs`, `Rdt2.Builder.cs`) plus
the RE1 Ghidra DB (`re1-ghidra` MCP) for anything ambiguous. Status: WIP.

## Containers

| | RE1 (`Rdt1`) | RE2 (`Rdt2`/engine) |
|---|---|---|
| Header | 6 bytes @0x00 (`nSprite,nCut,nOmodel,nItem,nDoor,nRoom_at`) | 8 bytes @0x00 (`RdtHeader`) |
| LIT | @0x06 | offset slot 9 |
| Offset table | 19 x u32 @0x48 | 23 x u32 @0x08 |
| Cameras (RID) | @0x94, `Rdt1Camera` 0x28 ea | offset slot 7, `RdtCamera` 0x20 ea |

## RE1 offset slots

| # | RE1 chunk | RE2 slot |
|---|---|---|
| 0 | zone.rvd (RVD) | 8 |
| 1 | collision.sca (SCA) | 6 |
| 2 | embedded object model table | 10 (MODELS) |
| 3 | embedded item model table | - (stub) |
| 4 | block.blk (BLK) | 12 |
| 5 | floor.flr (FLR) | 11 |
| 6 | init.scd | 16 (SCD_INIT, stub) |
| 7 | main.scd | 17 (SCD_MAIN, stub) |
| 8 | events.scd | - (stub) |
| 9 | animation.emr | - (stub) |
| 10 | animation.emd | - (stub) |
| 11 | message.msg (MSG) | 13 (MSG_JA) |
| 12 | embedded item PIX table | - |
| 13 | ESP IDs | 18 |
| 14 | embedded ESP EFF table | 19 |
| 15 | embedded ESP TIM table | - |
| 16 | sound/snd.edt (EDT) | 0 |
| 17 | sound/snd.vh (VH) | 1 |
| 18 | sound/snd.vb (VB) | 2 |

## Open questions

- VH/VB byte-format compatibility between RE1 and RE2 (topology header). Both
  are Capcom PSX-era; expected near-identical, needs byte inspection.
- `Rdt1Camera` field order (`masks_offset`, `tim_masks_offset`, from/to xyz,
  unknown1[3]) vs `RdtCamera` (`unknown0, const0`, from/to xyz, `masks_offset`).
- Embedded object TMD vs RE2 MD1: does the engine accept TMD directly?