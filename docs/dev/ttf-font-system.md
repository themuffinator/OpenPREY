# TrueType font system

openPREY can render scalable TrueType faces reconstructed from Prey (2006)'s
retail bitmap fonts. The renderer rasterises those same letterforms at the
display's resolution instead of magnifying the fixed 12/24/48-pixel atlases.
The original bitmap path remains available as the compatibility fallback.

## Shipped faces

The generated files live in `content/basepr/pak0/fonts/` and use the names that
the renderer resolves after a GUI requests a font.

| File | GUI / engine consumer | Retail source |
| --- | --- | --- |
| `english.ttf` | `fonts` (the default UI/HUD face) | `fonts/english/fontImage_48.dat` and five atlas pages |
| `menu.ttf` | `fonts/menu` | `fonts/english/menu/fontImage_48.dat` and five atlas pages |
| `alien.ttf` | `fonts/alien` | `fonts/english/alien/fontImage_48.dat` and three atlas pages |
| `bigchars.ttf` | `textures/bigchars` (console and loading text) | `textures/gfx/bigchars.tga` |

`alien.ttf` intentionally remains a Latin-symbol face. It folds accents onto
their base symbol rather than mixing the retail alien alphabet with donor
scripts. The other three faces carry the extended Latin, Greek, Cyrillic,
Arabic, Hebrew, punctuation and HUD-symbol coverage supplied by the shared
generator.

## Rebuild process

The generated TTFs are committed runtime assets; the retail `.dat` files and
atlas pages are only temporary inputs and must not be added to the repository.

```powershell
python tools/assets/fonts/extract_source_fonts.py `
  --install "C:/Program Files (x86)/Human Head Studios/Prey" `
  --output .tmp/prey-fontsrc
```

`--install` accepts either the game install root or its `base` directory. The
extractor reads the pk4 archives in the same override order as the engine and
also handles retail archives whose central-directory and local-header paths use
different slash directions.

Download the following variable fonts from the Google Fonts repository into a
temporary donor directory (alongside their OFL notices):

- `NotoSans-var.ttf`
- `NotoSansArabic-var.ttf`
- `NotoSansHebrew-var.ttf`

Then build the complete set:

```powershell
python tools/assets/fonts/build_openprey_fonts.py `
  --source .tmp/prey-fontsrc `
  --donors .tmp/fontdonors `
  --output content/basepr/pak0/fonts
```

Pass `--faces english,menu,alien,bigchars` to rebuild a selected subset. The
output must contain all four files before a runtime package is made.

### Pipeline

1. The reader decodes Prey's original Doom 3 glyph record, preserving each
   glyph's screen-space advance, bearing and UV bounds.
2. The serialized material name selects the correct source atlas page for that
   glyph; this is essential because the default and menu faces use five pages
   at 48 pixels and alien uses three.
3. The tracer recovers the sub-pixel outline from the atlas alpha coverage,
   then applies axis snapping and curve fitting to retain sharp stems and
   corners.
4. Accents are extracted from retail precomposed glyphs where available,
   shared Greek/Cyrillic shapes reuse the authentic outline, and remaining
   ordinary-script glyphs are imported from Noto at a weight and width fitted
   to the traced face. Common UI symbols are constructed at the same weight.
5. `bigchars` is traced from its 16×16 source grid and forced to one-cell
   advances, matching the console's existing fixed-cell indexing.

## Runtime

`src/renderer/tr_fontTTF.cpp` loads a requested face from `fonts/*.ttf`, builds
12/24/48-point atlas slots with the same `fontInfo_t` layout as the bitmap
loader, and falls back to the retail `.dat` files if a face is absent or fails
to load. The high Windows-1252 byte range is translated to its Unicode
codepoints before lookup, so the TTF uses normal Unicode mappings while legacy
GUI text keeps its original byte encoding.

The console and loading paths do not use `RegisterFont`; they directly slice
`textures/bigchars` as a 16×16 grid. `R_BuildConsoleFontAtlas` rebuilds that
sheet from `bigchars.ttf` at display resolution and retargets the existing
`textures/bigchars` material, preserving its blend and UV behaviour.

| Cvar | Default | Meaning |
| --- | --- | --- |
| `r_useTrueTypeFonts` | `0` | Enable TTF GUI and console atlases; `0` keeps retail bitmaps authoritative. |
| `r_ttfFontResolution` | `1.0` | Multiplier for TTF atlas rasterisation resolution. |
| `r_ttfFontDebug` | `0` | Dump generated atlas pages to `fs_savepath/ttfatlas` and log their layout. |

The default remains bitmap-first while Prey's retail spline/credits text effect
has no TTF glyph-path implementation. Enable the cvar explicitly when testing
the scalable path; it falls back cleanly to bitmap data when a face cannot be
registered.

## Validation

After rebuilding, verify all four TTFs with FontTools and package them through
the normal Meson `pak0.pk4` target. For a renderer check, launch windowed with
`+set r_fullscreen 0 +set r_useTrueTypeFonts 1 +set r_ttfFontDebug 1`, inspect
the generated engine-side `ttfatlas` images, then repeat with the cvar at `0`
to confirm the bitmap fallback and `uiFontParitySelfTest` still pass.

The current GUI text loop indexes byte codepoints only. The TTF files carry
wider coverage, but fully displaying UTF-8 and shaping Arabic still needs a
separate codepoint-aware text-path update.
