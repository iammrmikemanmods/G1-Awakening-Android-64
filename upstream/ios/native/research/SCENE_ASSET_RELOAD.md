# Native scene asset reload checkpoint

> Historical checkpoint: the current integration and all-21-map/style scene-catalog result are documented in `NATIVE_CAMPAIGN_UI.md` and `scene_catalog_validation/with_animated_surface/validation.json`. The earlier style 1 guard and host limitations below describe the prior snapshot.

This change generalizes native tactical asset construction and binds presentation responses to actual displayed frames. It does not execute an original binary, ship a CPU emulator, claim a complete campaign, or establish iPhone performance.

## Data and playback contracts

`MissionScene(archive, terrainTables, world)` builds the native terrain and sprite resources for a supplied, already applied `WorldBootstrap`. It accepts zero units and unique original types 0–22 in stable vector order. Inactive entries may retain coordinates from an earlier map; active positions must be inside the current grid. Duplicate/unknown types and malformed resources fail explicitly.

Map identity includes resource ID, style, dimensions and actual tile bytes. Roster asset identity includes type and vector index, not active/form/position state. `NativeApp` retains the archive and terrain table, rebuilds the affected scene when either identity changes, caches Metal textures by resource ID and initializes playback only for active entries. The session's spawn generation resets playback and interpolation for a repeated activation of the same type, including an already-active entry.

The `installSession` helper is the sole session-pointer assignment. It invalidates the drawable revision before replacement, resets dialogue/battle canvases, queued edges, camera/focus state, transform-completion records, playback and simulation accumulation, then builds the new scene. A reused numeric battle token or dialogue index cannot reuse the prior session's completed drawing. Campaign selection, save loading and command application remain core/host integration work; the helper itself makes no success or persistence decision.

## Presentation responses

Mission camera requests and typed session focus requests pan the native inspection camera to the actual requested tile. Positive, finite Metal `presentedTime` and the matching scene revision/token are required before acknowledgement. This is an explicit native presentation policy, not recovered original camera timing. The original helpers at `0x5c9ae` and `0x5d176` are both `movs r0,#1; bx lr`.

Transform completion is queued at the original special-to-normal animation transition. Static instruction evidence at `0x6ce42..0x6ce50` compares the saved special ID with the old animation ID, then stores -1 to global map+0x28 without comparing the unit type to that global. Consuming pending byte2e does not complete it. Native acknowledgement waits for a frame containing the changed playback state and checks both transform and spawn generation. A later activation or scene replacement invalidates the old response.

## Real-data validation

`scene_catalog_validation/validation.json` records exact source/input hashes and compiler commands. It rebuilds the current portable sources, compares the original first-scene JSON to the frozen reference, and compares all 23 sprite descriptor frame/animation counts to the independent atlas report. It also exercises empty/inactive rosters, identity changes, duplicate types and an active out-of-grid error.

All 23 gameplay descriptors loaded. Their complete frame set produced 19,050 finite native GPU-layout vertices. Every nonnegative animation-table entry references an existing descriptor animation. The original first-scene report is unchanged.

Scripts 310–339 reference 21 distinct map/style pairs. Ten currently load. These eleven style1 pairs hit the original existing guard for animated face14:

| Original map index | Grid resource | Script resources using this pair |
| --- | --- | --- |
| 6 | 265 | 324, 337, 339 |
| 7 | 266 | 337 |
| 12 | 275 | 321 |
| 13 | 267 | 322 |
| 14 | 279 | 323, 331 |
| 15 | 269 | 338 |
| 17 | 258 | 310 |
| 18 | 259 | 311 |
| 19 | 260 | 312 |
| 20 | 261 | 313 |
| 21 | 262 | 314 |

These are deliberate failures with the exact error `style 1 animated-terrain routing is not yet implemented`. No substitute terrain or guessed animation was introduced. The report retains each command index and the supported maps' actual quad counts. It is an asset-coverage probe, not execution of those missions.

Run from the project root with privately supplied original data:

```sh
python analysis_tools/scene_catalog/validate.py \
  --archive /path/to/iphone.big \
  --terrain /path/to/terrain-tables.bin \
  --output /path/to/scene-catalog-results
```

The default host CMake tool can be `scene_catalog_probe`, linked to `g1_resources`, with those two data-file arguments. Private assets are not required for ordinary CI tests and are not committed.

## Next gates

The modified Objective-C++ files still require an actual Apple compile/link and device interaction test. No local synthetic SDK or source-only check is counted as that gate. Style1 animated terrain is separate recovery work. Site artwork/state updates, campaign/save handoff and original battle3D rendering remain incomplete.

The next original battle3D milestone is rigid M3G mesh/vertex/strip/group/material decoding and an actual static character model. The existing M3G module only validates container framing; the current native battle panel reports core outcomes and is not a 3D cinematic substitute. Original model files contain authentic rigid geometry and animation tracks, so no invented model or sprite replacement is needed for that milestone.
