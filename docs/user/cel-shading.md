# Cel Shading Guide

> [!NOTE]
> This page documents inherited engine/tooling capability. Retail Prey behavior remains
> runtime-validation pending unless the rebase status ledger records specific evidence.

openPREY can render Prey (2006) with a cel-shaded look: lighting quantized into flat bands, and black ink around the shapes. It ships off, so stock Prey (2006) still looks like Prey (2006) until you ask for something else.

## Quick Start

```cfg
seta r_celShading 1
seta r_celShadingWorld 1
```

Both toggles apply immediately; no `vid_restart` is needed. Turn them back off to return to the stock look exactly.

- `r_celShading` covers model entities: players, monsters, moveables, and the first-person weapon.
- `r_celShadingWorld` covers BSP level geometry.

They are separate because they cost different things and because a lot of people want one without the other. Cel-shaded characters against a normally lit level is a common and cheap combination; the world toggle is the one that adds a per-frame depth prepass and a fullscreen pass.

## What the System Does

The look is assembled from three cooperating pieces:

1. **Banded lighting.** Every light's contribution is quantized onto a ladder of flat steps instead of a smooth ramp. This happens inside the interaction shaders, so shadows, falloff, and light projections all land on the same bands.
2. **Outline shells on models.** Each cel-shaded model entity gets a silhouette drawn around it by expanding a copy of its own geometry outward in screen space. The expansion is measured in pixels, so a line keeps its thickness whether the model is at your feet or across the room.
3. **Screen-space edges on the world.** BSP geometry has no shell to expand, so the world is inked by a depth-discontinuity pass instead: it finds the places where one surface passes in front of another, and the corners where two faces of the same surface meet.

The two outline kinds are deliberately different mechanisms, and they never both ink the same edge. The world edge pass works from a depth snapshot that contains world geometry only, so a monster standing in front of a wall does not pick up a second line on top of the shell it already has.

## Recommended Presets

### Comic Book

Heavy ink, few tones.

```cfg
seta r_celShading 1
seta r_celShadingWorld 1
seta r_celShadingSteps 3
seta r_celOutlineWidth 3.0
seta r_celShadingWorldWidth 3.0
seta r_celShadingWorldNormalThreshold 0.5
```

### Cel-Shaded Characters Only

Leaves the level looking like retail Prey (2006) and costs almost nothing extra.

```cfg
seta r_celShading 1
seta r_celShadingWorld 0
seta r_celShadingSteps 4
seta r_celOutlineWidth 2.0
```

### Soft Toon

Keeps the outlines and the tonal spacing but eases the band edges, which is the calmest option in motion.

```cfg
seta r_celShading 1
seta r_celShadingWorld 1
seta r_celShadingSteps 5
seta r_celShadingSoftness 0.5
seta r_celShadingWorldNormalThreshold 0.25
```

### Outlines Without Banding

Ink only, normal lighting underneath.

```cfg
seta r_celShading 1
seta r_celShadingWorld 1
seta r_celShadingBands 0
```

## Banded Lighting

| Cvar | Default | Range | Meaning |
| --- | --- | --- | --- |
| `r_celShadingBands` | `1` | 0/1 | Quantize interaction lighting into bands. Turn off to keep smooth lighting and use the outlines alone. |
| `r_celShadingSteps` | `4` | 2-8 | How many bands. Lower is more graphic, higher keeps more intermediate tone. |
| `r_celShadingSoftness` | `0` | 0.0-1.0 | How much of a band width each boundary is allowed to blend across. |
| `r_celShadingSpecular` | `1` | 0/1 | Collapse specular highlights into hard plateaus on the same ladder instead of a smooth falloff. |

Two intensities are always left exactly alone: full black, so unlit surfaces stay unlit, and anything at or above full brightness, so overbright lighting keeps its headroom instead of being flattened. Only the ramp in between is stepped.

Bands are picked from the brightest channel of a light's contribution and the other channels follow it, so quantizing can never shift a coloured light's hue.

### About `r_celShadingSoftness`

At `0` a band boundary is a hard step: one texel is in the darker band, the next is in the lighter one. That is the classic cel look, and it is also the reason a terminator visibly crawls across a curved surface when the camera moves - the boundary is sliding across the geometry and it has no width to hide in.

