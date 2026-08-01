/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/




#include "DeviceContext.h"
#include "UserInterface.h"

idVec4 idDeviceContext::colorPurple;
idVec4 idDeviceContext::colorOrange;
idVec4 idDeviceContext::colorYellow;
idVec4 idDeviceContext::colorGreen;
idVec4 idDeviceContext::colorBlue;
idVec4 idDeviceContext::colorRed;
idVec4 idDeviceContext::colorBlack;
idVec4 idDeviceContext::colorWhite;
idVec4 idDeviceContext::colorNone;


idCVar gui_smallFontLimit( "gui_smallFontLimit", "0.10", CVAR_GUI | CVAR_ARCHIVE, "" );
idCVar gui_mediumFontLimit( "gui_mediumFontLimit", "0.60", CVAR_GUI | CVAR_ARCHIVE, "" );

// Accessibility: Quake 4 draws most of its text straight over the world and over
// busy panel artwork, which leaves low-vision players with very little contrast
// to work with. A solid backing behind each line fixes the contrast ratio no
// matter what is underneath.
idCVar gui_textBackground( "gui_textBackground", "0", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT,
						   "accessibility: opacity of a solid black backing drawn behind menu and HUD text; 0 is off, 1 is fully opaque", 0.0f, 1.0f );
idCVar gui_textBackgroundPadding( "gui_textBackgroundPadding", "2", CVAR_GUI | CVAR_ARCHIVE | CVAR_FLOAT,
								  "accessibility: how far the text backing extends past the text, in 640x480 virtual units", 0.0f, 16.0f );


idList<fontInfoEx_t> idDeviceContext::fonts;

