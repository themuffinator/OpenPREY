# Level-Load Cache

> [!NOTE]
> This page documents inherited engine/tooling capability. Retail Prey behavior remains
> runtime-validation pending unless the rebase status ledger records specific evidence.

openPREY automatically builds compact binary caches for MD5 animations as they are first encountered. Later launches can load the final animation data directly instead of decompressing and parsing the much larger text source again.

The first visit to animation-heavy maps can therefore take slightly longer while cache files are written. Subsequent visits should be faster. Upstream reference testing generated 820 cache entries using about 27 MiB and reduced an animation-heavy model-definition phase from roughly 2.7–2.8 seconds to about 1.0 second on the tested Windows x64 system; Prey-specific performance remains validation-pending.

## Location

Caches are written below the active save path:

```text
<fs_savepath>/basepr/generated/animations/
```

They are generated runtime data, not replacement game assets, and should not be copied into retail Prey (2006) PK4s.

## Validation and portability

Each cache records the source path, source size, timestamp, and containing PK4 checksum where applicable. A stale, truncated, corrupt, or mismatched cache is ignored and rebuilt from the installed source animation.

The cache stores fixed-width values and floating-point arrays in a portable byte order. x64 and ARM64 builds use the same format, although moving the game installation or changing the source package can intentionally trigger a rebuild.

Loaded cache data becomes the normal runtime animation data; openPREY does not retain a second permanent copy in memory. Animation memory reporting also includes base-frame storage.

## Controls

Both controls default to enabled:

```text
g_useGeneratedAnimCache 1
g_writeGeneratedAnimCache 1
```

- Set `g_useGeneratedAnimCache 0` to bypass existing generated animation caches.
- Set `g_writeGeneratedAnimCache 0` to prevent new cache files from being written.
- Disable both for a source-parse benchmark.

To force a clean rebuild, close openPREY and delete the `generated/animations/` directory under the active `fs_savepath`. Do not delete the original `models/**/*.md5anim` files from the Prey (2006) installation.