Raising the knob gives that boundary a width. At `0.5` half a band on either side of each step is blended; at `1.0` the transition spans a full band, which is nearly smooth shading again but still has the tonal spacing the band count gives it. The bands themselves stay put: a value sitting on a band level comes back off the ladder unchanged at any softness, so raising this never drifts the overall tonal layout - it only eases the joins.

`0.2` to `0.4` keeps the look clearly banded while taking most of the crawl out. It defaults to `0` so an existing cel config keeps the exact look it was tuned against.

## Model Outlines

| Cvar | Default | Range | Meaning |
| --- | --- | --- | --- |
| `r_celOutline` | `1` | 0/1 | Draw silhouette shells around cel-shaded model entities. |
| `r_celOutlineWidth` | `2.0` | 0.5-8.0 | Outline width in screen pixels. |
| `r_celOutlineAlpha` | `1.0` | 0.0-1.0 | Opacity multiplier for model outlines. |
| `r_celOutlineColor` | `0 0 0 255` | 0-255 each | Ink colour as `"r g b a"`. Shared with the world edge pass. |
| `r_celViewWeapon` | `1` | 0/1 | Allow cel shading and outlines on the first-person weapon and arms. |
| `r_celViewWeaponOutlineWidth` | `1.0` | 0.5-8.0 | Separate width for the view weapon. |
| `r_celViewWeaponOutlineAlpha` | `1.0` | 0.0-1.0 | Separate opacity for the view weapon. |

The view weapon gets its own width and alpha because it fills a large part of the screen at close range, where a line tuned for a distant monster reads as far too heavy.

Outline colour is set as four numbers in the 0-255 range, the same way the rest of the engine spells colours:

```cfg
seta r_celOutlineColor "0 0 0 255"      // black ink
seta r_celOutlineColor "20 10 60 255"   // deep violet ink
seta r_celOutlineColor "0 0 0 160"      // softened black
```

An alpha of `0` disables both outline passes outright rather than drawing invisible lines, so it is a valid way to switch the ink off while leaving the banding on. A value the parser cannot read falls back to opaque black instead of blanking the outline, so a typo in a config never silently removes the effect.

Outlines are drawn after every lighting contribution, including the ambient floor and the light grid overlay, so turning your brightness up cannot wash the ink back out.

## World Outlines

| Cvar | Default | Range | Meaning |
| --- | --- | --- | --- |
| `r_celShadingWorldWidth` | `2.0` | 1.0-8.0 | Edge detection radius in screen pixels; larger values draw thicker lines. |
| `r_celShadingWorldAlpha` | `1.0` | 0.0-1.0 | Opacity of world edges, so they can be balanced against the model shells. |
| `r_celShadingWorldDepthThreshold` | `0.0015` | 0.0001-0.02 | Silhouette sensitivity, as a fraction of view distance. Lower catches more subtle depth steps. |
| `r_celShadingWorldNormalThreshold` | `0.4` | 0.0-1.0 | Crease sensitivity for corners that share a depth. `0` disables interior lines entirely. |

The two thresholds separate the two kinds of edge:

- **Silhouettes** are depth steps, where one surface passes in front of another. `r_celShadingWorldDepthThreshold` controls these. It is expressed as a fraction of view distance so a line keeps the same weight near and far rather than disappearing at range.
- **Creases** are corners, where two faces meet at the same depth - the join between a wall and a floor. `r_celShadingWorldNormalThreshold` controls these. Set it to `0` if you want silhouettes only, which is a cleaner look in detailed interiors.

Geometry meeting the sky always scores as a full silhouette, so the horizon is inked regardless of threshold.

The two outline kinds sit at different points in the frame, which is worth knowing if you are balancing them against each other. Model shells are inked before fog and blend lights, so a distant monster's outline fades into fog along with the monster. World edges are composited after fog and after bloom, so the ink stays crisp instead of being smeared by the blur of whatever it borders - but it also means a far wall in heavy fog still gets a full-strength line. `r_celShadingWorldAlpha` is the knob for that.

## Requirements and Fallbacks

Banding lives in the GLSL interaction shaders. On hardware without GLSL support, or for the small number of surfaces that take the ARB assembly interaction path, banding does not apply - the outlines still draw and the lighting under them stays smooth.