namespace {

static const float Q4_GUI_FONT_BASE_POINT_SIZE = 48.0f;
static const float Q4_TEXT_BRIGHTNESS_STEP = 0.1f;
static const float Q4_TEXT_RGB_ESCAPE_SCALE = 1.0f / 9.0f;
static const float Q4_TEXT_OUTLINE_DARK_THRESHOLD = 0.2f;
static const float Q4_TEXT_STYLE_OFFSET = 1.0f;
static const float Q4_TEXT_LINE_SPACING_SCALE = 1.25f;
static const float Q4_GLYPH_HORIZONTAL_GUARD_TEXELS = 0.5f;
static const float Q4_GLYPH_SMALL_ATLAS_HORIZONTAL_GUARD_TEXELS = 1.0f;
static const float Q4_GLYPH_SMALL_MARINE_CLIP_RIGHT_PAD_TEXELS = 2.0f;
static const float Q4_GLYPH_SMALL_FONT_MAX_POINT_SIZE = 12.0f;
// Retail guis/cinematic.gui used a 640x480 desktop with 60px top/bottom bars,
// leaving a 640x360 cinematic view.
static const float Q4_CINEMATIC_RETAIL_VISIBLE_WIDTH = 640.0f;
static const float Q4_CINEMATIC_RETAIL_VISIBLE_HEIGHT = 360.0f;
static const float Q4_CINEMATIC_RETAIL_ASPECT = Q4_CINEMATIC_RETAIL_VISIBLE_WIDTH / Q4_CINEMATIC_RETAIL_VISIBLE_HEIGHT;
static const int Q4_TEXT_STYLE_SHADOW = 1;
static const int Q4_TEXT_STYLE_OUTLINE = 2;
static const int Q4_TEXT_ALIGN_VERTICAL_CENTER = 3;
static const int Q4_TEXT_CURSOR_NONE = -1;
static const int Q4_TEXT_LINE_BUFFER_SIZE = 1024;
static const int Q4_TEXT_REPEAT_ESCAPE_MAX = 9;
static const int Q4_EMBEDDED_ICON_FULL_IMAGE = -1;
static const unsigned char Q4_INSERT_CURSOR_GLYPH = '|';
static const unsigned char Q4_OVERSTRIKE_CURSOR_GLYPH = '_';
static const unsigned char Q4_EMBEDDED_ICON_REFERENCE_GLYPH = 'W';

enum q4EmbeddedIconMeasure_t {
	Q4_EMBEDDED_ICON_DRAW_WIDTH,
	Q4_EMBEDDED_ICON_REGISTERED_WIDTH
};

static int openQ4_TextEscapeLength( const char *text, int *type = NULL ) {
	return idStr::IsEscape( text, type );
}

static bool openQ4_IsRepeatTextEscape( const char *escape, int escapeLength ) {
	return escapeLength > 2 && escape != NULL && ( escape[1] == 'N' || escape[1] == 'n' );
}

static int openQ4_TextEscapeRepeatCount( const char *escape ) {
	int repeats = static_cast<unsigned char>( escape[2] ) - '0';
	if ( repeats < 0 ) {
		repeats = 0;
	} else if ( repeats >= Q4_TEXT_REPEAT_ESCAPE_MAX ) {
		repeats = Q4_TEXT_REPEAT_ESCAPE_MAX;
	}
	return repeats;
}

static float openPREY_RetailSmoothStep( float t ) {
	t = idMath::ClampFloat( 0.0f, 1.0f, t );
	return t * t * ( 3.0f - 2.0f * t );
}

static float openPREY_RetailBezier1D( float a, float b, float c, float d, float t ) {
	const float invT = 1.0f - t;
	const float invT2 = invT * invT;
	const float t2 = t * t;
	return invT2 * invT * a + 3.0f * invT2 * t * b + 3.0f * invT * t2 * c + t2 * t * d;
}

static unsigned int openPREY_RetailHash( unsigned int value ) {
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return value;
}

static float openPREY_RetailHashFloat( unsigned int seed ) {
	return static_cast<float>( openPREY_RetailHash( seed ) & 0x00ffffffu ) * ( 1.0f / 16777215.0f );
}

static float openPREY_RetailHashSigned( unsigned int seed ) {
	return openPREY_RetailHashFloat( seed ) * 2.0f - 1.0f;
}

static bool openPREY_IsTrueTypeFont( const fontInfo_t *font ) {
	return font != NULL && idStr::Icmpn( font->name, "ttf/", 4 ) == 0;
}

static void openPREY_LogTTFSplineNotice() {
	static bool warned = false;
	if ( !warned ) {
		warned = true;
		common->Warning( "Retail spline text is currently available only for bitmap GUI fonts; drawing TrueType text without the effect" );
	}
}

// Count the things that actually occupy a glyph slot in the current text
// dialect. Colour/brightness escapes do not count; embedded icons and their
// repeat escapes do. This keeps the scatter distribution stable when retail
// strings mix ordinary glyphs with variable-length escape sequences.
static int openPREY_CountRetailGlyphs( const unsigned char *text, int len ) {
	int glyphs = 0;
	int consumed = 0;
	while ( text != NULL && *text != '\0' && consumed < len ) {
		int escapeType = 0;
		const int escapeLength = openQ4_TextEscapeLength( reinterpret_cast<const char *>( text ), &escapeType );
		if ( escapeLength > 0 ) {
			if ( openQ4_IsRepeatTextEscape( reinterpret_cast<const char *>( text ), escapeLength ) ) {
				int payloadType = 0;
				const int payloadLength = openQ4_TextEscapeLength( reinterpret_cast<const char *>( text + escapeLength ), &payloadType );
				if ( payloadLength > 0 ) {
					if ( payloadType == S_ESCAPE_ICON ) {
						glyphs += openQ4_TextEscapeRepeatCount( reinterpret_cast<const char *>( text ) );
					}
					text += escapeLength + payloadLength;
					consumed += escapeLength + payloadLength;
					continue;
				}
			}
			if ( escapeType == S_ESCAPE_ICON ) {
				++glyphs;
			}
			text += escapeLength;
			consumed += escapeLength;
			continue;
		}

		++glyphs;
		++text;
		++consumed;
	}
	return glyphs;
}

struct openPreyRetailSplineGlyph_t {
	float drawX;
	float drawY;
	float ghostX;
	float ghostY;
	float glyphAlpha;
	float ghostAlpha;
	bool drawGhost;
};

static openPreyRetailSplineGlyph_t openPREY_RetailSplineGlyph( float targetX, float targetY,
		float lineStartX, float lineWidth, float lineHeight, int glyphIndex, int glyphCount,
		unsigned int glyphCode, float effectProgress, int splinePoints ) {
	openPreyRetailSplineGlyph_t result = {};
	const float normalizedIndex = glyphCount > 1 ? static_cast<float>( glyphIndex ) / static_cast<float>( glyphCount - 1 ) : 0.5f;
	const unsigned int seedBase = static_cast<unsigned int>( glyphIndex * 131 ) + glyphCode * 911u + static_cast<unsigned int>( splinePoints ) * 37u;
	const float startDelay = 0.03f + 0.09f * openPREY_RetailHashFloat( seedBase + 0x11u );
	const float progressDenom = Max( 0.001f, 1.0f - startDelay );
	const float localProgress = openPREY_RetailSmoothStep( idMath::ClampFloat( 0.0f, 1.0f, ( effectProgress - startDelay ) / progressDenom ) );
	const float scatter = 1.0f - localProgress;
	const float pointScale = 1.0f + 0.08f * static_cast<float>( splinePoints - 3 );
	const float cloudCenterX = lineStartX + lineWidth * ( 0.66f + 0.08f * openPREY_RetailHashFloat( seedBase + 0x23u ) );
	const float cloudCenterY = targetY - lineHeight * ( 0.20f - 0.55f * openPREY_RetailHashSigned( seedBase + 0x29u ) );
	const float spreadX = idMath::ClampFloat( lineHeight * 1.6f, lineWidth + lineHeight * 4.0f,
		lineWidth * ( 0.30f + 0.02f * static_cast<float>( splinePoints ) ) + lineHeight * 2.25f );
	const float spreadY = idMath::ClampFloat( lineHeight * 0.85f, lineHeight * 5.0f,
		lineHeight * ( 1.45f + 0.10f * static_cast<float>( splinePoints ) ) );
	const float lateralBias = ( 0.5f - normalizedIndex ) * lineWidth * 0.28f;
	const float startX = cloudCenterX + lateralBias + openPREY_RetailHashSigned( seedBase + 0x31u ) * spreadX;
	const float startY = cloudCenterY + openPREY_RetailHashSigned( seedBase + 0x37u ) * spreadY;
	const float control1X = cloudCenterX + openPREY_RetailHashSigned( seedBase + 0x41u ) * spreadX * ( 0.55f + 0.08f * pointScale );
	const float control1Y = cloudCenterY + openPREY_RetailHashSigned( seedBase + 0x47u ) * spreadY * ( 0.55f + 0.05f * pointScale );
	const float control2X = targetX + lateralBias * scatter * 0.35f + openPREY_RetailHashSigned( seedBase + 0x53u ) * spreadX * scatter * 0.18f;
	const float control2Y = targetY + openPREY_RetailHashSigned( seedBase + 0x59u ) * spreadY * scatter * 0.16f;

	result.drawX = openPREY_RetailBezier1D( startX, control1X, control2X, targetX, localProgress );
	result.drawY = openPREY_RetailBezier1D( startY, control1Y, control2Y, targetY, localProgress );
	result.glyphAlpha = idMath::ClampFloat( 0.0f, 1.0f, 0.18f + 0.82f * idMath::Sqrt( localProgress ) );
	result.drawGhost = scatter > 0.12f;
	result.ghostAlpha = 0.32f * scatter;
	if ( result.drawGhost ) {
		const float ghostProgress = localProgress * 0.55f;
		result.ghostX = openPREY_RetailBezier1D( startX,
			cloudCenterX + openPREY_RetailHashSigned( seedBase + 0x5fu ) * spreadX * 0.8f,
			control2X, result.drawX, ghostProgress );
		result.ghostY = openPREY_RetailBezier1D( startY,
			cloudCenterY + openPREY_RetailHashSigned( seedBase + 0x61u ) * spreadY * 0.8f,
			control2Y, result.drawY, ghostProgress );
	}
	return result;
}

struct q4ScaledFont_t {
	const fontInfo_t *font;
	float renderScale;
	float maxWidth;
	float maxHeight;
};

struct q4VirtualScreenTransform_t {
	float xScale;
	float yScale;
	float xOffset;
	float yOffset;
};

static bool openQ4_ExtractIconCode( const char *escape, char code[4] ) {
	int escapeType = 0;
	if ( openQ4_TextEscapeLength( escape, &escapeType ) != 5 || escapeType != S_ESCAPE_ICON ) {
		return false;
	}

	code[0] = escape[2];
	code[1] = escape[3];
	code[2] = escape[4];
	code[3] = '\0';
	return true;
}

static float openQ4_FontRenderScale( const fontInfo_t *font, float scale ) {
	if ( font == NULL || font->pointSize == 0.0f ) {
		return 0.0f;
	}
	return scale / font->pointSize * Q4_GUI_FONT_BASE_POINT_SIZE;
}

static int openQ4_ScaledFontUnits( float fontScale, float units ) {
	return static_cast<int>( fontScale * units );
}

static int openQ4_RoundedGlyphAdvance( const glyphInfo_t *glyph ) {
	return static_cast<int>( idMath::Ceil( glyph->horiAdvance ) );
}

static int openQ4_GlyphAdvanceUnits( const glyphInfo_t *glyph, int adjust ) {
	return adjust + openQ4_RoundedGlyphAdvance( glyph );
}

static int openQ4_GlyphHeightUnits( const glyphInfo_t *glyph ) {
	return static_cast<int>( glyph->height );
}

static int openQ4_EmbeddedIconDimensionOrImageSize( int registeredDimension, float imageDimension ) {
	return registeredDimension == Q4_EMBEDDED_ICON_FULL_IMAGE ? static_cast<int>( imageDimension ) : registeredDimension;
}

static void openQ4_SetEmbeddedIconAxisUV( float &uv1, float &uv2, int registeredOffset, int registeredLength, float imageLength ) {
	if ( imageLength <= 0.0f ) {
		uv1 = 0.0f;
		uv2 = 0.0f;
		return;
	}

	if ( registeredOffset == Q4_EMBEDDED_ICON_FULL_IMAGE ) {
		uv1 = 0.0f;
		uv2 = 1.0f;
		return;
	}

	uv1 = static_cast<float>( registeredOffset ) / imageLength;
	uv2 = static_cast<float>( registeredOffset + registeredLength ) / imageLength;
}

static int openQ4_EmbeddedIconWidthUnits( float iconWidth, float iconHeight, float referenceHeight, q4EmbeddedIconMeasure_t measureMode = Q4_EMBEDDED_ICON_DRAW_WIDTH ) {
	if ( measureMode == Q4_EMBEDDED_ICON_REGISTERED_WIDTH ) {
		return static_cast<int>( iconWidth );
	}

	if ( referenceHeight <= 0.0f || iconWidth <= 0.0f || iconHeight <= 0.0f ) {
		return 0;
	}
	return static_cast<int>( iconWidth * ( referenceHeight / iconHeight ) );
}

static float openQ4_ScaledGlyphAdvance( float fontScale, const glyphInfo_t *glyph, float adjust ) {
	return idMath::Ceil( ( glyph->horiAdvance + adjust ) * fontScale );
}

static float openQ4_GlyphDrawX( float x, float fontScale, const glyphInfo_t *glyph ) {
	return x + fontScale * glyph->horiBearingX;
}

static float openQ4_GlyphDrawY( float y, float fontScale, const glyphInfo_t *glyph ) {
	return y - fontScale * glyph->horiBearingY;
}

static float openQ4_GlyphHorizontalGuardTexels( const fontInfo_t *font ) {
	return ( font != NULL && font->pointSize <= Q4_GLYPH_SMALL_FONT_MAX_POINT_SIZE ) ? Q4_GLYPH_SMALL_ATLAS_HORIZONTAL_GUARD_TEXELS : Q4_GLYPH_HORIZONTAL_GUARD_TEXELS;
}

static bool openQ4_IsSmallMarineFont( const fontInfo_t *font ) {
	return font != NULL && font->pointSize <= Q4_GLYPH_SMALL_FONT_MAX_POINT_SIZE && idStr::FindText( font->name, "marine_12.fontdat", false ) >= 0;
}

static bool openQ4_ApplyGlyphHorizontalGuard( const fontInfo_t *font, const glyphInfo_t *glyph, float fontScale, float &x, float &width, float &s1, float &s2 ) {
	if ( glyph == NULL || fontScale == 0.0f || width <= 0.0f || s2 <= s1 ) {
		return false;
	}

	const float atlasWidth = width / ( s2 - s1 );
	if ( atlasWidth <= 0.0f ) {
		return false;
	}

	const float guardTexels = openQ4_GlyphHorizontalGuardTexels( font );
	const float leftGuard = Min( guardTexels, Max( 0.0f, s1 * atlasWidth ) );
	const float rightGuard = Min( guardTexels, Max( 0.0f, ( 1.0f - s2 ) * atlasWidth ) );
	if ( leftGuard == 0.0f && rightGuard == 0.0f ) {
		return false;
	}

	x -= leftGuard * fontScale;
	width += leftGuard + rightGuard;
	s1 -= leftGuard / atlasWidth;
	s2 += rightGuard / atlasWidth;
	return true;
}

static float openQ4_GlyphVisibleRightEdge( float x, const fontInfo_t *font, float fontScale, const glyphInfo_t *glyph ) {
	if ( glyph == NULL || fontScale == 0.0f ) {
		return x;
	}

	float drawX = openQ4_GlyphDrawX( x, fontScale, glyph );
	float width = glyph->width;
	float s1 = glyph->s;
	float s2 = glyph->s2;
	openQ4_ApplyGlyphHorizontalGuard( font, glyph, fontScale, drawX, width, s1, s2 );
	return drawX + width * fontScale;
}

static float openQ4_GlyphClipRightPad( const fontInfo_t *font, const glyphInfo_t *glyph, float fontScale ) {
	if ( !openQ4_IsSmallMarineFont( font ) || glyph == NULL || fontScale <= 0.0f ) {
		return 0.0f;
	}
	return idMath::Ceil( Q4_GLYPH_SMALL_MARINE_CLIP_RIGHT_PAD_TEXELS * fontScale );
}

static bool openQ4_HasRenderableFont( const q4ScaledFont_t &scaledFont ) {
	return scaledFont.font != NULL && scaledFont.renderScale != 0.0f;
}

static const idMaterial *openPREY_ResolveGlyphMaterial( const fontInfo_t *font, const glyphInfo_t *glyph ) {
	if ( glyph != NULL && glyph->material != NULL ) {
		return glyph->material;
	}
	return font != NULL ? font->material : NULL;
}

// How far a font's glyphs actually reach either side of the baseline, in the
// font's own units.
struct q4TextInkExtents_t {
	float	ascent;
	float	descent;
};

/*
================
openQ4_FontInkExtents

Measured from the glyphs rather than read out of fontInfo_t.  The retail
ascender and descender fields do not describe where the ink is: the marine font
declares an ascender of 29.9 while its capitals reach 40.4, and declares no
descender at all despite having glyphs 11 units below the baseline.  A backing
box built from those numbers would clip the text it exists to sit behind.
================
*/
static void openQ4_FontInkExtents( const fontInfo_t *font, q4TextInkExtents_t &extents ) {
	extents.ascent = 0.0f;
	extents.descent = 0.0f;
	if ( font == NULL ) {
		return;
	}
	for ( int i = 0; i < GLYPHS_PER_FONT; i++ ) {
		const glyphInfo_t *glyph = &font->glyphs[i];
		if ( glyph->height <= 0.0f ) {
			continue;
		}
		extents.ascent = Max( extents.ascent, glyph->horiBearingY );
		extents.descent = Max( extents.descent, glyph->height - glyph->horiBearingY );
	}
}

/*
================
openQ4_TextBackgroundRect

Places the accessibility backing for one line of text.  The box is anchored on
the baseline and sized from the font's ink extents, so every line of a given
font gets the same height whatever characters it happens to contain.
================
*/
static bool openQ4_TextBackgroundRect( const q4ScaledFont_t &scaledFont, float x, float baselineY,
									   float textWidth, float padding,
									   float &outX, float &outY, float &outWidth, float &outHeight ) {
	if ( textWidth <= 0.0f || !openQ4_HasRenderableFont( scaledFont ) ) {
		return false;
	}

	q4TextInkExtents_t ink;
	openQ4_FontInkExtents( scaledFont.font, ink );

	const float ascent = ink.ascent * scaledFont.renderScale;
	const float descent = ink.descent * scaledFont.renderScale;
	if ( ascent + descent <= 0.0f ) {
		return false;
	}

	outX = x - padding;
	outY = baselineY - ascent - padding;
	outWidth = textWidth + padding * 2.0f;
	outHeight = ascent + descent + padding * 2.0f;
	return true;
}

static bool openQ4_TextCursorReached( int cursor, int count ) {
	return cursor != Q4_TEXT_CURSOR_NONE && cursor <= count;
}

static void openQ4_ApplyRgbTextEscapeColor( idVec4 &drawTextColor, idVec4 &currentColor, const unsigned char *payload ) {
	drawTextColor[0] = ( payload[2] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE;
	drawTextColor[1] = ( payload[3] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE;
	drawTextColor[2] = ( payload[4] - '0' ) * Q4_TEXT_RGB_ESCAPE_SCALE;
	currentColor = drawTextColor;
}

static bool openQ4_ShouldDrawEmptyTextCursor( bool calcOnly, int cursor ) {
	return !calcOnly && cursor == 0;
}

static bool openQ4_ShouldDrawFinalTextCursor( int cursor ) {
	return cursor == 0;
}

static bool openQ4_IsLineBreakChar( char c ) {
	return c == '\n' || c == '\r' || c == '\0';
}

static const char *openQ4_SkipPairedLineBreak( const char *text ) {
	if ( ( *text == '\n' && text[1] == '\r' ) || ( *text == '\r' && text[1] == '\n' ) ) {
		return text + 1;
	}
	return text;
}

static bool openQ4_ShouldCaptureBreak( bool lineBreak, bool wrap, char c ) {
	return lineBreak || ( wrap && ( c == ' ' || c == '\t' ) );
}

static float openQ4_InitialTextBaseline( idRectangle &rect, int &textAlign, float lineHeight ) {
	if ( textAlign == Q4_TEXT_ALIGN_VERTICAL_CENTER ) {
		textAlign = idDeviceContext::ALIGN_LEFT;
		return rect.y + rect.h * 0.5f + lineHeight * 0.5f;
	}
	return rect.y + lineHeight;
}

static float openQ4_AlignedTextX( const idRectangle &rect, int textAlign, int textWidth ) {
	if ( textAlign == idDeviceContext::ALIGN_RIGHT ) {
		return rect.x + rect.w - textWidth;
	}
	if ( textAlign == idDeviceContext::ALIGN_CENTER ) {
		return rect.x + ( rect.w - textWidth ) * 0.5f;
	}
	return rect.x;
}

static void openQ4_ClearVirtualScreenTransform( q4VirtualScreenTransform_t &transform ) {
	transform.xScale = 0.0f;
	transform.yScale = 0.0f;
	transform.xOffset = 0.0f;
	transform.yOffset = 0.0f;
}

static void openQ4_SetRetailVirtualTransform( float width, float height, q4VirtualScreenTransform_t &transform ) {
	openQ4_ClearVirtualScreenTransform( transform );

	if ( width <= 0.0f || height <= 0.0f ) {
		return;
	}

	transform.xScale = static_cast<float>( VIRTUAL_WIDTH ) * ( 1.0f / width );
	transform.yScale = static_cast<float>( VIRTUAL_HEIGHT ) * ( 1.0f / height );
}

static bool openQ4_GetCurrentViewportSize( float &windowWidth, float &windowHeight ) {
	windowWidth = static_cast<float>( engineWindowState.uiViewportWidth );
	windowHeight = static_cast<float>( engineWindowState.uiViewportHeight );
	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		windowWidth = static_cast<float>( engineWindowState.vidWidth );
		windowHeight = static_cast<float>( engineWindowState.vidHeight );
	}
	return windowWidth > 0.0f && windowHeight > 0.0f;
}

static void openQ4_CalcVirtualScreenTransform( float width, float height, float windowWidth, float windowHeight, bool aspectCorrect, q4VirtualScreenTransform_t &transform ) {
	openQ4_ClearVirtualScreenTransform( transform );

	if ( width <= 0.0f || height <= 0.0f ) {
		return;
	}

	if ( !aspectCorrect || windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		openQ4_SetRetailVirtualTransform( width, height, transform );
		return;
	}

	const float targetAspect = width / height;
	const float windowAspect = windowWidth / windowHeight;
	const float uniformPhysicalScale = ( windowAspect >= targetAspect ) ? ( windowHeight / height ) : ( windowWidth / width );
	const float drawWidth = width * uniformPhysicalScale;
	const float drawHeight = height * uniformPhysicalScale;

	const float virtualPerPhysicalX = static_cast<float>( VIRTUAL_WIDTH ) / windowWidth;
	const float virtualPerPhysicalY = static_cast<float>( VIRTUAL_HEIGHT ) / windowHeight;

	transform.xScale = uniformPhysicalScale * virtualPerPhysicalX;
	transform.yScale = uniformPhysicalScale * virtualPerPhysicalY;
	transform.xOffset = ( windowWidth - drawWidth ) * 0.5f * virtualPerPhysicalX;
	transform.yOffset = ( windowHeight - drawHeight ) * 0.5f * virtualPerPhysicalY;
}

static void openQ4_CalcVirtualScreenExpansion( float width, float height, float windowWidth, float windowHeight, bool aspectCorrect, float &xExpand, float &yExpand ) {
	xExpand = 0.0f;
	yExpand = 0.0f;

	if ( !aspectCorrect || width <= 0.0f || height <= 0.0f || windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		return;
	}

	const float targetAspect = width / height;
	const float windowAspect = windowWidth / windowHeight;
	const float aspectEpsilon = 0.0001f;

	if ( windowAspect > targetAspect + aspectEpsilon ) {
		xExpand = ( width * ( windowAspect / targetAspect - 1.0f ) ) * 0.5f;
	} else if ( windowAspect + aspectEpsilon < targetAspect ) {
		yExpand = ( height * ( targetAspect / windowAspect - 1.0f ) ) * 0.5f;
	}
}

static void openQ4_CalcCinematic16x9Bars( float width, float height, float windowWidth, float windowHeight, bool aspectCorrect, idRectangle &topBar, idRectangle &bottomBar, idRectangle &leftBar, idRectangle &rightBar, idRectangle &visibleArea ) {
	topBar.Empty();
	bottomBar.Empty();
	leftBar.Empty();
	rightBar.Empty();
	visibleArea.Empty();

	q4VirtualScreenTransform_t transform;
	openQ4_CalcVirtualScreenTransform( width, height, windowWidth, windowHeight, aspectCorrect, transform );

	if ( transform.xScale <= 0.0f || transform.yScale <= 0.0f || windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		return;
	}

	const idRectangle fullArea(
		-transform.xOffset / transform.xScale,
		-transform.yOffset / transform.yScale,
		static_cast<float>( VIRTUAL_WIDTH ) / transform.xScale,
		static_cast<float>( VIRTUAL_HEIGHT ) / transform.yScale );

	if ( fullArea.w <= 0.0f || fullArea.h <= 0.0f ) {
		return;
	}

	const float physicalScaleX = transform.xScale * ( windowWidth / static_cast<float>( VIRTUAL_WIDTH ) );
	const float physicalScaleY = transform.yScale * ( windowHeight / static_cast<float>( VIRTUAL_HEIGHT ) );
	if ( physicalScaleX <= 0.0f || physicalScaleY <= 0.0f ) {
		return;
	}

	const float targetPhysicalAspect = Q4_CINEMATIC_RETAIL_ASPECT;
	const float targetLogicalAspect = targetPhysicalAspect * ( physicalScaleY / physicalScaleX );
	const float fullLogicalAspect = fullArea.w / fullArea.h;
	const float aspectEpsilon = 0.0001f;

	visibleArea = fullArea;
	if ( fullLogicalAspect > targetLogicalAspect + aspectEpsilon ) {
		visibleArea.w = fullArea.h * targetLogicalAspect;
		visibleArea.x = fullArea.x + ( fullArea.w - visibleArea.w ) * 0.5f;
	} else if ( fullLogicalAspect + aspectEpsilon < targetLogicalAspect ) {
		visibleArea.h = fullArea.w / targetLogicalAspect;
		visibleArea.y = fullArea.y + ( fullArea.h - visibleArea.h ) * 0.5f;
	}

	const float topHeight = Max( 0.0f, visibleArea.y - fullArea.y );
	const float bottomY = visibleArea.y + visibleArea.h;
	const float bottomHeight = Max( 0.0f, fullArea.Bottom() - bottomY );
	const float leftWidth = Max( 0.0f, visibleArea.x - fullArea.x );
	const float rightX = visibleArea.x + visibleArea.w;
	const float rightWidth = Max( 0.0f, fullArea.Right() - rightX );

	topBar = idRectangle( fullArea.x, fullArea.y, fullArea.w, topHeight );
	bottomBar = idRectangle( fullArea.x, bottomY, fullArea.w, bottomHeight );
	leftBar = idRectangle( fullArea.x, fullArea.y, leftWidth, fullArea.h );
	rightBar = idRectangle( rightX, fullArea.y, rightWidth, fullArea.h );
}

static float openQ4_ApplyVirtualX( const q4VirtualScreenTransform_t &transform, float x ) {
	return x * transform.xScale + transform.xOffset;
}

static float openQ4_ApplyVirtualY( const q4VirtualScreenTransform_t &transform, float y ) {
	return y * transform.yScale + transform.yOffset;
}

static bool openQ4_NearlyEqual( float actual, float expected, float epsilon = 0.001f ) {
	return idMath::Fabs( actual - expected ) <= epsilon;
}

static bool openQ4_CheckNear( const char *label, float actual, float expected, float epsilon = 0.001f ) {
	if ( openQ4_NearlyEqual( actual, expected, epsilon ) ) {
		return true;
	}
	common->Warning( "uiFontParitySelfTest: %s was %.6f, expected %.6f", label, actual, expected );
	return false;
}

static bool openQ4_CheckBool( const char *label, bool actual, bool expected ) {
	if ( actual == expected ) {
		return true;
	}
	common->Warning( "uiFontParitySelfTest: %s was %d, expected %d", label, actual ? 1 : 0, expected ? 1 : 0 );
	return false;
}

static bool openQ4_CheckInt( const char *label, int actual, int expected ) {
	if ( actual == expected ) {
		return true;
	}
	common->Warning( "uiFontParitySelfTest: %s was %d, expected %d", label, actual, expected );
	return false;
}

#if defined(HUMANHEAD)
struct openPreyRetailFontPageAudit_t {
	int pageCount;
	bool namesMatch;
	bool visibleGlyphsCovered;
	bool imagesPresent;
};

static openPreyRetailFontPageAudit_t openPREY_AuditRetailFontPages( const fontInfo_t &fontSlot,
		const char *expectedMaterialPrefix ) {
	openPreyRetailFontPageAudit_t result = {};
	result.namesMatch = true;
	result.visibleGlyphsCovered = true;
	result.imagesPresent = true;

	const idMaterial *pages[GLYPHS_PER_FONT] = {};
	for ( int glyphIndex = 0; glyphIndex < GLYPHS_PER_FONT; ++glyphIndex ) {
		const glyphInfo_t &glyph = fontSlot.glyphs[glyphIndex];
		const idMaterial *material = glyph.material;
		if ( material == NULL ) {
			if ( glyph.width > 0.0f && glyph.height > 0.0f ) {
				result.visibleGlyphsCovered = false;
			}
			continue;
		}

		const char *materialName = material->GetName();
		if ( materialName == NULL || idStr::Icmpn( materialName, expectedMaterialPrefix,
				idStr::Length( expectedMaterialPrefix ) ) != 0 ) {
			result.namesMatch = false;
		}

		bool alreadyCounted = false;
		for ( int pageIndex = 0; pageIndex < result.pageCount; ++pageIndex ) {
			if ( pages[pageIndex] == material ) {
				alreadyCounted = true;
				break;
			}
		}
		if ( !alreadyCounted ) {
			pages[result.pageCount++] = material;
		}
	}

	for ( int pageIndex = 0; pageIndex < result.pageCount; ++pageIndex ) {
		materialImageInfo_t imageInfo;
		if ( pages[pageIndex]->GetNumStages() < 1 ||
				!renderSystem->GetMaterialStageImageInfo( pages[pageIndex], 0, imageInfo ) ) {
			result.imagesPresent = false;
		}
	}
	return result;
}
#endif

struct q4GlyphClipCase_t {
	const char *label;
	float x;
	float y;
	float w;
	float h;
	float s1;
	float t1;
	float s2;
	float t2;
	bool clipped;
	float expectedX;
	float expectedY;
	float expectedW;
	float expectedH;
	float expectedS1;
	float expectedT1;
	float expectedS2;
	float expectedT2;
};

static bool openQ4_CheckGlyphClipCase( idDeviceContext &dc, const q4GlyphClipCase_t &clipCase ) {
	float x = clipCase.x;
	float y = clipCase.y;
	float w = clipCase.w;
	float h = clipCase.h;
	float s1 = clipCase.s1;
	float t1 = clipCase.t1;
	float s2 = clipCase.s2;
	float t2 = clipCase.t2;
	bool ok = true;

	const bool clipped = dc.ClippedCoords( &x, &y, &w, &h, &s1, &t1, &s2, &t2 );
	ok &= openQ4_CheckBool( va( "%s result", clipCase.label ), clipped, clipCase.clipped );
	ok &= openQ4_CheckNear( va( "%s x", clipCase.label ), x, clipCase.expectedX );
	ok &= openQ4_CheckNear( va( "%s y", clipCase.label ), y, clipCase.expectedY );
	ok &= openQ4_CheckNear( va( "%s w", clipCase.label ), w, clipCase.expectedW );
	ok &= openQ4_CheckNear( va( "%s h", clipCase.label ), h, clipCase.expectedH );
	ok &= openQ4_CheckNear( va( "%s s1", clipCase.label ), s1, clipCase.expectedS1 );
	ok &= openQ4_CheckNear( va( "%s t1", clipCase.label ), t1, clipCase.expectedT1 );
	ok &= openQ4_CheckNear( va( "%s s2", clipCase.label ), s2, clipCase.expectedS2 );
	ok &= openQ4_CheckNear( va( "%s t2", clipCase.label ), t2, clipCase.expectedT2 );
	return ok;
}

static void openQ4_SetGuiSortForFont( fontInfoEx_t &font ) {
	fontInfo_t *fontSlots[] = {
		&font.fontInfoSmall,
		&font.fontInfoMedium,
		&font.fontInfoLarge
	};
	for ( int slotIndex = 0; slotIndex < 3; ++slotIndex ) {
		fontInfo_t *fontSlot = fontSlots[slotIndex];
		if ( fontSlot->material != NULL ) {
			fontSlot->material->SetSort( SS_GUI );
		}
		for ( int glyphIndex = 0; glyphIndex < GLYPHS_PER_FONT; ++glyphIndex ) {
			if ( fontSlot->glyphs[glyphIndex].material != NULL ) {
				fontSlot->glyphs[glyphIndex].material->SetSort( SS_GUI );
			}
		}
	}
}

}

int idDeviceContext::FindFont( const char *name ) {
	int c = fonts.Num();

	for (int i = 0; i < c; i++) {
		if (idStr::Icmp(name, fonts[i].name) == 0) {
			openQ4_SetGuiSortForFont( fonts[i] );
			return i;
		}
	}

	// If the font was not found, try to register it
	idStr fileName = name;
	fileName.Replace("fonts", va("fonts/%s", fontLang.c_str()) );

	fontInfoEx_t fontInfo;
	if ( renderSystem->RegisterFont( fileName, fontInfo ) ) {
		idStr::Copynz( fontInfo.name, name, sizeof( fontInfo.name ) );
		const int index = fonts.Append( fontInfo );
		return index;
	} else {
		common->Printf( "Could not register font %s [%s]\n", name, fileName.c_str() );
		return -1;
	}
}

void idDeviceContext::SetupFonts() {
	fonts.SetGranularity( 1 );

	fontLang = cvarSystem->GetCVarString( "sys_lang" );
	
	// western european languages can use the english font
	if ( fontLang == "french" || fontLang == "german" || fontLang == "spanish" || fontLang == "italian" ) {
		fontLang = "english";
	}

	// Default font has to be added first.
	FindFont( "fonts" );
}

void idDeviceContext::SetFont( int num ) {
	if ( fonts.Num() == 0 ) {
		activeFont = NULL;
		return;
	}
	if ( num >= 0 && num < fonts.Num() ) {
		activeFont = &fonts[num];
	} else {
		activeFont = &fonts[0];
	}
}

void idDeviceContext::SizeIcon( embeddedIcon_t &icon ) {
	if ( icon.material == NULL ) {
		return;
	}

	// Always resolve from the authored rect rather than from the previously
	// resolved UVs, so sizing an icon more than once is idempotent.
	const float imageWidth = static_cast<float>( icon.material->GetImageWidth() );
	const float imageHeight = static_cast<float>( icon.material->GetImageHeight() );
	if ( imageWidth <= 0.0f || imageHeight <= 0.0f ) {
		// The image is not resident yet; leave the icon unsized so SizeIcons()
		// or the next lookup can pick it up once level media has loaded.
		icon.width = 0.0f;
		icon.height = 0.0f;
		icon.sized = false;
		return;
	}

	openQ4_SetEmbeddedIconAxisUV( icon.s1, icon.s2, icon.registeredX, icon.registeredWidth, imageWidth );
	openQ4_SetEmbeddedIconAxisUV( icon.t1, icon.t2, icon.registeredY, icon.registeredHeight, imageHeight );
	icon.width = static_cast<float>( openQ4_EmbeddedIconDimensionOrImageSize( icon.registeredWidth, imageWidth ) );
	icon.height = static_cast<float>( openQ4_EmbeddedIconDimensionOrImageSize( icon.registeredHeight, imageHeight ) );
	icon.sized = true;
}

void idDeviceContext::SizeIcons() {
	for ( int i = 0; i < icons.Num(); ++i ) {
		embeddedIcon_t *icon = icons.GetIndex( i );
		if ( icon != NULL && !icon->sized ) {
			SizeIcon( *icon );
		}
	}
}

bool idDeviceContext::FindIcon( const char *code, const embeddedIcon_t **icon ) {
	embeddedIcon_t *foundIcon = NULL;
	const bool found = icons.Get( code, &foundIcon );
	if ( found && foundIcon != NULL && !foundIcon->sized ) {
		// registered before its image was resident
		SizeIcon( *foundIcon );
	}
	if ( icon != NULL ) {
		*icon = foundIcon;
	}
	return found && foundIcon != NULL;
}

float idDeviceContext::GetIconDisplayWidth( const embeddedIcon_t &icon, float referenceHeight ) const {
	return static_cast<float>( openQ4_EmbeddedIconWidthUnits( icon.width, icon.height, referenceHeight, Q4_EMBEDDED_ICON_DRAW_WIDTH ) );
}

void idDeviceContext::RegisterIcon( const char *code, const char *shader, int x, int y, int w, int h ) {
	if ( code == NULL || shader == NULL || code[0] == '\0' || shader[0] == '\0' ) {
		return;
	}

	embeddedIcon_t icon;
	idStr::Copynz( icon.code, code, sizeof( icon.code ) );
	icon.material = declManager->FindMaterial( shader );
	if ( icon.material == NULL ) {
		return;
	}

	const_cast<idMaterial *>( icon.material )->EnsureNotPurged();
	icon.material->SetSort( SS_GUI );
	icon.registeredX = x;
	icon.registeredY = y;
	icon.registeredWidth = w;
	icon.registeredHeight = h;
	SizeIcon( icon );
	icons.Set( icon.code, icon );
	idStr::RegisterIconEscapeCode( icon.code );
}

void idDeviceContext::RegisterBuiltinIcons() {
#if !defined(HUMANHEAD)
	static const struct {
		const char *code;
		const char *shader;
	} builtinIcons[] = {
		{ "vce", "gfx/guis/hud/icons/icon_speaker" },
		{ "vcd", "gfx/guis/hud/icons/icon_speaker_disabled" },
		{ "fde", "gfx/guis/hud/icons/icon_friend" },
		{ "fdd", "gfx/guis/hud/icons/icon_friend_disabled" },
		{ "flm", "gfx/guis/hud/icons/sb_flag_marine" },
		{ "fls", "gfx/guis/hud/icons/sb_flag_strogg" },
		{ "yrd", "gfx/guis/hud/icons/icon_ready" },
		{ "nrd", "gfx/guis/hud/icons/icon_notready" },
		{ "qad", "gfx/guis/hud/icons/item_quadkill_colored" },
		{ "ds0", "gfx/guis/mainmenu/icon_dedserver" },
		{ "dsp", "gfx/guis/mainmenu/icon_pb" },
		{ "sl0", "gfx/guis/mainmenu/icon_locked" },
		{ "sf0", "gfx/guis/mainmenu/icon_favorite" }
	};

	for ( int i = 0; i < static_cast<int>( sizeof( builtinIcons ) / sizeof( builtinIcons[0] ) ); ++i ) {
		RegisterIcon( builtinIcons[i].code, builtinIcons[i].shader );
	}
#else
	// These escape codes name Quake 4 HUD and server-browser images.  Prey
	// registers its authored GUI icons through the normal RegisterIcon path.
#endif
}


void idDeviceContext::Init() {
	xScale = 0.0;
	aspectCorrect = true;
	SetSize(VIRTUAL_WIDTH, VIRTUAL_HEIGHT);
	whiteImage = declManager->FindMaterial("gfx/guis/white");
	whiteImage->SetSort( SS_GUI );
	mbcs = false;
	SetupFonts();
	activeFont = fonts.Num() > 0 ? &fonts[0] : NULL;
	icons.Clear();
	idStr::ClearIconEscapeCodes();
	RegisterBuiltinIcons();
	colorPurple = idVec4(1, 0, 1, 1);
	colorOrange = idVec4(1, 1, 0, 1);
	colorYellow = idVec4(0, 1, 1, 1);
	colorGreen = idVec4(0, 1, 0, 1);
	colorBlue = idVec4(0, 0, 1, 1);
	colorRed = idVec4(1, 0, 0, 1);
	colorWhite = idVec4(1, 1, 1, 1);
	colorBlack = idVec4(0, 0, 0, 1);
	colorNone = idVec4(0, 0, 0, 0);
	cursorImages[CURSOR_ARROW] = declManager->FindMaterial("guis/assets/guicursor_arrow.tga");
	cursorImages[CURSOR_HAND] = declManager->FindMaterial("guis/assets/guicursor_hand.tga");
	cursorImages[CURSOR_MENU] = declManager->FindMaterial("guis/assets/guicursor_menu.tga");
	scrollBarImages[SCROLLBAR_HBACK] = declManager->FindMaterial("gfx/guis/scrollbarh");
	scrollBarImages[SCROLLBAR_VBACK] = declManager->FindMaterial("gfx/guis/scrollbarv");
	scrollBarImages[SCROLLBAR_THUMB] = declManager->FindMaterial("gfx/guis/scrollbar_thumb");
	scrollBarImages[SCROLLBAR_RIGHT] = declManager->FindMaterial("gfx/guis/scrollbar_right");
	scrollBarImages[SCROLLBAR_LEFT] = declManager->FindMaterial("gfx/guis/scrollbar_left");
	scrollBarImages[SCROLLBAR_UP] = declManager->FindMaterial("gfx/guis/scrollbar_up");
	scrollBarImages[SCROLLBAR_DOWN] = declManager->FindMaterial("gfx/guis/scrollbar_down");
	cursorImages[CURSOR_ARROW]->SetSort( SS_GUI );
	cursorImages[CURSOR_HAND]->SetSort( SS_GUI );
	cursorImages[CURSOR_MENU]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_HBACK]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_VBACK]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_THUMB]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_RIGHT]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_LEFT]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_UP]->SetSort( SS_GUI );
	scrollBarImages[SCROLLBAR_DOWN]->SetSort( SS_GUI );
	cursor = CURSOR_ARROW;
	enableClipping = true;
	overStrikeMode = true;
	drawTextColor = colorWhite;
	drawTextColorAdjust = 0.0f;
	mat.Identity();
	origin.Zero();
	initialized = true;
}

