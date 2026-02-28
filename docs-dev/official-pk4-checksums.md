# Official Prey (2006) PK4 Checksums

This document tracks the official PK4 checksum baseline used by OpenPrey startup validation.

## Status

OpenPrey now tracks two known official Prey base pack layouts:

1. Classic retail CD/DVD naming (`pak000` ... `pak004`).
2. Consolidated naming seen in legacy digital distributions (`pak_data`, `pak_sound`, `pak_en_v`, `pak_en_t`).

## Capture Method

1. Launch OpenPrey against a clean Prey installation.
2. Enable startup logging (`logFile 2`) and inspect `logs/openprey.log` under `fs_savepath`.
3. Record `Loaded pk4 ... with checksum ...` lines.
4. Populate required and optional PK4 tables below.

## Required Baseline: Classic Naming

| PK4 | Checksum |
|---|---|
| `base/pak000.pk4` | `TBD` |
| `base/pak001.pk4` | `TBD` |
| `base/pak002.pk4` | `TBD` |
| `base/pak003.pk4` | `TBD` |
| `base/pak004.pk4` | `TBD` |

## Required Baseline: Consolidated Naming

| PK4 | Checksum |
|---|---|
| `base/pak_data.pk4` | `0xbe295ead` |
| `base/pak_sound.pk4` | `0xe0c27ee2` |
| `base/pak_en_v.pk4` | `0x952b910e` |
| `base/pak_en_t.pk4` | `0x6625f12d` |

## Additional Official PK4s (Optional)

| PK4 | Checksum |
|---|---|
| `base/pak005.pk4` | `TBD` |
| `base/pak006.pk4` | `TBD` |
| `base/pak020.pk4` | `TBD` |
| `base/pak040.pk4` | `TBD` |
| `base/game00.pk4` | `TBD` |
| `base/game01.pk4` | `TBD` |
| `base/game02.pk4` | `TBD` |
| `base/game03.pk4` | `TBD` |

## Notes

- Checksum format is the engine PK4 checksum generated in `src/framework/FileSystem.cpp`.
- The consolidated checksums were captured from an installed retail-media layout via OpenPrey runtime logging (`Loaded pk4 ... checksum ...`).
- Classic naming entries remain presence-only until canonical checksums are captured.
- Keep this list synchronized with any strict validation policy changes.
