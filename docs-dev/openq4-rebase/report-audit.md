# Coverage Audit — openPREY changes since fork (e634332b vs working tree)

Ran `git diff --name-status e634332b` over the specified paths (excluding src/Prey and src/game): 1345 changed files. After matching against the claimed list (including its glob/catch-all entries), 68 paths were initially unmatched. Disposition:

## Meaningful gap (1)

- **`tools/assets/extract_menu_background_tiles.ps1`** (Added, 154 lines) — a new PowerShell asset tool that extracts/generates the menu background tile TGAs (the `loading_left|right|top|bottom` / `background_*` art the analysts DID catalog). The tool that produces those assets was not cataloged. Minor, but relevant if the rebase touches the aspect-expansion background feature.

## Borderline / trivial (noted, not really gaps)

- **`.github/FUNDING.yml`** (Added, 1 line: `github: themuffinator`) — repo metadata, not engine-relevant.
- **`src/renderer/Model_md5.cpp`** (Modified) — whitespace-only change (one trailing-space line). Noise.

## Classified as covered by catch-alls or as rebrand/asset noise

- ~60 `basepy/guis/**` files (alarm.gui, hud.gui, roadhouse/*, vehicles/*, forcefield_*, generic_*, mp*, etc.) — covered by the claimed catch-all "biolabs/*, keeperfortress/*, noninteract_*.gui (~136 gui files total)"; these are the imported Prey GUI asset set, not distinct un-cataloged work.
- Rebrand deletions of upstream-branded assets: `src/sys/linux/setup/image/doom3.png`, `src/sys/osx/OpenQ4.icns`, `src/sys/win32/rc/OpenQ4Version.rc`, `src/sys/win32/rc/res/icon2.ico`, `src/sys/win32/rc/res/quake4.ico` — pure branding removals paired with the claimed openprey_version.rc / prey.ico / prey.icns additions. Noise.
- All other 1277 changed paths matched the claimed list directly or via its documented globs (`.install/basepy/**`, `.install/openprey/**`, `basepy/guis/assets/**`, glprogs, strings, icons, docs, etc.).

## Verdict

Coverage is effectively complete. The only substantive omission is the new asset-generation script `E:\Repositories\openPREY\tools\assets\extract_menu_background_tiles.ps1`; everything else unmatched is repo metadata, whitespace, rebrand deletions, or files already covered by the analysts' catch-all entries.