void idDeviceContext::Shutdown() {
	fontName.Clear();
	fontLang.Clear();
	clipRects.Clear();
	fonts.Clear();
	Clear();
}

void idDeviceContext::Clear() {
	initialized = false;
	useFont = NULL;
	activeFont = NULL;
	mbcs = false;
	aspectCorrect = true;
	drawTextColor.Zero();
	drawTextColorAdjust = 0.0f;
	icons.Clear();
	retailSplineEffectActive = false;
	retailSplineEffectProgress = 0.0f;
	retailSplineEffectPoints = 0;
}

idDeviceContext::idDeviceContext() {
	Clear();
}

bool idDeviceContext::SetRetailSplineEffect( float progress, int splinePoints ) {
	if ( cvarSystem->GetCVarBool( "r_useTrueTypeFonts" ) ) {
		ClearRetailSplineEffect();
		openPREY_LogTTFSplineNotice();
		return false;
	}
	retailSplineEffectActive = true;
	retailSplineEffectProgress = idMath::ClampFloat( 0.0f, 1.0f, progress );
	retailSplineEffectPoints = idMath::ClampInt( 3, 100, splinePoints );
	return true;
}

void idDeviceContext::ClearRetailSplineEffect() {
	retailSplineEffectActive = false;
	retailSplineEffectProgress = 0.0f;
	retailSplineEffectPoints = 0;
}

void idDeviceContext::SetTransformInfo(const idVec3 &org, const idMat3 &m) {
	origin = org;
	mat = m;
}

void idDeviceContext::SetAspectCorrection( bool enabled ) {
	aspectCorrect = enabled;
}

// 
//  added method
void idDeviceContext::GetTransformInfo(idVec3& org, idMat3& m )
{
	m = mat;
	org = origin;
}
// 

void idDeviceContext::PopClipRect() {
	if (clipRects.Num()) {
		clipRects.RemoveIndex(clipRects.Num()-1);
	}
}

void idDeviceContext::PushClipRect(idRectangle r) {
	clipRects.Append(r);
}

void idDeviceContext::PushClipRect(float x, float y, float w, float h) {
	clipRects.Append(idRectangle(x, y, w, h));
}

bool idDeviceContext::ClippedCoords(float *x, float *y, float *w, float *h, float *s1, float *t1, float *s2, float *t2) {

	if ( enableClipping == false || clipRects.Num() == 0 ) {
		return false;
	}

	int c = clipRects.Num();
	while( --c > 0 ) {
		idRectangle *clipRect = &clipRects[c];
 
		float ox = *x;
		float oy = *y;
		float ow = *w;
		float oh = *h;

		if ( ow <= 0.0f || oh <= 0.0f ) {
			break;
		}

		if (*x < clipRect->x) {
			*w -= clipRect->x - *x;
			*x = clipRect->x;
		} else if (*x > clipRect->x + clipRect->w) {
			*x = *w = *y = *h = 0;
		}
		if (*y < clipRect->y) {
			*h -= clipRect->y - *y;
			*y = clipRect->y;
		} else if (*y > clipRect->y + clipRect->h) {
			*x = *w = *y = *h = 0;
		}
		if (*w > clipRect->w) {
			*w = clipRect->w - *x + clipRect->x;
		} else if (*x + *w > clipRect->x + clipRect->w) {
			*w = clipRect->Right() - *x;
		}
		if (*h > clipRect->h) {
			*h = clipRect->h - *y + clipRect->y;
		} else if (*y + *h > clipRect->y + clipRect->h) {
			*h = clipRect->Bottom() - *y;
		}

		if ( s1 && s2 && t1 && t2 && ow > 0.0f ) {
			float ns1, ns2, nt1, nt2;
			// upper left
			float u = ( *x - ox ) / ow;
			ns1 = *s1 * ( 1.0f - u ) + *s2 * ( u );

			// upper right
			u = ( *x + *w - ox ) / ow;
			ns2 = *s1 * ( 1.0f - u ) + *s2 * ( u );

			// lower left
			u = ( *y - oy ) / oh;
			nt1 = *t1 * ( 1.0f - u ) + *t2 * ( u );

			// lower right
			u = ( *y + *h - oy ) / oh;
			nt2 = *t1 * ( 1.0f - u ) + *t2 * ( u );

			// set values
			*s1 = ns1;
			*s2 = ns2;
			*t1 = nt1;
			*t2 = nt2;
		}
	}

	return (*w == 0 || *h == 0) ? true : false;
}


