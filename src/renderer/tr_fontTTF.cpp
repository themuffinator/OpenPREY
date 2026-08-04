/*
===========================================================================

openPREY Source Code
Copyright (C) 2026 DarkMatter Productions

This file is part of the openPREY Source Code.

openPREY Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

openPREY Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with openQ4 Source Code.  If not, see <http://www.gnu.org/licenses/>.

===========================================================================
*/

/*
===============================================================================

	TrueType-backed replacement for the retail bitmap fonts.

	The retail fonts are fixed 12/24/48 point atlases.  On anything above the
	640x480 virtual canvas the GUI scales those atlases up, so text softens as
	the display resolution rises.  This path rasterises the shipped .ttf faces
	at the resolution the display actually needs and packs the result into a
	glyph atlas, which keeps text crisp at 1080p and 4K alike.

	It fills the same fontInfo_t the bitmap loader produces - identical metric
	units, identical UV conventions - so nothing downstream has to know which
	path produced the font.

===============================================================================
*/

#include "tr_local.h"
#include "TrueType.h"

namespace {

// Glyph metrics live in "atlas pixels at the slot's point size", the same unit
// the retail .fontdat files use, so the GUI's scaling maths is unchanged.
const int Q4_TTF_FIRST_CODE = 32;
const int Q4_TTF_LAST_CODE = 255;

// Transparent gutter around every glyph.  DeviceContext widens each glyph's
// UV rect by a bleed guard expressed in retail atlas texels, and because these
// atlases are rasterised at 'upscale' times that resolution the guard covers
// that many real texels here.  The gutter has to be at least that wide or the
// guard reads into the neighbouring glyph.
static int R_TTFGlyphPadding( int pointSize, float upscale ) {
	const float guardTexels = ( pointSize <= 12 ) ? 1.0f : 0.5f;
	return (int)idMath::Ceil( guardTexels * upscale ) + 1;
}

const int Q4_TTF_MIN_PAGE = 128;
const int Q4_TTF_MAX_PAGE = 4096;
// Largest atlas one point size of one face may occupy, in texels. 2048x2048 at
// RGBA8 is 16MB, which is what a 1440p display needs for the 48 point slot at
// its native resolution.
const int Q4_TTF_MAX_PAGE_AREA = 2048 * 2048;
// Shelf packing leaves gaps, so ask for more area than the glyphs sum to.
const double Q4_TTF_PACKING_HEADROOM = 1.25;

// The console sheet: a 16x16 grid of 16x16 pixel cells indexed by byte.
const int Q4_CONSOLE_GRID = 16;
const int Q4_CONSOLE_CELL_SIZE = 16;
const char * const Q4_CONSOLE_ATLAS_IMAGE = "_ttfconsolefont";
// Prey keeps the fixed-cell sheet outside the GUI font directories.
const char * const Q4_CONSOLE_FONT_MATERIAL = "textures/bigchars";

// The virtual GUI canvas is 640x480; rasterising at the display's upscale
// factor is what makes the text resolution-independent.
const float Q4_TTF_REFERENCE_HEIGHT = 480.0f;
const float Q4_TTF_MIN_UPSCALE = 1.0f;
const float Q4_TTF_MAX_UPSCALE = 6.0f;

// The retail atlases index glyphs by Windows-1252 byte, so the 0x80..0x9F band
// is real typography rather than control codes.  The shipped .ttf files map the
// proper Unicode codepoints, so the byte has to be translated on the way in.
static const unsigned short Q4_CP1252_HIGH[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

static int R_TTFCodepointForByte( int code ) {
	if ( code >= 0x80 && code <= 0x9F ) {
		return Q4_CP1252_HIGH[code - 0x80];
	}
	return code;
}

// The three point sizes the GUI selects between, matching the retail atlases.
struct q4TTFSlotSpec_t {
	int			pointSize;
};

static const q4TTFSlotSpec_t Q4_TTF_SLOTS[3] = { { 12 }, { 24 }, { 48 } };

/*
================
q4TTFShelfPacker

Glyphs from one face at one size are close enough in height that a shelf
packer wastes very little, and it keeps the layout deterministic.
================
*/
class q4TTFShelfPacker {
public:
	void Init( int pageWidth, int pageHeight ) {
		width = pageWidth;
		height = pageHeight;
		shelfY = 0;
		shelfHeight = 0;
		cursorX = 0;
	}

	bool Place( int glyphWidth, int glyphHeight, int &outX, int &outY ) {
		if ( glyphWidth > width ) {
			return false;
		}
		if ( cursorX + glyphWidth > width ) {
			shelfY += shelfHeight;
			shelfHeight = 0;
			cursorX = 0;
		}
		if ( shelfY + glyphHeight > height ) {
			return false;
		}
		outX = cursorX;
		outY = shelfY;
		cursorX += glyphWidth;
		if ( glyphHeight > shelfHeight ) {
			shelfHeight = glyphHeight;
			// Growing the shelf after the fact can push it past the page.
			if ( shelfY + shelfHeight > height ) {
				return false;
			}
		}
		return true;
	}

private:
	int width;
	int height;
	int shelfY;
	int shelfHeight;
	int cursorX;
};

struct q4TTFRaster_t {
	ttGlyphBitmap_t	bitmap;
	int				advance;
	bool			valid;
};

/*
================
R_TTFUpscale

How many device pixels the GUI puts on one virtual unit.  Text rasterised at
this factor lands on the display at its native resolution.
================
*/
static float R_TTFUpscale( void ) {
	// glConfig, not engineWindowState: the latter is the engine-owned window
	// mirror that the module polls at present time, so at font registration it
	// still holds the pre-mode logical window size and its UI viewport is not
	// populated at all. Rasterising from it produced atlases at half the
	// resolution the display actually needed.
	float displayHeight = (float)glConfig.uiViewportHeight;
	if ( displayHeight <= 0.0f ) {
		displayHeight = (float)glConfig.vidHeight;
	}
	if ( displayHeight <= 0.0f ) {
		displayHeight = (float)engineWindowState.vidHeight;
	}
	if ( displayHeight <= 0.0f ) {
		return Q4_TTF_MIN_UPSCALE;
	}

	float upscale = displayHeight / Q4_TTF_REFERENCE_HEIGHT;
	if ( r_ttfFontResolution.GetFloat() > 0.0f ) {
		upscale *= r_ttfFontResolution.GetFloat();
	}
	if ( r_ttfFontDebug.GetBool() ) {
		common->Printf( "TTF font: upscale %.2f from ui=%ix%i vid=%ix%i glConfigUi=%ix%i glConfigVid=%ix%i\n",
						upscale, engineWindowState.uiViewportWidth, engineWindowState.uiViewportHeight,
						engineWindowState.vidWidth, engineWindowState.vidHeight,
						glConfig.uiViewportWidth, glConfig.uiViewportHeight,
						glConfig.vidWidth, glConfig.vidHeight );
	}
	return Max( Q4_TTF_MIN_UPSCALE, Min( Q4_TTF_MAX_UPSCALE, upscale ) );
}

class q4TTFFace {
public:
	idStr			name;
	idTrueTypeFont	face;
};

class q4TTFFontManager {
public:
					q4TTFFontManager() {}

	idTrueTypeFont *FindFace( const char *fontName );
	void			Shutdown();

private:
	idList<q4TTFFace *> faces;
};

static q4TTFFontManager ttfFonts;

/*
================
q4TTFFontManager::FindFace

'fontName' arrives as the resolved bitmap path, e.g. "fonts/english/chain".
The .ttf files are language independent, so the language folder is tried first
and then the shared location.
================
*/
idTrueTypeFont *q4TTFFontManager::FindFace( const char *fontName ) {
	idStr key = fontName;
	key.BackSlashesToSlashes();
	key.StripFileExtension();

	for ( int i = 0; i < faces.Num(); i++ ) {
		if ( faces[i]->name.Icmp( key ) == 0 ) {
			return faces[i]->face.IsLoaded() ? &faces[i]->face : NULL;
		}
	}

	idStr candidates[2];
	candidates[0] = key + ".ttf";

	// "fonts/english/chain" -> "fonts/chain"
	idStr shared = key;
	int firstSlash = shared.Find( '/' );
	int lastSlash = shared.Last( '/' );
	if ( firstSlash >= 0 && lastSlash > firstSlash ) {
		shared = shared.Left( firstSlash + 1 ) + shared.Right( shared.Length() - lastSlash - 1 );
	}
	candidates[1] = shared + ".ttf";

	q4TTFFace *entry = new q4TTFFace();
	entry->name = key;
	faces.Append( entry );

	for ( int i = 0; i < 2; i++ ) {
		if ( i == 1 && candidates[1].Icmp( candidates[0] ) == 0 ) {
			break;
		}
		void *buffer = NULL;
		const int length = fileSystem->ReadFile( candidates[i].c_str(), &buffer );
		if ( buffer == NULL || length <= 0 ) {
			continue;
		}
		const bool loaded = entry->face.Load( (const byte *)buffer, length );
		fileSystem->FreeFile( buffer );
		if ( loaded ) {
			common->Printf( "TTF font: loaded %s (%i glyphs, %i upem)\n",
							candidates[i].c_str(), entry->face.NumGlyphs(), entry->face.UnitsPerEm() );
			return &entry->face;
		}
		common->Warning( "TTF font: '%s' is not a usable TrueType file", candidates[i].c_str() );
	}

	return NULL;
}

void q4TTFFontManager::Shutdown() {
	faces.DeleteContents( true );
}

/*
================
R_TTFCreateAtlasMaterial

The atlas lives in a generated image whose name starts with an underscore, so
the image manager hands the same object to every reference regardless of the
sampler parameters asked for.  The material is created from source text rather
than shipped as a .mtr, which keeps this feature inside the executable.
================
*/
static const idMaterial *R_TTFCreateAtlasMaterial( const char *imageName, const char *materialName ) {
	// Only ever created here, so finding one means a previous registration
	// already built it; makeDefault is off so a miss really is a miss.
	const idMaterial *existing = declManager->FindMaterial( materialName, false );
	if ( existing != NULL ) {
		existing->SetSort( SS_GUI );
		return existing;
	}

	idStr source;
	source += va( "%s\n", materialName );
	source += "{\n";
	source += "\t{\n";
	source += "\t\tblend blend\n";
	source += "\t\tcolored\n";
	source += "\t\tnopicmip\n";
	source += "\t\tlinear\n";
	source += "\t\tclamp\n";
	source += va( "\t\tmap %s\n", imageName );
	source += "\t}\n";
	source += "}\n";

	idMaterial *material = static_cast<idMaterial *>( declManager->CreateNewDecl( DECL_MATERIAL, materialName, "openq4_ttf_fonts.mtr" ) );
	if ( material == NULL ) {
		return NULL;
	}
	material->SetText( source.c_str() );
	material->Parse( source.c_str(), source.Length() );
	material->SetSort( SS_GUI );
	return material;
}

/*
================
R_TTFBuildSlot

Rasterises codepoints 32..255 for one point size, packs them into a page and
fills in the fontInfo_t the GUI will read.
================
*/
static bool R_TTFBuildSlot( idTrueTypeFont &face, const char *fontName, const q4TTFSlotSpec_t &slot,
							float upscale, fontInfo_t &out, float &outMaxWidth, float &outMaxHeight ) {
	memset( &out, 0, sizeof( out ) );
	outMaxWidth = 0.0f;
	outMaxHeight = 0.0f;

	const int padding = R_TTFGlyphPadding( slot.pointSize, upscale );
	// Glyph area grows with the square of the rasterisation scale, so at 4K the
	// large slot would otherwise want a 4096x4096 page - 64MB for one size of
	// one face. Predict the area from the glyph metrics, which needs no
	// rasterising, and pull this slot's scale back until it fits the cap. The
	// clamp only ever engages above 1440p, where 3x is already well past the
	// point of visible return.
	float slotUpscale = upscale;
	{
		double requiredArea = 0.0;
		const float probeScale = face.ScaleForPixelHeight( (float)slot.pointSize * upscale );
		for ( int code = Q4_TTF_FIRST_CODE; code <= Q4_TTF_LAST_CODE; code++ ) {
			ttGlyphMetrics_t metrics;
			const int glyphIndex = face.GlyphForCodepoint( R_TTFCodepointForByte( code ) );
			if ( glyphIndex == 0 || !face.GetGlyphMetrics( glyphIndex, metrics ) ) {
				continue;
			}
			if ( metrics.xMax <= metrics.xMin || metrics.yMax <= metrics.yMin ) {
				continue;
			}
			const double width = ( metrics.xMax - metrics.xMin ) * probeScale + 2.0 + padding * 2.0;
			const double height = ( metrics.yMax - metrics.yMin ) * probeScale + 2.0 + padding * 2.0;
			requiredArea += width * height;
		}
		const double capArea = (double)Q4_TTF_MAX_PAGE_AREA;
		if ( requiredArea * Q4_TTF_PACKING_HEADROOM > capArea ) {
			slotUpscale = upscale * (float)sqrt( capArea / ( requiredArea * Q4_TTF_PACKING_HEADROOM ) );
			slotUpscale = Max( Q4_TTF_MIN_UPSCALE, slotUpscale );
		}
	}

	const float pixelEm = (float)slot.pointSize * slotUpscale;
	const float scale = face.ScaleForPixelHeight( pixelEm );
	if ( scale <= 0.0f ) {
		return false;
	}
	// Converts font design units straight to the slot's metric units.
	const float unitsToPoint = (float)slot.pointSize / (float)face.UnitsPerEm();
	const float pixelsToPoint = 1.0f / slotUpscale;

	const int count = Q4_TTF_LAST_CODE - Q4_TTF_FIRST_CODE + 1;
	q4TTFRaster_t *rasters = (q4TTFRaster_t *)Mem_ClearedAlloc( count * sizeof( q4TTFRaster_t ) );

	int totalArea = 0;
	int widest = 0;
	int tallest = 0;
	int rendered = 0;

	for ( int i = 0; i < count; i++ ) {
		const int code = Q4_TTF_FIRST_CODE + i;
		const int codepoint = R_TTFCodepointForByte( code );
		const int glyphIndex = face.GlyphForCodepoint( codepoint );

		ttGlyphMetrics_t metrics;
		if ( !face.GetGlyphMetrics( glyphIndex, metrics ) ) {
			continue;
		}
		rasters[i].advance = metrics.advance;
		rasters[i].valid = true;

		if ( glyphIndex == 0 && codepoint != 0 ) {
			// Not covered by this face; leave a blank slot but keep the advance.
			continue;
		}
		if ( !face.RasterizeGlyph( glyphIndex, scale, rasters[i].bitmap ) ) {
			continue;
		}
		if ( rasters[i].bitmap.pixels == NULL ) {
			continue;
		}

		const int paddedWidth = rasters[i].bitmap.width + padding * 2;
		const int paddedHeight = rasters[i].bitmap.height + padding * 2;
		totalArea += paddedWidth * paddedHeight;
		widest = Max( widest, paddedWidth );
		tallest = Max( tallest, paddedHeight );
		rendered++;
	}

	if ( rendered == 0 ) {
		Mem_Free( rasters );
		return false;
	}

	// Choose the smallest power-of-two page the shelf packer can actually fill.
	int pageWidth = Q4_TTF_MIN_PAGE;
	int pageHeight = Q4_TTF_MIN_PAGE;
	while ( pageWidth < widest && pageWidth < Q4_TTF_MAX_PAGE ) {
		pageWidth <<= 1;
	}
	while ( pageHeight < tallest && pageHeight < Q4_TTF_MAX_PAGE ) {
		pageHeight <<= 1;
	}
	while ( (double)pageWidth * pageHeight < (double)totalArea * Q4_TTF_PACKING_HEADROOM ) {
		if ( pageWidth <= pageHeight && pageWidth < Q4_TTF_MAX_PAGE ) {
			pageWidth <<= 1;
		} else if ( pageHeight < Q4_TTF_MAX_PAGE ) {
			pageHeight <<= 1;
		} else {
			break;
		}
	}

	byte *page = NULL;
	q4TTFShelfPacker packer;
	bool packed = false;

	for ( int attempt = 0; attempt < 6 && !packed; attempt++ ) {
		packer.Init( pageWidth, pageHeight );
		packed = true;
		for ( int i = 0; i < count && packed; i++ ) {
			if ( rasters[i].bitmap.pixels == NULL ) {
				continue;
			}
			int x = 0;
			int y = 0;
			if ( !packer.Place( rasters[i].bitmap.width + padding * 2,
								rasters[i].bitmap.height + padding * 2, x, y ) ) {
				packed = false;
			}
		}
		if ( packed ) {
			break;
		}
		if ( pageWidth <= pageHeight && pageWidth < Q4_TTF_MAX_PAGE ) {
			pageWidth <<= 1;
		} else if ( pageHeight < Q4_TTF_MAX_PAGE ) {
			pageHeight <<= 1;
		} else {
			break;
		}
	}

	if ( !packed ) {
		common->Warning( "TTF font: '%s' %ipt does not fit a %ix%i atlas", fontName, slot.pointSize, pageWidth, pageHeight );
		for ( int i = 0; i < count; i++ ) {
			idTrueTypeFont::FreeGlyphBitmap( rasters[i].bitmap );
		}
		Mem_Free( rasters );
		return false;
	}

	page = (byte *)Mem_ClearedAlloc( pageWidth * pageHeight );

	// Re-run the packer, this time actually blitting and recording the layout.
	packer.Init( pageWidth, pageHeight );
	for ( int i = 0; i < count; i++ ) {
		const int code = Q4_TTF_FIRST_CODE + i;
		glyphInfo_t &glyph = out.glyphs[code];

		if ( !rasters[i].valid ) {
			continue;
		}
		glyph.horiAdvance = rasters[i].advance * unitsToPoint;

		if ( rasters[i].bitmap.pixels == NULL ) {
			continue;
		}

		const ttGlyphBitmap_t &bitmap = rasters[i].bitmap;
		int x = 0;
		int y = 0;
		packer.Place( bitmap.width + padding * 2, bitmap.height + padding * 2, x, y );

		const int destX = x + padding;
		const int destY = y + padding;
		for ( int row = 0; row < bitmap.height; row++ ) {
			memcpy( page + ( destY + row ) * pageWidth + destX, bitmap.pixels + row * bitmap.width, bitmap.width );
		}

		glyph.width = bitmap.width * pixelsToPoint;
		glyph.height = bitmap.height * pixelsToPoint;
		glyph.horiBearingX = bitmap.left * pixelsToPoint;
		glyph.horiBearingY = -bitmap.top * pixelsToPoint;
		glyph.s = (float)destX / (float)pageWidth;
		glyph.t = (float)destY / (float)pageHeight;
		glyph.s2 = (float)( destX + bitmap.width ) / (float)pageWidth;
		glyph.t2 = (float)( destY + bitmap.height ) / (float)pageHeight;

		outMaxWidth = Max( outMaxWidth, glyph.width );
		outMaxHeight = Max( outMaxHeight, glyph.height );
	}

	for ( int i = 0; i < count; i++ ) {
		idTrueTypeFont::FreeGlyphBitmap( rasters[i].bitmap );
	}
	Mem_Free( rasters );

	// Upload.  The retail atlases are white RGB with coverage in alpha, and the
	// GUI blend path is built around exactly that, so the page is expanded to
	// the same layout rather than relying on a single-channel swizzle.
	idStr safeName = fontName;
	safeName.Replace( "/", "_" );
	const idStr imageName = va( "_ttfatlas_%s_%i", safeName.c_str(), slot.pointSize );
	const idStr materialName = va( "openprey/ttffont/%s_%i", safeName.c_str(), slot.pointSize );

	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA8;
	opts.colorFormat = CFM_DEFAULT;
	opts.width = pageWidth;
	opts.height = pageHeight;
	opts.numLevels = 1;

	idImage *image = globalImages->ScratchImage( imageName.c_str(), &opts, TF_LINEAR, TR_CLAMP, TD_LOOKUP_TABLE_RGBA );
	if ( image == NULL ) {
		Mem_Free( page );
		return false;
	}

	const int texels = pageWidth * pageHeight;
	byte *rgbaPage = (byte *)Mem_Alloc( texels * 4 );
	for ( int i = 0; i < texels; i++ ) {
		rgbaPage[i * 4 + 0] = 255;
		rgbaPage[i * 4 + 1] = 255;
		rgbaPage[i * 4 + 2] = 255;
		rgbaPage[i * 4 + 3] = page[i];
	}
	image->SubImageUpload( 0, 0, 0, 0, pageWidth, pageHeight, rgbaPage );
	Mem_Free( rgbaPage );

	if ( r_ttfFontDebug.GetBool() ) {
		// Dump what was handed to the GPU, so a glyph problem can be told apart
		// from an upload or sampling one without guessing.
		byte *rgba = (byte *)Mem_Alloc( pageWidth * pageHeight * 4 );
		for ( int i = 0; i < pageWidth * pageHeight; i++ ) {
			rgba[i * 4 + 0] = page[i];
			rgba[i * 4 + 1] = page[i];
			rgba[i * 4 + 2] = page[i];
			rgba[i * 4 + 3] = 255;
		}
		R_WriteTGA( va( "ttfatlas/%s.tga", imageName.c_str() ), rgba, pageWidth, pageHeight, false, "fs_savepath" );
		Mem_Free( rgba );
		common->Printf( "TTF font: %s atlas %ix%i, %i glyphs, format=%i levels=%i\n",
						imageName.c_str(), pageWidth, pageHeight, rendered, (int)opts.format, opts.numLevels );
	}

	Mem_Free( page );

	const idMaterial *material = R_TTFCreateAtlasMaterial( imageName.c_str(), materialName.c_str() );
	if ( material == NULL ) {
		return false;
	}

	if ( r_ttfFontDebug.GetBool() ) {
		const int stages = material->GetNumStages();
		const shaderStage_t *stage = ( stages > 0 ) ? material->GetStage( 0 ) : NULL;
		const idImage *bound = ( stage != NULL ) ? stage->texture.image : NULL;
		common->Printf( "TTF font: material '%s' stages=%i boundImage=%s %ix%i (atlas image %ix%i)\n",
						materialName.c_str(), stages,
						bound != NULL ? bound->GetName() : "<none>",
						bound != NULL ? bound->GetUploadWidth() : -1,
						bound != NULL ? bound->GetUploadHeight() : -1,
						image->GetUploadWidth(), image->GetUploadHeight() );
	}

	out.material = material;
	out.pointSize = (float)slot.pointSize;
	out.ascender = face.Ascender() * unitsToPoint;
	out.descender = -face.Descender() * unitsToPoint;
	out.fontHeight = out.ascender + out.descender;
	idStr::Copynz( out.name, va( "ttf/%s_%i", safeName.c_str(), slot.pointSize ), sizeof( out.name ) );

	return true;
}

}

/*
============
R_RegisterTrueTypeFont

Fills 'font' from a shipped .ttf face, matching the layout the bitmap loader
produces.  Returns false when no usable face exists, so the caller can fall
back to the retail atlases.
============
*/
bool R_RegisterTrueTypeFont( const char *fontName, fontInfoEx_t &font ) {
	if ( !r_useTrueTypeFonts.GetBool() ) {
		return false;
	}

	idTrueTypeFont *face = ttfFonts.FindFace( fontName );
	if ( face == NULL || !face->IsLoaded() ) {
		return false;
	}

	const float upscale = R_TTFUpscale();

	fontInfo_t *slots[3] = { &font.fontInfoSmall, &font.fontInfoMedium, &font.fontInfoLarge };
	float *maxWidths[3] = { &font.maxWidthSmall, &font.maxWidthMedium, &font.maxWidthLarge };
	float *maxHeights[3] = { &font.maxHeightSmall, &font.maxHeightMedium, &font.maxHeightLarge };

	int built = 0;
	for ( int i = 0; i < 3; i++ ) {
		if ( R_TTFBuildSlot( *face, fontName, Q4_TTF_SLOTS[i], upscale, *slots[i], *maxWidths[i], *maxHeights[i] ) ) {
			built++;
		}
	}

	if ( built != 3 ) {
		common->Warning( "TTF font: '%s' only produced %i of 3 sizes; using the bitmap font", fontName, built );
		return false;
	}

	return true;
}

/*
============
R_BuildConsoleFontAtlas

The console and the loading screen do not go through the font system at all:
they slice characters straight out of the 'bigchars' sheet, a 16x16 grid of
16x16 pixel cells indexed by byte.  At any modern resolution that sheet is
being magnified several times over, which is why console text is the blurriest
text in the game.

Rather than reworking those draw paths, this rebuilds the sheet itself at the
resolution the display needs - same 16x16 grid, same cell indexing, just larger
cells - and points the existing material at it.  Every caller keeps working
unchanged because the UV maths is purely fractional.
============
*/
bool R_BuildConsoleFontAtlas( void ) {
	if ( !r_useTrueTypeFonts.GetBool() ) {
		return false;
	}

	idTrueTypeFont *face = ttfFonts.FindFace( "fonts/bigchars" );
	if ( face == NULL || !face->IsLoaded() ) {
		return false;
	}

	// Keep the whole sheet inside the maximum texture the atlas code allows.
	const float upscale = R_TTFUpscale();
	int cellPixels = (int)idMath::Ceil( (float)Q4_CONSOLE_CELL_SIZE * upscale );
	cellPixels = Max( Q4_CONSOLE_CELL_SIZE, Min( Q4_TTF_MAX_PAGE / Q4_CONSOLE_GRID, cellPixels ) );

	const int pageSize = cellPixels * Q4_CONSOLE_GRID;
	const float scale = (float)cellPixels / (float)face->UnitsPerEm();
	// The face is built with one em to a cell, so its ascender is exactly how
	// far the baseline sits below the top of a cell.
	const int baselineOffset = (int)idMath::Rint( face->Ascender() * scale );

	byte *page = (byte *)Mem_ClearedAlloc( pageSize * pageSize );
	int rendered = 0;

	for ( int code = 32; code <= 255; code++ ) {
		const int codepoint = R_TTFCodepointForByte( code );
		const int glyphIndex = face->GlyphForCodepoint( codepoint );
		if ( glyphIndex == 0 ) {
			continue;
		}

		ttGlyphBitmap_t bitmap;
		if ( !face->RasterizeGlyph( glyphIndex, scale, bitmap ) || bitmap.pixels == NULL ) {
			continue;
		}

		const int cellX = ( code & 15 ) * cellPixels;
		const int cellY = ( code >> 4 ) * cellPixels;
		const int originX = cellX + bitmap.left;
		const int originY = cellY + baselineOffset + bitmap.top;

		// Clip to the cell. The console samples exactly one cell, so anything
		// spilling over would surface as a fragment of the neighbouring glyph.
		for ( int row = 0; row < bitmap.height; row++ ) {
			const int destY = originY + row;
			if ( destY < cellY || destY >= cellY + cellPixels ) {
				continue;
			}
			for ( int column = 0; column < bitmap.width; column++ ) {
				const int destX = originX + column;
				if ( destX < cellX || destX >= cellX + cellPixels ) {
					continue;
				}
				byte &target = page[destY * pageSize + destX];
				const byte source = bitmap.pixels[row * bitmap.width + column];
				if ( source > target ) {
					target = source;
				}
			}
		}

		idTrueTypeFont::FreeGlyphBitmap( bitmap );
		rendered++;
	}

	if ( rendered == 0 ) {
		Mem_Free( page );
		return false;
	}

	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA8;
	opts.colorFormat = CFM_DEFAULT;
	opts.width = pageSize;
	opts.height = pageSize;
	opts.numLevels = 1;

	idImage *image = globalImages->ScratchImage( Q4_CONSOLE_ATLAS_IMAGE, &opts, TF_LINEAR, TR_CLAMP, TD_LOOKUP_TABLE_RGBA );
	if ( image == NULL ) {
		Mem_Free( page );
		return false;
	}

	const int texels = pageSize * pageSize;
	byte *rgbaPage = (byte *)Mem_Alloc( texels * 4 );
	for ( int i = 0; i < texels; i++ ) {
		rgbaPage[i * 4 + 0] = 255;
		rgbaPage[i * 4 + 1] = 255;
		rgbaPage[i * 4 + 2] = 255;
		rgbaPage[i * 4 + 3] = page[i];
	}
	image->SubImageUpload( 0, 0, 0, 0, pageSize, pageSize, rgbaPage );
	Mem_Free( rgbaPage );

	if ( r_ttfFontDebug.GetBool() ) {
		byte *dump = (byte *)Mem_Alloc( texels * 4 );
		for ( int i = 0; i < texels; i++ ) {
			dump[i * 4 + 0] = page[i];
			dump[i * 4 + 1] = page[i];
			dump[i * 4 + 2] = page[i];
			dump[i * 4 + 3] = 255;
		}
		R_WriteTGA( "ttfatlas/console.tga", dump, pageSize, pageSize, false, "fs_savepath" );
		Mem_Free( dump );
	}
	Mem_Free( page );

	// Retarget the existing material rather than introducing a new name, so
	// the console and the loading screen pick it up without either of them
	// having to know this path exists. The replacement keeps the retail stage
	// keywords so blending, depth and filtering behave exactly as before.
	idMaterial *material = const_cast<idMaterial *>( declManager->FindMaterial( Q4_CONSOLE_FONT_MATERIAL, false ) );
	if ( material == NULL ) {
		return false;
	}

	idStr source;
	source += va( "%s\n", Q4_CONSOLE_FONT_MATERIAL );
	source += "{\n";
	source += "\t{\n";
	source += "\t\tblend blend\n";
	source += "\t\tmaskDepth\n";
	source += "\t\tcolored\n";
	source += "\t\tnopicmip\n";
	source += "\t\tlinear\n";
	source += "\t\tclamp\n";
	source += va( "\t\tmap %s\n", Q4_CONSOLE_ATLAS_IMAGE );
	source += "\t}\n";
	source += "}\n";
	material->SetText( source.c_str() );
	material->Parse( source.c_str(), source.Length() );
	material->SetSort( SS_GUI );

	const shaderStage_t *stage = ( material->GetNumStages() > 0 ) ? material->GetStage( 0 ) : NULL;
	const idImage *bound = ( stage != NULL ) ? stage->texture.image : NULL;
	if ( bound != image ) {
		common->Warning( "TTF font: console material did not take the rebuilt sheet" );
		return false;
	}

	common->Printf( "TTF font: console sheet rebuilt at %ix%i (%i px cells, %i glyphs), '%s' now samples %s\n",
					pageSize, pageSize, cellPixels, rendered, Q4_CONSOLE_FONT_MATERIAL, bound->GetName() );
	return true;
}

/*
============
R_ShutdownTrueTypeFonts
============
*/
void R_ShutdownTrueTypeFonts( void ) {
	ttfFonts.Shutdown();
}
