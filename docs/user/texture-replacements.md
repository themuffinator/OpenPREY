# DDS Texture Replacements

> [!NOTE]
> This page documents inherited engine/tooling capability. Retail Prey behavior remains
> runtime-validation pending unless the rebase status ledger records specific evidence.

openPREY can use DDS texture-pack replacements without changing the original Prey (2006) materials. This includes the stock game's `progimg/` image-program results, dhewm3-style `dds/` packs, and same-folder replacements used by some Prey (2006) packs.

## Where files belong

Put your `.dds` files under a `dds/` folder inside `basepr` (loose, or at the root of a PK4 you drop in `basepr`). Mirror the original asset path underneath it and change the extension:

```text
Original asset:  textures/stone/wall01.tga
Replacement:     basepr/dds/textures/stone/wall01.dds
```

```text
basepr/
  dds/
    textures/
      stone/
        wall01.dds
```

A `.dds` placed next to the original asset path instead — `basepr/textures/stone/wall01.dds`, the layout some Prey (2006) packs use — also works. The `dds/` tree is preferred and wins if both exist.

`base/dds/...` is on the search path too, but keep your files in `basepr` so a Steam file verification does not delete them.

### Bump maps and other image programs

Prey (2006) materials rarely name a bump texture directly. They usually write an image *program*:

```text
bumpmap addnormals( textures/stone/wall01_local, heightmap( textures/stone/wall01_h, 4 ) )
```

You can replace either end of that:

- **Replace an input.** Shipping `dds/textures/stone/wall01_local.dds` replaces the source texture and openPREY recomputes the program result on load. This is the simplest option and is what a Prey (2006)-shaped pack normally does.
- **Replace the finished result.** Name the file after the whole program using dhewm3's mangling:

  ```text
  heightmap(models/monsters/foo_local, 4)
      -> dds/heightmap/models/monsters/foo_local 4.dds
  ```

  Texture-pack tools should preserve that mapping for composed programs such as `heightmap`, `addnormals`, `add`, and `scale`.

If you are unsure what a given material image is called, turn on `image_showPrecompressedTextures` (below) — openPREY prints every path it looked for.

### Search order

For each image, openPREY probes in this order and takes the first usable file:

1. `dds/<image program name>.dds` — the dhewm3 naming scheme
2. `dds/<asset path>.dds` — for plain image references
3. `<asset path>.dds` — beside the original `.tga`/`.jpg`, the Prey (2006) style
4. `progimg/...` — the stock precomputed results shipped in the retail PK4s

The retail `progimg/` tree is probed **last**, so a user replacement always wins. openPREY recognizes those retail paths automatically, including nested image programs and plain `gfx/env/` cube sides; you do not need to extract, rename, or regenerate the retail files.

## Enable replacements

Set `image_usePrecompressedTextures` before loading a map:

- `0` disables automatic DDS replacements entirely, including your own packs.
- `1` (default) uses any supported DDS replacement — DXT1, DXT5, RXGB and BC7 — and also reuses the stock `progimg/` files shipped in the Prey (2006) PK4s. **This is the right setting for a BC7 pack.**
- `2` uses only BC7/BPTC replacements and ignores DXT/RXGB data, including the stock `progimg/` tree. Use this only if you specifically want everything else to come from the uncompressed sources.

BC7 requires a renderer with BPTC support. Run `gfxInfo` and look for the BC7/BPTC capability line if a pack is not being selected.
DXT/RXGB files fall back to CPU decoding when direct texture-compression upload is unavailable. A BC7 file on a renderer without BPTC support is uploaded uncompressed rather than failing the load.

> **macOS has no BC7.** BPTC entered OpenGL in 4.2, and Apple's OpenGL
> implementation stops at 4.1 — and at 2.1 on the compatibility profile openPREY
> actually runs there. `gfxInfo` reports `BC7/BPTC=0` on every Mac, Apple
> Silicon and Intel alike, and every BC7 file is uploaded uncompressed. This is
> a platform limit, not a configuration problem: no cvar, driver update or
> `image_usePrecompressedTextures` value changes it. Ship DXT/RXGB alongside BC7
> if Mac users matter to your pack.

## BC7 file requirements

Supported BC7 files are single 2D textures using either:

- a DX10 header with `DXGI_FORMAT_BC7_UNORM` or `DXGI_FORMAT_BC7_UNORM_SRGB`; or
- a common `BC7` FourCC variant.

Texture arrays, cube maps, volume textures, invalid dimensions, and truncated declared mip chains are rejected safely. Authored mip levels are uploaded directly. Texture downsizing skips leading authored levels, and images using an unmipped sampler upload only their base level.

### Normal maps

Write BC7 normal maps the ordinary way: X, Y, Z in RGB, alpha unused. That is what `texconv`, NVTT, Compressonator and `bc7enc` all produce by default, and it is what openPREY expects.

> **Changed:** earlier openPREY builds incorrectly forced BC7 bump maps into the DXT5nm/RXGB channel layout, which required encoding with `bc7enc -r2a`. A pack authored that way will now render with flat lighting and must be re-encoded without `-r2a`. Only DXT5 and true `RXGB` files use the alpha-swizzled layout; that convention exists because DXT5 gives alpha its own higher-precision interpolator, and BC7 has no such limitation.

## Troubleshooting

Use these commands when checking a pack:

```text
image_usePrecompressedTextures 1
image_showPrecompressedTextures 1
reloadImages all
```

The log then prints the DDS path selected for each replacement, and — for every image where nothing was selected — the full list of candidate paths it tried with the reason each one was rejected. That list is the fastest way to find out what a file should be named:

```text
no DDS replacement for 'textures/stone/wall01':
   dds/textures/stone/wall01.dds       [dds/ (dhewm3 naming)] no such file
   textures/stone/wall01.dds           [alongside the original asset (Prey (2006) style)] no such file
```

`listImages` identifies loaded BC7 images, while `imageDDSSelfTest` validates the built-in retail/dhewm3 naming conversions plus RXGB and BC7 header checks.

During development, an automatic DDS replacement older than its original source is ignored so stale generated data cannot hide a newer texture edit. The stock `progimg/` archives receive a four-second timestamp-granularity allowance; a genuinely newer loose source still wins. Rebuild or retimestamp a custom DDS after changing its source image. Note that files inside a PK4 report no timestamp at all, so a packed replacement is never treated as stale — if you are authoring loose files alongside extracted `.tga` sources, the extracted sources will look newer and your `.dds` will be skipped.