void idDeviceContext::AdjustCoords(float *x, float *y, float *w, float *h) {
	if (x) {
		*x = (*x * xScale) + xOffset;
	}
	if (y) {
		*y = (*y * yScale) + yOffset;
	}
	if (w) {
		*w *= xScale;
	}
	if (h) {
		*h *= yScale;
	}
}

static ID_INLINE void TransformVertInVirtualSpace( idDrawVert &vert, const idVec3 &origin, const idMat3 &mat, float xScale, float yScale, float xOffset, float yOffset ) {
	if ( xScale == 0.0f || yScale == 0.0f ) {
		vert.xyz -= origin;
		vert.xyz *= mat;
		vert.xyz += origin;
		return;
	}

	// UI transforms are authored in virtual GUI space, so map to virtual space,
	// apply transform, then map back to the current draw-space viewport.
	idVec3 virtualPos = vert.xyz;
	virtualPos[0] = ( virtualPos[0] - xOffset ) / xScale;
	virtualPos[1] = ( virtualPos[1] - yOffset ) / yScale;
	virtualPos -= origin;
	virtualPos *= mat;
	virtualPos += origin;
	vert.xyz[0] = ( virtualPos[0] * xScale ) + xOffset;
	vert.xyz[1] = ( virtualPos[1] * yScale ) + yOffset;
	vert.xyz[2] = virtualPos[2];
}

static ID_INLINE void openQ4_SetGuiDrawVert( idDrawVert &vert, float x, float y, float z, float s, float t ) {
	vert.Clear();
	vert.xyz.Set( x, y, z );
	vert.st.Set( s, t );
	vert.normal.Set( 0, 0, 1 );
	vert.tangents[0].Set( 1, 0, 0 );
	vert.tangents[1].Set( 0, 1, 0 );
	vert.color[0] = vert.color[1] = vert.color[2] = vert.color[3] = 255;
	vert.color2[0] = vert.color2[1] = vert.color2[2] = vert.color2[3] = 255;
}

void idDeviceContext::DrawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *shader) {
	idDrawVert verts[4];
	glIndex_t indexes[6];
	indexes[0] = 3;
	indexes[1] = 0;
	indexes[2] = 2;
	indexes[3] = 2;
	indexes[4] = 0;
	indexes[5] = 1;
	openQ4_SetGuiDrawVert( verts[0], x, y, 0.0f, s1, t1 );
	openQ4_SetGuiDrawVert( verts[1], x + w, y, 0.0f, s2, t1 );
	openQ4_SetGuiDrawVert( verts[2], x + w, y + h, 0.0f, s2, t2 );
	openQ4_SetGuiDrawVert( verts[3], x, y + h, 0.0f, s1, t2 );
	
	const bool hasTransform = !mat.IsIdentity();
	if ( hasTransform ) {
		for ( int i = 0; i < 4; i++ ) {
			TransformVertInVirtualSpace( verts[i], origin, mat, xScale, yScale, xOffset, yOffset );
		}
	}

	renderSystem->DrawStretchPic( &verts[0], &indexes[0], 4, 6, shader, hasTransform, 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) );
	
}


void idDeviceContext::DrawMaterial(float x, float y, float w, float h, const idMaterial *mat, const idVec4 &color, float scalex, float scaley) {

	renderSystem->SetColor(color);

	float	s0, s1, t0, t1;
// 
//  handle negative scales as well	
	if ( scalex < 0 )
	{
		w *= -1;
		scalex *= -1;
	}
	if ( scaley < 0 )
	{
		h *= -1;
		scaley *= -1;
	}
// 
	if( w < 0 ) {	// flip about vertical
		w  = -w;
		s0 = 1 * scalex;
		s1 = 0;
	}
	else {
		s0 = 0;
		s1 = 1 * scalex;
	}

	if( h < 0 ) {	// flip about horizontal
		h  = -h;
		t0 = 1 * scaley;
		t1 = 0;
	}
	else {
		t0 = 0;
		t1 = 1 * scaley;
	}

	if ( ClippedCoords( &x, &y, &w, &h, &s0, &t0, &s1, &t1 ) ) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);

	DrawStretchPic( x, y, w, h, s0, t0, s1, t1, mat);
}

void idDeviceContext::DrawMaterialUV( float x, float y, float w, float h, const idMaterial *mat,
		const idVec4 &color, float s0, float t0, float s1, float t1 ) {
	if ( mat == NULL ) {
		return;
	}
	renderSystem->SetColor( color );
	if ( w < 0.0f ) {
		w = -w;
		idSwap( s0, s1 );
	}
	if ( h < 0.0f ) {
		h = -h;
		idSwap( t0, t1 );
	}
	if ( ClippedCoords( &x, &y, &w, &h, &s0, &t0, &s1, &t1 ) ) {
		return;
	}
	AdjustCoords( &x, &y, &w, &h );
	DrawStretchPic( x, y, w, h, s0, t0, s1, t1, mat );
}

void idDeviceContext::DrawMaterialRotated(float x, float y, float w, float h, const idMaterial *mat, const idVec4 &color, float scalex, float scaley, float angle) {
	
	renderSystem->SetColor(color);

	float	s0, s1, t0, t1;
	// 
	//  handle negative scales as well	
	if ( scalex < 0 )
	{
		w *= -1;
		scalex *= -1;
	}
	if ( scaley < 0 )
	{
		h *= -1;
		scaley *= -1;
	}
	// 
	if( w < 0 ) {	// flip about vertical
		w  = -w;
		s0 = 1 * scalex;
		s1 = 0;
	}
	else {
		s0 = 0;
		s1 = 1 * scalex;
	}

	if( h < 0 ) {	// flip about horizontal
		h  = -h;
		t0 = 1 * scaley;
		t1 = 0;
	}
	else {
		t0 = 0;
		t1 = 1 * scaley;
	}

	if ( angle == 0.0f && ClippedCoords( &x, &y, &w, &h, &s0, &t0, &s1, &t1 ) ) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);

	DrawStretchPicRotated( x, y, w, h, s0, t0, s1, t1, mat, angle);
}

void idDeviceContext::DrawStretchPicRotated(float x, float y, float w, float h, float s1, float t1, float s2, float t2, const idMaterial *shader, float angle) {
	
	idDrawVert verts[4];
	glIndex_t indexes[6];
	indexes[0] = 3;
	indexes[1] = 0;
	indexes[2] = 2;
	indexes[3] = 2;
	indexes[4] = 0;
	indexes[5] = 1;
	openQ4_SetGuiDrawVert( verts[0], x, y, 0.0f, s1, t1 );
	openQ4_SetGuiDrawVert( verts[1], x + w, y, 0.0f, s2, t1 );
	openQ4_SetGuiDrawVert( verts[2], x + w, y + h, 0.0f, s2, t2 );
	openQ4_SetGuiDrawVert( verts[3], x, y + h, 0.0f, s1, t2 );

	const bool ident = !mat.IsIdentity();
	if ( ident ) {
		for ( int i = 0; i < 4; i++ ) {
			TransformVertInVirtualSpace( verts[i], origin, mat, xScale, yScale, xOffset, yOffset );
		}
	}

	//Generate a translation so we can translate to the center of the image rotate and draw
	idVec3 origTrans;
	origTrans.x = x+(w/2);
	origTrans.y = y+(h/2);
	origTrans.z = 0;


	//Rotate the verts about the z axis before drawing them
	idMat4 rotz;
	rotz.Identity();
	float sinAng = idMath::Sin(angle);
	float cosAng = idMath::Cos(angle);
	rotz[0][0] = cosAng;
	rotz[0][1] = sinAng;
	rotz[1][0] = -sinAng;
	rotz[1][1] = cosAng;
	for(int i = 0; i < 4; i++) {
		//Translate to origin
		verts[i].xyz -= origTrans;

		//Rotate
		verts[i].xyz = rotz * verts[i].xyz;

		//Translate back
		verts[i].xyz += origTrans;
	}


	renderSystem->DrawStretchPic( &verts[0], &indexes[0], 4, 6, shader, ident || angle != 0.0f, 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) );
}

void idDeviceContext::DrawFilledRect( float x, float y, float w, float h, const idVec4 &color) {

	if ( color.w == 0.0f ) {
		return;
	}

	renderSystem->SetColor(color);
	
	if (ClippedCoords(&x, &y, &w, &h, NULL, NULL, NULL, NULL)) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);
	DrawStretchPic( x, y, w, h, 0, 0, 0, 0, whiteImage);
}


void idDeviceContext::DrawRect( float x, float y, float w, float h, float size, const idVec4 &color) {

	if ( color.w == 0.0f ) {
		return;
	}

	renderSystem->SetColor(color);
	
	if (ClippedCoords(&x, &y, &w, &h, NULL, NULL, NULL, NULL)) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);
	DrawStretchPic( x, y, size, h, 0, 0, 0, 0, whiteImage );
	DrawStretchPic( x + w - size, y, size, h, 0, 0, 0, 0, whiteImage );
	DrawStretchPic( x, y, w, size, 0, 0, 0, 0, whiteImage );
	DrawStretchPic( x, y + h - size, w, size, 0, 0, 0, 0, whiteImage );
}

void idDeviceContext::DrawMaterialRect( float x, float y, float w, float h, float size, const idMaterial *mat, const idVec4 &color) {

	if ( color.w == 0.0f ) {
		return;
	}

	renderSystem->SetColor(color);
	DrawMaterial( x, y, size, h, mat, color );
	DrawMaterial( x + w - size, y, size, h, mat, color );
	DrawMaterial( x, y, w, size, mat, color );
	DrawMaterial( x, y + h - size, w, size, mat, color );
}


void idDeviceContext::SetCursor(int n) {
	cursor = (n < CURSOR_ARROW || n >= CURSOR_COUNT) ? CURSOR_ARROW : n;
}

void idDeviceContext::DrawCursor(float *x, float *y, float size) {
	renderSystem->SetColor(colorWhite);
	// Keep GUI cursor state in virtual coordinates; only transform local draw coords.
	float drawX = *x;
	float drawY = *y;
	float drawWidth = size;
	float drawHeight = size;
	// Scale dimensions independently for aspect correction while keeping the hotspot at drawX/drawY.
	AdjustCoords(&drawX, &drawY, &drawWidth, &drawHeight);
	DrawStretchPic(drawX, drawY, drawWidth, drawHeight, 0, 0, 1, 1, cursorImages[cursor]);
}
/*
 =======================================================================================================================
 =======================================================================================================================
 */

void idDeviceContext::PaintChar(float x,float y,float width,float height,float scale,float	s,float	t,float	s2,float t2,const idMaterial *hShader) {
	float	w, h;
	w = width * scale;
	h = height * scale;

	if (ClippedCoords(&x, &y, &w, &h, &s, &t, &s2, &t2)) {
		return;
	}

	AdjustCoords(&x, &y, &w, &h);
	DrawStretchPic(x, y, w, h, s, t, s2, t2, hShader);
}

void idDeviceContext::PaintGlyph( float x, float y, float scale, const fontInfo_t *font, const glyphInfo_t *glyph, const idMaterial *hShader ) {
	if ( glyph == NULL ) {
		return;
	}
	if ( glyph->material != NULL ) {
		hShader = glyph->material;
	} else if ( hShader == NULL ) {
		hShader = openPREY_ResolveGlyphMaterial( font, glyph );
	}
	if ( hShader == NULL ) {
		return;
	}

	float width = glyph->width;
	float s = glyph->s;
	float s2 = glyph->s2;
	openQ4_ApplyGlyphHorizontalGuard( font, glyph, scale, x, width, s, s2 );

	// Tight stock HUD windows can otherwise clip the guarded right edge of the small marine radio font.
	const float clipRightPad = openQ4_GlyphClipRightPad( font, glyph, scale );
	const int clipIndex = clipRightPad > 0.0f && enableClipping && clipRects.Num() > 1 ? clipRects.Num() - 1 : -1;
	if ( clipIndex >= 0 ) {
		clipRects[clipIndex].w += clipRightPad;
	}
	PaintChar( x, y, width, glyph->height, scale, s, glyph->t, s2, glyph->t2, hShader );
	if ( clipIndex >= 0 ) {
		clipRects[clipIndex].w -= clipRightPad;
	}
}


void idDeviceContext::SetFontByScale(float scale) {
	if ( activeFont == NULL ) {
		useFont = NULL;
		return;
	}
	if (scale <= gui_smallFontLimit.GetFloat()) {
		useFont = &activeFont->fontInfoSmall;
		activeFont->maxHeight = activeFont->maxHeightSmall;
		activeFont->maxWidth = activeFont->maxWidthSmall;
	} else if (scale <= gui_mediumFontLimit.GetFloat()) {
		useFont = &activeFont->fontInfoMedium;
		activeFont->maxHeight = activeFont->maxHeightMedium;
		activeFont->maxWidth = activeFont->maxWidthMedium;
	} else {
		useFont = &activeFont->fontInfoLarge;
		activeFont->maxHeight = activeFont->maxHeightLarge;
		activeFont->maxWidth = activeFont->maxWidthLarge;
	}
}