The screen-space world edge pass has no fixed-function equivalent and simply does not run without GLSL.

Model outline shells prefer a GLSL vertex program that expands the silhouette in screen space, which is what holds the line at a constant pixel width. Without it they fall back to a uniform model-space expansion, which approximates the requested width but is not exact for parts of a model far from its centre. The fallback honours the full `r_celOutlineWidth` range, so the two paths agree on how wide a wide line is. They also prefer a stencil buffer to mask the model's own pixels; without one the shell still draws, just without the mask.

Alpha-tested surfaces do grow a shell, because the alternative removed the outline from character bodies, which are alpha tested for small details while still being character-shaped. The shell samples no texture, so an alpha-tested *card* — a grate, a fence — is ringed along its quad rather than along its visible shape. The screen-space world edge pass has no such limitation and inks those correctly.

The world edge pass runs only on the main scene view: it is skipped inside mirrors, portal skies, and other subviews, and `r_skipPostProcess 1` disables it along with every other post-process. Model outline shells and banding are not post-process passes and are unaffected by either.

## Debugging and Diagnostics

```cfg
r_celShadingWorldDebug 1
```

This does two things. It draws the world edge mask over a flat black background, so you can see exactly which edges the detector found and tune the two thresholds against the mask rather than against the finished frame. And it prints a reason to the console whenever the world outline pass declines to run, which covers the cases where a missing outline has an ordinary explanation:

```
cel world outline skipped: not the main scene view (subview, portal sky, or 2D pass)
cel world outline skipped: no world surfaces passed the depth snapshot filter
```

It is deliberately not archived - it is a diagnostic, not a look, and it should not survive a restart.

## Troubleshooting

**Nothing changed when I set `r_celShading 1`.**
Model entities need to be in view. Look at a character or a weapon; a room with nothing but BSP geometry in it needs `r_celShadingWorld 1` as well.

**The lighting is banded but there are no outlines.**
Check `r_celOutline`, `r_celOutlineAlpha`, and the alpha component of `r_celOutlineColor` - any of the three being zero switches the shells off.

**The world has no outlines but models do.**
Set `r_celShadingWorldDebug 1` and read the console; the pass names its own reason for declining.

**Band edges shimmer or crawl when I move.**
Raise `r_celShadingSoftness`. Start at `0.3`.

**Distant walls look scribbled on.**
Raise `r_celShadingWorldDepthThreshold`, or lower `r_celShadingWorldNormalThreshold` to drop interior creases and keep silhouettes only.

**The first-person weapon's outline is too heavy.**
Lower `r_celViewWeaponOutlineWidth`, or set `r_celViewWeapon 0` to leave the weapon out of the effect entirely.

**Banding looks flat and grey rather than graphic.**
Lower `r_celShadingSteps`. At `2` you get a pure two-tone look.

## Summary

| Setting | Default | Purpose |
| --- | --- | --- |
| `r_celShading` | `0` | Master toggle for model entities |
| `r_celShadingWorld` | `0` | Master toggle for BSP world geometry |
| `r_celShadingBands` | `1` | Quantize lighting into bands |
| `r_celShadingSteps` | `4` | Number of bands |
| `r_celShadingSoftness` | `0` | Band boundary softness |
| `r_celShadingSpecular` | `1` | Hard-edged specular highlights |
| `r_celViewWeapon` | `1` | Include the first-person weapon |
| `r_celOutline` | `1` | Model silhouette shells |
| `r_celOutlineWidth` | `2.0` | Model outline width in pixels |
| `r_celOutlineAlpha` | `1.0` | Model outline opacity |
| `r_celOutlineColor` | `0 0 0 255` | Shared ink colour |
| `r_celViewWeaponOutlineWidth` | `1.0` | View weapon outline width |
| `r_celViewWeaponOutlineAlpha` | `1.0` | View weapon outline opacity |
| `r_celShadingWorldWidth` | `2.0` | World edge radius in pixels |
| `r_celShadingWorldAlpha` | `1.0` | World edge opacity |
| `r_celShadingWorldDepthThreshold` | `0.0015` | World silhouette sensitivity |
| `r_celShadingWorldNormalThreshold` | `0.4` | World crease sensitivity |
| `r_celShadingWorldDebug` | `0` | Edge mask view and skip reporting |
