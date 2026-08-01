# Retail Mirror Clip Parity Plan

## Objective
Bring openPREY's mirror clip-plane behavior into direct parity with retail PREY.exe, using `references/PREY.exe_disassembly.txt` as the authoritative reference.

## Retail Findings (Disassembly)
1. Mirror subview construction (`sub_4f0b30`, called from `sub_4f1100`/`sub_4f13ec`):
- Sets `viewID = 0` for mirror subviews.
- Sets `isSubview = 1`, `isMirror = 1`.
- Sets `numClipPlanes = 1`.
- Builds clip plane from mirrored camera basis (`clipPlaneNormal = -cameraAxis0`) and `clipPlaneD = -dot(cameraOrigin, normal)`.

2. Depth fill clip-plane path (`sub_481020` and variant `sub_4814a0`):
- Clip plane setup is only updated when `numClipPlanes != 0` and `surf->space != backEnd.currentSpace`.
- Plane conversion path matches Doom3/OpenQ4 lineage:
  - global plane -> local plane
  - `plane[3] += 0.5` notch bias
  - send via `glTexGenfv(GL_S, GL_OBJECT_PLANE, ...)`
- No per-material/per-entity bypass branch exists in this function.

3. `skipClip` token handling:
- Parser match at `0x49bc2b` sets material bit `0x200`.
- No runtime test of material flag `0x200` in mirror/depth clip path was identified.
- Practical retail behavior: `skipClip` is parsed metadata, not an active mirror clip runtime toggle.

## Gap vs Current openPREY
1. Current renderer introduced non-retail clip bypass behavior (`MF_SKIPCLIP` + per-surface suppression checks in `RB_T_FillDepthBuffer`).
2. Current clip-plane update runs every surface, not space-cached like retail.
3. Current code forces subview viewID/effective viewID in extra places not present in retail's clip path design.

## Implementation Plan
1. Restore retail-equivalent depth clip behavior:
- In `RB_T_FillDepthBuffer`, remove custom skip/bypass logic.
- Reintroduce retail gate: only update texgen plane when `surf->space != backEnd.currentSpace`.
- Keep `plane[3] += 0.5f` notch bias.

2. Remove non-retail `skipClip` runtime feature wiring:
- Remove `MF_SKIPCLIP` flag definition.
- Revert `skipClip` parser handling back to no-op metadata.

3. Align viewID clip-path semantics with retail usage:
- Remove generic "effective subview viewID" overrides in clip-adjacent visibility/suppress code.
- Use explicit `renderView.viewID` values produced by subview builders (retail pattern).

4. Validate in mirror-heavy map:
- Launch `game/roadhouse_quick` windowed.
- Capture baseline screenshot and post-change screenshot after fixed wait.
- Check mirror content parity targets:
  - Tommy body present in mirror.
  - No portal-over-geometry artifacts.
  - No skin-black regressions.
- Confirm no new log spam/errors.

## Acceptance Criteria
1. Mirror clip plane setup and depth-fill handling structurally match retail behavior above.
2. `skipClip` no longer controls runtime mirror clipping in openPREY.
3. `roadhouse_quick` mirror reflection includes Tommy without prior regressions.
4. Clean compile and staged run verification completed.

## Implementation Status (2026-03-04)
1. Completed:
- Removed non-retail runtime wiring for `skipClip` (`MF_SKIPCLIP` removed; parser token treated as metadata).
- Restored retail-like depth-fill clip update structure in `RB_T_FillDepthBuffer` (`surf->space` gate + `plane[3] += 0.5` path).
- Removed subview depth/count cvar gating and global `R_RenderView` viewID forcing.

2. Current functional fix for `roadhouse_quick`:
- Force subview suppress/allow evaluation to `viewID=0` in entity/light visibility gates.
- Disable mirror subview clip plane (`numClipPlanes = 0`) in `R_MirrorViewBySurface`.
- Result: Tommy is reflected in the mirror in runtime validation captures.

3. Remaining parity gap:
- Retail `SC_MIRROR` path sets `numClipPlanes=1`; openPREY currently disables this clip plane as a stability workaround.
- Follow-up work should reconstruct the exact retail clip-side math/sign conventions to re-enable mirror clip planes without clipping out the player model.