int idDeviceContext::DrawText(float x, float y, float scale, idVec4 color, const char *text, float adjust, int limit, int style, int cursor, bool resetEscapes) {
	SetFontByScale( scale );
	q4ScaledFont_t scaledFont;
	scaledFont.font = useFont;
	scaledFont.renderScale = openQ4_FontRenderScale( useFont, scale );
	scaledFont.maxWidth = activeFont != NULL ? activeFont->maxWidth : 0.0f;
	scaledFont.maxHeight = activeFont != NULL ? activeFont->maxHeight : 0.0f;

	if ( !openQ4_HasRenderableFont( scaledFont ) || text == NULL || color.w == 0.0f ) {
		return 0;
	}

	if ( resetEscapes ) {
		drawTextColor = color;
		drawTextColorAdjust = 0.0f;
	}

	idVec4 currentColor = drawTextColor;

	// Accessibility backing, drawn once for the whole line before any glyph so
	// it never shows through the gaps between characters. Colour escapes inside
	// the line change the text colour but not the backing.
	const float backgroundOpacity = gui_textBackground.GetFloat();
	if ( backgroundOpacity > 0.0f ) {
		float backgroundX = 0.0f;
		float backgroundY = 0.0f;
		float backgroundWidth = 0.0f;
		float backgroundHeight = 0.0f;
		const float lineWidth = static_cast<float>( TextWidth( text, scale, limit, static_cast<int>( adjust ) ) );
		if ( openQ4_TextBackgroundRect( scaledFont, x, y, lineWidth, gui_textBackgroundPadding.GetFloat(),
										backgroundX, backgroundY, backgroundWidth, backgroundHeight ) ) {
			DrawFilledRect( backgroundX, backgroundY, backgroundWidth, backgroundHeight,
							idVec4( 0.0f, 0.0f, 0.0f, backgroundOpacity * color.w ) );
		}
	}

	renderSystem->SetColor( currentColor );

	const unsigned char *s = reinterpret_cast<const unsigned char *>( text );
	int len = strlen( text );
	if ( limit > 0 && len > limit ) {
		len = limit;
	}

	const bool retailSplineTTF = retailSplineEffectActive && openPREY_IsTrueTypeFont( scaledFont.font );
	if ( retailSplineTTF ) {
		openPREY_LogTTFSplineNotice();
	}
	const bool retailSplineBitmap = retailSplineEffectActive && !retailSplineTTF;
	const float retailLineStartX = x;
	const float retailLineWidth = retailSplineBitmap ? static_cast<float>( TextWidth( text, scale, len, static_cast<int>( adjust ) ) ) : 0.0f;
	const float retailLineHeight = retailSplineBitmap ? static_cast<float>( MaxCharHeight( scale ) ) : 0.0f;
	const int retailGlyphCount = retailSplineBitmap ? openPREY_CountRetailGlyphs( s, len ) : 0;
	int retailGlyphIndex = 0;

	int count = 0;
	while ( *s != '\0' && count < len ) {
		int escapeType = 0;
		const int escapeLength = openQ4_TextEscapeLength( reinterpret_cast<const char *>( s ), &escapeType );
		if ( escapeLength > 0 ) {
			const unsigned char *payload = s;
			int payloadType = escapeType;
			int payloadLength = escapeLength;
			int sourceAdvance = escapeLength;
			int countAdvance = escapeLength;
			int repeats = 1;

			if ( openQ4_IsRepeatTextEscape( reinterpret_cast<const char *>( s ), escapeLength ) ) {
				payload = s + escapeLength;
				payloadLength = openQ4_TextEscapeLength( reinterpret_cast<const char *>( payload ), &payloadType );
				if ( payloadLength <= 0 ) {
					s += escapeLength;
					continue;
				}
				sourceAdvance = escapeLength + payloadLength;
				countAdvance = payloadLength;
				repeats = openQ4_TextEscapeRepeatCount( reinterpret_cast<const char *>( s ) );
			}

			for ( int repeatIndex = 0; repeatIndex < repeats; ++repeatIndex ) {
				if ( payloadType == S_ESCAPE_ICON ) {
					char iconCode[4];
					if ( openQ4_ExtractIconCode( reinterpret_cast<const char *>( payload ), iconCode ) ) {
						const embeddedIcon_t *icon = NULL;
						if ( FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
							const glyphInfo_t *referenceGlyph = &scaledFont.font->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
							const float referenceHeight = referenceGlyph->height;
							const float iconWidth = GetIconDisplayWidth( *icon, referenceHeight );
							if ( iconWidth > 0.0f ) {
								float iconX = x;
								float iconY = openQ4_GlyphDrawY( y, scaledFont.renderScale, referenceGlyph );
								if ( retailSplineBitmap && retailGlyphCount > 0 ) {
									const unsigned int iconHash = static_cast<unsigned int>( iconCode[0] ) |
										( static_cast<unsigned int>( iconCode[1] ) << 8 ) |
										( static_cast<unsigned int>( iconCode[2] ) << 16 );
									const openPreyRetailSplineGlyph_t splineGlyph = openPREY_RetailSplineGlyph(
										iconX, iconY, retailLineStartX, retailLineWidth, retailLineHeight,
										retailGlyphIndex, retailGlyphCount, iconHash,
										retailSplineEffectProgress, retailSplineEffectPoints );
									if ( splineGlyph.drawGhost ) {
										idVec4 ghostColor = currentColor;
										ghostColor[3] *= splineGlyph.ghostAlpha;
										renderSystem->SetColor( ghostColor );
										PaintChar( splineGlyph.ghostX, splineGlyph.ghostY, iconWidth, referenceHeight,
											scaledFont.renderScale, icon->s1, icon->t1, icon->s2, icon->t2, icon->material );
									}
									iconX = splineGlyph.drawX;
									iconY = splineGlyph.drawY;
									idVec4 glyphColor = currentColor;
									glyphColor[3] *= splineGlyph.glyphAlpha;
									renderSystem->SetColor( glyphColor );
								}
								PaintChar( iconX, iconY, iconWidth, referenceHeight, scaledFont.renderScale,
									icon->s1, icon->t1, icon->s2, icon->t2, icon->material );
								if ( retailSplineBitmap ) {
									renderSystem->SetColor( currentColor );
								}
								x += iconWidth;
							}
						}
					}
					if ( retailSplineBitmap ) {
						++retailGlyphIndex;
					}
				} else {
					switch ( payload[1] ) {
						case '+':
							drawTextColorAdjust += Q4_TEXT_BRIGHTNESS_STEP;
							currentColor = idVec4( drawTextColor.x + drawTextColorAdjust, drawTextColor.y + drawTextColorAdjust, drawTextColor.z + drawTextColorAdjust, color.w );
							renderSystem->SetColor( currentColor );
							break;
						case '-':
							drawTextColorAdjust -= Q4_TEXT_BRIGHTNESS_STEP;
							currentColor = idVec4( drawTextColor.x + drawTextColorAdjust, drawTextColor.y + drawTextColorAdjust, drawTextColor.z + drawTextColorAdjust, color.w );
							renderSystem->SetColor( currentColor );
							break;
						case '0':
						case 'R':
						case 'r':
							drawTextColor = color;
							drawTextColorAdjust = 0.0f;
							currentColor = color;
							renderSystem->SetColor( currentColor );
							break;
						case '1': case '2': case '3': case '4': case '5':
						case '6': case '7': case '8': case '9': case ':':
							drawTextColor = idStr::ColorForIndex( payload[1] );
							drawTextColor[3] = color[3];
							drawTextColorAdjust = 0.0f;
							currentColor = drawTextColor;
							renderSystem->SetColor( currentColor );
							break;
						case 'C':
						case 'c':
							if ( payloadLength >= 5 ) {
								openQ4_ApplyRgbTextEscapeColor( drawTextColor, currentColor, payload );
								drawTextColorAdjust = 0.0f;
								renderSystem->SetColor( currentColor );
							}
							break;
						default:
							break;
					}
				}
			}
			s += sourceAdvance;
			count += countAdvance;
			continue;
		}

		const glyphInfo_t *glyph = &scaledFont.font->glyphs[*s];
		float drawX = openQ4_GlyphDrawX( x, scaledFont.renderScale, glyph );
		float drawY = openQ4_GlyphDrawY( y, scaledFont.renderScale, glyph );
		openPreyRetailSplineGlyph_t splineGlyph = {};
		if ( retailSplineBitmap && retailGlyphCount > 0 ) {
			splineGlyph = openPREY_RetailSplineGlyph( drawX, drawY, retailLineStartX, retailLineWidth,
				retailLineHeight, retailGlyphIndex, retailGlyphCount, static_cast<unsigned int>( *s ),
				retailSplineEffectProgress, retailSplineEffectPoints );
			if ( splineGlyph.drawGhost ) {
				idVec4 ghostColor = currentColor;
				ghostColor[3] *= splineGlyph.ghostAlpha;
				renderSystem->SetColor( ghostColor );
				PaintGlyph( splineGlyph.ghostX, splineGlyph.ghostY, scaledFont.renderScale,
					scaledFont.font, glyph, scaledFont.font->material );
			}
			drawX = splineGlyph.drawX;
			drawY = splineGlyph.drawY;
			renderSystem->SetColor( currentColor );
		}

		if ( style == Q4_TEXT_STYLE_SHADOW ) {
			idVec4 shadowColor( 0.0f, 0.0f, 0.0f, currentColor[3] );
			renderSystem->SetColor( shadowColor );
			PaintGlyph( drawX + Q4_TEXT_STYLE_OFFSET, drawY + Q4_TEXT_STYLE_OFFSET, scaledFont.renderScale, scaledFont.font, glyph, scaledFont.font->material );
			renderSystem->SetColor( currentColor );
		} else if ( style == Q4_TEXT_STYLE_OUTLINE ) {
			const bool darkOutline = currentColor[0] >= Q4_TEXT_OUTLINE_DARK_THRESHOLD || currentColor[1] >= Q4_TEXT_OUTLINE_DARK_THRESHOLD || currentColor[2] >= Q4_TEXT_OUTLINE_DARK_THRESHOLD;
			idVec4 outlineColor = darkOutline ? idVec4( 0.0f, 0.0f, 0.0f, currentColor[3] ) : idVec4( 1.0f, 1.0f, 1.0f, currentColor[3] );
			static const float offsets[4][2] = {
				{ Q4_TEXT_STYLE_OFFSET, Q4_TEXT_STYLE_OFFSET },
				{ -Q4_TEXT_STYLE_OFFSET, Q4_TEXT_STYLE_OFFSET },
				{ -Q4_TEXT_STYLE_OFFSET, -Q4_TEXT_STYLE_OFFSET },
				{ Q4_TEXT_STYLE_OFFSET, -Q4_TEXT_STYLE_OFFSET }
			};
			renderSystem->SetColor( outlineColor );
			for ( int i = 0; i < 4; ++i ) {
				PaintGlyph( drawX + offsets[i][0], drawY + offsets[i][1], scaledFont.renderScale, scaledFont.font, glyph, scaledFont.font->material );
			}
			renderSystem->SetColor( currentColor );
		}

		if ( retailSplineBitmap ) {
			idVec4 glyphColor = currentColor;
			glyphColor[3] *= splineGlyph.glyphAlpha;
			renderSystem->SetColor( glyphColor );
		}
		PaintGlyph( drawX, drawY, scaledFont.renderScale, scaledFont.font, glyph, scaledFont.font->material );
		if ( retailSplineBitmap ) {
			renderSystem->SetColor( currentColor );
			++retailGlyphIndex;
		}

		if ( openQ4_TextCursorReached( cursor, count ) ) {
			DrawEditCursor( x, y, scale );
			cursor = Q4_TEXT_CURSOR_NONE;
		}
		x += openQ4_ScaledGlyphAdvance( scaledFont.renderScale, glyph, adjust );
		s++;
		count++;
	}
	if ( openQ4_TextCursorReached( cursor, count ) ) {
		DrawEditCursor( x, y, scale );
	}
	return count;
}

void idDeviceContext::CalcVirtualScaleOffset( float width, float height, float &outXScale, float &outYScale, float &outXOffset, float &outYOffset ) const {
	float windowWidth = 0.0f;
	float windowHeight = 0.0f;
	openQ4_GetCurrentViewportSize( windowWidth, windowHeight );

	q4VirtualScreenTransform_t transform;
	openQ4_CalcVirtualScreenTransform( width, height, windowWidth, windowHeight, aspectCorrect, transform );
	outXScale = transform.xScale;
	outYScale = transform.yScale;
	outXOffset = transform.xOffset;
	outYOffset = transform.yOffset;
}

void idDeviceContext::GetVirtualScreenExpansion( float width, float height, float &xExpand, float &yExpand ) const {
	float windowWidth = 0.0f;
	float windowHeight = 0.0f;
	openQ4_GetCurrentViewportSize( windowWidth, windowHeight );
	openQ4_CalcVirtualScreenExpansion( width, height, windowWidth, windowHeight, aspectCorrect, xExpand, yExpand );
}

void idDeviceContext::GetCinematic16x9Bars( float width, float height, idRectangle &topBar, idRectangle &bottomBar, idRectangle &leftBar, idRectangle &rightBar, idRectangle &visibleArea ) const {
	float windowWidth = 0.0f;
	float windowHeight = 0.0f;
	openQ4_GetCurrentViewportSize( windowWidth, windowHeight );
	openQ4_CalcCinematic16x9Bars( width, height, windowWidth, windowHeight, aspectCorrect, topBar, bottomBar, leftBar, rightBar, visibleArea );
}

float idDeviceContext::GetCanvasAspect() const {
	float windowWidth = static_cast<float>( engineWindowState.uiViewportWidth );
	float windowHeight = static_cast<float>( engineWindowState.uiViewportHeight );
	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		windowWidth = static_cast<float>( engineWindowState.vidWidth );
		windowHeight = static_cast<float>( engineWindowState.vidHeight );
	}

	if ( windowWidth <= 0.0f || windowHeight <= 0.0f ) {
		return static_cast<float>( VIRTUAL_WIDTH ) / static_cast<float>( VIRTUAL_HEIGHT );
	}

	return windowWidth / windowHeight;
}

void idDeviceContext::SetSize(float width, float height) {
	vidWidth = ( width > 0.0f ) ? width : static_cast<float>( VIRTUAL_WIDTH );
	vidHeight = ( height > 0.0f ) ? height : static_cast<float>( VIRTUAL_HEIGHT );

	CalcVirtualScaleOffset( width, height, xScale, yScale, xOffset, yOffset );
}

int idDeviceContext::CharWidth( const char c, float scale, int adjust ) {
	SetFontByScale( scale );
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f ) {
		return 0;
	}
	const glyphInfo_t *glyph = &useFont->glyphs[(const unsigned char)c];
	return static_cast<int>( openQ4_ScaledGlyphAdvance( useScale, glyph, static_cast<float>( adjust ) ) );
}

int idDeviceContext::TextWidth( const char *text, float scale, int limit, int adjust ) {
	SetFontByScale( scale );
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( text == NULL || useFont == NULL || useScale == 0.0f ) {
		return 0;
	}

	float advanceX = 0.0f;
	float visibleRight = 0.0f;
	int index = 0;
	const unsigned char *s = reinterpret_cast<const unsigned char *>( text );
	while ( *s != '\0' && ( limit <= 0 || index < limit ) ) {
		int escapeType = 0;
		const int escapeLength = openQ4_TextEscapeLength( reinterpret_cast<const char *>( s ), &escapeType );
		if ( escapeLength > 0 ) {
			if ( escapeType == S_ESCAPE_ICON ) {
				char iconCode[4];
				if ( openQ4_ExtractIconCode( reinterpret_cast<const char *>( s ), iconCode ) ) {
					const embeddedIcon_t *icon = NULL;
					if ( FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
						const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
						const float iconWidth = static_cast<float>( openQ4_EmbeddedIconWidthUnits( icon->width, icon->height, referenceGlyph->height, Q4_EMBEDDED_ICON_DRAW_WIDTH ) );
						visibleRight = Max( visibleRight, advanceX + iconWidth * useScale );
						advanceX += iconWidth;
					}
				}
			}
			s += escapeLength;
			index += escapeLength;
			continue;
		}

		const glyphInfo_t *glyph = &useFont->glyphs[*s];
		visibleRight = Max( visibleRight, openQ4_GlyphVisibleRightEdge( advanceX, useFont, useScale, glyph ) );
		advanceX += openQ4_ScaledGlyphAdvance( useScale, glyph, static_cast<float>( adjust ) );
		s++;
		index++;
	}
	return static_cast<int>( idMath::Ceil( Max( advanceX, visibleRight ) ) );
}

int idDeviceContext::TextHeight(const char *text, float scale, int limit, int adjust) {
	(void)adjust;

	SetFontByScale( scale );
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( text == NULL || useFont == NULL || useScale == 0.0f ) {
		return 0;
	}

	int maxHeight = 0;
	int index = 0;
	const char *s = text;
	while ( *s != '\0' && ( limit <= 0 || index < limit ) ) {
		int escapeType = 0;
		const int escapeLength = openQ4_TextEscapeLength( s, &escapeType );
		if ( escapeLength > 0 ) {
			if ( escapeType == S_ESCAPE_ICON ) {
				const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
				const int referenceHeight = openQ4_GlyphHeightUnits( referenceGlyph );
				if ( maxHeight < referenceHeight ) {
					maxHeight = referenceHeight;
				}
			}
			s += escapeLength;
			index += escapeLength;
			continue;
		}

		const glyphInfo_t *glyph = &useFont->glyphs[*(const unsigned char *)s];
		const int glyphHeight = openQ4_GlyphHeightUnits( glyph );
		if ( maxHeight < glyphHeight ) {
			maxHeight = glyphHeight;
		}
		s++;
		index++;
	}

	return openQ4_ScaledFontUnits( useScale, maxHeight );
}

bool idDeviceContext::GetMaxTextIndex( const char *text, int limit, float textScale, wrapInfo_t &wrapInfo ) {
	SetFontByScale( textScale );
	const float useScale = openQ4_FontRenderScale( useFont, textScale );
	if ( text == NULL || text[0] == '\0' || useFont == NULL || useScale == 0.0f ) {
		return false;
	}

	int width = 0;
	int index = 0;
	while ( text[index] != '\0' ) {
		int escapeType = 0;
		const int escapeLength = openQ4_TextEscapeLength( &text[index], &escapeType );
		const int tokenLength = escapeLength > 0 ? escapeLength : 1;
		int tokenWidth = 0;

		if ( escapeType == S_ESCAPE_ICON ) {
			char iconCode[4];
			if ( openQ4_ExtractIconCode( &text[index], iconCode ) ) {
				const embeddedIcon_t *icon = NULL;
				if ( FindIcon( iconCode, &icon ) ) {
					const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
					tokenWidth = openQ4_EmbeddedIconWidthUnits( icon->width, icon->height, referenceGlyph->height, Q4_EMBEDDED_ICON_REGISTERED_WIDTH );
				}
			}
		} else if ( escapeLength == 0 ) {
			tokenWidth = openQ4_GlyphAdvanceUnits( &useFont->glyphs[static_cast<unsigned char>( text[index] )], 0 );
		}

		width += tokenWidth;
		if ( openQ4_ScaledFontUnits( useScale, width ) > limit ) {
			const int lastTokenIndex = index + ( escapeLength > 0 ? escapeLength - 1 : 0 );
			wrapInfo.maxIndex = lastTokenIndex - 1;
			return true;
		}

		const int lastTokenIndex = index + tokenLength - 1;
		if ( text[lastTokenIndex] == ' ' ) {
			wrapInfo.lastWhitespace = lastTokenIndex;
		}

		index += tokenLength;
	}

	return false;
}

int idDeviceContext::MaxCharWidth(float scale) {
	SetFontByScale(scale);
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f || activeFont == NULL ) {
		return 0;
	}
	return openQ4_ScaledFontUnits( useScale, activeFont->maxWidth );
}

int idDeviceContext::MaxCharHeight(float scale) {
	SetFontByScale(scale);
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f || activeFont == NULL ) {
		return 0;
	}
	return openQ4_ScaledFontUnits( useScale, activeFont->maxHeight );
}

const idMaterial *idDeviceContext::GetScrollBarImage(int index) {
	if (index >= SCROLLBAR_HBACK && index < SCROLLBAR_COUNT) {
		return scrollBarImages[index];
	}
	return scrollBarImages[SCROLLBAR_HBACK];
}

// this only supports left aligned text
idRegion *idDeviceContext::GetTextRegion(const char *text, float textScale, idRectangle rectDraw, float xStart, float yStart) {
#if 0
	const char	*p, *textPtr, *newLinePtr;
	char		buff[1024];
	int			len, textWidth, newLine, newLineWidth;
	float		y;

	float charSkip = MaxCharWidth(textScale) + 1;
	float lineSkip = MaxCharHeight(textScale);

	textWidth = 0;
	newLinePtr = NULL;
#endif
	return NULL;
/*
	if (text == NULL) {
		return;
	}

	textPtr = text;
	if (*textPtr == '\0') {
		return;
	}

	y = lineSkip + rectDraw.y + yStart; 
	len = 0;
	buff[0] = '\0';
	newLine = 0;
	newLineWidth = 0;
	p = textPtr;

	textWidth = 0;
	while (p) {
		if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\0') {
			newLine = len;
			newLinePtr = p + 1;
			newLineWidth = textWidth;
		}

		if ((newLine && textWidth > rectDraw.w) || *p == '\n' || *p == '\0') {
			if (len) {

				float x = rectDraw.x ;
				
				buff[newLine] = '\0';
				DrawText(x, y, textScale, color, buff, 0, 0, 0);
				if (!wrap) {
					return;
				}
			}

			if (*p == '\0') {
				break;
			}

			y += lineSkip + 5;
			p = newLinePtr;
			len = 0;
			newLine = 0;
			newLineWidth = 0;
			continue;
		}

		buff[len++] = *p++;
		buff[len] = '\0';
		textWidth = TextWidth( buff, textScale, -1 );
	}
*/
}

void idDeviceContext::DrawEditCursor( float x, float y, float scale ) {
	if ( (int)( com_ticNumber >> 4 ) & 1 ) {
		return;
	}
	SetFontByScale(scale);
	const float useScale = openQ4_FontRenderScale( useFont, scale );
	if ( useFont == NULL || useScale == 0.0f ) {
		return;
	}
	const glyphInfo_t *glyph = &useFont->glyphs[overStrikeMode ? Q4_OVERSTRIKE_CURSOR_GLYPH : Q4_INSERT_CURSOR_GLYPH];
	const idMaterial *glyphMaterial = openPREY_ResolveGlyphMaterial( useFont, glyph );
	if ( glyphMaterial == NULL ) {
		return;
	}
	PaintGlyph( x, openQ4_GlyphDrawY( y, useScale, glyph ), useScale, useFont, glyph, glyphMaterial );
}

int idDeviceContext::DrawText( const char *text, float textScale, int textAlign, idVec4 color, idRectangle rectDraw, bool wrap, int cursor, bool calcOnly, idList<int> *breaks, int limit, int adjust, int style, bool chatWindow ) {
	const float charSkip = MaxCharWidth( textScale ) + 1;
	const float lineSkip = MaxCharHeight( textScale );
	const float cursorSkip = ( cursor >= 0 ? charSkip : 0 );
	const int visibleCellCount = charSkip > 0.0f ? idMath::FtoiFast( rectDraw.w / charSkip ) : 0;

	SetFontByScale( textScale );
	const float useScale = openQ4_FontRenderScale( useFont, textScale );
	if ( useFont == NULL || useScale == 0.0f ) {
		return visibleCellCount;
	}

	drawTextColor = color;
	drawTextColorAdjust = 0.0f;

	if ( breaks ) {
		breaks->Append( 0 );
	}

	if ( !( text && *text ) ) {
		if ( openQ4_ShouldDrawEmptyTextCursor( calcOnly, cursor ) ) {
			renderSystem->SetColor( color );
			DrawEditCursor( rectDraw.x, rectDraw.y + lineSkip, textScale );
		}
		return visibleCellCount;
	}

	char buff[Q4_TEXT_LINE_BUFFER_SIZE];
	buff[0] = '\0';
	int len = 0;
	int newLine = 0;
	int newLineWidth = 0;
	float textWidth = 0.0f;
	bool lineBreak = false;
	bool wordBreak = false;
	const char *p = text;
	const char *newLinePtr = NULL;
	float y = openQ4_InitialTextBaseline( rectDraw, textAlign, lineSkip );
	int count = 0;

	while ( p != NULL ) {
		if ( openQ4_IsLineBreakChar( *p ) ) {
			lineBreak = true;
			p = openQ4_SkipPairedLineBreak( p );
		}

		int escapeType = 0;
		const int escapeLength = openQ4_TextEscapeLength( p, &escapeType );
		const bool isIconEscape = escapeLength > 0 && escapeType == S_ESCAPE_ICON;
		if ( escapeLength > 0 ) {
			if ( len + escapeLength < static_cast<int>( sizeof( buff ) ) ) {
				idStr::Copynz( &buff[len], p, escapeLength + 1 );
			}
			if ( !isIconEscape ) {
				len += escapeLength;
				p += escapeLength;
				continue;
			}
		}

		int nextCharWidth = 0;
		if ( chatWindow && !lineBreak ) {
			if ( isIconEscape ) {
				char iconCode[4];
				if ( openQ4_ExtractIconCode( p, iconCode ) ) {
					const embeddedIcon_t *icon = NULL;
					if ( FindIcon( iconCode, &icon ) && icon->height > 0.0f ) {
						const glyphInfo_t *referenceGlyph = &useFont->glyphs[Q4_EMBEDDED_ICON_REFERENCE_GLYPH];
						nextCharWidth = openQ4_ScaledFontUnits( useScale, openQ4_EmbeddedIconWidthUnits( icon->width, icon->height, referenceGlyph->height, Q4_EMBEDDED_ICON_DRAW_WIDTH ) );
					}
				}
			} else if ( idStr::CharIsPrintable( *p ) ) {
				nextCharWidth = CharWidth( *p, textScale, adjust );
			} else {
				nextCharWidth = static_cast<int>( cursorSkip );
			}
		}

		if ( !lineBreak && ( textWidth + nextCharWidth ) > rectDraw.w ) {
			if ( len > 0 && newLine == 0 ) {
				newLine = len;
				newLinePtr = p;
				newLineWidth = static_cast<int>( textWidth );
			}
			wordBreak = true;
		} else if ( openQ4_ShouldCaptureBreak( lineBreak, wrap, *p ) ) {
			newLine = len;
			newLinePtr = p + 1;
			newLineWidth = static_cast<int>( textWidth );
		}

		if ( lineBreak || wordBreak ) {
			const float x = openQ4_AlignedTextX( rectDraw, textAlign, newLineWidth );

			if ( wrap || newLine > 0 ) {
				buff[newLine] = '\0';
				if ( wordBreak && cursor >= newLine && newLine == len ) {
					cursor++;
				}
			}

			if ( !calcOnly ) {
				count += DrawText( x, y, textScale, color, buff, static_cast<float>( adjust ), 0, style, cursor );
			}

			if ( cursor < newLine ) {
				cursor = Q4_TEXT_CURSOR_NONE;
			} else if ( cursor >= 0 ) {
				cursor -= ( newLine + 1 );
			}

			if ( !wrap ) {
				return newLine;
			}

			if ( ( limit && count > limit ) || *p == '\0' ) {
				return visibleCellCount;
			}

			y += lineSkip * Q4_TEXT_LINE_SPACING_SCALE;
			if ( !calcOnly && y > rectDraw.Bottom() ) {
				return visibleCellCount;
			}

			p = newLinePtr;
			if ( breaks ) {
				breaks->Append( p - text );
			}

			buff[0] = '\0';
			len = 0;
			newLine = 0;
			newLineWidth = 0;
			textWidth = 0.0f;
			lineBreak = false;
			wordBreak = false;
			continue;
		}

		if ( escapeLength > 0 ) {
			len += escapeLength;
			p += escapeLength;
		} else {
			if ( len + 1 < static_cast<int>( sizeof( buff ) ) ) {
				buff[len++] = *p;
				buff[len] = '\0';
			}
			p++;
		}

		textWidth = static_cast<float>( TextWidth( buff, textScale, -1, adjust ) );
	}

	if ( openQ4_ShouldDrawFinalTextCursor( cursor ) ) {
		renderSystem->SetColor( color );
		DrawEditCursor( rectDraw.x, rectDraw.y + lineSkip, textScale );
	}

	return visibleCellCount;
}

bool UI_FontParity_RunSelfTest( void ) {
	bool ok = true;

	// The later cases here assert parity with the game's retail bitmap atlases:
	// exact glyph advances, font registration, and atlas-page resolution.  The
	// TrueType path deliberately rasterises its own glyphs and
	// binds its own atlas, so measuring it against the retail atlas is a
	// category error rather than a regression.  Say so instead of failing.
	const bool retailAtlasActive = !cvarSystem->GetCVarBool( "r_useTrueTypeFonts" );
	if ( !retailAtlasActive ) {
		common->Printf( "uiFontParitySelfTest: skipping retail atlas parity cases, "
						"r_useTrueTypeFonts is enabled; set it to 0 to run them\n" );
	}

	fontInfo_t font = {};
	font.pointSize = 12.0f;
	ok &= openQ4_CheckNear( "12 point font scale", openQ4_FontRenderScale( &font, 0.25f ), 1.0f );
	font.pointSize = 24.0f;
	ok &= openQ4_CheckNear( "24 point font scale", openQ4_FontRenderScale( &font, 0.5f ), 1.0f );
	font.pointSize = Q4_GUI_FONT_BASE_POINT_SIZE;
	ok &= openQ4_CheckNear( "48 point font scale", openQ4_FontRenderScale( &font, 1.0f ), 1.0f );

	glyphInfo_t glyph = {};
	glyph.horiAdvance = 7.2f;
	glyph.height = 11.9f;
	glyph.horiBearingX = -1.5f;
	glyph.horiBearingY = 10.0f;
	ok &= openQ4_CheckNear( "rounded glyph advance", static_cast<float>( openQ4_RoundedGlyphAdvance( &glyph ) ), 8.0f );
	ok &= openQ4_CheckNear( "adjusted glyph advance units", static_cast<float>( openQ4_GlyphAdvanceUnits( &glyph, -1 ) ), 7.0f );
	ok &= openQ4_CheckNear( "glyph height units", static_cast<float>( openQ4_GlyphHeightUnits( &glyph ) ), 11.0f );
	ok &= openQ4_CheckNear( "scaled glyph advance", openQ4_ScaledGlyphAdvance( 1.0f, &glyph, -1.0f ), 7.0f );
	ok &= openQ4_CheckNear( "glyph draw x bearing", openQ4_GlyphDrawX( 20.0f, 2.0f, &glyph ), 17.0f );
	ok &= openQ4_CheckNear( "glyph draw y bearing", openQ4_GlyphDrawY( 30.0f, 2.0f, &glyph ), 10.0f );
	const idMaterial *fontMaterialToken = reinterpret_cast<const idMaterial *>( &font );
	const idMaterial *glyphMaterialToken = reinterpret_cast<const idMaterial *>( &glyph );
	font.material = fontMaterialToken;
	glyph.material = NULL;
	ok &= openQ4_CheckBool( "font-wide glyph material fallback",
		openPREY_ResolveGlyphMaterial( &font, &glyph ) == fontMaterialToken, true );
	glyph.material = glyphMaterialToken;
	ok &= openQ4_CheckBool( "retail per-glyph material override",
		openPREY_ResolveGlyphMaterial( &font, &glyph ) == glyphMaterialToken, true );

	glyphInfo_t guardedGlyph = {};
	guardedGlyph.width = 8.5625f;
	guardedGlyph.s = 145.0f / 256.0f;
	guardedGlyph.s2 = 153.5625f / 256.0f;
	fontInfo_t smallGuardFont = {};
	smallGuardFont.pointSize = 12.0f;
	float guardedX = 20.0f;
	float guardedW = guardedGlyph.width;
	float guardedS1 = guardedGlyph.s;
	float guardedS2 = guardedGlyph.s2;
	ok &= openQ4_CheckBool( "glyph horizontal guard applied", openQ4_ApplyGlyphHorizontalGuard( &smallGuardFont, &guardedGlyph, 0.8f, guardedX, guardedW, guardedS1, guardedS2 ), true );
	ok &= openQ4_CheckNear( "glyph horizontal guard x", guardedX, 19.2f );
	ok &= openQ4_CheckNear( "glyph horizontal guard width", guardedW, 10.5625f );
	ok &= openQ4_CheckNear( "glyph horizontal guard s1", guardedS1, 144.0f / 256.0f );
	ok &= openQ4_CheckNear( "glyph horizontal guard s2", guardedS2, 154.5625f / 256.0f );

	fontInfo_t mediumGuardFont = {};
	mediumGuardFont.pointSize = 24.0f;
	guardedX = 20.0f;
	guardedW = guardedGlyph.width;
	guardedS1 = guardedGlyph.s;
	guardedS2 = guardedGlyph.s2;
	ok &= openQ4_CheckBool( "medium glyph horizontal guard applied", openQ4_ApplyGlyphHorizontalGuard( &mediumGuardFont, &guardedGlyph, 0.8f, guardedX, guardedW, guardedS1, guardedS2 ), true );
	ok &= openQ4_CheckNear( "medium glyph horizontal guard x", guardedX, 19.6f );
	ok &= openQ4_CheckNear( "medium glyph horizontal guard width", guardedW, 9.5625f );
	ok &= openQ4_CheckNear( "medium glyph horizontal guard s1", guardedS1, 144.5f / 256.0f );
	ok &= openQ4_CheckNear( "medium glyph horizontal guard s2", guardedS2, 154.0625f / 256.0f );

	fontInfo_t marineClipFont = {};
	marineClipFont.pointSize = 12.0f;
	idStr::Copynz( marineClipFont.name, "fonts/english/marine_12.fontdat", sizeof( marineClipFont.name ) );
	glyphInfo_t &marineClipGlyph = marineClipFont.glyphs[static_cast<unsigned char>( 'g' )];
	marineClipGlyph.width = 8.296875f;
	marineClipGlyph.height = 7.078125f;
	marineClipGlyph.s = 231.0f / 256.0f;
	marineClipGlyph.s2 = 239.296875f / 256.0f;
	ok &= openQ4_CheckNear( "marine small glyph clip pad", openQ4_GlyphClipRightPad( &marineClipFont, &marineClipGlyph, 0.8f ), 2.0f );
	// Accessibility text backing. These are font-path independent, so they run
	// whichever glyph source is active.
	fontInfo_t inkFont = {};
	inkFont.glyphs['H'].horiBearingY = 10.0f;
	inkFont.glyphs['H'].height = 10.0f;
	inkFont.glyphs['g'].horiBearingY = 7.0f;
	inkFont.glyphs['g'].height = 9.0f;		// two units below the baseline
	inkFont.glyphs[' '].horiBearingY = 99.0f;	// blank glyphs must not count
	inkFont.glyphs[' '].height = 0.0f;
	q4TextInkExtents_t ink;
	openQ4_FontInkExtents( &inkFont, ink );
	ok &= openQ4_CheckNear( "font ink ascent", ink.ascent, 10.0f );
	ok &= openQ4_CheckNear( "font ink descent", ink.descent, 2.0f );

	q4ScaledFont_t inkScaledFont;
	inkScaledFont.font = &inkFont;
	inkScaledFont.renderScale = 2.0f;
	inkScaledFont.maxWidth = 0.0f;
	inkScaledFont.maxHeight = 0.0f;
	float backX = 0.0f;
	float backY = 0.0f;
	float backW = 0.0f;
	float backH = 0.0f;
	ok &= openQ4_CheckBool( "text background rect built", openQ4_TextBackgroundRect( inkScaledFont, 100.0f, 50.0f, 40.0f, 3.0f, backX, backY, backW, backH ), true );
	ok &= openQ4_CheckNear( "text background x", backX, 97.0f );
	ok &= openQ4_CheckNear( "text background y", backY, 27.0f );
	ok &= openQ4_CheckNear( "text background width", backW, 46.0f );
	ok &= openQ4_CheckNear( "text background height", backH, 30.0f );
	ok &= openQ4_CheckBool( "empty text draws no background", openQ4_TextBackgroundRect( inkScaledFont, 100.0f, 50.0f, 0.0f, 3.0f, backX, backY, backW, backH ), false );

	ok &= openQ4_CheckNear( "embedded icon draw width units", static_cast<float>( openQ4_EmbeddedIconWidthUnits( 32.0f, 16.0f, 12.0f, Q4_EMBEDDED_ICON_DRAW_WIDTH ) ), 24.0f );
	ok &= openQ4_CheckNear( "embedded icon registered width units", static_cast<float>( openQ4_EmbeddedIconWidthUnits( 32.0f, 16.0f, 12.0f, Q4_EMBEDDED_ICON_REGISTERED_WIDTH ) ), 32.0f );
	ok &= openQ4_CheckNear( "embedded icon full-image dimension", static_cast<float>( openQ4_EmbeddedIconDimensionOrImageSize( Q4_EMBEDDED_ICON_FULL_IMAGE, 64.0f ) ), 64.0f );
	ok &= openQ4_CheckNear( "embedded icon registered dimension", static_cast<float>( openQ4_EmbeddedIconDimensionOrImageSize( 24, 64.0f ) ), 24.0f );

	float iconUv1 = -1.0f;
	float iconUv2 = -1.0f;
	openQ4_SetEmbeddedIconAxisUV( iconUv1, iconUv2, Q4_EMBEDDED_ICON_FULL_IMAGE, 16, 64.0f );
	ok &= openQ4_CheckNear( "embedded icon full-image uv1", iconUv1, 0.0f );
	ok &= openQ4_CheckNear( "embedded icon full-image uv2", iconUv2, 1.0f );
	openQ4_SetEmbeddedIconAxisUV( iconUv1, iconUv2, 8, 16, 64.0f );
	ok &= openQ4_CheckNear( "embedded icon sprite uv1", iconUv1, 0.125f );
	ok &= openQ4_CheckNear( "embedded icon sprite uv2", iconUv2, 0.375f );

	idRectangle alignRect( 100.0f, 50.0f, 200.0f, 40.0f );
	ok &= openQ4_CheckNear( "text left align x", openQ4_AlignedTextX( alignRect, idDeviceContext::ALIGN_LEFT, 40 ), 100.0f );
	ok &= openQ4_CheckNear( "text center align x", openQ4_AlignedTextX( alignRect, idDeviceContext::ALIGN_CENTER, 40 ), 180.0f );
	ok &= openQ4_CheckNear( "text right align x", openQ4_AlignedTextX( alignRect, idDeviceContext::ALIGN_RIGHT, 40 ), 260.0f );

	int verticalAlign = Q4_TEXT_ALIGN_VERTICAL_CENTER;
	idRectangle baselineRect( 100.0f, 50.0f, 200.0f, 40.0f );
	ok &= openQ4_CheckNear( "vertical-center baseline", openQ4_InitialTextBaseline( baselineRect, verticalAlign, 12.0f ), 76.0f );
	ok &= openQ4_CheckNear( "vertical-center align reset", static_cast<float>( verticalAlign ), static_cast<float>( idDeviceContext::ALIGN_LEFT ) );
	ok &= openQ4_CheckBool( "empty calcOnly cursor draw", openQ4_ShouldDrawEmptyTextCursor( true, 0 ), false );
	ok &= openQ4_CheckBool( "final calcOnly cursor draw", openQ4_ShouldDrawFinalTextCursor( 0 ), true );
	ok &= openQ4_CheckBool( "final hidden cursor draw", openQ4_ShouldDrawFinalTextCursor( Q4_TEXT_CURSOR_NONE ), false );

	idVec4 rgbEscapeDrawColor( 0.25f, 0.5f, 0.75f, 0.42f );
	idVec4 rgbEscapeCurrentColor( 1.0f, 1.0f, 1.0f, 0.9f );
	const unsigned char rgbEscape[] = { '^', 'c', '9', '4', '1', '\0' };
	openQ4_ApplyRgbTextEscapeColor( rgbEscapeDrawColor, rgbEscapeCurrentColor, rgbEscape );
	ok &= openQ4_CheckNear( "rgb escape red", rgbEscapeCurrentColor[0], 1.0f );
	ok &= openQ4_CheckNear( "rgb escape green", rgbEscapeCurrentColor[1], 4.0f / 9.0f );
	ok &= openQ4_CheckNear( "rgb escape blue", rgbEscapeCurrentColor[2], 1.0f / 9.0f );
	ok &= openQ4_CheckNear( "rgb escape preserves alpha", rgbEscapeCurrentColor[3], 0.42f );
	ok &= openQ4_CheckNear( "rgb escape draw color alpha", rgbEscapeDrawColor[3], 0.42f );

#if defined(HUMANHEAD)
	idStr fontAtlasLang = cvarSystem->GetCVarString( "sys_lang" );
	if ( fontAtlasLang == "french" || fontAtlasLang == "german" || fontAtlasLang == "spanish" || fontAtlasLang == "italian" ) {
		fontAtlasLang = "english";
	}
	// These are the three bitmap families authored by retail Prey GUIs.  The
	// exact page counts catch both a truncated .dat decode and loss of the
	// per-glyph atlas material that Doom 3/Prey fonts require.
	if ( retailAtlasActive ) {
		static const struct {
			const char *label;
			const char *relativePath;
			const char *atlasStem;
			int expectedPages[3];
		} retailFontCases[] = {
			{ "default", "", "fontImage_", { 1, 2, 5 } },
			{ "menu", "/menu", "menu_", { 1, 2, 5 } },
			{ "alien", "/alien", "alien_", { 1, 1, 3 } }
		};
		static const int pointSizes[] = { 12, 24, 48 };

		for ( int fontCaseIndex = 0; fontCaseIndex < static_cast<int>( sizeof( retailFontCases ) / sizeof( retailFontCases[0] ) ); ++fontCaseIndex ) {
			const idStr fontPath = va( "fonts/%s%s", fontAtlasLang.c_str(), retailFontCases[fontCaseIndex].relativePath );
			fontInfoEx_t retailFont = {};
			const bool registered = renderSystem->RegisterFont( fontPath.c_str(), retailFont );
			idStr label = va( "Prey %s font registered", retailFontCases[fontCaseIndex].label );
			ok &= openQ4_CheckBool( label.c_str(), registered, true );
			if ( !registered ) {
				continue;
			}

			const fontInfo_t *fontSlots[] = {
				&retailFont.fontInfoSmall,
				&retailFont.fontInfoMedium,
				&retailFont.fontInfoLarge
			};
			idStr materialPrefix = fontPath;
			materialPrefix += "/";
			materialPrefix += retailFontCases[fontCaseIndex].atlasStem;
			for ( int slotIndex = 0; slotIndex < 3; ++slotIndex ) {
				const fontInfo_t &fontSlot = *fontSlots[slotIndex];
				idStr expectedDataName = va( "%s/fontImage_%d.dat", fontPath.c_str(), pointSizes[slotIndex] );
				label = va( "Prey %s %d point data source", retailFontCases[fontCaseIndex].label, pointSizes[slotIndex] );
				ok &= openQ4_CheckBool( label.c_str(), idStr::Icmp( fontSlot.name, expectedDataName.c_str() ) == 0, true );
				label = va( "Prey %s %d point size", retailFontCases[fontCaseIndex].label, pointSizes[slotIndex] );
				ok &= openQ4_CheckNear( label.c_str(), fontSlot.pointSize, static_cast<float>( pointSizes[slotIndex] ) );

				const openPreyRetailFontPageAudit_t pageAudit = openPREY_AuditRetailFontPages( fontSlot, materialPrefix.c_str() );
				label = va( "Prey %s %d point material page count", retailFontCases[fontCaseIndex].label, pointSizes[slotIndex] );
				ok &= openQ4_CheckInt( label.c_str(), pageAudit.pageCount, retailFontCases[fontCaseIndex].expectedPages[slotIndex] );
				label = va( "Prey %s %d point material page names", retailFontCases[fontCaseIndex].label, pointSizes[slotIndex] );
				ok &= openQ4_CheckBool( label.c_str(), pageAudit.namesMatch, true );
				label = va( "Prey %s %d point visible glyph materials", retailFontCases[fontCaseIndex].label, pointSizes[slotIndex] );
				ok &= openQ4_CheckBool( label.c_str(), pageAudit.visibleGlyphsCovered, true );
				label = va( "Prey %s %d point material page images", retailFontCases[fontCaseIndex].label, pointSizes[slotIndex] );
				ok &= openQ4_CheckBool( label.c_str(), pageAudit.imagesPresent, true );
			}
		}
	}
#else
	idDeviceContext radioFontDc;
	radioFontDc.Init();
	const int radioFont = radioFontDc.FindFont( "fonts/marine" );
	ok &= openQ4_CheckBool( "hud radio marine font registered", radioFont >= 0, true );
	// The measurements below are keyed to the retail atlas's exact advances.
	if ( radioFont >= 0 && retailAtlasActive ) {
		radioFontDc.SetFont( radioFont );
		const float radioFontScale = 0.2f / 12.0f * Q4_GUI_FONT_BASE_POINT_SIZE;
		const float incomingGlyphBearingX = 1.140625f;
		const float incomingGlyphBearingY = 7.078125f;
		const float incomingGlyphWidth = 6.484375f;
		const float incomingGlyphHeight = 7.078125f;
		glyphInfo_t incomingGlyph = {};
		incomingGlyph.width = incomingGlyphWidth;
		incomingGlyph.height = incomingGlyphHeight;
		incomingGlyph.horiBearingY = incomingGlyphBearingY;
		incomingGlyph.s = 20.0f / 256.0f;
		incomingGlyph.t = 65.0f / 128.0f;
		incomingGlyph.s2 = 26.484375f / 256.0f;
		incomingGlyph.t2 = 72.078125f / 128.0f;
		idRectangle radioIncomingTextRect( 545.0f + 2.0f, 6.0f + 2.0f, 81.0f - 2.0f, 12.0f - 2.0f );
		idRectangle radioTransmissionTextRect( 545.0f + 2.0f, 13.0f + 2.0f, 81.0f - 2.0f, 12.0f - 2.0f );
		int radioIncomingTextAlign = idDeviceContext::ALIGN_LEFT;
		int radioTransmissionTextAlign = idDeviceContext::ALIGN_LEFT;
		const float radioLineHeight = static_cast<float>( radioFontDc.MaxCharHeight( 0.2f ) );
		const float radioIncomingBaseline = openQ4_InitialTextBaseline( radioIncomingTextRect, radioIncomingTextAlign, radioLineHeight );
		const float radioTransmissionBaseline = openQ4_InitialTextBaseline( radioTransmissionTextRect, radioTransmissionTextAlign, radioLineHeight );
		const float incomingGlyphX = radioIncomingTextRect.x + radioFontScale * incomingGlyphBearingX;
		const float incomingGlyphY = openQ4_GlyphDrawY( radioIncomingBaseline, radioFontScale, &incomingGlyph );
		const float transmissionGlyphX = radioTransmissionTextRect.x + radioFontScale * incomingGlyphBearingX;
		const float transmissionGlyphY = openQ4_GlyphDrawY( radioTransmissionBaseline, radioFontScale, &incomingGlyph );
		const float incomingGlyphH = incomingGlyphHeight * radioFontScale;
		float guardedIncomingGlyphX = incomingGlyphX;
		float guardedIncomingGlyphWidth = incomingGlyphWidth;
		float guardedIncomingGlyphS1 = incomingGlyph.s;
		float guardedIncomingGlyphS2 = incomingGlyph.s2;
		ok &= openQ4_CheckBool( "hud radio glyph horizontal guard applied", openQ4_ApplyGlyphHorizontalGuard( &smallGuardFont, &incomingGlyph, radioFontScale, guardedIncomingGlyphX, guardedIncomingGlyphWidth, guardedIncomingGlyphS1, guardedIncomingGlyphS2 ), true );
		const float guardedIncomingGlyphW = guardedIncomingGlyphWidth * radioFontScale;

		ok &= openQ4_CheckNear( "hud radio marine scale", radioFontScale, 0.8f );
		ok &= openQ4_CheckNear( "hud radio marine line height", radioLineHeight, 11.0f );
		ok &= openQ4_CheckNear( "hud radio incoming width", static_cast<float>( radioFontDc.TextWidth( "incoming", 0.2f, 0, 0 ) ), 57.0f );
		ok &= openQ4_CheckNear( "hud radio transmission width", static_cast<float>( radioFontDc.TextWidth( "transmission", 0.2f, 0, 0 ) ), 84.0f );
		ok &= openQ4_CheckNear( "hud radio communication width", static_cast<float>( radioFontDc.TextWidth( "communication", 0.2f, 0, 0 ) ), 92.0f );
		ok &= openQ4_CheckNear( "loading title visual width", static_cast<float>( radioFontDc.TextWidth( "Data Networking Tower", 0.36f, 0, -2 ) ), 223.0f );
		idRectangle loadingTitleRect( 290.0f, 11.0f, 336.0f, 20.0f );
		ok &= openQ4_CheckNear( "loading title right align x", openQ4_AlignedTextX( loadingTitleRect, idDeviceContext::ALIGN_RIGHT, radioFontDc.TextWidth( "Data Networking Tower", 0.36f, 0, -2 ) ), 403.0f );
		ok &= openQ4_CheckNear( "hud radio incoming text height", static_cast<float>( radioFontDc.TextHeight( "incoming", 0.2f, 0, 0 ) ), 5.0f );
		ok &= openQ4_CheckNear( "hud radio incoming baseline", radioIncomingBaseline, 19.0f );
		ok &= openQ4_CheckNear( "hud radio transmission baseline", radioTransmissionBaseline, 26.0f );
		ok &= openQ4_CheckNear( "hud radio incoming glyph y", incomingGlyphY, 13.3375f );
		ok &= openQ4_CheckNear( "hud radio incoming glyph bottom", incomingGlyphY + incomingGlyphH, 19.0f );
		ok &= openQ4_CheckNear( "hud radio incoming unclipped overhang", incomingGlyphY + incomingGlyphH - radioIncomingTextRect.Bottom(), 1.0f );
		ok &= openQ4_CheckNear( "hud radio transmission glyph y", transmissionGlyphY, 20.3375f );
		ok &= openQ4_CheckNear( "hud radio transmission glyph bottom", transmissionGlyphY + incomingGlyphH, 26.0f );
		ok &= openQ4_CheckNear( "hud radio transmission unclipped overhang", transmissionGlyphY + incomingGlyphH - radioTransmissionTextRect.Bottom(), 1.0f );
		ok &= openQ4_CheckNear( "hud radio guarded glyph x", guardedIncomingGlyphX, 547.1125f );
		ok &= openQ4_CheckNear( "hud radio guarded glyph width", guardedIncomingGlyphW, 6.7875f );
		ok &= openQ4_CheckNear( "hud radio guarded glyph s1", guardedIncomingGlyphS1, 19.0f / 256.0f );
		ok &= openQ4_CheckNear( "hud radio guarded glyph s2", guardedIncomingGlyphS2, 27.484375f / 256.0f );

		idDeviceContext radioClipDc;
		radioClipDc.EnableClipping( true );
		radioClipDc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
		radioClipDc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
		ok &= openQ4_CheckGlyphClipCase( radioClipDc, {
			"hud radio incoming parent clip",
			guardedIncomingGlyphX, incomingGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2,
			false,
			guardedIncomingGlyphX, incomingGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2
		} );
		ok &= openQ4_CheckGlyphClipCase( radioClipDc, {
			"hud radio transmission parent clip",
			transmissionGlyphX - ( incomingGlyphX - guardedIncomingGlyphX ), transmissionGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2,
			false,
			transmissionGlyphX - ( incomingGlyphX - guardedIncomingGlyphX ), transmissionGlyphY, guardedIncomingGlyphW, incomingGlyphH,
			guardedIncomingGlyphS1, incomingGlyph.t, guardedIncomingGlyphS2, incomingGlyph.t2
		} );
	}

	idStr fontAtlasLang = cvarSystem->GetCVarString( "sys_lang" );
	if ( fontAtlasLang == "french" || fontAtlasLang == "german" || fontAtlasLang == "spanish" || fontAtlasLang == "italian" ) {
		fontAtlasLang = "english";
	}
	// The TrueType path never touches the retail .fontdat atlas, so this only
	// means anything while the bitmap fonts are in charge.
	if ( retailAtlasActive ) {
		const idMaterial *fontAtlasMaterial = declManager->FindMaterial( va( "fonts/%s/marine_12.fontdat", fontAtlasLang.c_str() ), false );
		ok &= openQ4_CheckBool( "hud radio marine atlas material", fontAtlasMaterial != NULL, true );
		if ( fontAtlasMaterial != NULL && fontAtlasMaterial->GetNumStages() > 0 ) {
			materialImageInfo_t fontAtlasInfo;
			const bool fontAtlasImagePresent = renderSystem->GetMaterialStageImageInfo( fontAtlasMaterial, 0, fontAtlasInfo );
			ok &= openQ4_CheckBool( "hud radio marine atlas image", fontAtlasImagePresent, true );
			if ( fontAtlasImagePresent ) {
				ok &= openQ4_CheckBool( "hud radio marine atlas format", fontAtlasInfo.isDXT1Compressed, true );
				ok &= openQ4_CheckBool( "hud radio marine atlas color format", fontAtlasInfo.usesGreenAlphaColorFormat, true );
				ok &= openQ4_CheckInt( "hud radio marine atlas mip levels", fontAtlasInfo.numLevels, 4 );
			}
		}
	}
#endif

	q4VirtualScreenTransform_t transform;
	openQ4_CalcVirtualScreenTransform( 640.0f, 480.0f, 640.0f, 480.0f, true, transform );
	ok &= openQ4_CheckNear( "4:3 x scale", transform.xScale, 1.0f );
	ok &= openQ4_CheckNear( "4:3 y scale", transform.yScale, 1.0f );
	ok &= openQ4_CheckNear( "4:3 x offset", transform.xOffset, 0.0f );
	ok &= openQ4_CheckNear( "4:3 y offset", transform.yOffset, 0.0f );

	openQ4_CalcVirtualScreenTransform( 640.0f, 480.0f, 1920.0f, 1080.0f, true, transform );
	ok &= openQ4_CheckNear( "wide x scale", transform.xScale, 0.75f );
	ok &= openQ4_CheckNear( "wide y scale", transform.yScale, 1.0f );
	ok &= openQ4_CheckNear( "wide x offset", transform.xOffset, 80.0f );
	ok &= openQ4_CheckNear( "wide y offset", transform.yOffset, 0.0f );

	float wideExpand = 0.0f;
	float tallExpand = 0.0f;
	openQ4_CalcVirtualScreenExpansion( 640.0f, 480.0f, 1920.0f, 1080.0f, true, wideExpand, tallExpand );
	ok &= openQ4_CheckNear( "wide expansion x", wideExpand, 106.666664f );
	ok &= openQ4_CheckNear( "wide expansion y", tallExpand, 0.0f );
	ok &= openQ4_CheckNear( "wide expanded left edge", openQ4_ApplyVirtualX( transform, -wideExpand ), 0.0f );
	ok &= openQ4_CheckNear( "wide authored left edge", openQ4_ApplyVirtualX( transform, 0.0f ), 80.0f );
	ok &= openQ4_CheckNear( "wide authored right edge", openQ4_ApplyVirtualX( transform, 640.0f ), 560.0f );
	ok &= openQ4_CheckNear( "wide expanded right edge", openQ4_ApplyVirtualX( transform, 640.0f + wideExpand ), 640.0f );

	openQ4_CalcVirtualScreenTransform( 640.0f, 480.0f, 1080.0f, 1920.0f, true, transform );
	ok &= openQ4_CheckNear( "tall x scale", transform.xScale, 1.0f );
	ok &= openQ4_CheckNear( "tall y scale", transform.yScale, 0.421875f );
	ok &= openQ4_CheckNear( "tall x offset", transform.xOffset, 0.0f );
	ok &= openQ4_CheckNear( "tall y offset", transform.yOffset, 138.75f );

	float xExpand = 0.0f;
	float yExpand = 0.0f;
	openQ4_CalcVirtualScreenExpansion( 640.0f, 480.0f, 1080.0f, 1920.0f, true, xExpand, yExpand );
	ok &= openQ4_CheckNear( "tall expansion x", xExpand, 0.0f );
	ok &= openQ4_CheckNear( "tall expansion y", yExpand, 328.888885f );
	ok &= openQ4_CheckNear( "tall expanded top edge", openQ4_ApplyVirtualY( transform, -yExpand ), 0.0f );
	ok &= openQ4_CheckNear( "tall expanded bottom edge", openQ4_ApplyVirtualY( transform, 480.0f + yExpand ), 480.0f );

	idRectangle cinematicTop;
	idRectangle cinematicBottom;
	idRectangle cinematicLeft;
	idRectangle cinematicRight;
	idRectangle cinematicVisible;
	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 640.0f, 480.0f, true, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "4:3 cinematic top bar height", cinematicTop.h, 60.0f );
	ok &= openQ4_CheckNear( "4:3 cinematic bottom bar y", cinematicBottom.y, 420.0f );
	ok &= openQ4_CheckNear( "4:3 cinematic visible width", cinematicVisible.w, Q4_CINEMATIC_RETAIL_VISIBLE_WIDTH );
	ok &= openQ4_CheckNear( "4:3 cinematic visible height", cinematicVisible.h, Q4_CINEMATIC_RETAIL_VISIBLE_HEIGHT );
	ok &= openQ4_CheckNear( "4:3 cinematic left pillar width", cinematicLeft.w, 0.0f );

	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 1080.0f, 1920.0f, true, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "tall cinematic top bar height", cinematicTop.h, 388.888885f );
	ok &= openQ4_CheckNear( "tall cinematic bottom bar y", cinematicBottom.y, 420.0f );
	ok &= openQ4_CheckNear( "tall cinematic visible height", cinematicVisible.h, Q4_CINEMATIC_RETAIL_VISIBLE_HEIGHT );
	ok &= openQ4_CheckNear( "tall cinematic left pillar width", cinematicLeft.w, 0.0f );

	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 1920.0f, 1080.0f, true, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "16:9 cinematic top bar height", cinematicTop.h, 0.0f );
	ok &= openQ4_CheckNear( "16:9 cinematic left pillar width", cinematicLeft.w, 0.0f );
	ok &= openQ4_CheckNear( "16:9 cinematic visible width", cinematicVisible.w, 853.333313f );

	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 2560.0f, 1080.0f, true, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "ultrawide cinematic top bar height", cinematicTop.h, 0.0f );
	ok &= openQ4_CheckNear( "ultrawide cinematic left pillar width", cinematicLeft.w, 142.222229f );
	ok &= openQ4_CheckNear( "ultrawide cinematic right pillar width", cinematicRight.w, 142.222229f );
	ok &= openQ4_CheckNear( "ultrawide cinematic visible width", cinematicVisible.w, 853.333313f );

	openQ4_CalcVirtualScreenTransform( 320.0f, 240.0f, 1920.0f, 1080.0f, false, transform );
	ok &= openQ4_CheckNear( "retail x scale without aspect correction", transform.xScale, 2.0f );
	ok &= openQ4_CheckNear( "retail y scale without aspect correction", transform.yScale, 2.0f );
	ok &= openQ4_CheckNear( "retail x offset without aspect correction", transform.xOffset, 0.0f );
	ok &= openQ4_CheckNear( "retail y offset without aspect correction", transform.yOffset, 0.0f );

	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 1920.0f, 1080.0f, false, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "stretched 16:9 cinematic top bar height", cinematicTop.h, 0.0f );
	ok &= openQ4_CheckNear( "stretched 16:9 cinematic left pillar width", cinematicLeft.w, 0.0f );

	openQ4_CalcCinematic16x9Bars( 640.0f, 480.0f, 2560.0f, 1080.0f, false, cinematicTop, cinematicBottom, cinematicLeft, cinematicRight, cinematicVisible );
	ok &= openQ4_CheckNear( "stretched ultrawide cinematic left pillar width", cinematicLeft.w, 80.0f );
	ok &= openQ4_CheckNear( "stretched ultrawide cinematic visible width", cinematicVisible.w, 480.0f );

	idDeviceContext baseClipDc;
	baseClipDc.EnableClipping( true );
	baseClipDc.PushClipRect( idRectangle( 100.0f, 100.0f, 50.0f, 40.0f ) );
	ok &= openQ4_CheckGlyphClipCase( baseClipDc, {
		"retail base clip skip",
		90.0f, 110.0f, 20.0f, 10.0f,
		0.0f, 0.0f, 1.0f, 1.0f,
		false,
		90.0f, 110.0f, 20.0f, 10.0f,
		0.0f, 0.0f, 1.0f, 1.0f
	} );

	idDeviceContext glyphClipDc;
	glyphClipDc.EnableClipping( true );
	glyphClipDc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
	glyphClipDc.PushClipRect( idRectangle( 100.0f, 100.0f, 50.0f, 40.0f ) );
	const q4GlyphClipCase_t glyphClipCases[] = {
		{
			"glyph partial left clip",
			90.0f, 110.0f, 20.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			100.0f, 110.0f, 10.0f, 10.0f,
			0.5f, 0.0f, 1.0f, 1.0f
		},
		{
			"glyph partial right clip",
			140.0f, 110.0f, 20.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			140.0f, 110.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 0.5f, 1.0f
		},
		{
			"glyph partial top clip",
			110.0f, 90.0f, 10.0f, 20.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			110.0f, 100.0f, 10.0f, 10.0f,
			0.0f, 0.5f, 1.0f, 1.0f
		},
		{
			"glyph partial bottom clip",
			110.0f, 130.0f, 10.0f, 20.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			false,
			110.0f, 130.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 0.5f
		},
		{
			"glyph exact right edge cull",
			150.0f, 110.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			true,
			150.0f, 110.0f, 0.0f, 10.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		},
		{
			"glyph exact bottom edge cull",
			110.0f, 140.0f, 10.0f, 10.0f,
			0.0f, 0.0f, 1.0f, 1.0f,
			true,
			110.0f, 140.0f, 10.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f
		}
	};
	for ( int glyphClipCaseIndex = 0; glyphClipCaseIndex < static_cast<int>( sizeof( glyphClipCases ) / sizeof( glyphClipCases[0] ) ); ++glyphClipCaseIndex ) {
		ok &= openQ4_CheckGlyphClipCase( glyphClipDc, glyphClipCases[glyphClipCaseIndex] );
	}

	idDeviceContext dc;
	dc.EnableClipping( true );
	dc.PushClipRect( idRectangle( 0.0f, 0.0f, static_cast<float>( VIRTUAL_WIDTH ), static_cast<float>( VIRTUAL_HEIGHT ) ) );
	dc.PushClipRect( idRectangle( -wideExpand, 0.0f, static_cast<float>( VIRTUAL_WIDTH ) + 2.0f * wideExpand, static_cast<float>( VIRTUAL_HEIGHT ) ) );

	float x = -110.0f;
	float y = 10.0f;
	float w = 10.0f;
	float h = 20.0f;
	float s1 = 0.0f;
	float t1 = 0.0f;
	float s2 = 1.0f;
	float t2 = 1.0f;
	const bool partialClippedOut = dc.ClippedCoords( &x, &y, &w, &h, &s1, &t1, &s2, &t2 );
	ok &= openQ4_CheckBool( "expanded partial clip result", partialClippedOut, false );
	ok &= openQ4_CheckNear( "expanded partial clip x", x, -wideExpand );
	ok &= openQ4_CheckNear( "expanded partial clip width", w, wideExpand - 100.0f );
	ok &= openQ4_CheckNear( "expanded partial clip s1", s1, ( 110.0f - wideExpand ) * 0.1f );
	ok &= openQ4_CheckNear( "expanded partial clip s2", s2, 1.0f );
	ok &= openQ4_CheckNear( "expanded partial clip t1", t1, 0.0f );
	ok &= openQ4_CheckNear( "expanded partial clip t2", t2, 1.0f );

	x = -100.0f;
	y = 10.0f;
	w = 20.0f;
	h = 20.0f;
	s1 = 0.0f;
	t1 = 0.0f;
	s2 = 1.0f;
	t2 = 1.0f;
	const bool insideClippedOut = dc.ClippedCoords( &x, &y, &w, &h, &s1, &t1, &s2, &t2 );
	ok &= openQ4_CheckBool( "expanded inside clip result", insideClippedOut, false );
	ok &= openQ4_CheckNear( "expanded inside clip x", x, -100.0f );
	ok &= openQ4_CheckNear( "expanded inside clip width", w, 20.0f );
	ok &= openQ4_CheckNear( "expanded inside clip s1", s1, 0.0f );
	ok &= openQ4_CheckNear( "expanded inside clip s2", s2, 1.0f );

	if ( ok ) {
		common->Printf( "uiFontParitySelfTest passed: retail glyph metrics, per-glyph atlas selection, atlas upload, icon sizing, cursor handling, alignment, aspect expansion, and clipping are stable\n" );
	}
	return ok;
}

/*
=============
idRectangle::String
=============
*/
char *idRectangle::String( void ) const {
	static	int		index = 0;
	static	char	str[ 8 ][ 48 ];
	char	*s;

	// use an array so that multiple toString's won't collide
	s = str[ index ];
	index = (index + 1)&7;

	idStr::snPrintf( s, sizeof( str[0] ), "%.2f %.2f %.2f %.2f", x, y, w, h );

	return s;
}
