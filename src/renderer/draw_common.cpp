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



#include "tr_local.h"
#include "CelShading.h"
#include "ModernGLExecutor.h"
#include "ModernGLShaderLibrary.h"
#include "RendererMetrics.h"

#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

static bool RB_ImageIsCurrentRender( const idImage *image ) {
	if ( image == NULL ) {
		return false;
	}

	if ( image == globalImages->currentRenderImage || image == globalImages->originalCurrentRenderImage ) {
		return true;
	}

	const char *name = image->GetName();
	if ( name == NULL ) {
		return false;
	}

	return idStr::Icmpn( name, "_currentRender", 14 ) == 0;
}

static bool RB_ImageIsCurrentDepth( const idImage *image ) {
	if ( image == NULL ) {
		return false;
	}

	if ( image == globalImages->currentDepthImage ) {
		return true;
	}

	const char *name = image->GetName();
	if ( name == NULL ) {
		return false;
	}

	return idStr::Icmpn( name, "_currentDepth", 13 ) == 0;
}

static bool RB_StageUsesCurrentRender( const shaderStage_t *stage ) {
	if ( stage == NULL ) {
		return false;
	}

	if ( RB_ImageIsCurrentRender( stage->texture.image ) ) {
		return true;
	}

	const newShaderStage_t *newStage = stage->newStage;
	if ( newStage == NULL ) {
		return false;
	}

	for ( int i = 0; i < newStage->numFragmentProgramImages; i++ ) {
		if ( RB_ImageIsCurrentRender( newStage->fragmentProgramImages[i] ) ) {
			return true;
		}
	}

	for ( int i = 0; i < newStage->numShaderTextures; i++ ) {
		if ( RB_ImageIsCurrentRender( newStage->shaderTextureImages[i] ) ) {
			return true;
		}
	}

	return false;
}

static bool RB_StageUsesCurrentDepth( const shaderStage_t *stage ) {
	if ( stage == NULL ) {
		return false;
	}

	if ( RB_ImageIsCurrentDepth( stage->texture.image ) ) {
		return true;
	}

	const newShaderStage_t *newStage = stage->newStage;
	if ( newStage == NULL ) {
		return false;
	}

	for ( int i = 0; i < newStage->numFragmentProgramImages; i++ ) {
		if ( RB_ImageIsCurrentDepth( newStage->fragmentProgramImages[i] ) ) {
			return true;
		}
	}

	for ( int i = 0; i < newStage->numShaderTextures; i++ ) {
		if ( RB_ImageIsCurrentDepth( newStage->shaderTextureImages[i] ) ) {
			return true;
		}
	}

	return false;
}

static bool RB_MaterialUsesCurrentDepth( const idMaterial *material ) {
	if ( material == NULL ) {
		return false;
	}

	const int stageCount = material->GetNumStages();
	for ( int i = 0; i < stageCount; i++ ) {
		if ( RB_StageUsesCurrentDepth( material->GetStage( i ) ) ) {
			return true;
		}
	}

	return false;
}

typedef bool ( *rbShaderPassSurfFilter_t )( const drawSurf_t *surf );

static bool RB_DrawSurfNeedsLegacyFeedback( const drawSurf_t *surf ) {
	const idMaterial *material = surf != NULL ? surf->material : NULL;
	if ( material == NULL ) {
		return false;
	}
	if ( RB_DrawSurfHasSoftParticleStage( surf ) ) {
		return true;
	}
	return material->TestMaterialFlag( MF_NEED_CURRENT_RENDER )
		|| material->HasSubview()
		|| material->GetSort() == SS_SUBVIEW;
}

static bool RB_DrawSurfIsDecalMaterialPass( const drawSurf_t *surf ) {
	const idMaterial *material = surf != NULL ? surf->material : NULL;
	if ( material == NULL ) {
		return false;
	}
	return surf->decalColorCache != NULL
		|| ( material->GetSort() >= SS_DECAL && material->GetSort() < SS_FAR );
}

static bool RB_DrawSurfIsPreFogMaterialPass( const drawSurf_t *surf ) {
	const idMaterial *material = surf != NULL ? surf->material : NULL;
	if ( material == NULL ) {
		return false;
	}
	if ( RB_DrawSurfIsDecalMaterialPass( surf ) ) {
		return !r_skipDecals.GetBool();
	}
	return material->GetSort() < SS_MEDIUM;
}

static bool RB_DrawSurfIsPostFogMaterialPass( const drawSurf_t *surf ) {
	const idMaterial *material = surf != NULL ? surf->material : NULL;
	if ( material == NULL ) {
		return false;
	}
	return material->GetSort() >= SS_MEDIUM && material->GetSort() < SS_POST_PROCESS;
}

static bool RB_DrawSurfNeedsPreFogLegacyFeedback( const drawSurf_t *surf ) {
	return RB_DrawSurfNeedsLegacyFeedback( surf ) && RB_DrawSurfIsPreFogMaterialPass( surf );
}

static bool RB_DrawSurfNeedsPostFogLegacyFeedback( const drawSurf_t *surf ) {
	return RB_DrawSurfNeedsLegacyFeedback( surf ) && RB_DrawSurfIsPostFogMaterialPass( surf );
}

static bool RB_HasLegacyFeedbackDrawSurfs( drawSurf_t **drawSurfs, int numDrawSurfs, rbShaderPassSurfFilter_t filter = NULL ) {
	for ( int i = 0; i < numDrawSurfs; ++i ) {
		if ( filter != NULL && !filter( drawSurfs[i] ) ) {
			continue;
		}
		if ( RB_DrawSurfNeedsLegacyFeedback( drawSurfs[i] ) ) {
			return true;
		}
	}
	return false;
}

static const int RB_STOCK_GAUSSIAN_SAMPLE_COUNT = 15;
static const idVec4 RB_STOCK_COLOR_MATRIX_ROWS[3] = {
	idVec4( 1.0f, 0.0f, 0.0f, 0.0f ),
	idVec4( 0.0f, 1.0f, 0.0f, 0.0f ),
	idVec4( 0.0f, 0.0f, 1.0f, 0.0f )
};

enum md5rFogProgramParameter_t {
	MD5R_BASIC_FOG_VPROG_BASE = ARB2_MD5R_BASIC_FOG_VPROG_BASE,
	MD5R_FOG_DISTANCE_PLANE_PARAM = 93,
	MD5R_FOG_DISTANCE_BIAS_PARAM,
	MD5R_FOG_ENTER_PLANE_T_PARAM,
	MD5R_FOG_ENTER_PLANE_S_PARAM
};

static idVec4 rbStockGaussianSampleOffsets[RB_STOCK_GAUSSIAN_SAMPLE_COUNT];
static idVec4 rbStockGaussianSampleWeights[RB_STOCK_GAUSSIAN_SAMPLE_COUNT];
static idVec4 rbStockGaussianSampleOffsetsHorizontal[RB_STOCK_GAUSSIAN_SAMPLE_COUNT];
static idVec4 rbStockGaussianSampleOffsetsVertical[RB_STOCK_GAUSSIAN_SAMPLE_COUNT];
static idVec4 rbStockGaussianSampleWeights2[RB_STOCK_GAUSSIAN_SAMPLE_COUNT];
static int rbStockGaussianViewportWidth = -1;
static int rbStockGaussianViewportHeight = -1;
idPlane fogTexGenPlanes[4];

static float RB_StockGaussian1D( float offset, float deviation ) {
	const float variance = deviation * deviation;
	const float normalization = 1.0f / idMath::Sqrt( 2.0f * idMath::PI * variance );
	return normalization * idMath::Exp( -( offset * offset ) / ( 2.0f * variance ) );
}

static float RB_FogDistanceScale( float alpha ) {
	if ( alpha <= 1.0f ) {
		return -0.5f / DEFAULT_FOG_DISTANCE;
	}

	return -0.5f / alpha;
}

static void RB_CalculateStockGaussianCoefficients( int width, int height, float multiplier ) {
	memset( rbStockGaussianSampleOffsets, 0, sizeof( rbStockGaussianSampleOffsets ) );
	memset( rbStockGaussianSampleWeights, 0, sizeof( rbStockGaussianSampleWeights ) );

	float totalWeight = 0.0f;
	int count = 0;
	for ( int y = -2; y <= 2 && count < RB_STOCK_GAUSSIAN_SAMPLE_COUNT; y++ ) {
		for ( int x = -2; x <= 2 && count < RB_STOCK_GAUSSIAN_SAMPLE_COUNT; x++ ) {
			if ( abs( x ) + abs( y ) > 2 ) {
				continue;
			}

			const float weight = RB_StockGaussian1D( idMath::Sqrt( static_cast<float>( x * x + y * y ) ), 1.0f );
			rbStockGaussianSampleOffsets[count].Set(
				static_cast<float>( x ) / static_cast<float>( width ),
				static_cast<float>( y ) / static_cast<float>( height ),
				0.0f,
				0.0f );
			rbStockGaussianSampleWeights[count].Set( weight, weight, weight, weight );
			totalWeight += weight;
			count++;
		}
	}

	if ( totalWeight <= 0.0f ) {
		return;
	}

	const float scale = multiplier / totalWeight;
	for ( int i = 0; i < RB_STOCK_GAUSSIAN_SAMPLE_COUNT; i++ ) {
		rbStockGaussianSampleWeights[i] *= scale;
	}
}

static void RB_CalculateStockGaussianCoefficients1D( int size, float multiplier, float deviation,
	idVec4 *sampleOffsets, idVec4 *sampleWeights ) {
	const int halfSampleCount = ( RB_STOCK_GAUSSIAN_SAMPLE_COUNT + 1 ) / 2;

	memset( sampleOffsets, 0, sizeof( idVec4 ) * RB_STOCK_GAUSSIAN_SAMPLE_COUNT );
	if ( sampleWeights != NULL ) {
		memset( sampleWeights, 0, sizeof( idVec4 ) * RB_STOCK_GAUSSIAN_SAMPLE_COUNT );
	}

	for ( int i = 0; i < halfSampleCount; i++ ) {
		const float offset = static_cast<float>( i ) / static_cast<float>( size );
		sampleOffsets[i].Set( offset, 0.0f, 0.0f, 0.0f );

		if ( sampleWeights != NULL ) {
			const float weight = RB_StockGaussian1D( static_cast<float>( i ), deviation ) * multiplier;
			sampleWeights[i].Set( weight, weight, weight, 1.0f );
		}
	}

	for ( int i = halfSampleCount; i < RB_STOCK_GAUSSIAN_SAMPLE_COUNT; i++ ) {
		const int mirrorIndex = RB_STOCK_GAUSSIAN_SAMPLE_COUNT - i;
		sampleOffsets[i].Set( -sampleOffsets[mirrorIndex].x, 0.0f, 0.0f, 0.0f );
		if ( sampleWeights != NULL ) {
			sampleWeights[i] = sampleWeights[mirrorIndex];
		}
	}
}

static void RB_UpdateStockGLSLShaderConstantCache() {
	if ( backEnd.viewDef == NULL ) {
		return;
	}

	const int viewportWidth = Max( 1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1 );
	const int viewportHeight = Max( 1, backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
	if ( viewportWidth == rbStockGaussianViewportWidth && viewportHeight == rbStockGaussianViewportHeight ) {
		return;
	}

	rbStockGaussianViewportWidth = viewportWidth;
	rbStockGaussianViewportHeight = viewportHeight;

	RB_CalculateStockGaussianCoefficients( viewportWidth, viewportHeight, 1.0f );
	RB_CalculateStockGaussianCoefficients1D( viewportWidth, 1.0f, 3.0f,
		rbStockGaussianSampleOffsetsHorizontal, rbStockGaussianSampleWeights2 );
	RB_CalculateStockGaussianCoefficients1D( viewportHeight, 1.0f, 3.0f,
		rbStockGaussianSampleOffsetsVertical, NULL );

	for ( int i = 0; i < RB_STOCK_GAUSSIAN_SAMPLE_COUNT; i++ ) {
		rbStockGaussianSampleOffsetsVertical[i].y = rbStockGaussianSampleOffsetsVertical[i].x;
		rbStockGaussianSampleOffsetsVertical[i].x = 0.0f;
	}
}

bool RB_BindGLSLShaderParm( glslShaderParmBinding_t binding, int location, const shaderStage_t *stage, const drawInteraction_t *din ) {
	if ( location < 0 || backEnd.viewDef == NULL ) {
		return false;
	}

	switch ( binding ) {
	case GLSL_SHADERPARM_LOCAL_LIGHT_ORIGIN:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->localLightOrigin.ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_LOCAL_VIEW_ORIGIN:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->localViewOrigin.ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_LIGHT_PROJECT_S:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->lightProjection[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_LIGHT_PROJECT_T:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->lightProjection[1].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_LIGHT_PROJECT_Q:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->lightProjection[2].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_LIGHT_FALLOFF_S:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->lightProjection[3].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_BUMP_MATRIX_S:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->bumpMatrix[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_BUMP_MATRIX_T:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->bumpMatrix[1].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_DIFFUSE_MATRIX_S:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->diffuseMatrix[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_DIFFUSE_MATRIX_T:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->diffuseMatrix[1].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_SPECULAR_MATRIX_S:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->specularMatrix[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_SPECULAR_MATRIX_T:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->specularMatrix[1].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_DIFFUSE_COLOR:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->diffuseColor.ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_SPECULAR_COLOR:
		if ( din == NULL ) {
			return false;
		}
		glUniform4fvARB( location, 1, din->specularColor.ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_VIEW_ORIGIN: {
		idVec4 viewOrigin;
		viewOrigin.ToVec3() = backEnd.viewDef->renderView.vieworg;
		viewOrigin.w = 1.0f;
		glUniform4fvARB( location, 1, viewOrigin.ToFloatPtr() );
		return true;
	}
	case GLSL_SHADERPARM_COLOR_MATRIX0:
		glUniform4fvARB( location, 1, RB_STOCK_COLOR_MATRIX_ROWS[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_COLOR_MATRIX1:
		glUniform4fvARB( location, 1, RB_STOCK_COLOR_MATRIX_ROWS[1].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_COLOR_MATRIX2:
		glUniform4fvARB( location, 1, RB_STOCK_COLOR_MATRIX_ROWS[2].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_COLOR_MODULATE:
		if ( din == NULL && stage == NULL ) {
			return false;
		}
		switch ( din != NULL ? din->vertexColor : stage->vertexColor ) {
		case SVC_IGNORE:
			glUniform4fvARB( location, 1, vec4_zero.ToFloatPtr() );
			break;
		case SVC_MODULATE:
			glUniform4fvARB( location, 1, colorWhite.ToFloatPtr() );
			break;
		case SVC_INVERSE_MODULATE: {
			const idVec4 negOne( -1.0f, -1.0f, -1.0f, -1.0f );
			glUniform4fvARB( location, 1, negOne.ToFloatPtr() );
			break;
		}
		}
		return true;
	case GLSL_SHADERPARM_COLOR_ADD:
		if ( din == NULL && stage == NULL ) {
			return false;
		}
		switch ( din != NULL ? din->vertexColor : stage->vertexColor ) {
		case SVC_MODULATE:
			glUniform4fvARB( location, 1, vec4_zero.ToFloatPtr() );
			break;
		case SVC_IGNORE:
		case SVC_INVERSE_MODULATE:
			glUniform4fvARB( location, 1, colorWhite.ToFloatPtr() );
			break;
		}
		return true;
	case GLSL_SHADERPARM_PROJECTION_ROW_0:
	case GLSL_SHADERPARM_PROJECTION_ROW_1:
	case GLSL_SHADERPARM_PROJECTION_ROW_2:
	case GLSL_SHADERPARM_PROJECTION_ROW_3: {
		const int row = binding - GLSL_SHADERPARM_PROJECTION_ROW_0;
		idVec4 projectionRow(
			backEnd.viewDef->projectionMatrix[row + 0],
			backEnd.viewDef->projectionMatrix[row + 4],
			backEnd.viewDef->projectionMatrix[row + 8],
			backEnd.viewDef->projectionMatrix[row + 12] );
		glUniform4fvARB( location, 1, projectionRow.ToFloatPtr() );
		return true;
	}
	case GLSL_SHADERPARM_MODEL_ROW_0:
	case GLSL_SHADERPARM_MODEL_ROW_1:
	case GLSL_SHADERPARM_MODEL_ROW_2: {
		const viewEntity_t *space = din != NULL && din->surf != NULL ? din->surf->space : backEnd.currentSpace;
		if ( space == NULL ) {
			return false;
		}
		const int row = binding - GLSL_SHADERPARM_MODEL_ROW_0;
		idVec4 modelRow(
			space->modelMatrix[row + 0],
			space->modelMatrix[row + 4],
			space->modelMatrix[row + 8],
			space->modelMatrix[row + 12] );
		glUniform4fvARB( location, 1, modelRow.ToFloatPtr() );
		return true;
	}
	case GLSL_SHADERPARM_GAUSSIAN_SAMPLE_OFFSETS:
		RB_UpdateStockGLSLShaderConstantCache();
		glUniform4fvARB( location, RB_STOCK_GAUSSIAN_SAMPLE_COUNT, rbStockGaussianSampleOffsets[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_GAUSSIAN_SAMPLE_OFFSETS_HORIZONTAL:
		RB_UpdateStockGLSLShaderConstantCache();
		glUniform4fvARB( location, RB_STOCK_GAUSSIAN_SAMPLE_COUNT, rbStockGaussianSampleOffsetsHorizontal[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_GAUSSIAN_SAMPLE_OFFSETS_VERTICAL:
		RB_UpdateStockGLSLShaderConstantCache();
		glUniform4fvARB( location, RB_STOCK_GAUSSIAN_SAMPLE_COUNT, rbStockGaussianSampleOffsetsVertical[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_GAUSSIAN_SAMPLE_WEIGHTS:
		RB_UpdateStockGLSLShaderConstantCache();
		glUniform4fvARB( location, RB_STOCK_GAUSSIAN_SAMPLE_COUNT, rbStockGaussianSampleWeights[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_GAUSSIAN_SAMPLE_WEIGHTS2:
		RB_UpdateStockGLSLShaderConstantCache();
		glUniform4fvARB( location, RB_STOCK_GAUSSIAN_SAMPLE_COUNT, rbStockGaussianSampleWeights2[0].ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_POSTPROCESS_INV_TEX_SIZE: {
		const GLfloat invTexSize[2] = {
			backEnd.postProcessTexelSize.x,
			backEnd.postProcessTexelSize.y
		};
		glUniform2fvARB( location, 1, invTexSize );
		return true;
	}
	case GLSL_SHADERPARM_POSTPROCESS_TEX_SIZE: {
		const GLfloat texSize[2] = {
			backEnd.postProcessTexelSize.z,
			backEnd.postProcessTexelSize.w
		};
		glUniform2fvARB( location, 1, texSize );
		return true;
	}
	case GLSL_SHADERPARM_POSTPROCESS_SOURCE_COLOR_SPACE:
		glUniform4fvARB( location, 1, backEnd.postProcessSourceColorSpace.ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_POSTPROCESS_SMAA_QUALITY:
		glUniform4fvARB( location, 1, backEnd.postProcessSMAAQuality.ToFloatPtr() );
		return true;
	case GLSL_SHADERPARM_CURRENT_RENDER_VIEWPORT_ORIGIN: {
		const GLfloat viewportOrigin[2] = {
			static_cast<GLfloat>( backEnd.viewDef->viewport.x1 ),
			static_cast<GLfloat>( backEnd.viewDef->viewport.y1 )
		};
		glUniform2fvARB( location, 1, viewportOrigin );
		return true;
	}
	case GLSL_SHADERPARM_CURRENT_RENDER_VIEWPORT_SIZE: {
		const GLfloat viewportSize[2] = {
			static_cast<GLfloat>( Max( 1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1 ) ),
			static_cast<GLfloat>( Max( 1, backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 ) )
		};
		glUniform2fvARB( location, 1, viewportSize );
		return true;
	}
	case GLSL_SHADERPARM_CURRENT_RENDER_TEXTURE_SCALE: {
		const int viewportWidth = Max( 1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1 );
		const int viewportHeight = Max( 1, backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
		int textureWidth = viewportWidth;
		int textureHeight = viewportHeight;

		if ( globalImages->currentRenderImage != NULL ) {
			textureWidth = Max( 1, globalImages->currentRenderImage->GetOpts().width );
			textureHeight = Max( 1, globalImages->currentRenderImage->GetOpts().height );
		}

		const GLfloat textureScale[2] = {
			static_cast<GLfloat>( viewportWidth ) / static_cast<GLfloat>( textureWidth ),
			static_cast<GLfloat>( viewportHeight ) / static_cast<GLfloat>( textureHeight )
		};
		glUniform2fvARB( location, 1, textureScale );
		return true;
	}
	case GLSL_SHADERPARM_REGISTERS:
	default:
		return false;
	}
}

idImage *RB_ResolveGLSLShaderTextureImage( const newShaderStage_t *stage, int slot, const drawInteraction_t *din ) {
	if ( stage == NULL || slot < 0 || slot >= stage->numShaderTextures ) {
		return NULL;
	}

	switch ( stage->shaderTextureBindings[slot] ) {
	case GLSL_SHADERTEXTURE_LIGHT_FALLOFF:
		if ( din != NULL && din->lightFalloffImage != NULL ) {
			return din->lightFalloffImage;
		}
		return globalImages->whiteImage;
	case GLSL_SHADERTEXTURE_LIGHT_IMAGE:
		if ( din != NULL && din->lightImage != NULL ) {
			return din->lightImage;
		}
		return globalImages->whiteImage;
	case GLSL_SHADERTEXTURE_AMBIENT_NORMAL_MAP:
		return globalImages->ambientNormalMap ? globalImages->ambientNormalMap : globalImages->defaultImage;
	case GLSL_SHADERTEXTURE_NORMAL_CUBE_MAP:
		return globalImages->normalCubeMapImage ? globalImages->normalCubeMapImage : globalImages->defaultImage;
	case GLSL_SHADERTEXTURE_SPECULAR_TABLE:
		return globalImages->specularTableImage ? globalImages->specularTableImage : globalImages->defaultImage;
	case GLSL_SHADERTEXTURE_IMAGE:
	default:
		if ( din != NULL
			&& RB_FlatDiffuseSurfaceActive( din->surf )
			&& idStr::Icmp( stage->shaderTextureNames[slot], "DiffuseMap" ) == 0 ) {
			return globalImages->whiteImage;
		}
		return stage->shaderTextureImages[slot];
	}
}

static inline void RB_SetStageVertexColorPointer( const drawSurf_t *surf, int stage, idDrawVert *ac ) {
	if ( surf->decalColorCache != NULL && stage >= 0 && stage < surf->decalColorStageCount && surf->decalColorStride > 0 ) {
		void *colorData = vertexCache.Position( surf->decalColorCache );
		glColorPointer( 4, GL_UNSIGNED_BYTE, 0, RB_DrawVertAttributePointer( colorData, surf->decalColorOffset + stage * surf->decalColorStride ) );
		return;
	}

	glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, color ) ) );
}

static bool RB_UseAlphaToCoverage( const idMaterial *shader ) {
	if ( !r_msaaAlphaToCoverage.GetBool() ) {
		return false;
	}

	if ( shader == NULL || shader->Coverage() != MC_PERFORATED ) {
		return false;
	}

	if ( !( GLEW_ARB_multisample || GLEW_VERSION_1_3 ) ) {
		return false;
	}

	if ( backEnd.renderTexture == NULL || backEnd.renderTexture->GetNumColorImages() <= 0 ) {
		return false;
	}

	idImage *colorImage = backEnd.renderTexture->GetColorImage( 0 );
	if ( colorImage == NULL ) {
		return false;
	}

	return colorImage->GetOpts().numMSAASamples > 1;
}

static void RB_FreeGLSLProgram( newShaderStage_t *stage ) {
	if ( stage == NULL ) {
		return;
	}

	// only delete handles compiled in the current context generation; handles
	// from a destroyed context died with it and their names may alias live
	// objects in this one (partial restarts keep the context, so the
	// generation matches and the delete still runs)
	if ( stage->glslProgramObject != 0 && glConfig.isInitialized
			&& stage->glslProgramGeneration == tr.glContextGeneration ) {
		if ( stage->glslVertexShaderObject != 0 ) {
			glDetachObjectARB(
				(GLhandleARB)stage->glslProgramObject,
				(GLhandleARB)stage->glslVertexShaderObject );
			glDeleteObjectARB( (GLhandleARB)stage->glslVertexShaderObject );
		}
		if ( stage->glslFragmentShaderObject != 0 ) {
			glDetachObjectARB(
				(GLhandleARB)stage->glslProgramObject,
				(GLhandleARB)stage->glslFragmentShaderObject );
			glDeleteObjectARB( (GLhandleARB)stage->glslFragmentShaderObject );
		}
		glDeleteObjectARB( (GLhandleARB)stage->glslProgramObject );
	}

	stage->glslProgramObject = 0;
	stage->glslVertexShaderObject = 0;
	stage->glslFragmentShaderObject = 0;
	stage->glslProgramLoaded = false;
	stage->glslProgramValid = false;
	stage->glslProgramGeneration = 0;
}

static void RB_PrintGLSLInfoLog( GLhandleARB object, const char *label, const char *name ) {
	GLint logLength = 0;
	glGetObjectParameterivARB( object, GL_OBJECT_INFO_LOG_LENGTH_ARB, &logLength );
	if ( logLength <= 1 ) {
		common->Warning( "GLSL %s error in '%s' (no info log)", label, name );
		return;
	}
	if ( logLength > 1024 * 1024 ) {
		common->Warning( "GLSL %s error in '%s' has oversized info log (%d bytes), truncating", label, name, logLength );
		logLength = 1024 * 1024;
	}

	char *logBuffer = (char *)Mem_ClearedAlloc( logLength + 1 );
	GLsizei written = 0;
	glGetInfoLogARB( object, logLength, &written, logBuffer );
	logBuffer[ idMath::ClampInt( 0, logLength, written ) ] = '\0';
	common->Warning( "GLSL %s error in '%s':\n%s", label, name, logBuffer );
	Mem_Free( logBuffer );
}

static bool RB_PathHasGlprogsPrefix( const idStr &path ) {
	return idStr::Icmpn( path.c_str(), "glprogs/", 8 ) == 0;
}

static idStr RB_NormalizeGLSLPath( const idStr &path ) {
	idStr result = path;
	result.BackSlashesToSlashes();
	if ( !RB_PathHasGlprogsPrefix( result ) ) {
		idStr prefixed = "glprogs/";
		prefixed += result;
		return prefixed;
	}
	return result;
}

static bool RB_ReadGLSLSourcePair( const idStr &vertexPath, const idStr &fragmentPath, char **vertexBuffer, char **fragmentBuffer ) {
	*vertexBuffer = NULL;
	*fragmentBuffer = NULL;

	fileSystem->ReadFile( vertexPath.c_str(), (void **)vertexBuffer, NULL );
	if ( *vertexBuffer == NULL ) {
		return false;
	}

	fileSystem->ReadFile( fragmentPath.c_str(), (void **)fragmentBuffer, NULL );
	if ( *fragmentBuffer == NULL ) {
		fileSystem->FreeFile( *vertexBuffer );
		*vertexBuffer = NULL;
		return false;
	}

	return true;
}

static bool RB_FindGLSLSourcePair( const char *programName, idStr &vertexPath, idStr &fragmentPath, char **vertexBuffer, char **fragmentBuffer ) {
	idStr name = programName;
	name.BackSlashesToSlashes();

	idStr stripped = name;
	stripped.StripFileExtension();

	idStr ext;
	const char *dot = strrchr( name.c_str(), '.' );
	if ( dot != NULL ) {
		ext = dot + 1;
		ext.ToLower();
	}

	idStr vertexCandidates[10];
	idStr fragmentCandidates[10];
	int numCandidates = 0;

	if ( ext.Length() > 0 ) {
		if ( ext == "glsl" ) {
			vertexCandidates[numCandidates] = stripped + ".glslvp";
			fragmentCandidates[numCandidates++] = stripped + ".glslfp";
			vertexCandidates[numCandidates] = stripped + ".vs";
			fragmentCandidates[numCandidates++] = stripped + ".fs";
		} else if ( ext == "fs" ) {
			vertexCandidates[numCandidates] = stripped + ".vs";
			fragmentCandidates[numCandidates++] = name;
		} else if ( ext == "vs" ) {
			vertexCandidates[numCandidates] = name;
			fragmentCandidates[numCandidates++] = stripped + ".fs";
		} else if ( ext == "fp" ) {
			vertexCandidates[numCandidates] = stripped + ".vp";
			fragmentCandidates[numCandidates++] = name;
		} else if ( ext == "vp" ) {
			vertexCandidates[numCandidates] = name;
			fragmentCandidates[numCandidates++] = stripped + ".fp";
		}
	}

	vertexCandidates[numCandidates] = name + ".vs";
	fragmentCandidates[numCandidates++] = name + ".fs";
	vertexCandidates[numCandidates] = name + ".glslvp";
	fragmentCandidates[numCandidates++] = name + ".glslfp";
	vertexCandidates[numCandidates] = name + ".vp";
	fragmentCandidates[numCandidates++] = name + ".fp";
	vertexCandidates[numCandidates] = stripped + ".vs";
	fragmentCandidates[numCandidates++] = stripped + ".fs";
	vertexCandidates[numCandidates] = stripped + ".glslvp";
	fragmentCandidates[numCandidates++] = stripped + ".glslfp";
	vertexCandidates[numCandidates] = stripped + ".vp";
	fragmentCandidates[numCandidates++] = stripped + ".fp";

	for ( int i = 0; i < numCandidates; i++ ) {
		const idStr candidateVertex = RB_NormalizeGLSLPath( vertexCandidates[i] );
		const idStr candidateFragment = RB_NormalizeGLSLPath( fragmentCandidates[i] );
		if ( RB_ReadGLSLSourcePair( candidateVertex, candidateFragment, vertexBuffer, fragmentBuffer ) ) {
			vertexPath = candidateVertex;
			fragmentPath = candidateFragment;
			return true;
		}
	}

	return false;
}

bool R_ValidateGLSLProgram( newShaderStage_t *stage ) {
	if ( !stage->glslProgram ) {
		return false;
	}

	if ( !glConfig.GLSLProgramAvailable ) {
		stage->glslProgramLoaded = true;
		stage->glslProgramValid = false;
		return false;
	}

	if ( stage->glslProgramLoaded && stage->glslProgramGeneration == tr.glContextGeneration ) {
		return stage->glslProgramValid;
	}

	RB_FreeGLSLProgram( stage );

	char *vertexBuffer = NULL;
	char *fragmentBuffer = NULL;
	idStr vertexPath;
	idStr fragmentPath;
	if ( !RB_FindGLSLSourcePair( stage->glslProgramName, vertexPath, fragmentPath, &vertexBuffer, &fragmentBuffer ) ) {
		stage->glslProgramLoaded = true;
		stage->glslProgramValid = false;
		common->Warning( "Couldn't find GLSL sources for program '%s'", stage->glslProgramName );
		return false;
	}

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );

	const GLcharARB *vertexSource = (const GLcharARB *)vertexBuffer;
	const GLcharARB *fragmentSource = (const GLcharARB *)fragmentBuffer;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	fileSystem->FreeFile( vertexBuffer );
	fileSystem->FreeFile( fragmentBuffer );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_PrintGLSLInfoLog( vertexShader, "vertex shader compile", stage->glslProgramName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		stage->glslProgramLoaded = true;
		stage->glslProgramValid = false;
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_PrintGLSLInfoLog( fragmentShader, "fragment shader compile", stage->glslProgramName );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		stage->glslProgramLoaded = true;
		stage->glslProgramValid = false;
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glBindAttribLocationARB( programObject, 0, "attr_Position" );
	glBindAttribLocationARB( programObject, 8, "attr_TexCoord0" );
	glBindAttribLocationARB( programObject, 9, "attr_Tangent" );
	glBindAttribLocationARB( programObject, 10, "attr_Bitangent" );
	glBindAttribLocationARB( programObject, 11, "attr_Normal" );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_PrintGLSLInfoLog( programObject, "program link", stage->glslProgramName );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		stage->glslProgramLoaded = true;
		stage->glslProgramValid = false;
		return false;
	}

	stage->glslProgramObject = (int)programObject;
	stage->glslVertexShaderObject = (int)vertexShader;
	stage->glslFragmentShaderObject = (int)fragmentShader;
	stage->glslProgramLoaded = true;
	stage->glslProgramValid = true;
	stage->glslProgramGeneration = tr.glContextGeneration;

	for ( int i = 0; i < stage->numShaderParms; i++ ) {
		stage->shaderParmLocations[i] = glGetUniformLocationARB( programObject, stage->shaderParmNames[i] );
	}
	for ( int i = 0; i < stage->numShaderTextures; i++ ) {
		stage->shaderTextureLocations[i] = glGetUniformLocationARB( programObject, stage->shaderTextureNames[i] );
		if ( stage->shaderTextureLocations[i] < 0 ) {
			common->Warning(
				"GLSL program '%s' is missing sampler uniform '%s' declared by the material stage.",
				stage->glslProgramName,
				stage->shaderTextureNames[i] );
			RB_FreeGLSLProgram( stage );
			return false;
		}
	}

	common->Printf( "Loaded GLSL program '%s' (%s, %s)\n",
		stage->glslProgramName, vertexPath.c_str(), fragmentPath.c_str() );

	return true;
}

static bool RB_IsMainScenePostProcessView( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return false;
	}

	// Fullscreen 2D GUI/menu passes are emitted as standalone views without
	// view entities. Skip scene post-process passes on those views so menu
	// assets stay unfiltered.
	if ( viewDef->viewEntitys == NULL ) {
		return false;
	}

	// Portal-sky views are scene contributors for the following root view.
	// They must not run SSAO/HDR/bloom or present as an independent final frame.
	if ( ( viewDef->renderFlags & RF_PORTAL_SKY ) != 0 ) {
		return false;
	}

	// Scene HDR, bloom, SSAO, and tonemapping are final-frame
	// effects. Nested render-to-texture views such as skies, mirrors, remote
	// cameras, and render demos should feed the root scene unfiltered, then be
	// processed once with the full frame. Filtering sidecar views separately
	// makes the effect appear attached to those subviews instead of the world.
	if ( viewDef->isSubview
		|| viewDef->superView != NULL
		|| viewDef->subviewSurface != NULL
		|| viewDef->renderView.viewID < 0 ) {
		return false;
	}

	// GUI renderDef previews allocate their own transient renderWorld with no
	// loaded map. Those views must composite directly over the already drawn
	// menu instead of being routed through the fullscreen scene-target present
	// path, which would overwrite the menu with an opaque buffer.
	if ( viewDef->renderWorld != NULL && viewDef->renderWorld->mapName.Length() == 0 ) {
		return false;
	}

	// X-ray subviews intentionally diverge from the normal scene shading path.
	return !viewDef->isXraySubview;
}

static bool RB_IsMainScenePostProcessView( void ) {
	return RB_IsMainScenePostProcessView( backEnd.viewDef );
}

static const int RB_BLOOM_MAX_LEVELS = 5;
static const int RB_HDR_EXPOSURE_MAX_LEVELS = 12;
static const float RB_BLOOM_BASE_WEIGHTS[RB_BLOOM_MAX_LEVELS] = {
	0.34f, 0.24f, 0.17f, 0.14f, 0.11f
};

static idImage *rbSceneColorImage = NULL;
static idImage *rbSceneDepthStencilImage = NULL;
static idRenderTexture *rbSceneRenderTexture = NULL;
static int rbSceneRenderTextureSamples = -1;
static int rbSceneRenderTargetPreserveFarDepthFrame = -1;
static const viewDef_t *rbSceneRenderTargetPreserveFarDepthView = NULL;
static int rbSceneRenderTargetPortalSkyFrame = -1;
static idScreenRect rbSceneRenderTargetPortalSkyViewport;
static int rbSceneRenderTargetPortalSkyWidth = 0;
static int rbSceneRenderTargetPortalSkyHeight = 0;
static idImage *rbSceneRenderTargetPreserveDepthImage = NULL;
static int rbSceneRenderTargetPreserveDepthFrame = -1;
static int rbSceneRenderTargetPreserveDepthWidth = 0;
static int rbSceneRenderTargetPreserveDepthHeight = 0;
static GLhandleARB rbSceneDepthAwarePresentProgram = 0;
static GLhandleARB rbSceneDepthAwarePresentVertexShader = 0;
static GLhandleARB rbSceneDepthAwarePresentFragmentShader = 0;
static int rbSceneDepthAwarePresentGeneration = -1;
static GLint rbSceneDepthAwarePresentSceneLocation = -1;
static GLint rbSceneDepthAwarePresentDepthLocation = -1;
static const int RB_SCREEN_FRACTION_MIN = 10;
static const int RB_SCREEN_FRACTION_NATIVE = 100;
static const int RB_SCREEN_FRACTION_MAX = 200;
static int rbLastReportedScreenFractionRequest = 0;
static int rbLastReportedScreenFractionEffective = 0;
static float rbHDRAdaptedExposure = 1.0f;
static float rbHDRLastAverageLuminance = 1.0f;
static float rbHDRLastTargetExposure = 1.0f;
static float rbHDRLastAdaptationTime = -1.0f;
static bool rbHDRExposureInitialized = false;

// double-buffered pixel-pack buffers so the auto-exposure luminance sample can
// be read back one frame late instead of draining the GPU pipeline every frame
static GLuint rbHDRExposureReadbackPBOs[2] = { 0, 0 };
static bool rbHDRExposureReadbackPrimed[2] = { false, false };
static int rbHDRExposureReadbackIndex = 0;

struct rbSceneScaleState_t {
	bool active;
	int requestedPercent;
	int effectivePercent;
	int nativeWidth;
	int nativeHeight;
	int scaledWidth;
	int scaledHeight;
	idScreenRect nativeViewport;
	idScreenRect nativeScissor;
};

static void RB_ClearSceneScaleState( rbSceneScaleState_t &state ) {
	memset( &state, 0, sizeof( state ) );
}

static int RB_RequestedScreenFraction( void ) {
	return idMath::ClampInt( RB_SCREEN_FRACTION_MIN, RB_SCREEN_FRACTION_MAX, r_screenFraction.GetInteger() );
}

static bool RB_ViewCoversBackBuffer( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return false;
	}

	const int viewportWidth = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int viewportHeight = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	return viewDef->viewport.x1 == 0
		&& viewDef->viewport.y1 == 0
		&& viewportWidth == glConfig.vidWidth
		&& viewportHeight == glConfig.vidHeight;
}

static int RB_MaxSceneScaleDimension( void ) {
	int maxDimension = ( glConfig.maxTextureSize > 0 ) ? glConfig.maxTextureSize : 4096;
	maxDimension = Min( maxDimension, 32767 );
	return Max( 1, maxDimension );
}

static int RB_EffectiveScreenFractionForView( const viewDef_t *viewDef ) {
	const int requestedPercent = RB_RequestedScreenFraction();
	if ( requestedPercent <= RB_SCREEN_FRACTION_NATIVE ) {
		return requestedPercent;
	}
	if ( viewDef == NULL ) {
		return RB_SCREEN_FRACTION_NATIVE;
	}

	const int nativeWidth = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int nativeHeight = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	if ( nativeWidth <= 0 || nativeHeight <= 0 ) {
		return RB_SCREEN_FRACTION_NATIVE;
	}

	const int maxDimension = RB_MaxSceneScaleDimension();
	int maxPercent = RB_SCREEN_FRACTION_MAX;
	maxPercent = Min( maxPercent, static_cast<int>( ( static_cast<int64>( maxDimension ) * 100 ) / nativeWidth ) );
	maxPercent = Min( maxPercent, static_cast<int>( ( static_cast<int64>( maxDimension ) * 100 ) / nativeHeight ) );
	const int effectivePercent = idMath::ClampInt( RB_SCREEN_FRACTION_NATIVE, RB_SCREEN_FRACTION_MAX, Min( requestedPercent, maxPercent ) );

	if ( effectivePercent != requestedPercent
		&& ( rbLastReportedScreenFractionRequest != requestedPercent || rbLastReportedScreenFractionEffective != effectivePercent ) ) {
		common->Warning(
			"Requested r_screenFraction %d%% exceeds the safe render target size for %dx%d (GL max texture size %d); using %d%%.",
			requestedPercent,
			nativeWidth,
			nativeHeight,
			glConfig.maxTextureSize,
			effectivePercent );
		rbLastReportedScreenFractionRequest = requestedPercent;
		rbLastReportedScreenFractionEffective = effectivePercent;
	}

	return effectivePercent;
}

static int RB_ScaledDimension( int nativeDimension, int scalePercent ) {
	if ( nativeDimension <= 0 ) {
		return 0;
	}
	const int64 scaled = static_cast<int64>( nativeDimension ) * scalePercent + 50;
	return Max( 1, static_cast<int>( scaled / 100 ) );
}

static bool RB_SupersampledSceneTargetRequested( const viewDef_t *viewDef ) {
	if ( RB_RequestedScreenFraction() <= RB_SCREEN_FRACTION_NATIVE ) {
		return false;
	}
	if ( !RB_IsMainScenePostProcessView( viewDef ) || !RB_ViewCoversBackBuffer( viewDef ) ) {
		return false;
	}
	return RB_EffectiveScreenFractionForView( viewDef ) > RB_SCREEN_FRACTION_NATIVE;
}

static bool RB_ComputeSupersampledSceneSize( const viewDef_t *viewDef, int &targetWidth, int &targetHeight, int *effectivePercent = NULL ) {
	if ( !RB_SupersampledSceneTargetRequested( viewDef ) ) {
		return false;
	}

	const int nativeWidth = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int nativeHeight = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	const int scalePercent = RB_EffectiveScreenFractionForView( viewDef );
	targetWidth = RB_ScaledDimension( nativeWidth, scalePercent );
	targetHeight = RB_ScaledDimension( nativeHeight, scalePercent );
	if ( effectivePercent != NULL ) {
		*effectivePercent = scalePercent;
	}
	return targetWidth > nativeWidth && targetHeight > nativeHeight;
}

static int RB_ScaleRectStart( int value, int sourceExtent, int targetExtent ) {
	return static_cast<int>( ( static_cast<int64>( value ) * targetExtent ) / sourceExtent );
}

static int RB_ScaleRectEnd( int value, int sourceExtent, int targetExtent ) {
	return static_cast<int>( ( ( static_cast<int64>( value + 1 ) * targetExtent ) + sourceExtent - 1 ) / sourceExtent ) - 1;
}

static void RB_ScaleLocalScreenRect( idScreenRect &rect, int sourceWidth, int sourceHeight, int targetWidth, int targetHeight ) {
	if ( rect.IsEmpty() || sourceWidth <= 0 || sourceHeight <= 0 || targetWidth <= 0 || targetHeight <= 0 ) {
		return;
	}

	const int x1 = idMath::ClampInt( 0, targetWidth - 1, RB_ScaleRectStart( rect.x1, sourceWidth, targetWidth ) );
	const int y1 = idMath::ClampInt( 0, targetHeight - 1, RB_ScaleRectStart( rect.y1, sourceHeight, targetHeight ) );
	const int x2 = idMath::ClampInt( 0, targetWidth - 1, RB_ScaleRectEnd( rect.x2, sourceWidth, targetWidth ) );
	const int y2 = idMath::ClampInt( 0, targetHeight - 1, RB_ScaleRectEnd( rect.y2, sourceHeight, targetHeight ) );

	if ( x2 < x1 || y2 < y1 ) {
		rect.Clear();
		return;
	}

	rect.x1 = static_cast<short>( x1 );
	rect.y1 = static_cast<short>( y1 );
	rect.x2 = static_cast<short>( x2 );
	rect.y2 = static_cast<short>( y2 );
}

typedef struct rbScaledSurfSet_s {
	idList<const drawSurf_t *>	surfs;
	idHashIndex					hash;
} rbScaledSurfSet_t;

static bool RB_MarkDrawSurfScaled( rbScaledSurfSet_t &scaledSurfs, const drawSurf_t *surf ) {
	if ( surf == NULL ) {
		return false;
	}
	const int key = static_cast<int>( reinterpret_cast<uintptr_t>( surf ) >> 4 );
	for ( int i = scaledSurfs.hash.First( key ); i != -1; i = scaledSurfs.hash.Next( i ) ) {
		if ( scaledSurfs.surfs[i] == surf ) {
			return false;
		}
	}
	scaledSurfs.hash.Add( key, scaledSurfs.surfs.Append( surf ) );
	return true;
}

static void RB_ScaleDrawSurfScissor( rbScaledSurfSet_t &scaledSurfs, const drawSurf_t *surf, int sourceWidth, int sourceHeight, int targetWidth, int targetHeight ) {
	if ( !RB_MarkDrawSurfScaled( scaledSurfs, surf ) ) {
		return;
	}
	RB_ScaleLocalScreenRect( const_cast<drawSurf_t *>( surf )->scissorRect, sourceWidth, sourceHeight, targetWidth, targetHeight );
}

static void RB_ScaleDrawSurfChainScissors( rbScaledSurfSet_t &scaledSurfs, const drawSurf_t *surf, int sourceWidth, int sourceHeight, int targetWidth, int targetHeight ) {
	for ( const drawSurf_t *chainSurf = surf; chainSurf != NULL; chainSurf = chainSurf->nextOnLight ) {
		RB_ScaleDrawSurfScissor( scaledSurfs, chainSurf, sourceWidth, sourceHeight, targetWidth, targetHeight );
	}
}

static void RB_ScaleLightDrawSurfScissors( rbScaledSurfSet_t &scaledSurfs, const viewLight_t *vLight, int sourceWidth, int sourceHeight, int targetWidth, int targetHeight ) {
	if ( vLight == NULL ) {
		return;
	}

	RB_ScaleDrawSurfChainScissors( scaledSurfs, vLight->globalShadows, sourceWidth, sourceHeight, targetWidth, targetHeight );
	RB_ScaleDrawSurfChainScissors( scaledSurfs, vLight->localInteractions, sourceWidth, sourceHeight, targetWidth, targetHeight );
	RB_ScaleDrawSurfChainScissors( scaledSurfs, vLight->localShadows, sourceWidth, sourceHeight, targetWidth, targetHeight );
	RB_ScaleDrawSurfChainScissors( scaledSurfs, vLight->globalInteractions, sourceWidth, sourceHeight, targetWidth, targetHeight );
	RB_ScaleDrawSurfChainScissors( scaledSurfs, vLight->localShadowMapCasters, sourceWidth, sourceHeight, targetWidth, targetHeight );
	RB_ScaleDrawSurfChainScissors( scaledSurfs, vLight->globalShadowMapCasters, sourceWidth, sourceHeight, targetWidth, targetHeight );
	RB_ScaleDrawSurfChainScissors( scaledSurfs, vLight->localTranslucentShadowMapCasters, sourceWidth, sourceHeight, targetWidth, targetHeight );
	RB_ScaleDrawSurfChainScissors( scaledSurfs, vLight->globalTranslucentShadowMapCasters, sourceWidth, sourceHeight, targetWidth, targetHeight );
	RB_ScaleDrawSurfChainScissors( scaledSurfs, vLight->translucentInteractions, sourceWidth, sourceHeight, targetWidth, targetHeight );
}

static void RB_BeginSceneSupersampling( rbSceneScaleState_t &state, const viewDef_t *sceneTargetView ) {
	RB_ClearSceneScaleState( state );
	if ( backEnd.viewDef == NULL || sceneTargetView == NULL ) {
		return;
	}
	viewDef_t *viewDef = const_cast<viewDef_t *>( backEnd.viewDef );

	int targetWidth = 0;
	int targetHeight = 0;
	int effectivePercent = RB_SCREEN_FRACTION_NATIVE;
	if ( !RB_ComputeSupersampledSceneSize( sceneTargetView, targetWidth, targetHeight, &effectivePercent ) ) {
		return;
	}

	const int nativeWidth = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int nativeHeight = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	if ( nativeWidth <= 0 || nativeHeight <= 0 || targetWidth <= nativeWidth || targetHeight <= nativeHeight ) {
		return;
	}

	state.active = true;
	state.requestedPercent = RB_RequestedScreenFraction();
	state.effectivePercent = effectivePercent;
	state.nativeWidth = nativeWidth;
	state.nativeHeight = nativeHeight;
	state.scaledWidth = targetWidth;
	state.scaledHeight = targetHeight;
	state.nativeViewport = viewDef->viewport;
	state.nativeScissor = viewDef->scissor;

	RB_ScaleLocalScreenRect( viewDef->scissor, nativeWidth, nativeHeight, targetWidth, targetHeight );
	viewDef->viewport.x1 = 0;
	viewDef->viewport.y1 = 0;
	viewDef->viewport.x2 = static_cast<short>( targetWidth - 1 );
	viewDef->viewport.y2 = static_cast<short>( targetHeight - 1 );

	rbScaledSurfSet_t scaledSurfs;
	scaledSurfs.surfs.SetGranularity( 256 );
	scaledSurfs.hash.Clear( 4096, 4096 );
	for ( int i = 0; i < viewDef->numDrawSurfs; i++ ) {
		RB_ScaleDrawSurfScissor( scaledSurfs, viewDef->drawSurfs[i], nativeWidth, nativeHeight, targetWidth, targetHeight );
	}
	for ( viewLight_t *vLight = viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
		RB_ScaleLocalScreenRect( vLight->scissorRect, nativeWidth, nativeHeight, targetWidth, targetHeight );
		RB_ScaleLightDrawSurfScissors( scaledSurfs, vLight, nativeWidth, nativeHeight, targetWidth, targetHeight );
	}
	for ( viewEntity_t *vEntity = viewDef->viewEntitys; vEntity != NULL; vEntity = vEntity->next ) {
		RB_ScaleLocalScreenRect( vEntity->scissorRect, nativeWidth, nativeHeight, targetWidth, targetHeight );
	}
}

static void RB_RestoreSceneSupersampling( const rbSceneScaleState_t &state ) {
	if ( !state.active || backEnd.viewDef == NULL ) {
		return;
	}

	viewDef_t *viewDef = const_cast<viewDef_t *>( backEnd.viewDef );
	viewDef->viewport = state.nativeViewport;
	viewDef->scissor = state.nativeScissor;
}

static bool RB_PostProcessBloomRequested( void ) {
	return r_bloom.GetBool() && r_bloomIntensity.GetFloat() > 0.0001f;
}

static bool RB_IsMainMotionBlurView( const viewDef_t *viewDef ) {
	if ( !RB_IsMainScenePostProcessView( viewDef ) ) {
		return false;
	}
	if ( viewDef->isSubview || viewDef->superView != NULL ) {
		return false;
	}
	if ( viewDef->renderView.viewID < 0 ) {
		return false;
	}
	return true;
}

static bool RB_IsMainMotionBlurView( void ) {
	return RB_IsMainMotionBlurView( backEnd.viewDef );
}

static bool RB_PostProcessMotionBlurRequested( const viewDef_t *viewDef ) {
	if ( !r_motionBlur.GetBool() ) {
		return false;
	}
	if ( !RB_IsMainMotionBlurView( viewDef ) ) {
		return false;
	}
	if ( r_jitter.GetBool() ) {
		return false;
	}
	if ( r_motionBlurDebug.GetBool() ) {
		return true;
	}
	return r_motionBlurStrength.GetFloat() > 0.0f
		&& r_motionBlurMaxPixels.GetFloat() > 0.0f
		&& r_motionBlurSamples.GetInteger() > 0;
}

static bool RB_PostProcessMotionBlurRequested( void ) {
	return RB_PostProcessMotionBlurRequested( backEnd.viewDef );
}

static int RB_HDRDebugViewValue( void ) {
	return idMath::ClampInt( 0, 2, r_hdrDebugView.GetInteger() );
}

static bool RB_ModernVisibleSceneTargetRequested( void ) {
	return R_ModernGLExecutor_ModernVisibleRequestedForPost() && r_hdrSceneTarget.GetBool();
}

static bool RB_HDRAutoExposureRequested( void ) {
	return r_hdrAutoExposure.GetBool() && r_hdrToneMap.GetBool() && R_ModernGLExecutor_ModernVisibleRequestedForPost();
}

static bool RB_HDRAutoExposureEnabled( void ) {
	return r_hdrAutoExposure.GetBool() && r_hdrToneMap.GetBool() && R_ModernGLExecutor_ModernVisiblePostProcessHandoffActive();
}

static bool RB_ViewRequestsSceneRenderTarget( const viewDef_t *viewDef ) {
	if ( r_skipPostProcess.GetBool() ) {
		return false;
	}
	if ( !RB_IsMainScenePostProcessView( viewDef ) ) {
		return false;
	}

	const bool supersampledSceneRequested = RB_SupersampledSceneTargetRequested( viewDef );
	if ( !glConfig.GLSLProgramAvailable && !supersampledSceneRequested ) {
		return false;
	}

	const bool bloomRequested = RB_PostProcessBloomRequested();
	const bool motionBlurRequested = RB_PostProcessMotionBlurRequested( viewDef );
	const bool ssaoRequested = r_ssao.GetBool();
	const bool toneMapRequested = r_hdrToneMap.GetBool();
	const bool autoExposureRequested = RB_HDRAutoExposureRequested();
	const bool hdrDebugRequested = RB_HDRDebugViewValue() > 0;
	const bool modernVisibleSceneTargetRequested = RB_ModernVisibleSceneTargetRequested();

	// Bloom now always routes through the resolved scene target. The direct
	// back-buffer capture path was fragile when toggling bloom live and during
	// map handoffs, and it also clipped highlight energy before the bright-pass.
	return bloomRequested
		|| motionBlurRequested
		|| ssaoRequested
		|| toneMapRequested
		|| autoExposureRequested
		|| hdrDebugRequested
		|| modernVisibleSceneTargetRequested
		|| supersampledSceneRequested;
}

static bool RB_IsInlineSubviewOfScenePostProcessView( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->superView == NULL || viewDef->subviewSurface == NULL ) {
		return false;
	}
	if ( !viewDef->isSubview || viewDef->isXraySubview ) {
		return false;
	}
	if ( viewDef->subviewSurface->material == NULL || viewDef->subviewSurface->material->GetSort() != SS_SUBVIEW ) {
		return false;
	}

	const viewDef_t *superView = viewDef->superView;
	if ( !RB_IsMainScenePostProcessView( superView ) ) {
		return false;
	}

	// Inline portal-sky subviews inherit the parent viewport and render
	// directly into it. Dynamic render-map subviews are cropped to scratch
	// images and must stay isolated from the parent scene target.
	if ( !viewDef->viewport.Equals( superView->viewport ) ) {
		return false;
	}
	if ( viewDef->renderView.width != superView->renderView.width
		|| viewDef->renderView.height != superView->renderView.height ) {
		return false;
	}

	return true;
}

static bool RB_IsPortalSkyView( const viewDef_t *viewDef ) {
	return viewDef != NULL && ( viewDef->renderFlags & RF_PORTAL_SKY ) != 0;
}

static bool RB_MaterialIsPortalSkyForSSAO( const idMaterial *material ) {
	return material != NULL && ( material->IsPortalSky() || material->GetSort() == SS_PORTAL_SKY );
}

static bool RB_ViewHasPortalSkyMaskSurfaces( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->drawSurfs == NULL ) {
		return false;
	}
	for ( int i = 0; i < viewDef->numDrawSurfs; i++ ) {
		const drawSurf_t *surf = viewDef->drawSurfs[i];
		if ( surf != NULL && RB_MaterialIsPortalSkyForSSAO( surf->material ) ) {
			return true;
		}
	}
	return false;
}

static bool RB_ViewHasSkyBackdropSurfaces( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->drawSurfs == NULL ) {
		return false;
	}
	for ( int i = 0; i < viewDef->numDrawSurfs; i++ ) {
		const drawSurf_t *surf = viewDef->drawSurfs[i];
		if ( surf == NULL || surf->material == NULL ) {
			continue;
		}
		if ( surf->material->HasTexgen( TG_SKYBOX_CUBE ) || surf->material->HasTexgen( TG_WOBBLESKY_CUBE ) ) {
			return true;
		}
	}
	return false;
}

static const viewDef_t *RB_PortalSkySceneTargetView( const viewDef_t *viewDef ) {
	if ( !RB_IsPortalSkyView( viewDef ) || backEnd.renderTexture != NULL ) {
		return NULL;
	}

	const viewDef_t *rootView = tr.primaryView;
	if ( rootView == NULL || rootView == viewDef || RB_IsPortalSkyView( rootView ) ) {
		return NULL;
	}
	if ( !RB_ViewRequestsSceneRenderTarget( rootView ) ) {
		return NULL;
	}

	// Game code emits RF_PORTAL_SKY immediately before the root view it feeds.
	// Only inherit the scene target when both views address the same framebuffer
	// footprint; render-to-texture captures and odd side views should stay direct.
	if ( !viewDef->viewport.Equals( rootView->viewport ) ) {
		return NULL;
	}
	if ( viewDef->renderView.width != rootView->renderView.width
		|| viewDef->renderView.height != rootView->renderView.height ) {
		return NULL;
	}

	return rootView;
}

static void RB_MarkSceneRenderTargetPreserveFarDepth( const viewDef_t *rootView ) {
	if ( rootView == NULL ) {
		return;
	}
	rbSceneRenderTargetPreserveFarDepthFrame = backEnd.frameCount;
	rbSceneRenderTargetPreserveFarDepthView = rootView;
}

static void RB_MarkPortalSkyBackdropForSceneTarget( const viewDef_t *viewDef ) {
	if ( !RB_IsPortalSkyView( viewDef ) || backEnd.renderTexture != NULL ) {
		return;
	}
	if ( !RB_ViewHasSkyBackdropSurfaces( viewDef ) ) {
		return;
	}
	rbSceneRenderTargetPortalSkyFrame = backEnd.frameCount;
	rbSceneRenderTargetPortalSkyViewport = viewDef->viewport;
	rbSceneRenderTargetPortalSkyWidth = viewDef->renderView.width;
	rbSceneRenderTargetPortalSkyHeight = viewDef->renderView.height;
}

static bool RB_HasPortalSkyBackdropForSceneTarget( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || rbSceneRenderTargetPortalSkyFrame != backEnd.frameCount ) {
		return false;
	}
	return viewDef->viewport.Equals( rbSceneRenderTargetPortalSkyViewport )
		&& viewDef->renderView.width == rbSceneRenderTargetPortalSkyWidth
		&& viewDef->renderView.height == rbSceneRenderTargetPortalSkyHeight;
}

static bool RB_ShouldPreserveSceneRenderTargetFarDepth( const viewDef_t *viewDef ) {
	if ( viewDef == NULL ) {
		return false;
	}
	if ( rbSceneRenderTargetPreserveFarDepthFrame == backEnd.frameCount
		&& rbSceneRenderTargetPreserveFarDepthView == viewDef ) {
		return true;
	}
	return RB_ViewRequestsSceneRenderTarget( viewDef )
		&& ( RB_ViewHasPortalSkyMaskSurfaces( viewDef ) || RB_HasPortalSkyBackdropForSceneTarget( viewDef ) );
}

static bool RB_IsSceneRenderTexture( const idRenderTexture *renderTexture ) {
	return renderTexture != NULL && renderTexture == rbSceneRenderTexture;
}

static bool RB_IsFeedbackSceneRenderTexture( const idRenderTexture *renderTexture ) {
	return renderTexture != NULL && renderTexture == backEnd.feedbackRenderTexture;
}

static bool RB_AutomaticCurrentRenderCaptureAllowed( void ) {
	return backEnd.renderTexture == NULL
		|| RB_IsSceneRenderTexture( backEnd.renderTexture )
		|| RB_IsFeedbackSceneRenderTexture( backEnd.renderTexture );
}

static void RB_SetFramebufferSRGBEnabled( bool enabled ) {
	if ( !glConfig.framebufferSRGBAvailable ) {
		return;
	}

	const bool strictLinearOutputEnabled = false;

	// Keep stock SDR presentation unless/until the full renderer adopts a
	// verified scene-linear workflow. Archived cvar values should not force the
	// experimental path on.
	if ( enabled && strictLinearOutputEnabled && r_hdrSRGB.GetBool() ) {
		glEnable( GL_FRAMEBUFFER_SRGB );
	} else {
		glDisable( GL_FRAMEBUFFER_SRGB );
	}
}

static void RB_CaptureCurrentRenderImage( int viewportWidth, int viewportHeight ) {
	idImage *sceneImage = globalImages->currentRenderImage;
	if ( sceneImage == NULL || viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	if ( backEnd.renderTexture != NULL && backEnd.renderTexture->GetNumColorImages() > 0 ) {
		idImage *colorImage = backEnd.renderTexture->GetColorImage( 0 );
		if ( colorImage == sceneImage ) {
			backEnd.currentRenderCopied = true;
			return;
		}
	}

	sceneImage->CopyFramebuffer(
		backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );
	backEnd.currentRenderCopied = true;
}

static void RB_CaptureCurrentDepthImage( int viewportWidth, int viewportHeight ) {
	idImage *depthImage = globalImages->currentDepthImage;
	if ( depthImage == NULL || viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	if ( backEnd.renderTexture != NULL ) {
		idImage *renderDepthImage = backEnd.renderTexture->GetDepthImage();
		if ( renderDepthImage == depthImage ) {
			backEnd.currentDepthCopied = true;
			return;
		}
	}

	depthImage->CopyDepthbuffer(
		backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );
	backEnd.currentDepthCopied = true;
}

static idImage *RB_EnsureSceneRenderTargetPreserveDepthImage( int viewportWidth, int viewportHeight ) {
	if ( globalImages == NULL || viewportWidth <= 0 || viewportHeight <= 0 ) {
		return NULL;
	}

	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_DEPTH;
	opts.width = viewportWidth;
	opts.height = viewportHeight;
	opts.numLevels = 1;
	opts.numMSAASamples = 0;
	opts.isPersistant = true;

	rbSceneRenderTargetPreserveDepthImage = globalImages->ScratchImage( "_scenePreserveDepth", &opts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
	return rbSceneRenderTargetPreserveDepthImage;
}

static void RB_CaptureSceneRenderTargetPreserveDepthImage( void ) {
	rbSceneRenderTargetPreserveDepthFrame = -1;
	rbSceneRenderTargetPreserveDepthWidth = 0;
	rbSceneRenderTargetPreserveDepthHeight = 0;

	if ( !RB_IsSceneRenderTexture( backEnd.renderTexture ) || backEnd.viewDef == NULL ) {
		return;
	}
	if ( !RB_ShouldPreserveSceneRenderTargetFarDepth( backEnd.viewDef ) ) {
		return;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	idImage *depthImage = RB_EnsureSceneRenderTargetPreserveDepthImage( viewportWidth, viewportHeight );
	if ( depthImage == NULL ) {
		return;
	}

	depthImage->CopyDepthbuffer(
		backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );
	rbSceneRenderTargetPreserveDepthFrame = backEnd.frameCount;
	rbSceneRenderTargetPreserveDepthWidth = viewportWidth;
	rbSceneRenderTargetPreserveDepthHeight = viewportHeight;
}

static bool RB_EnsureSceneRenderTexture( const viewDef_t *sceneTargetView ) {
	if ( sceneTargetView == NULL ) {
		return false;
	}

	int supersampledWidth = 0;
	int supersampledHeight = 0;
	const bool supersampledScene = RB_ComputeSupersampledSceneSize( sceneTargetView, supersampledWidth, supersampledHeight );
	const int targetWidth = supersampledScene ? supersampledWidth : Max( glConfig.vidWidth, sceneTargetView->viewport.x2 + 1 );
	const int targetHeight = supersampledScene ? supersampledHeight : Max( glConfig.vidHeight, sceneTargetView->viewport.y2 + 1 );
	const int requestedSamples = Max( 0, r_multiSamples.GetInteger() );
	// Supersampling is already an antialiasing resolve. Keep the oversized
	// scene target single-sample instead of stacking an MSAA FP16 FBO on top of it.
	const int sceneSamples = ( !supersampledScene && requestedSamples > 1 && !R_ModernGLExecutor_ModernVisibleRequestedForPost() ) ? requestedSamples : 0;

	if ( targetWidth <= 0 || targetHeight <= 0 ) {
		return false;
	}

	idImageOpts colorOpts;
	colorOpts.textureType = TT_2D;
	colorOpts.format = FMT_RGBA16F;
	colorOpts.width = targetWidth;
	colorOpts.height = targetHeight;
	colorOpts.numLevels = 1;
	colorOpts.numMSAASamples = sceneSamples;
	colorOpts.isPersistant = true;
	rbSceneColorImage = globalImages->ScratchImage( "_hdrSceneColor", &colorOpts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );

	idImageOpts depthOpts;
	depthOpts.textureType = TT_2D;
	depthOpts.format = FMT_DEPTH_STENCIL;
	depthOpts.width = targetWidth;
	depthOpts.height = targetHeight;
	depthOpts.numLevels = 1;
	depthOpts.numMSAASamples = sceneSamples;
	depthOpts.isPersistant = true;
	rbSceneDepthStencilImage = globalImages->ScratchImage( "_hdrSceneDepthStencil", &depthOpts, TF_NEAREST, TR_CLAMP, TD_DEPTH );

	if ( rbSceneColorImage == NULL || rbSceneDepthStencilImage == NULL ) {
		return false;
	}

	const bool recreateRenderTexture =
		( rbSceneRenderTexture == NULL ) ||
		( rbSceneRenderTexture->GetWidth() != targetWidth ) ||
		( rbSceneRenderTexture->GetHeight() != targetHeight ) ||
		( rbSceneRenderTextureSamples != sceneSamples );

	if ( recreateRenderTexture ) {
		if ( rbSceneRenderTexture != NULL ) {
			tr.DestroyRenderTexture( rbSceneRenderTexture );
			rbSceneRenderTexture = NULL;
		}
		rbSceneRenderTexture = tr.CreateRenderTexture( rbSceneColorImage, rbSceneDepthStencilImage );
		rbSceneRenderTextureSamples = sceneSamples;
	}

	return rbSceneRenderTexture != NULL;
}

static bool RB_SceneRenderTargetRequested( void ) {
	if ( backEnd.renderTexture != NULL ) {
		return false;
	}
	return RB_ViewRequestsSceneRenderTarget( backEnd.viewDef );
}

static bool RB_InlineSubviewSceneRenderTargetRequested( void ) {
	if ( backEnd.renderTexture != NULL ) {
		return false;
	}
	if ( !RB_IsInlineSubviewOfScenePostProcessView( backEnd.viewDef ) ) {
		return false;
	}
	return RB_ViewRequestsSceneRenderTarget( backEnd.viewDef->superView );
}

static void RB_BeginFullscreenPostProcessPass( int scissorX, int scissorY, int scissorWidth, int scissorHeight ) {
	// Fullscreen post-process passes must never inherit stale light/material scissors.
	glEnable( GL_SCISSOR_TEST );
	glScissor( scissorX, scissorY, scissorWidth, scissorHeight );

	// Fullscreen composites must start from a known programmable-pipeline state.
	// Level changes and SP/MP transitions can leave legacy ARB programs bound or
	// higher texture units configured by material stages, which causes the
	// tonemap/bloom fullscreen quad to sample garbage or render solid black.
	glUseProgramObjectARB( 0 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, 0 );

	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0, 1, 0, 1, -1, 1 );

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_DEPTHMASK | GLS_DEPTHFUNC_ALWAYS );
	GL_Cull( CT_TWO_SIDED );

	const int maxStateUnits = Max( 0, Min( MAX_MULTITEXTURE_UNITS, Min( glConfig.maxTextureUnits, glConfig.maxTextureImageUnits ) ) );
	for ( int unit = 0; unit < maxStateUnits; unit++ ) {
		GL_SelectTexture( unit );
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_R );
		glDisable( GL_TEXTURE_GEN_Q );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
		globalImages->BindNull();
	}

	GL_SelectTexture( 0 );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
}

static void RB_DrawFullscreenPostProcessQuad( int viewportWidth, int viewportHeight, int textureWidth, int textureHeight ) {
	const float maxS = static_cast<float>( viewportWidth ) / static_cast<float>( textureWidth );
	const float maxT = static_cast<float>( viewportHeight ) / static_cast<float>( textureHeight );

	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glBegin( GL_QUADS );
	glTexCoord2f( 0.0f, 0.0f );
	glVertex2f( 0.0f, 0.0f );
	glTexCoord2f( 0.0f, maxT );
	glVertex2f( 0.0f, 1.0f );
	glTexCoord2f( maxS, maxT );
	glVertex2f( 1.0f, 1.0f );
	glTexCoord2f( maxS, 0.0f );
	glVertex2f( 1.0f, 0.0f );
	glEnd();
}

static void RB_DrawFullscreenPostProcessQuadUnitUV( void ) {
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glBegin( GL_QUADS );
	glTexCoord2f( 0.0f, 0.0f );
	glVertex2f( 0.0f, 0.0f );
	glTexCoord2f( 0.0f, 1.0f );
	glVertex2f( 0.0f, 1.0f );
	glTexCoord2f( 1.0f, 1.0f );
	glVertex2f( 1.0f, 1.0f );
	glTexCoord2f( 1.0f, 0.0f );
	glVertex2f( 1.0f, 0.0f );
	glEnd();
}

static bool RB_IsSMAAPostAAGLSLProgram( const newShaderStage_t *stage ) {
	if ( stage == NULL || !stage->glslProgram ) {
		return false;
	}

	const char *name = stage->glslProgramName;
	return idStr::Icmp( name, "smaa_edge.fs" ) == 0 ||
		idStr::Icmp( name, "glprogs/smaa_edge.fs" ) == 0 ||
		idStr::Icmp( name, "smaa_weights.fs" ) == 0 ||
		idStr::Icmp( name, "glprogs/smaa_weights.fs" ) == 0 ||
		idStr::Icmp( name, "smaa_blend.fs" ) == 0 ||
		idStr::Icmp( name, "glprogs/smaa_blend.fs" ) == 0;
}

static void RB_PoisonPostAAGLSLStateForValidation( void ) {
	if ( !r_postAAStatePoisonTest.GetBool() ) {
		return;
	}

	static bool logged = false;
	if ( !logged ) {
		common->Printf( "PostAA state-poison validation active: dirtying active texture/client state before SMAA fullscreen draws.\n" );
		logged = true;
	}

	const int maxStateUnits = Max( 0, Min( MAX_MULTITEXTURE_UNITS, Min( glConfig.maxTextureUnits, glConfig.maxTextureImageUnits ) ) );
	const int dirtyUnit = maxStateUnits > 1 ? Min( maxStateUnits - 1, 3 ) : 0;
	GL_SelectTexture( dirtyUnit );
}

static void RB_DrawSMAAExplicitFullscreenQuad( void ) {
	static const GLfloat positions[8] = {
		-1.0f,  1.0f,
		-1.0f, -1.0f,
		 1.0f,  1.0f,
		 1.0f, -1.0f
	};
	static const GLfloat texCoords[8] = {
		0.0f, 1.0f,
		0.0f, 0.0f,
		1.0f, 1.0f,
		1.0f, 0.0f
	};

	glDisableClientState( GL_VERTEX_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );
	const int maxStateUnits = Max( 0, Min( MAX_MULTITEXTURE_UNITS, Min( glConfig.maxTextureUnits, glConfig.maxTextureImageUnits ) ) );
	for ( int unit = 0; unit < maxStateUnits; unit++ ) {
		GL_SelectTexture( unit );
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	}

	GLint previousArrayBuffer = 0;
	glGetIntegerv( GL_ARRAY_BUFFER_BINDING_ARB, &previousArrayBuffer );
	idVertexCache::BindArrayBuffer( 0 );

	glVertexAttribPointerARB( 0, 2, GL_FLOAT, false, 0, positions );
	glVertexAttribPointerARB( 8, 2, GL_FLOAT, false, 0, texCoords );
	glEnableVertexAttribArrayARB( 0 );
	glEnableVertexAttribArrayARB( 8 );

	const int previousCullType = backEnd.glState.faceCulling;
	GL_Cull( CT_TWO_SIDED );

	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	backEnd.pc.c_drawElements++;
	backEnd.pc.c_drawIndexes += 4;
	backEnd.pc.c_drawVertexes += 4;

	if ( previousCullType >= CT_FRONT_SIDED && previousCullType <= CT_TWO_SIDED ) {
		GL_Cull( previousCullType );
	}

	glDisableVertexAttribArrayARB( 8 );
	glDisableVertexAttribArrayARB( 0 );

	idVertexCache::BindArrayBuffer( static_cast<GLuint>( previousArrayBuffer ) );
	GL_SelectTexture( 0 );
	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
}

static void RB_EndFullscreenPostProcessPass( void ) {
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glEnable( GL_DEPTH_TEST );
	glEnable( GL_STENCIL_TEST );
	glMatrixMode( GL_MODELVIEW );
	GL_Cull( CT_FRONT_SIDED );
}

struct rbBuiltinUniformDef_t {
	const char *name;
	int components;
};

enum rbLightGridUniformIndex_t {
	RB_LIGHTGRID_UNIFORM_BUMP_MATRIX_S = 0,
	RB_LIGHTGRID_UNIFORM_BUMP_MATRIX_T,
	RB_LIGHTGRID_UNIFORM_DIFFUSE_MATRIX_S,
	RB_LIGHTGRID_UNIFORM_DIFFUSE_MATRIX_T,
	RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW0,
	RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW1,
	RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW2,
	RB_LIGHTGRID_UNIFORM_LIGHTGRID_ORIGIN,
	RB_LIGHTGRID_UNIFORM_LIGHTGRID_SIZE,
	RB_LIGHTGRID_UNIFORM_LIGHTGRID_BOUNDS,
	RB_LIGHTGRID_UNIFORM_ATLAS_INFO,
	RB_LIGHTGRID_UNIFORM_VISIBILITY_INFO,
	RB_LIGHTGRID_UNIFORM_PROBE_INFO,
	RB_LIGHTGRID_UNIFORM_BLEND_INFO,
	RB_LIGHTGRID_UNIFORM_PORTAL_PLANE,
	RB_LIGHTGRID_UNIFORM_PORTAL_BOUNDS_MIN,
	RB_LIGHTGRID_UNIFORM_PORTAL_BOUNDS_MAX,
	RB_LIGHTGRID_UNIFORM_DEBUG_INFO,
	RB_LIGHTGRID_UNIFORM_DEPTH_INFO,
	RB_LIGHTGRID_UNIFORM_DEPTH_VIEWPORT,
	RB_LIGHTGRID_UNIFORM_COLOR_INFO,
	RB_LIGHTGRID_UNIFORM_DIFFUSE_COLOR,
	RB_LIGHTGRID_UNIFORM_VERTEX_COLOR_PARAMS,
	RB_LIGHTGRID_UNIFORM_FLAT_DIFFUSE_PARAMS,
	RB_LIGHTGRID_UNIFORM_COUNT
};

static newShaderStage_t rbLightGridIndirectStage;
static bool rbLightGridIndirectStageInitialized = false;
static bool rbLightGridDepthCompareAvailable = false;
static int rbLightGridDepthCompareWidth = 0;
static int rbLightGridDepthCompareHeight = 0;

enum rbPlayerRimlightUniformIndex_t {
	RB_PLAYER_RIMLIGHT_UNIFORM_MODEL_MATRIX_ROW0 = 0,
	RB_PLAYER_RIMLIGHT_UNIFORM_MODEL_MATRIX_ROW1,
	RB_PLAYER_RIMLIGHT_UNIFORM_MODEL_MATRIX_ROW2,
	RB_PLAYER_RIMLIGHT_UNIFORM_VIEW_ORIGIN,
	RB_PLAYER_RIMLIGHT_UNIFORM_COLOR,
	RB_PLAYER_RIMLIGHT_UNIFORM_PARAMS,
	RB_PLAYER_RIMLIGHT_UNIFORM_COUNT
};

static newShaderStage_t rbPlayerRimlightStage;
static bool rbPlayerRimlightStageInitialized = false;

enum rbPlayerOutlineUniformIndex_t {
	RB_PLAYER_OUTLINE_UNIFORM_COLOR = 0,
	RB_PLAYER_OUTLINE_UNIFORM_PARAMS,
	RB_PLAYER_OUTLINE_UNIFORM_COUNT
};

static newShaderStage_t rbPlayerOutlineStage;
static bool rbPlayerOutlineStageInitialized = false;

static void RB_InitLightGridIndirectStage( void ) {
	if ( rbLightGridIndirectStageInitialized ) {
		return;
	}

	memset( &rbLightGridIndirectStage, 0, sizeof( rbLightGridIndirectStage ) );
	rbLightGridIndirectStage.glslProgram = true;
	idStr::Copynz( rbLightGridIndirectStage.glslProgramName, "lightgrid_indirect.fs", sizeof( rbLightGridIndirectStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_LIGHTGRID_UNIFORM_COUNT] = {
		{ "uBumpMatrixS", 4 },
		{ "uBumpMatrixT", 4 },
		{ "uDiffuseMatrixS", 4 },
		{ "uDiffuseMatrixT", 4 },
		{ "uModelMatrixRow0", 4 },
		{ "uModelMatrixRow1", 4 },
		{ "uModelMatrixRow2", 4 },
		{ "uLightGridOrigin", 4 },
		{ "uLightGridSize", 4 },
		{ "uLightGridBounds", 4 },
		{ "uAtlasInfo", 4 },
		{ "uVisibilityInfo", 4 },
		{ "uProbeInfo", 4 },
		{ "uBlendInfo", 4 },
		{ "uPortalPlane", 4 },
		{ "uPortalBoundsMin", 4 },
		{ "uPortalBoundsMax", 4 },
		{ "uDebugInfo", 4 },
		{ "uDepthInfo", 4 },
		{ "uDepthViewport", 4 },
		{ "uColorInfo", 4 },
		{ "uDiffuseColor", 4 },
		{ "uVertexColorParams", 2 },
		{ "uFlatDiffuseParams", 4 }
	};

	rbLightGridIndirectStage.numShaderParms = RB_LIGHTGRID_UNIFORM_COUNT;
	for ( int i = 0; i < RB_LIGHTGRID_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbLightGridIndirectStage.shaderParmNames[i], uniforms[i].name, sizeof( rbLightGridIndirectStage.shaderParmNames[i] ) );
		rbLightGridIndirectStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbLightGridIndirectStage.numShaderTextures = 6;
	idStr::Copynz( rbLightGridIndirectStage.shaderTextureNames[0], "uBumpMap", sizeof( rbLightGridIndirectStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbLightGridIndirectStage.shaderTextureNames[1], "uDiffuseMap", sizeof( rbLightGridIndirectStage.shaderTextureNames[1] ) );
	idStr::Copynz( rbLightGridIndirectStage.shaderTextureNames[2], "uLightGridAtlas", sizeof( rbLightGridIndirectStage.shaderTextureNames[2] ) );
	idStr::Copynz( rbLightGridIndirectStage.shaderTextureNames[3], "uLightGridVisibilityAtlas", sizeof( rbLightGridIndirectStage.shaderTextureNames[3] ) );
	idStr::Copynz( rbLightGridIndirectStage.shaderTextureNames[4], "uLightGridProbeAtlas", sizeof( rbLightGridIndirectStage.shaderTextureNames[4] ) );
	idStr::Copynz( rbLightGridIndirectStage.shaderTextureNames[5], "uSceneDepth", sizeof( rbLightGridIndirectStage.shaderTextureNames[5] ) );

	rbLightGridIndirectStageInitialized = true;
}

static void RB_InitPlayerRimlightStage( void ) {
	if ( rbPlayerRimlightStageInitialized ) {
		return;
	}

	memset( &rbPlayerRimlightStage, 0, sizeof( rbPlayerRimlightStage ) );
	rbPlayerRimlightStage.glslProgram = true;
	idStr::Copynz( rbPlayerRimlightStage.glslProgramName, "player_rimlight.fs", sizeof( rbPlayerRimlightStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_PLAYER_RIMLIGHT_UNIFORM_COUNT] = {
		{ "uModelMatrixRow0", 4 },
		{ "uModelMatrixRow1", 4 },
		{ "uModelMatrixRow2", 4 },
		{ "uViewOrigin", 4 },
		{ "uColor", 4 },
		{ "uRimParams", 4 }
	};

	rbPlayerRimlightStage.numShaderParms = RB_PLAYER_RIMLIGHT_UNIFORM_COUNT;
	for ( int i = 0; i < RB_PLAYER_RIMLIGHT_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbPlayerRimlightStage.shaderParmNames[i], uniforms[i].name, sizeof( rbPlayerRimlightStage.shaderParmNames[i] ) );
		rbPlayerRimlightStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbPlayerRimlightStageInitialized = true;
}

static void RB_InitPlayerOutlineStage( void ) {
	if ( rbPlayerOutlineStageInitialized ) {
		return;
	}

	memset( &rbPlayerOutlineStage, 0, sizeof( rbPlayerOutlineStage ) );
	rbPlayerOutlineStage.glslProgram = true;
	idStr::Copynz( rbPlayerOutlineStage.glslProgramName, "player_outline.fs", sizeof( rbPlayerOutlineStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_PLAYER_OUTLINE_UNIFORM_COUNT] = {
		{ "uColor", 4 },
		{ "uOutlineParams", 4 }
	};

	rbPlayerOutlineStage.numShaderParms = RB_PLAYER_OUTLINE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_PLAYER_OUTLINE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbPlayerOutlineStage.shaderParmNames[i], uniforms[i].name, sizeof( rbPlayerOutlineStage.shaderParmNames[i] ) );
		rbPlayerOutlineStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbPlayerOutlineStageInitialized = true;
}

enum rbSSAOUniformIndex_t {
	RB_SSAO_UNIFORM_INV_TEX_SIZE = 0,
	RB_SSAO_UNIFORM_PROJECTION_INFO,
	RB_SSAO_UNIFORM_DEPTH_PROJECTION,
	RB_SSAO_UNIFORM_PROJECTION_SCALE,
	RB_SSAO_UNIFORM_RADIUS,
	RB_SSAO_UNIFORM_BIAS,
	RB_SSAO_UNIFORM_INTENSITY,
	RB_SSAO_UNIFORM_POWER,
	RB_SSAO_UNIFORM_MAX_DISTANCE,
	RB_SSAO_UNIFORM_SAMPLE_COUNT,
	RB_SSAO_UNIFORM_DEBUG_VIEW,
	RB_SSAO_UNIFORM_COUNT
};

static newShaderStage_t rbSSAOStage;
static bool rbSSAOStageInitialized = false;
static idImage *rbSSAOWorldDepthImage = NULL;
static idImage *rbSSAOFinalDepthImage = NULL;
static int rbSSAOWorldDepthFrame = -1;
static int rbSSAOWorldDepthWidth = 0;
static int rbSSAOWorldDepthHeight = 0;

static bool RB_SSAORequestedForCurrentView( void ) {
	if ( r_skipPostProcess.GetBool() || !r_ssao.GetBool() ) {
		return false;
	}
	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}
	if ( !RB_IsMainScenePostProcessView() ) {
		return false;
	}
	return r_ssaoRadius.GetFloat() > 0.0f && r_ssaoIntensity.GetFloat() > 0.0f;
}

static idImage *RB_EnsureSSAODepthScratchImage( idImage *&image, const char *name, int viewportWidth, int viewportHeight ) {
	if ( globalImages == NULL || viewportWidth <= 0 || viewportHeight <= 0 ) {
		return NULL;
	}

	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_DEPTH;
	opts.width = viewportWidth;
	opts.height = viewportHeight;
	opts.numLevels = 1;
	opts.numMSAASamples = 0;
	opts.isPersistant = true;

	image = globalImages->ScratchImage( name, &opts, TF_NEAREST, TR_CLAMP, TD_DEPTH );
	return image;
}

static void RB_InitSSAOStage( void ) {
	if ( rbSSAOStageInitialized ) {
		return;
	}

	memset( &rbSSAOStage, 0, sizeof( rbSSAOStage ) );
	rbSSAOStage.glslProgram = true;
	idStr::Copynz( rbSSAOStage.glslProgramName, "ssao.fs", sizeof( rbSSAOStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_SSAO_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "projectionInfo", 4 },
		{ "depthProjection", 2 },
		{ "projectionScale", 1 },
		{ "ssaoRadius", 1 },
		{ "ssaoBias", 1 },
		{ "ssaoIntensity", 1 },
		{ "ssaoPower", 1 },
		{ "ssaoMaxDistance", 1 },
		{ "ssaoSampleCount", 1 },
		{ "ssaoDebugView", 1 }
	};

	rbSSAOStage.numShaderParms = RB_SSAO_UNIFORM_COUNT;
	for ( int i = 0; i < RB_SSAO_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbSSAOStage.shaderParmNames[i], uniforms[i].name, sizeof( rbSSAOStage.shaderParmNames[i] ) );
		rbSSAOStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbSSAOStage.numShaderTextures = 3;
	idStr::Copynz( rbSSAOStage.shaderTextureNames[0], "Scene", sizeof( rbSSAOStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbSSAOStage.shaderTextureNames[1], "DepthBuffer", sizeof( rbSSAOStage.shaderTextureNames[1] ) );
	idStr::Copynz( rbSSAOStage.shaderTextureNames[2], "FinalDepthBuffer", sizeof( rbSSAOStage.shaderTextureNames[2] ) );

	rbSSAOStageInitialized = true;
}

static void RB_STD_SSAO( void ) {
	if ( !RB_SSAORequestedForCurrentView() ) {
		return;
	}

	RB_InitSSAOStage();
	if ( !R_ValidateGLSLProgram( &rbSSAOStage ) ) {
		return;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	idImage *fallbackDepthImage = globalImages->currentDepthImage;
	if ( sceneImage == NULL || fallbackDepthImage == NULL ) {
		return;
	}

	const GLfloat projX = backEnd.viewDef->projectionMatrix[0];
	const GLfloat projY = backEnd.viewDef->projectionMatrix[5];
	if ( idMath::Fabs( projX ) <= 0.00001f || idMath::Fabs( projY ) <= 0.00001f ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_SSAO ----------\n" );

	sceneImage->CopyFramebuffer(
		backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );

	idImage *depthImage = NULL;
	if ( rbSSAOWorldDepthFrame == backEnd.frameCount
		&& rbSSAOWorldDepthWidth == viewportWidth
		&& rbSSAOWorldDepthHeight == viewportHeight
		&& rbSSAOWorldDepthImage != NULL ) {
		depthImage = rbSSAOWorldDepthImage;
	} else {
		if ( !backEnd.currentDepthCopied ) {
			RB_CaptureCurrentDepthImage( viewportWidth, viewportHeight );
		}
		depthImage = fallbackDepthImage;
	}
	if ( depthImage == NULL ) {
		return;
	}

	idImage *finalDepthImage = RB_EnsureSSAODepthScratchImage( rbSSAOFinalDepthImage, "_ssaoFinalDepth", viewportWidth, viewportHeight );
	if ( finalDepthImage != NULL ) {
		finalDepthImage->CopyDepthbuffer(
			backEnd.viewDef->viewport.x1,
			backEnd.viewDef->viewport.y1,
			viewportWidth,
			viewportHeight );
	} else {
		finalDepthImage = depthImage;
	}

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	const int depthTextureWidth = depthImage->GetOpts().width;
	const int depthTextureHeight = depthImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 || depthTextureWidth <= 0 || depthTextureHeight <= 0 ) {
		return;
	}

	backEnd.currentScissor = backEnd.viewDef->scissor;

	RB_BeginFullscreenPostProcessPass(
		backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );

	GL_SelectTexture( 0 );
	sceneImage->Bind();
	GL_SelectTexture( 1 );
	depthImage->Bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
	glTexParameteri( GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE );
	GL_SelectTexture( 2 );
	finalDepthImage->Bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
	glTexParameteri( GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE );
	GL_SelectTexture( 0 );

	glUseProgramObjectARB( (GLhandleARB)rbSSAOStage.glslProgramObject );

	const int sceneLocation = rbSSAOStage.shaderTextureLocations[0];
	if ( sceneLocation >= 0 ) {
		glUniform1iARB( sceneLocation, 0 );
	}

	const int depthLocation = rbSSAOStage.shaderTextureLocations[1];
	if ( depthLocation >= 0 ) {
		glUniform1iARB( depthLocation, 1 );
	}

	const int finalDepthLocation = rbSSAOStage.shaderTextureLocations[2];
	if ( finalDepthLocation >= 0 ) {
		glUniform1iARB( finalDepthLocation, 2 );
	}

	const GLfloat radius = r_ssaoRadius.GetFloat();
	const GLfloat intensity = r_ssaoIntensity.GetFloat();
	const GLfloat invTexSize[2] = {
		1.0f / static_cast<GLfloat>( depthTextureWidth ),
		1.0f / static_cast<GLfloat>( depthTextureHeight )
	};
	const GLfloat projectionInfo[4] = {
		1.0f / projX,
		1.0f / projY,
		backEnd.viewDef->projectionMatrix[8],
		backEnd.viewDef->projectionMatrix[9]
	};
	const GLfloat depthProjection[2] = {
		backEnd.viewDef->projectionMatrix[10],
		backEnd.viewDef->projectionMatrix[14]
	};
	const GLfloat projectionScale = 0.5f * static_cast<GLfloat>( depthTextureHeight ) * idMath::Fabs( projY );
	const GLfloat bias = r_ssaoBias.GetFloat();
	const GLfloat power = r_ssaoPower.GetFloat();
	const GLfloat maxDistance = r_ssaoMaxDistance.GetFloat();
	const GLfloat sampleCount = static_cast<GLfloat>( idMath::ClampInt( 4, 32, r_ssaoSamples.GetInteger() ) );
	const GLfloat debugView = r_ssaoDebug.GetBool() ? 1.0f : 0.0f;

	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_INV_TEX_SIZE] >= 0 ) {
		glUniform2fvARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_INV_TEX_SIZE], 1, invTexSize );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_PROJECTION_INFO] >= 0 ) {
		glUniform4fvARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_PROJECTION_INFO], 1, projectionInfo );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_DEPTH_PROJECTION] >= 0 ) {
		glUniform2fvARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_DEPTH_PROJECTION], 1, depthProjection );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_PROJECTION_SCALE] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_PROJECTION_SCALE], projectionScale );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_RADIUS] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_RADIUS], radius );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_BIAS] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_BIAS], bias );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_INTENSITY] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_INTENSITY], intensity );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_POWER] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_POWER], power );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_MAX_DISTANCE] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_MAX_DISTANCE], maxDistance );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_SAMPLE_COUNT] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_SAMPLE_COUNT], sampleCount );
	}
	if ( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_DEBUG_VIEW] >= 0 ) {
		glUniform1fARB( rbSSAOStage.shaderParmLocations[RB_SSAO_UNIFORM_DEBUG_VIEW], debugView );
	}

	RB_DrawFullscreenPostProcessQuad( viewportWidth, viewportHeight, textureWidth, textureHeight );

	glUseProgramObjectARB( 0 );
	GL_SelectTexture( 2 );
	globalImages->BindNull();
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();
}

/*
=============================================================================================

WORLD CEL OUTLINE

Model entities get their outline from a geometry shell, but BSP geometry has no
shell to expand, so the world is inked in screen space instead: a depth and
normal discontinuity pass over a world-only depth snapshot. Working from a
snapshot rather than the final depth buffer is what keeps the ink off model
entities, which already carry their own outline.

That snapshot predates every model in the frame, though, so it cannot say which
of its edges the player can still see. The finished depth buffer is captured
alongside it and handed over as well, and the shader inks an edge only where the
world is the surface the frame settled on. Without it the pass paints world
edges straight over the first-person weapon.

The depth reconstruction below is deliberately identical to the SSAO pass so
the two agree on what "one world unit away" means.

=============================================================================================
*/

enum rbCelOutlineUniformIndex_t {
	RB_CEL_OUTLINE_UNIFORM_INV_TEX_SIZE = 0,
	RB_CEL_OUTLINE_UNIFORM_PROJECTION_INFO,
	RB_CEL_OUTLINE_UNIFORM_DEPTH_PROJECTION,
	RB_CEL_OUTLINE_UNIFORM_EDGE_PARAMS,
	RB_CEL_OUTLINE_UNIFORM_COLOR,
	RB_CEL_OUTLINE_UNIFORM_COUNT
};

static newShaderStage_t rbCelOutlineStage;
static bool rbCelOutlineStageInitialized = false;
static idImage *rbCelWorldDepthImage = NULL;
static idImage *rbCelSceneDepthImage = NULL;
static int rbCelWorldDepthFrame = -1;
static int rbCelWorldDepthWidth = 0;
static int rbCelWorldDepthHeight = 0;

/*
==================
RB_ReportCelWorldOutlineSkip

The world pass has several ways to decline, and every one of them looks the same
from the outside: no ink. Under r_celShadingWorldDebug each distinct reason is
named once, so a missing outline reports its own cause instead of needing a
debugger. Reasons are string literals, so identity comparison is enough to keep
a steady state from repeating every frame.
==================
*/
static void RB_ReportCelWorldOutlineSkip( const char *reason ) {
	static const char *lastReason = NULL;

	if ( !r_celShadingWorldDebug.GetBool() ) {
		// Forget the last reason while the diagnostic is off. Otherwise a player
		// who turns it on to ask why there is no outline is answered with
		// silence, because that same reason was already printed the last time
		// they had it on.
		lastReason = NULL;
		return;
	}
	if ( reason == lastReason ) {
		return;
	}

	lastReason = reason;
	if ( reason != NULL ) {
		common->Printf( "cel world outline skipped: %s\n", reason );
	}
}

static bool RB_CelWorldOutlineRequestedForCurrentView( void ) {
	if ( r_skipPostProcess.GetBool() ) {
		RB_ReportCelWorldOutlineSkip( "r_skipPostProcess is set" );
		return false;
	}
	if ( !R_CelWorldOutlineEnabled() ) {
		RB_ReportCelWorldOutlineSkip( "r_celShadingWorld is off, or r_celShadingWorldAlpha / the alpha in r_celOutlineColor is zero" );
		return false;
	}
	if ( !glConfig.GLSLProgramAvailable ) {
		RB_ReportCelWorldOutlineSkip( "no GLSL support; the screen-space pass has no fallback" );
		return false;
	}
	if ( !RB_IsMainScenePostProcessView() ) {
		RB_ReportCelWorldOutlineSkip( "not the main scene view (subview, portal sky, or 2D pass)" );
		return false;
	}

	RB_ReportCelWorldOutlineSkip( NULL );
	return true;
}

static void RB_InitCelOutlineStage( void ) {
	if ( rbCelOutlineStageInitialized ) {
		return;
	}

	memset( &rbCelOutlineStage, 0, sizeof( rbCelOutlineStage ) );
	rbCelOutlineStage.glslProgram = true;
	idStr::Copynz( rbCelOutlineStage.glslProgramName, "celoutline.fs", sizeof( rbCelOutlineStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_CEL_OUTLINE_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "projectionInfo", 4 },
		{ "depthProjection", 2 },
		{ "celEdgeParams", 4 },
		{ "celOutlineColor", 4 }
	};

	rbCelOutlineStage.numShaderParms = RB_CEL_OUTLINE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_CEL_OUTLINE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbCelOutlineStage.shaderParmNames[i], uniforms[i].name, sizeof( rbCelOutlineStage.shaderParmNames[i] ) );
		rbCelOutlineStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbCelOutlineStage.numShaderTextures = 3;
	idStr::Copynz( rbCelOutlineStage.shaderTextureNames[0], "Scene", sizeof( rbCelOutlineStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbCelOutlineStage.shaderTextureNames[1], "WorldDepthBuffer", sizeof( rbCelOutlineStage.shaderTextureNames[1] ) );
	idStr::Copynz( rbCelOutlineStage.shaderTextureNames[2], "SceneDepthBuffer", sizeof( rbCelOutlineStage.shaderTextureNames[2] ) );

	rbCelOutlineStageInitialized = true;
}

static void RB_STD_CelWorldOutline( void ) {
	if ( !RB_CelWorldOutlineRequestedForCurrentView() ) {
		return;
	}
	if ( rbCelWorldDepthFrame != backEnd.frameCount || rbCelWorldDepthImage == NULL ) {
		// No world was drawn this view, so there is nothing to ink.
		return;
	}

	RB_InitCelOutlineStage();
	if ( !R_ValidateGLSLProgram( &rbCelOutlineStage ) ) {
		return;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0
		|| rbCelWorldDepthWidth != viewportWidth || rbCelWorldDepthHeight != viewportHeight ) {
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	if ( sceneImage == NULL ) {
		return;
	}

	const GLfloat projX = backEnd.viewDef->projectionMatrix[0];
	const GLfloat projY = backEnd.viewDef->projectionMatrix[5];
	if ( idMath::Fabs( projX ) <= 0.00001f || idMath::Fabs( projY ) <= 0.00001f ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_CelWorldOutline ----------\n" );

	sceneImage->CopyFramebuffer(
		backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );

	// The depth buffer still holds the finished opaque scene at this point: the
	// passes since the prepass have all been full-screen and depth-preserving.
	// That is what tells the shader which world edges are still visible.
	idImage *sceneDepthImage = RB_EnsureSSAODepthScratchImage( rbCelSceneDepthImage, "_celSceneDepth", viewportWidth, viewportHeight );
	if ( sceneDepthImage != NULL ) {
		sceneDepthImage->CopyDepthbuffer(
			backEnd.viewDef->viewport.x1,
			backEnd.viewDef->viewport.y1,
			viewportWidth,
			viewportHeight );
	} else {
		// Handing the world snapshot over for both scores every pixel as world
		// visible, which degrades to the unmasked look rather than to no ink.
		sceneDepthImage = rbCelWorldDepthImage;
	}

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	const int depthTextureWidth = rbCelWorldDepthImage->GetOpts().width;
	const int depthTextureHeight = rbCelWorldDepthImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 || depthTextureWidth <= 0 || depthTextureHeight <= 0 ) {
		return;
	}

	backEnd.currentScissor = backEnd.viewDef->scissor;

	RB_BeginFullscreenPostProcessPass(
		backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );

	GL_SelectTexture( 0 );
	sceneImage->Bind();
	GL_SelectTexture( 1 );
	rbCelWorldDepthImage->Bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
	glTexParameteri( GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE );
	GL_SelectTexture( 2 );
	sceneDepthImage->Bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
	glTexParameteri( GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE );
	GL_SelectTexture( 0 );

	glUseProgramObjectARB( (GLhandleARB)rbCelOutlineStage.glslProgramObject );

	if ( rbCelOutlineStage.shaderTextureLocations[0] >= 0 ) {
		glUniform1iARB( rbCelOutlineStage.shaderTextureLocations[0], 0 );
	}
	if ( rbCelOutlineStage.shaderTextureLocations[1] >= 0 ) {
		glUniform1iARB( rbCelOutlineStage.shaderTextureLocations[1], 1 );
	}
	if ( rbCelOutlineStage.shaderTextureLocations[2] >= 0 ) {
		glUniform1iARB( rbCelOutlineStage.shaderTextureLocations[2], 2 );
	}

	const GLfloat invTexSize[2] = {
		1.0f / static_cast<GLfloat>( depthTextureWidth ),
		1.0f / static_cast<GLfloat>( depthTextureHeight )
	};
	const GLfloat projectionInfo[4] = {
		1.0f / projX,
		1.0f / projY,
		backEnd.viewDef->projectionMatrix[8],
		backEnd.viewDef->projectionMatrix[9]
	};
	const GLfloat depthProjection[2] = {
		backEnd.viewDef->projectionMatrix[10],
		backEnd.viewDef->projectionMatrix[14]
	};
	const GLfloat edgeParams[4] = {
		R_CelWorldOutlineWidth(),
		R_CelWorldOutlineDepthThreshold(),
		R_CelWorldOutlineNormalThreshold(),
		r_celShadingWorldDebug.GetBool() ? 1.0f : 0.0f
	};

	idVec4 outlineColor;
	R_CelWorldOutlineColor( outlineColor );

	if ( rbCelOutlineStage.shaderParmLocations[RB_CEL_OUTLINE_UNIFORM_INV_TEX_SIZE] >= 0 ) {
		glUniform2fvARB( rbCelOutlineStage.shaderParmLocations[RB_CEL_OUTLINE_UNIFORM_INV_TEX_SIZE], 1, invTexSize );
	}
	if ( rbCelOutlineStage.shaderParmLocations[RB_CEL_OUTLINE_UNIFORM_PROJECTION_INFO] >= 0 ) {
		glUniform4fvARB( rbCelOutlineStage.shaderParmLocations[RB_CEL_OUTLINE_UNIFORM_PROJECTION_INFO], 1, projectionInfo );
	}
	if ( rbCelOutlineStage.shaderParmLocations[RB_CEL_OUTLINE_UNIFORM_DEPTH_PROJECTION] >= 0 ) {
		glUniform2fvARB( rbCelOutlineStage.shaderParmLocations[RB_CEL_OUTLINE_UNIFORM_DEPTH_PROJECTION], 1, depthProjection );
	}
	if ( rbCelOutlineStage.shaderParmLocations[RB_CEL_OUTLINE_UNIFORM_EDGE_PARAMS] >= 0 ) {
		glUniform4fvARB( rbCelOutlineStage.shaderParmLocations[RB_CEL_OUTLINE_UNIFORM_EDGE_PARAMS], 1, edgeParams );
	}
	if ( rbCelOutlineStage.shaderParmLocations[RB_CEL_OUTLINE_UNIFORM_COLOR] >= 0 ) {
		glUniform4fvARB( rbCelOutlineStage.shaderParmLocations[RB_CEL_OUTLINE_UNIFORM_COLOR], 1, outlineColor.ToFloatPtr() );
	}

	RB_DrawFullscreenPostProcessQuad( viewportWidth, viewportHeight, textureWidth, textureHeight );

	glUseProgramObjectARB( 0 );
	GL_SelectTexture( 2 );
	globalImages->BindNull();
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();

	backEnd.currentRenderCopied = false;
}

enum rbMotionBlurUniformIndex_t {
	RB_MOTION_BLUR_UNIFORM_INV_TEX_SIZE = 0,
	RB_MOTION_BLUR_UNIFORM_VIEWPORT_SIZE,
	RB_MOTION_BLUR_UNIFORM_CURRENT_RECONSTRUCT_INFO,
	RB_MOTION_BLUR_UNIFORM_PREVIOUS_PROJECT_INFO,
	RB_MOTION_BLUR_UNIFORM_DEPTH_PROJECTION,
	RB_MOTION_BLUR_UNIFORM_CURRENT_VIEW_ORIGIN,
	RB_MOTION_BLUR_UNIFORM_CURRENT_VIEW_AXIS0,
	RB_MOTION_BLUR_UNIFORM_CURRENT_VIEW_AXIS1,
	RB_MOTION_BLUR_UNIFORM_CURRENT_VIEW_AXIS2,
	RB_MOTION_BLUR_UNIFORM_PREVIOUS_VIEW_ORIGIN,
	RB_MOTION_BLUR_UNIFORM_PREVIOUS_VIEW_AXIS0,
	RB_MOTION_BLUR_UNIFORM_PREVIOUS_VIEW_AXIS1,
	RB_MOTION_BLUR_UNIFORM_PREVIOUS_VIEW_AXIS2,
	RB_MOTION_BLUR_UNIFORM_PARAMS,
	RB_MOTION_BLUR_UNIFORM_OBJECT_PARAMS,
	RB_MOTION_BLUR_UNIFORM_COUNT
};

struct rbMotionBlurViewState_t {
	const idRenderWorldLocal *renderWorld;
	idStr mapName;
	int videoRestartCount;
	int viewportWidth;
	int viewportHeight;
	int renderTime;
	float fovX;
	float fovY;
	idVec3 viewOrigin;
	idVec3 viewAxis[3];
	float reconstructInfo[4];
	float projectInfo[4];
	float depthProjection[2];
	float projectionMatrix[16];
	float worldModelViewMatrix[16];
};

enum rbMotionVectorUniformIndex_t {
	RB_MOTION_VECTOR_UNIFORM_PREVIOUS_MODEL_VIEW_PROJECTION = 0,
	RB_MOTION_VECTOR_UNIFORM_VIEWPORT_SIZE,
	RB_MOTION_VECTOR_UNIFORM_COUNT
};

struct rbMotionBlurEntityHistory_t {
	int entityIndex;
	float modelMatrix[16];
};

static newShaderStage_t rbMotionBlurStage;
static bool rbMotionBlurStageInitialized = false;
static newShaderStage_t rbMotionVectorStage;
static bool rbMotionVectorStageInitialized = false;
static rbMotionBlurViewState_t rbMotionBlurHistory;
static bool rbMotionBlurHistoryValid = false;
static idList<rbMotionBlurEntityHistory_t> rbMotionBlurEntityHistory;
static idList<rbMotionBlurEntityHistory_t> rbMotionBlurNextEntityHistory;
static idImage *rbMotionVectorImage = NULL;
static idRenderTexture *rbMotionVectorRenderTexture = NULL;
static bool rbMotionVectorImageValid = false;

static void RB_ResetMotionBlurHistory( void ) {
	rbMotionBlurHistoryValid = false;
	rbMotionVectorImageValid = false;
	rbMotionBlurEntityHistory.Clear();
	rbMotionBlurNextEntityHistory.Clear();
}

static void RB_InitMotionBlurStage( void ) {
	if ( rbMotionBlurStageInitialized ) {
		return;
	}

	memset( &rbMotionBlurStage, 0, sizeof( rbMotionBlurStage ) );
	rbMotionBlurStage.glslProgram = true;
	idStr::Copynz( rbMotionBlurStage.glslProgramName, "motionblur.fs", sizeof( rbMotionBlurStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_MOTION_BLUR_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "viewportSize", 2 },
		{ "currentReconstructInfo", 4 },
		{ "previousProjectInfo", 4 },
		{ "depthProjection", 2 },
		{ "currentViewOrigin", 4 },
		{ "currentViewAxis0", 4 },
		{ "currentViewAxis1", 4 },
		{ "currentViewAxis2", 4 },
		{ "previousViewOrigin", 4 },
		{ "previousViewAxis0", 4 },
		{ "previousViewAxis1", 4 },
		{ "previousViewAxis2", 4 },
		{ "motionBlurParams", 4 },
		{ "motionBlurObjectParams", 4 }
	};

	rbMotionBlurStage.numShaderParms = RB_MOTION_BLUR_UNIFORM_COUNT;
	for ( int i = 0; i < RB_MOTION_BLUR_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbMotionBlurStage.shaderParmNames[i], uniforms[i].name, sizeof( rbMotionBlurStage.shaderParmNames[i] ) );
		rbMotionBlurStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbMotionBlurStage.numShaderTextures = 3;
	idStr::Copynz( rbMotionBlurStage.shaderTextureNames[0], "Scene", sizeof( rbMotionBlurStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbMotionBlurStage.shaderTextureNames[1], "DepthBuffer", sizeof( rbMotionBlurStage.shaderTextureNames[1] ) );
	idStr::Copynz( rbMotionBlurStage.shaderTextureNames[2], "VelocityBuffer", sizeof( rbMotionBlurStage.shaderTextureNames[2] ) );

	rbMotionBlurStageInitialized = true;
}

static void RB_InitMotionVectorStage( void ) {
	if ( rbMotionVectorStageInitialized ) {
		return;
	}

	memset( &rbMotionVectorStage, 0, sizeof( rbMotionVectorStage ) );
	rbMotionVectorStage.glslProgram = true;
	idStr::Copynz( rbMotionVectorStage.glslProgramName, "motionvectors.fs", sizeof( rbMotionVectorStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_MOTION_VECTOR_UNIFORM_COUNT] = {
		{ "previousModelViewProjection", 16 },
		{ "viewportSize", 2 }
	};

	rbMotionVectorStage.numShaderParms = RB_MOTION_VECTOR_UNIFORM_COUNT;
	for ( int i = 0; i < RB_MOTION_VECTOR_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbMotionVectorStage.shaderParmNames[i], uniforms[i].name, sizeof( rbMotionVectorStage.shaderParmNames[i] ) );
		rbMotionVectorStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbMotionVectorStage.numShaderTextures = 1;
	idStr::Copynz( rbMotionVectorStage.shaderTextureNames[0], "DepthBuffer", sizeof( rbMotionVectorStage.shaderTextureNames[0] ) );

	rbMotionVectorStageInitialized = true;
}

static bool RB_BuildMotionBlurViewState( rbMotionBlurViewState_t &state, int viewportWidth, int viewportHeight ) {
	const float projX = backEnd.viewDef->projectionMatrix[0];
	const float projY = backEnd.viewDef->projectionMatrix[5];
	if ( idMath::Fabs( projX ) <= 0.00001f || idMath::Fabs( projY ) <= 0.00001f ) {
		return false;
	}

	state.renderWorld = backEnd.viewDef->renderWorld;
	state.mapName.Clear();
	if ( state.renderWorld != NULL ) {
		state.mapName = state.renderWorld->mapName;
	}
	state.videoRestartCount = tr.videoRestartCount;
	state.viewportWidth = viewportWidth;
	state.viewportHeight = viewportHeight;
	state.renderTime = backEnd.viewDef->renderView.time;
	state.fovX = backEnd.viewDef->renderView.fov_x;
	state.fovY = backEnd.viewDef->renderView.fov_y;
	state.viewOrigin = backEnd.viewDef->renderView.vieworg;
	state.viewAxis[0] = backEnd.viewDef->renderView.viewaxis[0];
	state.viewAxis[1] = backEnd.viewDef->renderView.viewaxis[1];
	state.viewAxis[2] = backEnd.viewDef->renderView.viewaxis[2];
	state.reconstructInfo[0] = 1.0f / projX;
	state.reconstructInfo[1] = 1.0f / projY;
	state.reconstructInfo[2] = backEnd.viewDef->projectionMatrix[8];
	state.reconstructInfo[3] = backEnd.viewDef->projectionMatrix[9];
	state.projectInfo[0] = projX;
	state.projectInfo[1] = projY;
	state.projectInfo[2] = backEnd.viewDef->projectionMatrix[8];
	state.projectInfo[3] = backEnd.viewDef->projectionMatrix[9];
	state.depthProjection[0] = backEnd.viewDef->projectionMatrix[10];
	state.depthProjection[1] = backEnd.viewDef->projectionMatrix[14];
	memcpy( state.projectionMatrix, backEnd.viewDef->projectionMatrix, sizeof( state.projectionMatrix ) );
	memcpy( state.worldModelViewMatrix, backEnd.viewDef->worldSpace.modelViewMatrix, sizeof( state.worldModelViewMatrix ) );
	return true;
}

static bool RB_MotionBlurProjectionChanged( const rbMotionBlurViewState_t &current, const rbMotionBlurViewState_t &previous ) {
	if ( idMath::Fabs( current.fovX - previous.fovX ) > 0.01f || idMath::Fabs( current.fovY - previous.fovY ) > 0.01f ) {
		return true;
	}
	for ( int i = 0; i < 4; i++ ) {
		if ( idMath::Fabs( current.projectInfo[i] - previous.projectInfo[i] ) > 0.0001f ) {
			return true;
		}
	}
	for ( int i = 0; i < 2; i++ ) {
		if ( idMath::Fabs( current.depthProjection[i] - previous.depthProjection[i] ) > 0.0001f ) {
			return true;
		}
	}
	return false;
}

static bool RB_MotionBlurObjectVectorsRequested( void ) {
	return r_motionBlur.GetBool()
		&& r_motionBlurObjectVectors.GetBool()
		&& glConfig.GLSLProgramAvailable;
}

static bool RB_MotionBlurCameraMovedEnough( const rbMotionBlurViewState_t &current, const rbMotionBlurViewState_t &previous ) {
	if ( ( current.viewOrigin - previous.viewOrigin ).LengthSqr() >= Square( 0.10f ) ) {
		return true;
	}

	const float axisEpsilonSqr = Square( 0.00075f );
	for ( int i = 0; i < 3; i++ ) {
		if ( ( current.viewAxis[i] - previous.viewAxis[i] ).LengthSqr() >= axisEpsilonSqr ) {
			return true;
		}
	}

	return false;
}

static bool RB_MotionBlurHistoryUsable( const rbMotionBlurViewState_t &current, const rbMotionBlurViewState_t &previous, bool allowStillCameraObjectVectors ) {
	if ( !rbMotionBlurHistoryValid ) {
		return false;
	}
	if ( r_jitter.GetBool() ) {
		return false;
	}
	if ( current.videoRestartCount != previous.videoRestartCount ) {
		return false;
	}
	if ( current.renderWorld != previous.renderWorld || current.mapName.Icmp( previous.mapName ) != 0 ) {
		return false;
	}
	if ( current.viewportWidth != previous.viewportWidth || current.viewportHeight != previous.viewportHeight ) {
		return false;
	}
	if ( current.renderTime <= previous.renderTime ) {
		return false;
	}
	if ( current.renderTime - previous.renderTime > 100 ) {
		return false;
	}
	if ( !allowStillCameraObjectVectors && !RB_MotionBlurCameraMovedEnough( current, previous ) ) {
		return false;
	}
	if ( ( current.viewOrigin - previous.viewOrigin ).LengthSqr() > Square( 512.0f ) ) {
		return false;
	}
	if ( RB_MotionBlurProjectionChanged( current, previous ) ) {
		return false;
	}
	return true;
}

static void RB_UploadMotionBlurVec4( rbMotionBlurUniformIndex_t index, const idVec3 &value ) {
	if ( rbMotionBlurStage.shaderParmLocations[index] < 0 ) {
		return;
	}
	const GLfloat vector[4] = { value.x, value.y, value.z, 0.0f };
	glUniform4fvARB( rbMotionBlurStage.shaderParmLocations[index], 1, vector );
}

static bool RB_EnsurePackedClassicDrawCaches( const drawSurf_t *surf, bool needsLighting, bool createIndexCache );
static void RB_BindPostProcessRenderTexture( idRenderTexture *renderTexture, int width, int height );
static void RB_RestorePostProcessTarget( idRenderTexture *renderTexture, int viewportWidth, int viewportHeight );

static bool RB_MotionVectorSurfaceEligible( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->geo == NULL || surf->material == NULL || surf->space == NULL || surf->space->entityDef == NULL ) {
		return false;
	}

	const srfTriangles_t *tri = surf->geo;
	const idMaterial *shader = surf->material;
	const idRenderEntityLocal *entity = surf->space->entityDef;
	if ( !shader->IsDrawn() || !shader->HasAmbient() || shader->IsPortalSky() || shader->SuppressInSubview() ) {
		return false;
	}
	if ( shader->Coverage() != MC_OPAQUE || shader->GetSort() >= SS_POST_PROCESS ) {
		return false;
	}
	if ( tri->numIndexes <= 0 || R_TriHasPrimBatchMesh( tri ) || tri->deformedSurface ) {
		return false;
	}
	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		return false;
	}
	if ( entity->index < 0 || entity->parms.callback != NULL || entity->parms.hModel == NULL ) {
		return false;
	}
	if ( entity->parms.hModel->IsDynamicModel() != DM_STATIC || entity->dynamicModel != NULL ) {
		return false;
	}
	return true;
}

static bool RB_FindMotionBlurEntityHistory( int entityIndex, float previousModelMatrix[16] ) {
	for ( int i = 0; i < rbMotionBlurEntityHistory.Num(); i++ ) {
		if ( rbMotionBlurEntityHistory[i].entityIndex == entityIndex ) {
			memcpy( previousModelMatrix, rbMotionBlurEntityHistory[i].modelMatrix, sizeof( rbMotionBlurEntityHistory[i].modelMatrix ) );
			return true;
		}
	}
	return false;
}

static void RB_StoreMotionBlurEntityHistory( idList<rbMotionBlurEntityHistory_t> &history, int entityIndex, const float modelMatrix[16] ) {
	for ( int i = 0; i < history.Num(); i++ ) {
		if ( history[i].entityIndex == entityIndex ) {
			return;
		}
	}

	rbMotionBlurEntityHistory_t &entry = history.Alloc();
	entry.entityIndex = entityIndex;
	memcpy( entry.modelMatrix, modelMatrix, sizeof( entry.modelMatrix ) );
}

static void RB_UpdateMotionBlurEntityHistory( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	rbMotionBlurNextEntityHistory.Clear();
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *surf = drawSurfs[i];
		if ( !RB_MotionVectorSurfaceEligible( surf ) ) {
			continue;
		}
		RB_StoreMotionBlurEntityHistory(
			rbMotionBlurNextEntityHistory,
			surf->space->entityDef->index,
			surf->space->modelMatrix );
	}
	rbMotionBlurEntityHistory.Swap( rbMotionBlurNextEntityHistory );
	rbMotionBlurNextEntityHistory.Clear();
}

static bool RB_EnsureMotionVectorRenderTexture( int viewportWidth, int viewportHeight ) {
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return false;
	}

	idImageOpts opts;
	opts.textureType = TT_2D;
	opts.format = FMT_RGBA16F;
	opts.width = viewportWidth;
	opts.height = viewportHeight;
	opts.numLevels = 1;
	opts.numMSAASamples = 0;
	opts.isPersistant = true;

	rbMotionVectorImage = globalImages->ScratchImage( "_motionVector", &opts, TF_NEAREST, TR_CLAMP, TD_DEFAULT );
	if ( rbMotionVectorImage == NULL ) {
		return false;
	}

	if ( rbMotionVectorRenderTexture == NULL ) {
		rbMotionVectorRenderTexture = tr.CreateRenderTexture( rbMotionVectorImage, NULL );
	} else if ( rbMotionVectorRenderTexture->GetWidth() != viewportWidth || rbMotionVectorRenderTexture->GetHeight() != viewportHeight ) {
		tr.ResizeRenderTexture( rbMotionVectorRenderTexture, viewportWidth, viewportHeight );
	}

	return rbMotionVectorRenderTexture != NULL;
}

static const rbMotionBlurViewState_t *rbMotionVectorPreviousState = NULL;
static bool rbMotionVectorDrewSurface = false;

static void RB_T_RenderMotionVectorSurface( const drawSurf_t *surf ) {
	if ( !RB_MotionVectorSurfaceEligible( surf ) || rbMotionVectorPreviousState == NULL ) {
		return;
	}

	float previousModelMatrix[16];
	if ( !RB_FindMotionBlurEntityHistory( surf->space->entityDef->index, previousModelMatrix ) ) {
		return;
	}

	const srfTriangles_t *tri = surf->geo;
	if ( !RB_EnsurePackedClassicDrawCaches( surf, false, true ) || tri->ambientCache == NULL ) {
		return;
	}

	float previousModelView[16];
	float previousModelViewProjection[16];
	myGlMultMatrix( previousModelMatrix, rbMotionVectorPreviousState->worldModelViewMatrix, previousModelView );
	myGlMultMatrix( previousModelView, rbMotionVectorPreviousState->projectionMatrix, previousModelViewProjection );

	const int previousMatrixLocation = rbMotionVectorStage.shaderParmLocations[RB_MOTION_VECTOR_UNIFORM_PREVIOUS_MODEL_VIEW_PROJECTION];
	if ( previousMatrixLocation >= 0 ) {
		glUniformMatrix4fvARB( previousMatrixLocation, 1, GL_FALSE, previousModelViewProjection );
	}

	GL_Cull( surf->material->GetCullType() );

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );
	RB_DrawElementsWithCounters( tri );
	rbMotionVectorDrewSurface = true;
}

static bool RB_RenderMotionVectorBuffer( drawSurf_t **drawSurfs, int numDrawSurfs, const rbMotionBlurViewState_t &previousState, int viewportWidth, int viewportHeight ) {
	if ( !RB_MotionBlurObjectVectorsRequested() ) {
		return false;
	}
	if ( globalImages->currentDepthImage == NULL ) {
		return false;
	}

	RB_InitMotionVectorStage();
	if ( !R_ValidateGLSLProgram( &rbMotionVectorStage ) ) {
		return false;
	}

	if ( !RB_EnsureMotionVectorRenderTexture( viewportWidth, viewportHeight ) ) {
		return false;
	}

	idRenderTexture *previousRenderTexture = backEnd.renderTexture;
	rbMotionVectorImageValid = false;
	rbMotionVectorDrewSurface = false;
	rbMotionVectorPreviousState = &previousState;

	RB_BindPostProcessRenderTexture( rbMotionVectorRenderTexture, viewportWidth, viewportHeight );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_DEPTHMASK | GLS_DEPTHFUNC_ALWAYS );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	GL_SelectTexture( 0 );
	globalImages->currentDepthImage->Bind();
	glUseProgramObjectARB( (GLhandleARB)rbMotionVectorStage.glslProgramObject );

	for ( int i = 0; i < rbMotionVectorStage.numShaderTextures; i++ ) {
		if ( rbMotionVectorStage.shaderTextureLocations[i] >= 0 ) {
			glUniform1iARB( rbMotionVectorStage.shaderTextureLocations[i], i );
		}
	}

	const int viewportLocation = rbMotionVectorStage.shaderParmLocations[RB_MOTION_VECTOR_UNIFORM_VIEWPORT_SIZE];
	if ( viewportLocation >= 0 ) {
		const GLfloat viewportSize[2] = {
			static_cast<GLfloat>( viewportWidth ),
			static_cast<GLfloat>( viewportHeight )
		};
		glUniform2fvARB( viewportLocation, 1, viewportSize );
	}

	glEnableClientState( GL_VERTEX_ARRAY );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );

	backEnd.currentSpace = NULL;
	backEnd.currentScissor.Clear();
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *surf = drawSurfs[i];
		if ( !RB_MotionVectorSurfaceEligible( surf ) ) {
			continue;
		}
		if ( surf->space != backEnd.currentSpace ) {
			glLoadMatrixf( surf->space->modelViewMatrix );
			backEnd.currentSpace = surf->space;
		}
		if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
			backEnd.currentScissor = surf->scissorRect;
			glScissor(
				backEnd.currentScissor.x1,
				backEnd.currentScissor.y1,
				backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		}
		RB_T_RenderMotionVectorSurface( surf );
	}

	glUseProgramObjectARB( 0 );
	GL_Cull( CT_FRONT_SIDED );
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	rbMotionVectorPreviousState = NULL;

	RB_RestorePostProcessTarget( previousRenderTexture, viewportWidth, viewportHeight );
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	rbMotionVectorImageValid = rbMotionVectorDrewSurface;
	return rbMotionVectorImageValid;
}

static void RB_STD_MotionBlur( void ) {
	if ( r_skipPostProcess.GetBool() || !r_motionBlur.GetBool() || !glConfig.GLSLProgramAvailable ) {
		RB_ResetMotionBlurHistory();
		return;
	}

	if ( !RB_IsMainMotionBlurView() ) {
		if ( backEnd.viewDef != NULL && backEnd.viewDef->viewEntitys != NULL ) {
			RB_ResetMotionBlurHistory();
		}
		return;
	}

	if ( !r_motionBlurDebug.GetBool() &&
		( r_motionBlurStrength.GetFloat() <= 0.0f || r_motionBlurMaxPixels.GetFloat() <= 0.0f || r_motionBlurSamples.GetInteger() <= 0 ) ) {
		RB_ResetMotionBlurHistory();
		return;
	}

	if ( r_jitter.GetBool() ) {
		RB_ResetMotionBlurHistory();
		return;
	}

	RB_InitMotionBlurStage();
	if ( !R_ValidateGLSLProgram( &rbMotionBlurStage ) ) {
		RB_ResetMotionBlurHistory();
		return;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		RB_ResetMotionBlurHistory();
		return;
	}

	rbMotionBlurViewState_t currentState;
	if ( !RB_BuildMotionBlurViewState( currentState, viewportWidth, viewportHeight ) ) {
		RB_ResetMotionBlurHistory();
		return;
	}

	const rbMotionBlurViewState_t previousState = rbMotionBlurHistory;
	const bool objectVectorsRequested = RB_MotionBlurObjectVectorsRequested();
	const bool cameraMovedEnough = rbMotionBlurHistoryValid && RB_MotionBlurCameraMovedEnough( currentState, previousState );
	const bool historyUsable = RB_MotionBlurHistoryUsable( currentState, previousState, objectVectorsRequested );
	rbMotionBlurHistory = currentState;
	rbMotionBlurHistoryValid = true;

	drawSurf_t **drawSurfs = (drawSurf_t **)&backEnd.viewDef->drawSurfs[0];
	const int numDrawSurfs = backEnd.viewDef->numDrawSurfs;
	if ( !historyUsable ) {
		rbMotionVectorImageValid = false;
		RB_UpdateMotionBlurEntityHistory( drawSurfs, numDrawSurfs );
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	idImage *depthImage = globalImages->currentDepthImage;
	if ( sceneImage == NULL || depthImage == NULL ) {
		RB_ResetMotionBlurHistory();
		return;
	}

	RB_LogComment( "---------- RB_STD_MotionBlur ----------\n" );

	RB_CaptureCurrentRenderImage( viewportWidth, viewportHeight );
	if ( !backEnd.currentDepthCopied ) {
		RB_CaptureCurrentDepthImage( viewportWidth, viewportHeight );
	}
	rbMotionVectorImageValid = false;
	if ( objectVectorsRequested ) {
		RB_RenderMotionVectorBuffer( drawSurfs, numDrawSurfs, previousState, viewportWidth, viewportHeight );
	}
	RB_UpdateMotionBlurEntityHistory( drawSurfs, numDrawSurfs );

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	const int depthTextureWidth = depthImage->GetOpts().width;
	const int depthTextureHeight = depthImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 || depthTextureWidth <= 0 || depthTextureHeight <= 0 ) {
		return;
	}

	backEnd.currentScissor = backEnd.viewDef->scissor;

	RB_BeginFullscreenPostProcessPass(
		backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );

	GL_SelectTexture( 0 );
	sceneImage->Bind();
	GL_SelectTexture( 1 );
	depthImage->Bind();
	GL_SelectTexture( 2 );
	if ( rbMotionVectorImageValid && rbMotionVectorImage != NULL ) {
		rbMotionVectorImage->Bind();
	} else {
		globalImages->blackImage->Bind();
	}
	GL_SelectTexture( 0 );

	glUseProgramObjectARB( (GLhandleARB)rbMotionBlurStage.glslProgramObject );

	for ( int i = 0; i < rbMotionBlurStage.numShaderTextures; i++ ) {
		if ( rbMotionBlurStage.shaderTextureLocations[i] >= 0 ) {
			glUniform1iARB( rbMotionBlurStage.shaderTextureLocations[i], i );
		}
	}

	const GLfloat invTexSize[2] = {
		1.0f / static_cast<GLfloat>( textureWidth ),
		1.0f / static_cast<GLfloat>( textureHeight )
	};
	const GLfloat viewportSize[2] = {
		static_cast<GLfloat>( viewportWidth ),
		static_cast<GLfloat>( viewportHeight )
	};
	const GLfloat params[4] = {
		idMath::ClampFloat( 0.0f, 2.0f, r_motionBlurStrength.GetFloat() ),
		idMath::ClampFloat( 0.0f, 64.0f, r_motionBlurMaxPixels.GetFloat() ),
		static_cast<GLfloat>( idMath::ClampInt( 1, 16, r_motionBlurSamples.GetInteger() ) ),
		r_motionBlurDebug.GetBool() ? 1.0f : 0.0f
	};

	if ( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_INV_TEX_SIZE] >= 0 ) {
		glUniform2fvARB( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_INV_TEX_SIZE], 1, invTexSize );
	}
	if ( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_VIEWPORT_SIZE] >= 0 ) {
		glUniform2fvARB( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_VIEWPORT_SIZE], 1, viewportSize );
	}
	if ( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_CURRENT_RECONSTRUCT_INFO] >= 0 ) {
		glUniform4fvARB( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_CURRENT_RECONSTRUCT_INFO], 1, currentState.reconstructInfo );
	}
	if ( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_PREVIOUS_PROJECT_INFO] >= 0 ) {
		glUniform4fvARB( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_PREVIOUS_PROJECT_INFO], 1, previousState.projectInfo );
	}
	if ( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_DEPTH_PROJECTION] >= 0 ) {
		glUniform2fvARB( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_DEPTH_PROJECTION], 1, currentState.depthProjection );
	}

	RB_UploadMotionBlurVec4( RB_MOTION_BLUR_UNIFORM_CURRENT_VIEW_ORIGIN, currentState.viewOrigin );
	RB_UploadMotionBlurVec4( RB_MOTION_BLUR_UNIFORM_CURRENT_VIEW_AXIS0, currentState.viewAxis[0] );
	RB_UploadMotionBlurVec4( RB_MOTION_BLUR_UNIFORM_CURRENT_VIEW_AXIS1, currentState.viewAxis[1] );
	RB_UploadMotionBlurVec4( RB_MOTION_BLUR_UNIFORM_CURRENT_VIEW_AXIS2, currentState.viewAxis[2] );
	RB_UploadMotionBlurVec4( RB_MOTION_BLUR_UNIFORM_PREVIOUS_VIEW_ORIGIN, previousState.viewOrigin );
	RB_UploadMotionBlurVec4( RB_MOTION_BLUR_UNIFORM_PREVIOUS_VIEW_AXIS0, previousState.viewAxis[0] );
	RB_UploadMotionBlurVec4( RB_MOTION_BLUR_UNIFORM_PREVIOUS_VIEW_AXIS1, previousState.viewAxis[1] );
	RB_UploadMotionBlurVec4( RB_MOTION_BLUR_UNIFORM_PREVIOUS_VIEW_AXIS2, previousState.viewAxis[2] );

	if ( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_PARAMS] >= 0 ) {
		glUniform4fvARB( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_PARAMS], 1, params );
	}
	if ( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_OBJECT_PARAMS] >= 0 ) {
		const GLfloat objectParams[4] = {
			rbMotionVectorImageValid ? 1.0f : 0.0f,
			cameraMovedEnough ? 1.0f : 0.0f,
			0.0f,
			0.0f
		};
		glUniform4fvARB( rbMotionBlurStage.shaderParmLocations[RB_MOTION_BLUR_UNIFORM_OBJECT_PARAMS], 1, objectParams );
	}

	RB_DrawFullscreenPostProcessQuad( viewportWidth, viewportHeight, textureWidth, textureHeight );

	glUseProgramObjectARB( 0 );
	GL_SelectTexture( 2 );
	globalImages->BindNull();
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();

	// The destination changed after the pre-blur capture. Let any later pass that
	// samples the current scene take a fresh copy of the blurred image.
	backEnd.currentRenderCopied = false;
}

enum rbBloomExtractUniformIndex_t {
	RB_BLOOM_EXTRACT_UNIFORM_INV_TEX_SIZE = 0,
	RB_BLOOM_EXTRACT_UNIFORM_THRESHOLD,
	RB_BLOOM_EXTRACT_UNIFORM_SOFT_KNEE,
	RB_BLOOM_EXTRACT_UNIFORM_COUNT
};

enum rbBloomDownsampleUniformIndex_t {
	RB_BLOOM_DOWNSAMPLE_UNIFORM_INV_TEX_SIZE = 0,
	RB_BLOOM_DOWNSAMPLE_UNIFORM_COUNT
};

enum rbBloomBlurUniformIndex_t {
	RB_BLOOM_BLUR_UNIFORM_INV_TEX_SIZE = 0,
	RB_BLOOM_BLUR_UNIFORM_BLUR_AXIS,
	RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS,
	RB_BLOOM_BLUR_UNIFORM_COUNT
};

enum rbHDRLuminanceUniformIndex_t {
	RB_HDR_LUMINANCE_UNIFORM_INV_TEX_SIZE = 0,
	RB_HDR_LUMINANCE_UNIFORM_SOURCE_IS_COLOR,
	RB_HDR_LUMINANCE_UNIFORM_COUNT
};

enum rbBloomCompositeUniformIndex_t {
	RB_BLOOM_COMPOSITE_UNIFORM_INTENSITY = 0,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_ENABLED,
	RB_BLOOM_COMPOSITE_UNIFORM_TONEMAP_ENABLED,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_EXPOSURE,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_WHITE_POINT,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_LIFT,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_POST_GAMMA,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAIN,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_VIBRANCE,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_SATURATION,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_CONTRAST,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_HIGHLIGHT_DESATURATION,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAMUT_COMPRESSION,
	RB_BLOOM_COMPOSITE_UNIFORM_HDR_DEBUG_VIEW,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT0,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT1,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT2,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT3,
	RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT4,
	RB_BLOOM_COMPOSITE_UNIFORM_COUNT
};

static newShaderStage_t rbBloomExtractStage;
static newShaderStage_t rbBloomDownsampleStage;
static newShaderStage_t rbBloomBlurStage;
static newShaderStage_t rbHDRLuminanceStage;
static newShaderStage_t rbBloomCompositeStage;
static bool rbBloomStagesInitialized = false;
static idImage *rbBloomImages[RB_BLOOM_MAX_LEVELS][2];
static idRenderTexture *rbBloomRenderTextures[RB_BLOOM_MAX_LEVELS][2];
static idImage *rbHDRExposureImages[RB_HDR_EXPOSURE_MAX_LEVELS];
static idRenderTexture *rbHDRExposureRenderTextures[RB_HDR_EXPOSURE_MAX_LEVELS];
static int rbHDRExposureLevelCount = 0;

static void RB_InitBloomStages( void ) {
	if ( rbBloomStagesInitialized ) {
		return;
	}

	memset( &rbBloomExtractStage, 0, sizeof( rbBloomExtractStage ) );
	rbBloomExtractStage.glslProgram = true;
	idStr::Copynz( rbBloomExtractStage.glslProgramName, "bloom_extract.fs", sizeof( rbBloomExtractStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t extractUniforms[RB_BLOOM_EXTRACT_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "bloomThreshold", 1 },
		{ "bloomSoftKnee", 1 }
	};

	rbBloomExtractStage.numShaderParms = RB_BLOOM_EXTRACT_UNIFORM_COUNT;
	for ( int i = 0; i < RB_BLOOM_EXTRACT_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbBloomExtractStage.shaderParmNames[i], extractUniforms[i].name, sizeof( rbBloomExtractStage.shaderParmNames[i] ) );
		rbBloomExtractStage.shaderParmNumRegisters[i] = extractUniforms[i].components;
	}
	rbBloomExtractStage.numShaderTextures = 1;
	idStr::Copynz( rbBloomExtractStage.shaderTextureNames[0], "Scene", sizeof( rbBloomExtractStage.shaderTextureNames[0] ) );

	memset( &rbBloomDownsampleStage, 0, sizeof( rbBloomDownsampleStage ) );
	rbBloomDownsampleStage.glslProgram = true;
	idStr::Copynz( rbBloomDownsampleStage.glslProgramName, "bloom_downsample.fs", sizeof( rbBloomDownsampleStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t downsampleUniforms[RB_BLOOM_DOWNSAMPLE_UNIFORM_COUNT] = {
		{ "invTexSize", 2 }
	};

	rbBloomDownsampleStage.numShaderParms = RB_BLOOM_DOWNSAMPLE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_BLOOM_DOWNSAMPLE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbBloomDownsampleStage.shaderParmNames[i], downsampleUniforms[i].name, sizeof( rbBloomDownsampleStage.shaderParmNames[i] ) );
		rbBloomDownsampleStage.shaderParmNumRegisters[i] = downsampleUniforms[i].components;
	}
	rbBloomDownsampleStage.numShaderTextures = 1;
	idStr::Copynz( rbBloomDownsampleStage.shaderTextureNames[0], "Scene", sizeof( rbBloomDownsampleStage.shaderTextureNames[0] ) );

	memset( &rbBloomBlurStage, 0, sizeof( rbBloomBlurStage ) );
	rbBloomBlurStage.glslProgram = true;
	idStr::Copynz( rbBloomBlurStage.glslProgramName, "bloom_blur.fs", sizeof( rbBloomBlurStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t blurUniforms[RB_BLOOM_BLUR_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "blurAxis", 2 },
		{ "blurRadius", 1 }
	};

	rbBloomBlurStage.numShaderParms = RB_BLOOM_BLUR_UNIFORM_COUNT;
	for ( int i = 0; i < RB_BLOOM_BLUR_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbBloomBlurStage.shaderParmNames[i], blurUniforms[i].name, sizeof( rbBloomBlurStage.shaderParmNames[i] ) );
		rbBloomBlurStage.shaderParmNumRegisters[i] = blurUniforms[i].components;
	}
	rbBloomBlurStage.numShaderTextures = 1;
	idStr::Copynz( rbBloomBlurStage.shaderTextureNames[0], "Scene", sizeof( rbBloomBlurStage.shaderTextureNames[0] ) );

	memset( &rbHDRLuminanceStage, 0, sizeof( rbHDRLuminanceStage ) );
	rbHDRLuminanceStage.glslProgram = true;
	idStr::Copynz( rbHDRLuminanceStage.glslProgramName, "hdr_luminance.fs", sizeof( rbHDRLuminanceStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t luminanceUniforms[RB_HDR_LUMINANCE_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "sourceIsColor", 1 }
	};

	rbHDRLuminanceStage.numShaderParms = RB_HDR_LUMINANCE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_HDR_LUMINANCE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbHDRLuminanceStage.shaderParmNames[i], luminanceUniforms[i].name, sizeof( rbHDRLuminanceStage.shaderParmNames[i] ) );
		rbHDRLuminanceStage.shaderParmNumRegisters[i] = luminanceUniforms[i].components;
	}
	rbHDRLuminanceStage.numShaderTextures = 1;
	idStr::Copynz( rbHDRLuminanceStage.shaderTextureNames[0], "Scene", sizeof( rbHDRLuminanceStage.shaderTextureNames[0] ) );

	memset( &rbBloomCompositeStage, 0, sizeof( rbBloomCompositeStage ) );
	rbBloomCompositeStage.glslProgram = true;
	idStr::Copynz( rbBloomCompositeStage.glslProgramName, "bloom.fs", sizeof( rbBloomCompositeStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t compositeUniforms[RB_BLOOM_COMPOSITE_UNIFORM_COUNT] = {
		{ "bloomIntensity", 1 },
		{ "bloomEnabled", 1 },
		{ "toneMapEnabled", 1 },
		{ "hdrExposure", 1 },
		{ "hdrWhitePoint", 1 },
		{ "hdrLift", 1 },
		{ "hdrPostGamma", 1 },
		{ "hdrGain", 1 },
		{ "hdrVibrance", 1 },
		{ "hdrSaturation", 1 },
		{ "hdrContrast", 1 },
		{ "hdrHighlightDesaturation", 1 },
		{ "hdrGamutCompression", 1 },
		{ "hdrDebugView", 1 },
		{ "bloomWeight0", 1 },
		{ "bloomWeight1", 1 },
		{ "bloomWeight2", 1 },
		{ "bloomWeight3", 1 },
		{ "bloomWeight4", 1 }
	};

	rbBloomCompositeStage.numShaderParms = RB_BLOOM_COMPOSITE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_BLOOM_COMPOSITE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbBloomCompositeStage.shaderParmNames[i], compositeUniforms[i].name, sizeof( rbBloomCompositeStage.shaderParmNames[i] ) );
		rbBloomCompositeStage.shaderParmNumRegisters[i] = compositeUniforms[i].components;
	}
	rbBloomCompositeStage.numShaderTextures = 1 + RB_BLOOM_MAX_LEVELS;
	idStr::Copynz( rbBloomCompositeStage.shaderTextureNames[0], "Scene", sizeof( rbBloomCompositeStage.shaderTextureNames[0] ) );
	for ( int i = 0; i < RB_BLOOM_MAX_LEVELS; i++ ) {
		idStr::Copynz( rbBloomCompositeStage.shaderTextureNames[i + 1], va( "BloomTex%d", i ), sizeof( rbBloomCompositeStage.shaderTextureNames[i + 1] ) );
	}

	rbBloomStagesInitialized = true;
}

static void RB_GetBloomLevelSize( int viewportWidth, int viewportHeight, int level, int &levelWidth, int &levelHeight ) {
	levelWidth = Max( 1, viewportWidth );
	levelHeight = Max( 1, viewportHeight );

	for ( int i = 0; i < level; i++ ) {
		levelWidth = Max( 1, ( levelWidth + 1 ) / 2 );
		levelHeight = Max( 1, ( levelHeight + 1 ) / 2 );
	}
}

static bool RB_EnsureBloomRenderTextures( int viewportWidth, int viewportHeight, int levelCount ) {
	for ( int level = 0; level < levelCount; level++ ) {
		int bloomWidth = 0;
		int bloomHeight = 0;
		RB_GetBloomLevelSize( viewportWidth, viewportHeight, level, bloomWidth, bloomHeight );

		for ( int ping = 0; ping < 2; ping++ ) {
			idImageOpts opts;
			opts.textureType = TT_2D;
			opts.format = FMT_RGBA16F;
			opts.width = bloomWidth;
			opts.height = bloomHeight;
			opts.numLevels = 1;
			opts.isPersistant = true;

			rbBloomImages[level][ping] = globalImages->ScratchImage( va( "_bloomL%dP%d", level, ping ), &opts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );
			if ( rbBloomImages[level][ping] == NULL ) {
				return false;
			}

			if ( rbBloomRenderTextures[level][ping] == NULL ) {
				rbBloomRenderTextures[level][ping] = tr.CreateRenderTexture( rbBloomImages[level][ping], NULL );
			} else if ( rbBloomRenderTextures[level][ping]->GetWidth() != bloomWidth || rbBloomRenderTextures[level][ping]->GetHeight() != bloomHeight ) {
				tr.ResizeRenderTexture( rbBloomRenderTextures[level][ping], bloomWidth, bloomHeight );
			}

			if ( rbBloomRenderTextures[level][ping] == NULL ) {
				return false;
			}
		}
	}

	return true;
}

static void RB_BindPostProcessRenderTexture( idRenderTexture *renderTexture, int width, int height ) {
	backEnd.renderTexture = renderTexture;
	renderTexture->MakeCurrent();
	glViewport( 0, 0, width, height );
	glScissor( 0, 0, width, height );
}

static void RB_RestorePostProcessTarget( idRenderTexture *renderTexture, int viewportWidth, int viewportHeight ) {
	backEnd.renderTexture = renderTexture;
	if ( renderTexture != NULL ) {
		renderTexture->MakeCurrent();
	} else {
		idRenderTexture::BindNull();
		glDrawBuffer( GL_BACK );
		glReadBuffer( GL_BACK );
	}

	glViewport(
		tr.viewportOffset[0] + backEnd.viewDef->viewport.x1,
		tr.viewportOffset[1] + backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );
	glScissor(
		tr.viewportOffset[0] + backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		tr.viewportOffset[1] + backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
	backEnd.currentScissor = backEnd.viewDef->scissor;
}

static bool RB_EnsureHDRExposureRenderTextures( int viewportWidth, int viewportHeight ) {
	rbHDRExposureLevelCount = 0;

	int levelWidth = Max( 1, ( viewportWidth + 1 ) / 2 );
	int levelHeight = Max( 1, ( viewportHeight + 1 ) / 2 );

	while ( rbHDRExposureLevelCount < RB_HDR_EXPOSURE_MAX_LEVELS ) {
		idImageOpts opts;
		opts.textureType = TT_2D;
		opts.format = FMT_RGBA16F;
		opts.width = levelWidth;
		opts.height = levelHeight;
		opts.numLevels = 1;
		opts.isPersistant = true;

		const int level = rbHDRExposureLevelCount;
		rbHDRExposureImages[level] = globalImages->ScratchImage( va( "_hdrLum%d", level ), &opts, TF_LINEAR, TR_CLAMP, TD_DEFAULT );
		if ( rbHDRExposureImages[level] == NULL ) {
			return false;
		}

		if ( rbHDRExposureRenderTextures[level] == NULL ) {
			rbHDRExposureRenderTextures[level] = tr.CreateRenderTexture( rbHDRExposureImages[level], NULL );
		} else if ( rbHDRExposureRenderTextures[level]->GetWidth() != levelWidth || rbHDRExposureRenderTextures[level]->GetHeight() != levelHeight ) {
			tr.ResizeRenderTexture( rbHDRExposureRenderTextures[level], levelWidth, levelHeight );
		}

		if ( rbHDRExposureRenderTextures[level] == NULL ) {
			return false;
		}

		rbHDRExposureLevelCount++;
		if ( levelWidth == 1 && levelHeight == 1 ) {
			break;
		}

		levelWidth = Max( 1, ( levelWidth + 1 ) / 2 );
		levelHeight = Max( 1, ( levelHeight + 1 ) / 2 );
	}

	return rbHDRExposureLevelCount > 0;
}

static float RB_UpdateHDRAutoExposure( idImage *sceneImage, int viewportWidth, int viewportHeight ) {
	if ( !RB_HDRAutoExposureEnabled() ) {
		rbHDRLastAverageLuminance = 1.0f;
		rbHDRLastTargetExposure = 1.0f;
		return 1.0f;
	}

	if ( sceneImage == NULL ) {
		return rbHDRExposureInitialized ? rbHDRAdaptedExposure : 1.0f;
	}

	RB_InitBloomStages();
	if ( !R_ValidateGLSLProgram( &rbHDRLuminanceStage ) || !RB_EnsureHDRExposureRenderTextures( viewportWidth, viewportHeight ) ) {
		return rbHDRExposureInitialized ? rbHDRAdaptedExposure : 1.0f;
	}

	idRenderTexture *originalRenderTexture = backEnd.renderTexture;
	idImage *sourceImage = sceneImage;
	int sourceWidth = Max( 1, sceneImage->GetOpts().width );
	int sourceHeight = Max( 1, sceneImage->GetOpts().height );
	bool sourceIsColor = true;

	for ( int level = 0; level < rbHDRExposureLevelCount; level++ ) {
		const int levelWidth = rbHDRExposureRenderTextures[level]->GetWidth();
		const int levelHeight = rbHDRExposureRenderTextures[level]->GetHeight();
		const GLfloat invTexSize[2] = {
			1.0f / static_cast<GLfloat>( Max( 1, sourceWidth ) ),
			1.0f / static_cast<GLfloat>( Max( 1, sourceHeight ) )
		};

		RB_BindPostProcessRenderTexture( rbHDRExposureRenderTextures[level], levelWidth, levelHeight );
		RB_BeginFullscreenPostProcessPass( 0, 0, levelWidth, levelHeight );
		GL_SelectTexture( 0 );
		sourceImage->Bind();

		glUseProgramObjectARB( (GLhandleARB)rbHDRLuminanceStage.glslProgramObject );
		if ( rbHDRLuminanceStage.shaderTextureLocations[0] >= 0 ) {
			glUniform1iARB( rbHDRLuminanceStage.shaderTextureLocations[0], 0 );
		}
		if ( rbHDRLuminanceStage.shaderParmLocations[RB_HDR_LUMINANCE_UNIFORM_INV_TEX_SIZE] >= 0 ) {
			glUniform2fvARB( rbHDRLuminanceStage.shaderParmLocations[RB_HDR_LUMINANCE_UNIFORM_INV_TEX_SIZE], 1, invTexSize );
		}
		if ( rbHDRLuminanceStage.shaderParmLocations[RB_HDR_LUMINANCE_UNIFORM_SOURCE_IS_COLOR] >= 0 ) {
			glUniform1fARB( rbHDRLuminanceStage.shaderParmLocations[RB_HDR_LUMINANCE_UNIFORM_SOURCE_IS_COLOR], sourceIsColor ? 1.0f : 0.0f );
		}

		RB_DrawFullscreenPostProcessQuadUnitUV();
		glUseProgramObjectARB( 0 );
		RB_EndFullscreenPostProcessPass();

		sourceImage = rbHDRExposureImages[level];
		sourceWidth = levelWidth;
		sourceHeight = levelHeight;
		sourceIsColor = false;
	}

	float averageLogLuminance = 0.0f;
	bool haveLuminanceSample = false;
	const bool asyncReadbackSupported = glConfig.pixelBufferObjectAvailable;
	bool hdrAsyncReadbackActive = r_hdrAutoExposureAsync.GetBool() && asyncReadbackSupported;
	if ( hdrAsyncReadbackActive ) {
		// queue this frame's 1x1 luminance read into a pixel-pack buffer and
		// consume the previous frame's sample; exposure adaptation is a slow
		// temporal filter, so one frame of latency is invisible while the
		// synchronous glReadPixels stall it replaces is not
		if ( rbHDRExposureReadbackPBOs[0] == 0 ) {
			glGenBuffersARB( 2, rbHDRExposureReadbackPBOs );
		}
		for ( int i = 0; i < 2; i++ ) {
			if ( rbHDRExposureReadbackPBOs[i] == 0 ) {
				hdrAsyncReadbackActive = false;
				break;
			}
		}
		if ( hdrAsyncReadbackActive && !rbHDRExposureReadbackPrimed[0] && !rbHDRExposureReadbackPrimed[1] ) {
			for ( int i = 0; i < 2; i++ ) {
				glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, rbHDRExposureReadbackPBOs[i] );
				glBufferDataARB( GL_PIXEL_PACK_BUFFER_ARB, sizeof( GLfloat ) * 4, NULL, GL_STREAM_READ_ARB );
				rbHDRExposureReadbackPrimed[i] = false;
			}
		}
		if ( !hdrAsyncReadbackActive ) {
			if ( glDeleteBuffersARB != NULL ) {
				glDeleteBuffersARB( 2, rbHDRExposureReadbackPBOs );
			}
			rbHDRExposureReadbackPBOs[0] = 0;
			rbHDRExposureReadbackPBOs[1] = 0;
			rbHDRExposureReadbackPrimed[0] = false;
			rbHDRExposureReadbackPrimed[1] = false;
			glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, 0 );
		}
	}

	if ( hdrAsyncReadbackActive ) {
		const int writeIndex = rbHDRExposureReadbackIndex;
		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, rbHDRExposureReadbackPBOs[writeIndex] );
		glReadPixels( 0, 0, 1, 1, GL_RGBA, GL_FLOAT, NULL );
		rbHDRExposureReadbackPrimed[writeIndex] = true;

		const int readIndex = writeIndex ^ 1;
		if ( rbHDRExposureReadbackPrimed[readIndex] ) {
			glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, rbHDRExposureReadbackPBOs[readIndex] );
			const GLfloat *mapped = static_cast<const GLfloat *>( glMapBufferARB( GL_PIXEL_PACK_BUFFER_ARB, GL_READ_ONLY_ARB ) );
			if ( mapped != NULL ) {
				averageLogLuminance = mapped[0];
				haveLuminanceSample = true;
				glUnmapBufferARB( GL_PIXEL_PACK_BUFFER_ARB );
			}
		}
		glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, 0 );
		rbHDRExposureReadbackIndex = readIndex;
	} else {
		GLfloat pixel[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		glReadPixels( 0, 0, 1, 1, GL_RGBA, GL_FLOAT, pixel );
		averageLogLuminance = pixel[0];
		haveLuminanceSample = true;
	}
	RB_RestorePostProcessTarget( originalRenderTexture, viewportWidth, viewportHeight );

	if ( !haveLuminanceSample ) {
		// first async frame: no completed sample yet, keep the current exposure
		return rbHDRExposureInitialized ? rbHDRAdaptedExposure : 1.0f;
	}

	if ( averageLogLuminance != averageLogLuminance ) {
		averageLogLuminance = 0.0f;
	}
	averageLogLuminance = idMath::ClampFloat( -16.0f, 16.0f, averageLogLuminance );

	const float averageLuminance = Max( idMath::Exp( averageLogLuminance ), 0.0001f );
	const float keyValue = r_hdrKeyValue.GetFloat();
	const float minExposure = Min( r_hdrMinExposure.GetFloat(), r_hdrMaxExposure.GetFloat() );
	const float maxExposure = Max( r_hdrMinExposure.GetFloat(), r_hdrMaxExposure.GetFloat() );
	const float targetExposure = idMath::ClampFloat( minExposure, maxExposure, keyValue / averageLuminance );
	const float now = backEnd.viewDef->floatTime;

	if ( !rbHDRExposureInitialized || now < rbHDRLastAdaptationTime || ( now - rbHDRLastAdaptationTime ) > 1.0f ) {
		rbHDRAdaptedExposure = targetExposure;
		rbHDRExposureInitialized = true;
	} else {
		const float deltaSeconds = Max( 0.0f, now - rbHDRLastAdaptationTime );
		const float adaptationSpeed = ( targetExposure > rbHDRAdaptedExposure ) ? r_hdrAdaptUpSpeed.GetFloat() : r_hdrAdaptDownSpeed.GetFloat();
		const float blend = idMath::ClampFloat( 0.0f, 1.0f, 1.0f - idMath::Exp( -adaptationSpeed * deltaSeconds ) );
		rbHDRAdaptedExposure += ( targetExposure - rbHDRAdaptedExposure ) * blend;
	}

	rbHDRLastAverageLuminance = averageLuminance;
	rbHDRLastTargetExposure = targetExposure;
	rbHDRLastAdaptationTime = now;
	return rbHDRAdaptedExposure;
}

static void RB_STD_Bloom( void ) {
	if ( r_skipPostProcess.GetBool() ) {
		return;
	}

	const bool bloomRequested = RB_PostProcessBloomRequested();
	const bool toneMapEnabled = r_hdrToneMap.GetBool();
	const int hdrDebugView = RB_HDRDebugViewValue();
	if ( !bloomRequested && !toneMapEnabled && hdrDebugView == 0 ) {
		return;
	}

	if ( !glConfig.GLSLProgramAvailable ) {
		return;
	}

	if ( !RB_IsMainScenePostProcessView() ) {
		return;
	}

	RB_InitBloomStages();
	if ( !R_ValidateGLSLProgram( &rbBloomCompositeStage ) ) {
		return;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	if ( sceneImage == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_Bloom ----------\n" );
	RB_CaptureCurrentRenderImage( viewportWidth, viewportHeight );

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 ) {
		return;
	}

	const GLfloat adaptedExposure = RB_HDRAutoExposureEnabled()
		? static_cast<GLfloat>( RB_UpdateHDRAutoExposure( sceneImage, viewportWidth, viewportHeight ) )
		: 1.0f;
	const GLfloat hdrExposure = r_hdrExposure.GetFloat() * adaptedExposure;
	const GLfloat hdrWhitePoint = r_hdrWhitePoint.GetFloat();
	const GLfloat hdrLift = r_hdrLift.GetFloat();
	const GLfloat hdrPostGamma = r_hdrPostGamma.GetFloat();
	const GLfloat hdrGain = r_hdrGain.GetFloat();
	const GLfloat hdrVibrance = r_hdrVibrance.GetFloat();
	const GLfloat hdrSaturation = r_hdrSaturation.GetFloat();
	const GLfloat hdrContrast = r_hdrContrast.GetFloat();
	const GLfloat hdrHighlightDesaturation = r_hdrHighlightDesaturation.GetFloat();
	const GLfloat hdrGamutCompression = r_hdrGamutCompression.GetFloat();
	const GLfloat bloomIntensity = bloomRequested ? r_bloomIntensity.GetFloat() : 0.0f;
	const GLfloat bloomRadius = Max( r_bloomRadius.GetFloat(), 0.1f );
	const GLfloat bloomThreshold = r_bloomThreshold.GetFloat();
	const GLfloat bloomSoftKnee = r_bloomSoftKnee.GetFloat();
	const GLfloat toneMapToggle = toneMapEnabled ? 1.0f : 0.0f;
	const int bloomLevelCount = idMath::ClampInt( 1, RB_BLOOM_MAX_LEVELS, r_bloomMipCount.GetInteger() );

	idImage *bloomImages[RB_BLOOM_MAX_LEVELS];
	GLfloat bloomWeights[RB_BLOOM_MAX_LEVELS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	for ( int i = 0; i < RB_BLOOM_MAX_LEVELS; i++ ) {
		bloomImages[i] = globalImages->blackImage;
	}

	idRenderTexture *originalRenderTexture = backEnd.renderTexture;
	bool bloomEnabled = false;

	if ( bloomRequested ) {
		RB_InitBloomStages();
		if ( R_ValidateGLSLProgram( &rbBloomExtractStage ) &&
			R_ValidateGLSLProgram( &rbBloomDownsampleStage ) &&
			R_ValidateGLSLProgram( &rbBloomBlurStage ) &&
			RB_EnsureBloomRenderTextures( viewportWidth, viewportHeight, bloomLevelCount ) ) {
			float weightSum = 0.0f;
			for ( int level = 0; level < bloomLevelCount; level++ ) {
				weightSum += RB_BLOOM_BASE_WEIGHTS[level];
			}
			if ( weightSum <= 0.0f ) {
				weightSum = 1.0f;
			}

			for ( int level = 0; level < bloomLevelCount; level++ ) {
				int bloomWidth = 0;
				int bloomHeight = 0;
				RB_GetBloomLevelSize( viewportWidth, viewportHeight, level, bloomWidth, bloomHeight );

				idImage *sourceImage = ( level == 0 ) ? sceneImage : rbBloomImages[level - 1][0];
				const int sourceWidth = ( level == 0 ) ? textureWidth : rbBloomRenderTextures[level - 1][0]->GetWidth();
				const int sourceHeight = ( level == 0 ) ? textureHeight : rbBloomRenderTextures[level - 1][0]->GetHeight();
				const GLfloat sourceInvTexSize[2] = {
					1.0f / static_cast<GLfloat>( Max( 1, sourceWidth ) ),
					1.0f / static_cast<GLfloat>( Max( 1, sourceHeight ) )
				};
				const GLfloat bloomInvTexSize[2] = {
					1.0f / static_cast<GLfloat>( Max( 1, bloomWidth ) ),
					1.0f / static_cast<GLfloat>( Max( 1, bloomHeight ) )
				};
				const GLfloat blurRadiusForLevel = bloomRadius * ( 1.0f + static_cast<GLfloat>( level ) * 0.65f );

				RB_BindPostProcessRenderTexture( rbBloomRenderTextures[level][0], bloomWidth, bloomHeight );
				RB_BeginFullscreenPostProcessPass( 0, 0, bloomWidth, bloomHeight );
				GL_SelectTexture( 0 );
				sourceImage->Bind();
				glUseProgramObjectARB( (GLhandleARB)( ( level == 0 ) ? rbBloomExtractStage.glslProgramObject : rbBloomDownsampleStage.glslProgramObject ) );
				if ( level == 0 ) {
					if ( rbBloomExtractStage.shaderTextureLocations[0] >= 0 ) {
						glUniform1iARB( rbBloomExtractStage.shaderTextureLocations[0], 0 );
					}
					if ( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_INV_TEX_SIZE] >= 0 ) {
						glUniform2fvARB( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_INV_TEX_SIZE], 1, sourceInvTexSize );
					}
					if ( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_THRESHOLD] >= 0 ) {
						glUniform1fARB( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_THRESHOLD], bloomThreshold );
					}
					if ( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_SOFT_KNEE] >= 0 ) {
						glUniform1fARB( rbBloomExtractStage.shaderParmLocations[RB_BLOOM_EXTRACT_UNIFORM_SOFT_KNEE], bloomSoftKnee );
					}
				} else {
					if ( rbBloomDownsampleStage.shaderTextureLocations[0] >= 0 ) {
						glUniform1iARB( rbBloomDownsampleStage.shaderTextureLocations[0], 0 );
					}
					if ( rbBloomDownsampleStage.shaderParmLocations[RB_BLOOM_DOWNSAMPLE_UNIFORM_INV_TEX_SIZE] >= 0 ) {
						glUniform2fvARB( rbBloomDownsampleStage.shaderParmLocations[RB_BLOOM_DOWNSAMPLE_UNIFORM_INV_TEX_SIZE], 1, sourceInvTexSize );
					}
				}
				RB_DrawFullscreenPostProcessQuadUnitUV();
				glUseProgramObjectARB( 0 );
				RB_EndFullscreenPostProcessPass();

				RB_BindPostProcessRenderTexture( rbBloomRenderTextures[level][1], bloomWidth, bloomHeight );
				RB_BeginFullscreenPostProcessPass( 0, 0, bloomWidth, bloomHeight );
				GL_SelectTexture( 0 );
				rbBloomImages[level][0]->Bind();
				glUseProgramObjectARB( (GLhandleARB)rbBloomBlurStage.glslProgramObject );
				if ( rbBloomBlurStage.shaderTextureLocations[0] >= 0 ) {
					glUniform1iARB( rbBloomBlurStage.shaderTextureLocations[0], 0 );
				}
				if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_INV_TEX_SIZE] >= 0 ) {
					glUniform2fvARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_INV_TEX_SIZE], 1, bloomInvTexSize );
				}
				if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_AXIS] >= 0 ) {
					const GLfloat blurAxisX[2] = { 1.0f, 0.0f };
					glUniform2fvARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_AXIS], 1, blurAxisX );
				}
				if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS] >= 0 ) {
					glUniform1fARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS], blurRadiusForLevel );
				}
				RB_DrawFullscreenPostProcessQuadUnitUV();
				glUseProgramObjectARB( 0 );
				RB_EndFullscreenPostProcessPass();

				RB_BindPostProcessRenderTexture( rbBloomRenderTextures[level][0], bloomWidth, bloomHeight );
				RB_BeginFullscreenPostProcessPass( 0, 0, bloomWidth, bloomHeight );
				GL_SelectTexture( 0 );
				rbBloomImages[level][1]->Bind();
				glUseProgramObjectARB( (GLhandleARB)rbBloomBlurStage.glslProgramObject );
				if ( rbBloomBlurStage.shaderTextureLocations[0] >= 0 ) {
					glUniform1iARB( rbBloomBlurStage.shaderTextureLocations[0], 0 );
				}
				if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_INV_TEX_SIZE] >= 0 ) {
					glUniform2fvARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_INV_TEX_SIZE], 1, bloomInvTexSize );
				}
				if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_AXIS] >= 0 ) {
					const GLfloat blurAxisY[2] = { 0.0f, 1.0f };
					glUniform2fvARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_AXIS], 1, blurAxisY );
				}
				if ( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS] >= 0 ) {
					glUniform1fARB( rbBloomBlurStage.shaderParmLocations[RB_BLOOM_BLUR_UNIFORM_BLUR_RADIUS], blurRadiusForLevel );
				}
				RB_DrawFullscreenPostProcessQuadUnitUV();
				glUseProgramObjectARB( 0 );
				RB_EndFullscreenPostProcessPass();

				bloomImages[level] = rbBloomImages[level][0];
				bloomWeights[level] = RB_BLOOM_BASE_WEIGHTS[level] / weightSum;
			}

			bloomEnabled = true;
		}
	}

	RB_RestorePostProcessTarget( originalRenderTexture, viewportWidth, viewportHeight );
	RB_BeginFullscreenPostProcessPass(
		backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );

	GL_SelectTexture( 0 );
	sceneImage->Bind();
	for ( int level = 0; level < RB_BLOOM_MAX_LEVELS; level++ ) {
		GL_SelectTexture( level + 1 );
		bloomImages[level]->Bind();
	}
	GL_SelectTexture( 0 );

	glUseProgramObjectARB( (GLhandleARB)rbBloomCompositeStage.glslProgramObject );
	for ( int i = 0; i < rbBloomCompositeStage.numShaderTextures; i++ ) {
		if ( rbBloomCompositeStage.shaderTextureLocations[i] >= 0 ) {
			glUniform1iARB( rbBloomCompositeStage.shaderTextureLocations[i], i );
		}
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_INTENSITY] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_INTENSITY], bloomIntensity );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_ENABLED] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_ENABLED], bloomEnabled ? 1.0f : 0.0f );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_TONEMAP_ENABLED] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_TONEMAP_ENABLED], toneMapToggle );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_EXPOSURE] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_EXPOSURE], hdrExposure );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_WHITE_POINT] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_WHITE_POINT], hdrWhitePoint );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_LIFT] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_LIFT], hdrLift );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_POST_GAMMA] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_POST_GAMMA], hdrPostGamma );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAIN] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAIN], hdrGain );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_VIBRANCE] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_VIBRANCE], hdrVibrance );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_SATURATION] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_SATURATION], hdrSaturation );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_CONTRAST] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_CONTRAST], hdrContrast );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_HIGHLIGHT_DESATURATION] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_HIGHLIGHT_DESATURATION], hdrHighlightDesaturation );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAMUT_COMPRESSION] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_GAMUT_COMPRESSION], hdrGamutCompression );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_DEBUG_VIEW] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_HDR_DEBUG_VIEW], static_cast<GLfloat>( hdrDebugView ) );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT0] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT0], bloomWeights[0] );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT1] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT1], bloomWeights[1] );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT2] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT2], bloomWeights[2] );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT3] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT3], bloomWeights[3] );
	}
	if ( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT4] >= 0 ) {
		glUniform1fARB( rbBloomCompositeStage.shaderParmLocations[RB_BLOOM_COMPOSITE_UNIFORM_BLOOM_WEIGHT4], bloomWeights[4] );
	}

	if ( originalRenderTexture == NULL ) {
		RB_SetFramebufferSRGBEnabled( true );
	}
	RB_DrawFullscreenPostProcessQuad( viewportWidth, viewportHeight, textureWidth, textureHeight );
	if ( originalRenderTexture == NULL ) {
		RB_SetFramebufferSRGBEnabled( false );
	}
	glUseProgramObjectARB( 0 );
	for ( int level = RB_BLOOM_MAX_LEVELS; level >= 1; level-- ) {
		GL_SelectTexture( level );
		globalImages->BindNull();
	}
	GL_SelectTexture( 0 );
	RB_EndFullscreenPostProcessPass();

	if ( originalRenderTexture != NULL ) {
		RB_CaptureCurrentRenderImage( viewportWidth, viewportHeight );
	}
}

static void RB_FreeSceneDepthAwarePresentProgram( void ) {
	if ( rbSceneDepthAwarePresentProgram != 0 && glConfig.isInitialized ) {
		if ( rbSceneDepthAwarePresentVertexShader != 0 ) {
			glDetachObjectARB( rbSceneDepthAwarePresentProgram, rbSceneDepthAwarePresentVertexShader );
		}
		if ( rbSceneDepthAwarePresentFragmentShader != 0 ) {
			glDetachObjectARB( rbSceneDepthAwarePresentProgram, rbSceneDepthAwarePresentFragmentShader );
		}
		glDeleteObjectARB( rbSceneDepthAwarePresentProgram );
	}
	if ( rbSceneDepthAwarePresentVertexShader != 0 && glConfig.isInitialized ) {
		glDeleteObjectARB( rbSceneDepthAwarePresentVertexShader );
	}
	if ( rbSceneDepthAwarePresentFragmentShader != 0 && glConfig.isInitialized ) {
		glDeleteObjectARB( rbSceneDepthAwarePresentFragmentShader );
	}
	rbSceneDepthAwarePresentProgram = 0;
	rbSceneDepthAwarePresentVertexShader = 0;
	rbSceneDepthAwarePresentFragmentShader = 0;
	rbSceneDepthAwarePresentGeneration = -1;
	rbSceneDepthAwarePresentSceneLocation = -1;
	rbSceneDepthAwarePresentDepthLocation = -1;
}

static bool RB_EnsureSceneDepthAwarePresentProgram( void ) {
	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}
	if ( rbSceneDepthAwarePresentProgram != 0 && rbSceneDepthAwarePresentGeneration == tr.videoRestartCount ) {
		return true;
	}

	RB_FreeSceneDepthAwarePresentProgram();

	static const char *vertexSource =
		"void main() {\n"
		"	gl_Position = ftransform();\n"
		"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
		"}\n";
	static const char *fragmentSource =
		"uniform sampler2D Scene;\n"
		"uniform sampler2D DepthBuffer;\n"
		"void main() {\n"
		"	vec2 uv = gl_TexCoord[0].st;\n"
		"	if ( texture2D( DepthBuffer, uv ).r >= 0.99999 ) {\n"
		"		discard;\n"
		"	}\n"
		"	gl_FragColor = texture2D( Scene, uv );\n"
		"}\n";

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	if ( vertexShader == 0 || fragmentShader == 0 ) {
		if ( vertexShader != 0 ) {
			glDeleteObjectARB( vertexShader );
		}
		if ( fragmentShader != 0 ) {
			glDeleteObjectARB( fragmentShader );
		}
		return false;
	}

	const GLcharARB *vertexSourceARB = (const GLcharARB *)vertexSource;
	const GLcharARB *fragmentSourceARB = (const GLcharARB *)fragmentSource;
	glShaderSourceARB( vertexShader, 1, &vertexSourceARB, NULL );
	glShaderSourceARB( fragmentShader, 1, &fragmentSourceARB, NULL );
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_PrintGLSLInfoLog( vertexShader, "vertex shader compile", "scene depth-aware present" );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		return false;
	}
	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_PrintGLSLInfoLog( fragmentShader, "fragment shader compile", "scene depth-aware present" );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	if ( programObject == 0 ) {
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		return false;
	}
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_PrintGLSLInfoLog( programObject, "program link", "scene depth-aware present" );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		return false;
	}

	rbSceneDepthAwarePresentProgram = programObject;
	rbSceneDepthAwarePresentVertexShader = vertexShader;
	rbSceneDepthAwarePresentFragmentShader = fragmentShader;
	rbSceneDepthAwarePresentGeneration = tr.videoRestartCount;
	rbSceneDepthAwarePresentSceneLocation = glGetUniformLocationARB( programObject, "Scene" );
	rbSceneDepthAwarePresentDepthLocation = glGetUniformLocationARB( programObject, "DepthBuffer" );

	if ( rbSceneDepthAwarePresentSceneLocation < 0 || rbSceneDepthAwarePresentDepthLocation < 0 ) {
		common->Warning( "scene depth-aware present shader is missing required sampler uniforms" );
		RB_FreeSceneDepthAwarePresentProgram();
		return false;
	}

	return true;
}

static void RB_PresentSceneRenderTargetToBackBuffer( const rbSceneScaleState_t &scaleState ) {
	if ( !RB_IsSceneRenderTexture( backEnd.renderTexture ) || backEnd.viewDef == NULL ) {
		return;
	}

	const int sourceViewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int sourceViewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	const idScreenRect targetViewport = scaleState.active ? scaleState.nativeViewport : backEnd.viewDef->viewport;
	const idScreenRect targetScissor = scaleState.active ? scaleState.nativeScissor : backEnd.viewDef->scissor;
	const int targetViewportWidth = targetViewport.x2 - targetViewport.x1 + 1;
	const int targetViewportHeight = targetViewport.y2 - targetViewport.y1 + 1;
	if ( sourceViewportWidth <= 0 || sourceViewportHeight <= 0 || targetViewportWidth <= 0 || targetViewportHeight <= 0 ) {
		return;
	}

	idImage *presentImage = NULL;
	idImage *copyImage = globalImages->currentRenderImage;
	idImage *sceneColorImage = backEnd.renderTexture->GetNumColorImages() > 0
		? backEnd.renderTexture->GetColorImage( 0 )
		: NULL;

	const bool canPresentSceneColorDirectly =
		sceneColorImage != NULL &&
		sceneColorImage->GetOpts().numMSAASamples <= 1 &&
		backEnd.viewDef->viewport.x1 == 0 &&
		backEnd.viewDef->viewport.y1 == 0;

	if ( canPresentSceneColorDirectly ) {
		presentImage = sceneColorImage;
	} else if ( copyImage != NULL ) {
		RB_CaptureCurrentRenderImage( sourceViewportWidth, sourceViewportHeight );
		presentImage = copyImage;
	}

	if ( presentImage == NULL ) {
		return;
	}

	const int textureWidth = presentImage->GetOpts().width;
	const int textureHeight = presentImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 ) {
		return;
	}

	bool preserveFarDepth = RB_ShouldPreserveSceneRenderTargetFarDepth( backEnd.viewDef );
	idImage *presentDepthImage = NULL;
	if ( preserveFarDepth && globalImages != NULL && RB_EnsureSceneDepthAwarePresentProgram() ) {
		if ( rbSceneRenderTargetPreserveDepthFrame == backEnd.frameCount
			&& rbSceneRenderTargetPreserveDepthWidth == sourceViewportWidth
			&& rbSceneRenderTargetPreserveDepthHeight == sourceViewportHeight
			&& rbSceneRenderTargetPreserveDepthImage != NULL ) {
			presentDepthImage = rbSceneRenderTargetPreserveDepthImage;
		} else {
			presentDepthImage = globalImages->currentDepthImage;
		}
		if ( presentDepthImage != NULL && presentDepthImage == globalImages->currentDepthImage ) {
			RB_CaptureCurrentDepthImage( sourceViewportWidth, sourceViewportHeight );
		}
		if ( presentDepthImage != NULL ) {
			const int depthTextureWidth = presentDepthImage->GetOpts().width;
			const int depthTextureHeight = presentDepthImage->GetOpts().height;
			if ( depthTextureWidth <= 0 || depthTextureHeight <= 0 ) {
				presentDepthImage = NULL;
			}
		}
	} else {
		preserveFarDepth = false;
	}
	preserveFarDepth = preserveFarDepth && presentDepthImage != NULL;

	idRenderTexture::BindNull();
	backEnd.renderTexture = NULL;
	glDrawBuffer( GL_BACK );
	glReadBuffer( GL_BACK );
	glViewport(
		tr.viewportOffset[0] + targetViewport.x1,
		tr.viewportOffset[1] + targetViewport.y1,
		targetViewportWidth,
		targetViewportHeight );
	glScissor(
		tr.viewportOffset[0] + targetViewport.x1 + targetScissor.x1,
		tr.viewportOffset[1] + targetViewport.y1 + targetScissor.y1,
		targetScissor.x2 - targetScissor.x1 + 1,
		targetScissor.y2 - targetScissor.y1 + 1 );
	backEnd.currentScissor = targetScissor;

	RB_BeginFullscreenPostProcessPass(
		targetViewport.x1 + targetScissor.x1,
		targetViewport.y1 + targetScissor.y1,
		targetScissor.x2 - targetScissor.x1 + 1,
		targetScissor.y2 - targetScissor.y1 + 1 );
	GL_SelectTexture( 0 );
	presentImage->Bind();
	GL_TexEnv( GL_MODULATE );

	RB_SetFramebufferSRGBEnabled( true );
	if ( preserveFarDepth ) {
		GL_SelectTexture( 1 );
		presentDepthImage->Bind();
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
		glTexParameteri( GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE );
		GL_SelectTexture( 0 );
		glUseProgramObjectARB( rbSceneDepthAwarePresentProgram );
		glUniform1iARB( rbSceneDepthAwarePresentSceneLocation, 0 );
		glUniform1iARB( rbSceneDepthAwarePresentDepthLocation, 1 );
	}
	RB_DrawFullscreenPostProcessQuad( sourceViewportWidth, sourceViewportHeight, textureWidth, textureHeight );
	if ( preserveFarDepth ) {
		glUseProgramObjectARB( 0 );
		GL_SelectTexture( 1 );
		globalImages->BindNull();
		GL_SelectTexture( 0 );
	}
	RB_SetFramebufferSRGBEnabled( false );

	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();
}

enum rbResolutionScaleUniformIndex_t {
	RB_RES_SCALE_UNIFORM_INV_TEX_SIZE = 0,
	RB_RES_SCALE_UNIFORM_INV_LOW_RES_SIZE,
	RB_RES_SCALE_UNIFORM_SHARPEN_AMOUNT,
	RB_RES_SCALE_UNIFORM_COUNT
};

static newShaderStage_t rbResolutionScaleStage;
static bool rbResolutionScaleStageInitialized = false;

static void RB_InitResolutionScaleStage( void ) {
	if ( rbResolutionScaleStageInitialized ) {
		return;
	}

	memset( &rbResolutionScaleStage, 0, sizeof( rbResolutionScaleStage ) );
	rbResolutionScaleStage.glslProgram = true;
	idStr::Copynz( rbResolutionScaleStage.glslProgramName, "resolutionscale.fs", sizeof( rbResolutionScaleStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_RES_SCALE_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "invLowResSize", 2 },
		{ "sharpenAmount", 1 }
	};

	rbResolutionScaleStage.numShaderParms = RB_RES_SCALE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_RES_SCALE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbResolutionScaleStage.shaderParmNames[i], uniforms[i].name, sizeof( rbResolutionScaleStage.shaderParmNames[i] ) );
		rbResolutionScaleStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbResolutionScaleStage.numShaderTextures = 1;
	idStr::Copynz( rbResolutionScaleStage.shaderTextureNames[0], "Scene", sizeof( rbResolutionScaleStage.shaderTextureNames[0] ) );

	rbResolutionScaleStageInitialized = true;
}

void RB_ApplyResolutionScaleToBackBuffer( void ) {
	if ( r_skipPostProcess.GetBool() ) {
		return;
	}

	const int scalePercent = RB_RequestedScreenFraction();
	if ( scalePercent >= RB_SCREEN_FRACTION_NATIVE ) {
		return;
	}

	int mode = idMath::ClampInt( 0, 2, r_resolutionScaleMode.GetInteger() );
	if ( mode == 0 ) {
		// Legacy path: BeginFrame crop mode without fullscreen upscale.
		return;
	}

	const int viewportWidth = glConfig.vidWidth;
	const int viewportHeight = glConfig.vidHeight;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	const int sourceWidth = idMath::ClampInt( 1, viewportWidth,
		idMath::Ftoi( static_cast<float>( viewportWidth ) * ( static_cast<float>( scalePercent ) * 0.01f ) + 0.5f ) );
	const int sourceHeight = idMath::ClampInt( 1, viewportHeight,
		idMath::Ftoi( static_cast<float>( viewportHeight ) * ( static_cast<float>( scalePercent ) * 0.01f ) + 0.5f ) );
	if ( sourceWidth <= 0 || sourceHeight <= 0 ) {
		return;
	}

	if ( !glConfig.GLSLProgramAvailable ) {
		return;
	}

	RB_InitResolutionScaleStage();
	if ( !R_ValidateGLSLProgram( &rbResolutionScaleStage ) ) {
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	if ( sceneImage == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_ApplyResolutionScaleToBackBuffer ----------\n" );

	idRenderTexture::BindNull();
	backEnd.renderTexture = NULL;
	glDrawBuffer( GL_BACK );
	glReadBuffer( GL_BACK );
	glViewport( 0, 0, viewportWidth, viewportHeight );
	glScissor( 0, 0, viewportWidth, viewportHeight );

	// Copy the full back buffer; the resolution-scale shader samples this image
	// on a reduced grid so output always fills the screen.
	sceneImage->CopyFramebuffer( 0, 0, viewportWidth, viewportHeight );

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 ) {
		return;
	}

	RB_BeginFullscreenPostProcessPass( 0, 0, viewportWidth, viewportHeight );
	GL_SelectTexture( 0 );
	sceneImage->Bind();
	GL_TexEnv( GL_MODULATE );

	glUseProgramObjectARB( (GLhandleARB)rbResolutionScaleStage.glslProgramObject );

	const int sceneLocation = rbResolutionScaleStage.shaderTextureLocations[0];
	if ( sceneLocation >= 0 ) {
		glUniform1iARB( sceneLocation, 0 );
	}

	const GLfloat invTexSize[2] = {
		1.0f / static_cast<GLfloat>( textureWidth ),
		1.0f / static_cast<GLfloat>( textureHeight )
	};
	const GLfloat invLowResSize[2] = {
		1.0f / static_cast<GLfloat>( sourceWidth ),
		1.0f / static_cast<GLfloat>( sourceHeight )
	};
	const GLfloat sharpenAmount = ( mode == 2 )
		? idMath::ClampFloat( 0.0f, 1.5f, r_resolutionScaleSharpness.GetFloat() )
		: 0.0f;

	if ( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_INV_TEX_SIZE] >= 0 ) {
		glUniform2fvARB( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_INV_TEX_SIZE], 1, invTexSize );
	}
	if ( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_INV_LOW_RES_SIZE] >= 0 ) {
		glUniform2fvARB( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_INV_LOW_RES_SIZE], 1, invLowResSize );
	}
	if ( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_SHARPEN_AMOUNT] >= 0 ) {
		glUniform1fARB( rbResolutionScaleStage.shaderParmLocations[RB_RES_SCALE_UNIFORM_SHARPEN_AMOUNT], sharpenAmount );
	}

	RB_DrawFullscreenPostProcessQuadUnitUV();
	glUseProgramObjectARB( 0 );
	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();
}

enum rbCRTUniformIndex_t {
	RB_CRT_UNIFORM_INV_TEX_SIZE = 0,
	RB_CRT_UNIFORM_AMOUNT,
	RB_CRT_UNIFORM_SCANLINE_STRENGTH,
	RB_CRT_UNIFORM_MASK_STRENGTH,
	RB_CRT_UNIFORM_CURVATURE,
	RB_CRT_UNIFORM_CHROMATIC_ABERRATION,
	RB_CRT_UNIFORM_TIME_SECONDS,
	RB_CRT_UNIFORM_COUNT
};

static newShaderStage_t rbCRTStage;
static bool rbCRTStageInitialized = false;

static void RB_InitCRTStage( void ) {
	if ( rbCRTStageInitialized ) {
		return;
	}

	memset( &rbCRTStage, 0, sizeof( rbCRTStage ) );
	rbCRTStage.glslProgram = true;
	idStr::Copynz( rbCRTStage.glslProgramName, "crt.fs", sizeof( rbCRTStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_CRT_UNIFORM_COUNT] = {
		{ "invTexSize", 2 },
		{ "crtAmount", 1 },
		{ "scanlineStrength", 1 },
		{ "maskStrength", 1 },
		{ "curvature", 1 },
		{ "chromaticAberration", 1 },
		{ "timeSeconds", 1 }
	};

	rbCRTStage.numShaderParms = RB_CRT_UNIFORM_COUNT;
	for ( int i = 0; i < RB_CRT_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbCRTStage.shaderParmNames[i], uniforms[i].name, sizeof( rbCRTStage.shaderParmNames[i] ) );
		rbCRTStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbCRTStage.numShaderTextures = 1;
	idStr::Copynz( rbCRTStage.shaderTextureNames[0], "Scene", sizeof( rbCRTStage.shaderTextureNames[0] ) );

	rbCRTStageInitialized = true;
}

void RB_ApplyCRTToBackBuffer( void ) {
	if ( r_skipPostProcess.GetBool() || !r_crt.GetBool() ) {
		return;
	}

	const GLfloat amount = idMath::ClampFloat( 0.0f, 1.0f, r_crtAmount.GetFloat() );
	if ( amount <= 0.001f ) {
		return;
	}

	if ( !glConfig.GLSLProgramAvailable ) {
		return;
	}

	RB_InitCRTStage();
	if ( !R_ValidateGLSLProgram( &rbCRTStage ) ) {
		return;
	}

	const int viewportWidth = glConfig.vidWidth;
	const int viewportHeight = glConfig.vidHeight;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	if ( sceneImage == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_ApplyCRTToBackBuffer ----------\n" );

	idRenderTexture::BindNull();
	backEnd.renderTexture = NULL;
	glDrawBuffer( GL_BACK );
	glReadBuffer( GL_BACK );
	glViewport( 0, 0, viewportWidth, viewportHeight );
	glScissor( 0, 0, viewportWidth, viewportHeight );

	sceneImage->CopyFramebuffer( 0, 0, viewportWidth, viewportHeight );

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 ) {
		return;
	}

	RB_BeginFullscreenPostProcessPass( 0, 0, viewportWidth, viewportHeight );
	GL_SelectTexture( 0 );
	sceneImage->Bind();

	glUseProgramObjectARB( (GLhandleARB)rbCRTStage.glslProgramObject );

	const int sceneLocation = rbCRTStage.shaderTextureLocations[0];
	if ( sceneLocation >= 0 ) {
		glUniform1iARB( sceneLocation, 0 );
	}

	const GLfloat invTexSize[2] = {
		1.0f / static_cast<GLfloat>( textureWidth ),
		1.0f / static_cast<GLfloat>( textureHeight )
	};
	const GLfloat scanlineStrength = idMath::ClampFloat( 0.0f, 1.0f, r_crtScanlineStrength.GetFloat() );
	const GLfloat maskStrength = idMath::ClampFloat( 0.0f, 1.0f, r_crtMaskStrength.GetFloat() );
	const GLfloat curvature = idMath::ClampFloat( 0.0f, 0.25f, r_crtCurvature.GetFloat() );
	const GLfloat chromaticAberration = idMath::ClampFloat( 0.0f, 0.35f, r_crtChromatic.GetFloat() );
	const GLfloat timeSeconds = static_cast<GLfloat>( backEnd.frameCount ) * ( 1.0f / 60.0f );

	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_INV_TEX_SIZE] >= 0 ) {
		glUniform2fvARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_INV_TEX_SIZE], 1, invTexSize );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_AMOUNT] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_AMOUNT], amount );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_SCANLINE_STRENGTH] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_SCANLINE_STRENGTH], scanlineStrength );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_MASK_STRENGTH] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_MASK_STRENGTH], maskStrength );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_CURVATURE] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_CURVATURE], curvature );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_CHROMATIC_ABERRATION] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_CHROMATIC_ABERRATION], chromaticAberration );
	}
	if ( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_TIME_SECONDS] >= 0 ) {
		glUniform1fARB( rbCRTStage.shaderParmLocations[RB_CRT_UNIFORM_TIME_SECONDS], timeSeconds );
	}

	RB_DrawFullscreenPostProcessQuad( viewportWidth, viewportHeight, textureWidth, textureHeight );
	glUseProgramObjectARB( 0 );
	RB_EndFullscreenPostProcessPass();
}

static GLhandleARB rbColorMappingProgram = 0;
static GLhandleARB rbColorMappingVertexShader = 0;
static GLhandleARB rbColorMappingFragmentShader = 0;
static int rbColorMappingProgramGeneration = 0;
static GLint rbColorMappingSceneLocation = -1;
static GLint rbColorMappingBrightnessLocation = -1;
static GLint rbColorMappingGammaLocation = -1;

static void RB_FreeColorMappingProgram( void ) {
	if ( rbColorMappingProgram != 0 && glConfig.isInitialized && rbColorMappingProgramGeneration == tr.glContextGeneration ) {
		if ( rbColorMappingVertexShader != 0 ) {
			glDetachObjectARB( rbColorMappingProgram, rbColorMappingVertexShader );
			glDeleteObjectARB( rbColorMappingVertexShader );
		}
		if ( rbColorMappingFragmentShader != 0 ) {
			glDetachObjectARB( rbColorMappingProgram, rbColorMappingFragmentShader );
			glDeleteObjectARB( rbColorMappingFragmentShader );
		}
		glDeleteObjectARB( rbColorMappingProgram );
	}

	rbColorMappingProgram = 0;
	rbColorMappingVertexShader = 0;
	rbColorMappingFragmentShader = 0;
	rbColorMappingProgramGeneration = 0;
	rbColorMappingSceneLocation = -1;
	rbColorMappingBrightnessLocation = -1;
	rbColorMappingGammaLocation = -1;
}

static bool RB_EnsureColorMappingProgram( void ) {
	if ( rbColorMappingProgram != 0 && rbColorMappingProgramGeneration == tr.glContextGeneration ) {
		return true;
	}

	RB_FreeColorMappingProgram();

	if ( !glConfig.GLSLProgramAvailable ) {
		return false;
	}

	static const char *colorMappingVertexSource =
		"void main() {\n"
		"	gl_Position = ftransform();\n"
		"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
		"}\n";
	static const char *colorMappingFragmentSource =
		"uniform sampler2D Scene;\n"
		"uniform float brightness;\n"
		"uniform float gamma;\n"
		"\n"
		"void main() {\n"
		"	vec4 sampleColor = texture2D( Scene, gl_TexCoord[0].st );\n"
		"	vec3 color = clamp( sampleColor.rgb * brightness, 0.0, 1.0 );\n"
		"	float safeGamma = max( gamma, 0.001 );\n"
		"	color = pow( color, vec3( 1.0 / safeGamma ) );\n"
		"	gl_FragColor = vec4( color, sampleColor.a );\n"
		"}\n";

	GLhandleARB vertexShader = glCreateShaderObjectARB( GL_VERTEX_SHADER_ARB );
	GLhandleARB fragmentShader = glCreateShaderObjectARB( GL_FRAGMENT_SHADER_ARB );
	if ( vertexShader == 0 || fragmentShader == 0 ) {
		if ( vertexShader != 0 ) {
			glDeleteObjectARB( vertexShader );
		}
		if ( fragmentShader != 0 ) {
			glDeleteObjectARB( fragmentShader );
		}
		return false;
	}

	const GLcharARB *vertexSource = (const GLcharARB *)colorMappingVertexSource;
	const GLcharARB *fragmentSource = (const GLcharARB *)colorMappingFragmentSource;
	glShaderSourceARB( vertexShader, 1, &vertexSource, NULL );
	glShaderSourceARB( fragmentShader, 1, &fragmentSource, NULL );
	glCompileShaderARB( vertexShader );
	glCompileShaderARB( fragmentShader );

	GLint status = GL_FALSE;
	glGetObjectParameterivARB( vertexShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_PrintGLSLInfoLog( vertexShader, "vertex shader compile", "builtin/final_color_mapping" );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		return false;
	}

	glGetObjectParameterivARB( fragmentShader, GL_OBJECT_COMPILE_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_PrintGLSLInfoLog( fragmentShader, "fragment shader compile", "builtin/final_color_mapping" );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		return false;
	}

	GLhandleARB programObject = glCreateProgramObjectARB();
	glAttachObjectARB( programObject, vertexShader );
	glAttachObjectARB( programObject, fragmentShader );
	glLinkProgramARB( programObject );

	glGetObjectParameterivARB( programObject, GL_OBJECT_LINK_STATUS_ARB, &status );
	if ( status == GL_FALSE ) {
		RB_PrintGLSLInfoLog( programObject, "program link", "builtin/final_color_mapping" );
		glDetachObjectARB( programObject, vertexShader );
		glDetachObjectARB( programObject, fragmentShader );
		glDeleteObjectARB( vertexShader );
		glDeleteObjectARB( fragmentShader );
		glDeleteObjectARB( programObject );
		return false;
	}

	rbColorMappingProgram = programObject;
	rbColorMappingVertexShader = vertexShader;
	rbColorMappingFragmentShader = fragmentShader;
	rbColorMappingProgramGeneration = tr.glContextGeneration;
	rbColorMappingSceneLocation = glGetUniformLocationARB( programObject, "Scene" );
	rbColorMappingBrightnessLocation = glGetUniformLocationARB( programObject, "brightness" );
	rbColorMappingGammaLocation = glGetUniformLocationARB( programObject, "gamma" );
	if ( rbColorMappingSceneLocation < 0 || rbColorMappingBrightnessLocation < 0 || rbColorMappingGammaLocation < 0 ) {
		common->Warning( "GLSL builtin/final_color_mapping is missing required uniforms" );
		RB_FreeColorMappingProgram();
		return false;
	}

	common->Printf( "Loaded built-in GLSL program 'builtin/final_color_mapping'\n" );
	return true;
}

static bool RB_ColorMappingsAreNeutral( float brightness, float gamma ) {
	return idMath::Fabs( brightness - 1.0f ) <= 0.0001f
		&& idMath::Fabs( gamma - 1.0f ) <= 0.0001f;
}

void RB_ApplyColorMappingsToBackBuffer( void ) {
	if ( GLimp_UseNativeGammaRamps() ) {
		return;
	}

	const GLfloat brightness = idMath::ClampFloat( 0.0f, 16.0f, r_brightness.GetFloat() );
	const GLfloat gamma = Max( r_gamma.GetFloat(), 0.001f );
	if ( RB_ColorMappingsAreNeutral( brightness, gamma ) ) {
		return;
	}

	if ( !glConfig.GLSLProgramAvailable ) {
		static bool warned = false;
		if ( !warned ) {
			common->Warning( "r_brightness/r_gamma require GLSL on this platform backend because native gamma ramps are unavailable" );
			warned = true;
		}
		return;
	}

	if ( !RB_EnsureColorMappingProgram() ) {
		return;
	}

	const int viewportWidth = glConfig.vidWidth;
	const int viewportHeight = glConfig.vidHeight;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	idImage *sceneImage = globalImages->currentRenderImage;
	if ( sceneImage == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_ApplyColorMappingsToBackBuffer ----------\n" );

	idRenderTexture::BindNull();
	backEnd.renderTexture = NULL;
	glDrawBuffer( GL_BACK );
	glReadBuffer( GL_BACK );
	glViewport( 0, 0, viewportWidth, viewportHeight );
	glScissor( 0, 0, viewportWidth, viewportHeight );

	sceneImage->CopyFramebuffer( 0, 0, viewportWidth, viewportHeight );

	const int textureWidth = sceneImage->GetOpts().width;
	const int textureHeight = sceneImage->GetOpts().height;
	if ( textureWidth <= 0 || textureHeight <= 0 ) {
		return;
	}

	RB_BeginFullscreenPostProcessPass( 0, 0, viewportWidth, viewportHeight );
	GL_SelectTexture( 0 );
	sceneImage->Bind();
	GL_TexEnv( GL_MODULATE );

	glUseProgramObjectARB( rbColorMappingProgram );
	glUniform1iARB( rbColorMappingSceneLocation, 0 );
	glUniform1fARB( rbColorMappingBrightnessLocation, brightness );
	glUniform1fARB( rbColorMappingGammaLocation, gamma );

	RB_DrawFullscreenPostProcessQuad( viewportWidth, viewportHeight, textureWidth, textureHeight );
	glUseProgramObjectARB( 0 );
	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();
}

/*
=====================
RB_BakeTextureMatrixIntoTexgen
=====================
*/
void RB_BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float *textureMatrix ) {
	float	genMatrix[16];
	float	final[16];

	genMatrix[0] = lightProject[0][0];
	genMatrix[4] = lightProject[0][1];
	genMatrix[8] = lightProject[0][2];
	genMatrix[12] = lightProject[0][3];

	genMatrix[1] = lightProject[1][0];
	genMatrix[5] = lightProject[1][1];
	genMatrix[9] = lightProject[1][2];
	genMatrix[13] = lightProject[1][3];

	genMatrix[2] = 0;
	genMatrix[6] = 0;
	genMatrix[10] = 0;
	genMatrix[14] = 0;

	genMatrix[3] = lightProject[2][0];
	genMatrix[7] = lightProject[2][1];
	genMatrix[11] = lightProject[2][2];
	genMatrix[15] = lightProject[2][3];

	myGlMultMatrix( genMatrix, backEnd.lightTextureMatrix, final );

	lightProject[0][0] = final[0];
	lightProject[0][1] = final[4];
	lightProject[0][2] = final[8];
	lightProject[0][3] = final[12];

	lightProject[1][0] = final[1];
	lightProject[1][1] = final[5];
	lightProject[1][2] = final[9];
	lightProject[1][3] = final[13];
}

/*
================
RB_PrepareStageTexturing
================
*/
static bool RB_PrepareStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf, idDrawVert *ac,
	bool fillingDepth ) {
	if ( R_TriHasPrimBatchMesh( surf->geo ) ) {
		if ( tr.backEndRenderer == BE_ARB2 ) {
			RB_ARB2_PrepareStageTexturing( pStage, surf, fillingDepth );
		}
		return true;
	}

	(void)fillingDepth;

	// set privatePolygonOffset if necessary
	if ( pStage->privatePolygonOffset ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset );
	}

	// set the texture matrix if needed
	if ( pStage->texture.hasMatrix ) {
		RB_LoadShaderTextureMatrix( surf->shaderRegisters, &pStage->texture );
	}

	// texgens
	if ( pStage->texture.texgen == TG_DIFFUSE_CUBE ) {
		glTexCoordPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, normal ) ) );
	}
	if ( pStage->texture.texgen == TG_SKYBOX_CUBE || pStage->texture.texgen == TG_WOBBLESKY_CUBE ) {
		glTexCoordPointer( 3, GL_FLOAT, 0, vertexCache.Position( surf->dynamicTexCoords ) );
	}
	if ( pStage->texture.texgen == TG_SCREEN ) {
		glEnable( GL_TEXTURE_GEN_S );
		glEnable( GL_TEXTURE_GEN_T );
		glEnable( GL_TEXTURE_GEN_Q );

		float	mat[16], plane[4];
		myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat );

		plane[0] = mat[0];
		plane[1] = mat[4];
		plane[2] = mat[8];
		plane[3] = mat[12];
		glTexGenfv( GL_S, GL_OBJECT_PLANE, plane );

		plane[0] = mat[1];
		plane[1] = mat[5];
		plane[2] = mat[9];
		plane[3] = mat[13];
		glTexGenfv( GL_T, GL_OBJECT_PLANE, plane );

		plane[0] = mat[3];
		plane[1] = mat[7];
		plane[2] = mat[11];
		plane[3] = mat[15];
		glTexGenfv( GL_Q, GL_OBJECT_PLANE, plane );
	}

	if ( pStage->texture.texgen == TG_SCREEN2 ) {
		glEnable( GL_TEXTURE_GEN_S );
		glEnable( GL_TEXTURE_GEN_T );
		glEnable( GL_TEXTURE_GEN_Q );

		float	mat[16], plane[4];
		myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat );

		plane[0] = mat[0];
		plane[1] = mat[4];
		plane[2] = mat[8];
		plane[3] = mat[12];
		glTexGenfv( GL_S, GL_OBJECT_PLANE, plane );

		plane[0] = mat[1];
		plane[1] = mat[5];
		plane[2] = mat[9];
		plane[3] = mat[13];
		glTexGenfv( GL_T, GL_OBJECT_PLANE, plane );

		plane[0] = mat[3];
		plane[1] = mat[7];
		plane[2] = mat[11];
		plane[3] = mat[15];
		glTexGenfv( GL_Q, GL_OBJECT_PLANE, plane );
	}

	if ( pStage->texture.texgen == TG_GLASSWARP ) {
		if ( tr.backEndRenderer == BE_ARB2 /*|| tr.backEndRenderer == BE_NV30*/ ) {
			if ( !R_BindARBProgram( GL_FRAGMENT_PROGRAM_ARB, FPROG_GLASSWARP, "glasswarp fragment program", false ) ) {
				return false;
			}
			glEnable( GL_FRAGMENT_PROGRAM_ARB );

			GL_SelectTexture( 2 );
			globalImages->scratchImage->Bind();

			GL_SelectTexture( 1 );
			globalImages->scratchImage2->Bind();

			glEnable( GL_TEXTURE_GEN_S );
			glEnable( GL_TEXTURE_GEN_T );
			glEnable( GL_TEXTURE_GEN_Q );

			float	mat[16], plane[4];
			myGlMultMatrix( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat );

			plane[0] = mat[0];
			plane[1] = mat[4];
			plane[2] = mat[8];
			plane[3] = mat[12];
			glTexGenfv( GL_S, GL_OBJECT_PLANE, plane );

			plane[0] = mat[1];
			plane[1] = mat[5];
			plane[2] = mat[9];
			plane[3] = mat[13];
			glTexGenfv( GL_T, GL_OBJECT_PLANE, plane );

			plane[0] = mat[3];
			plane[1] = mat[7];
			plane[2] = mat[11];
			plane[3] = mat[15];
			glTexGenfv( GL_Q, GL_OBJECT_PLANE, plane );

			GL_SelectTexture( 0 );
		}
	}

	if ( pStage->texture.texgen == TG_REFLECT_CUBE ) {
		if ( tr.backEndRenderer == BE_ARB2 ) {
			// see if there is also a bump map specified
			const shaderStage_t *bumpStage = surf->material->GetBumpStage();
			if ( bumpStage ) {
				if ( !R_BindARBProgram( GL_FRAGMENT_PROGRAM_ARB, FPROG_BUMPY_ENVIRONMENT, "bumpy environment fragment program", false ) ||
					!R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, VPROG_BUMPY_ENVIRONMENT, "bumpy environment vertex program", false ) ) {
					return false;
				}

				// per-pixel reflection mapping with bump mapping
				GL_SelectTexture( 1 );
				bumpStage->texture.image->Bind();
				GL_SelectTexture( 0 );

				glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, normal ) ) );
				glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET ) );
				glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET ) );

				glEnableVertexAttribArrayARB( 9 );
				glEnableVertexAttribArrayARB( 10 );
				glEnableClientState( GL_NORMAL_ARRAY );

				// Program env 5, 6, 7, 8 have been set in RB_SetProgramEnvironmentSpace

				glEnable( GL_FRAGMENT_PROGRAM_ARB );
				glEnable( GL_VERTEX_PROGRAM_ARB );
			} else {
				if ( !R_BindARBProgram( GL_FRAGMENT_PROGRAM_ARB, FPROG_ENVIRONMENT, "environment fragment program", false ) ||
					!R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, VPROG_ENVIRONMENT, "environment vertex program", false ) ) {
					return false;
				}

				// per-pixel reflection mapping without a normal map
				glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, normal ) ) );
				glEnableClientState( GL_NORMAL_ARRAY );

				glEnable( GL_FRAGMENT_PROGRAM_ARB );
				glEnable( GL_VERTEX_PROGRAM_ARB );
			}
		} else {
			glEnable( GL_TEXTURE_GEN_S );
			glEnable( GL_TEXTURE_GEN_T );
			glEnable( GL_TEXTURE_GEN_R );
			glTexGenf( GL_S, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
			glTexGenf( GL_T, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
			glTexGenf( GL_R, GL_TEXTURE_GEN_MODE, GL_REFLECTION_MAP_EXT );
			glEnableClientState( GL_NORMAL_ARRAY );
			glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, normal ) ) );

			glMatrixMode( GL_TEXTURE );
			float	mat[16];

			R_TransposeGLMatrix( backEnd.viewDef->worldSpace.modelViewMatrix, mat );

			glLoadMatrixf( mat );
			glMatrixMode( GL_MODELVIEW );
		}
	}

	return true;
}

bool RB_PrepareStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf, idDrawVert *ac ) {
	return RB_PrepareStageTexturing( pStage, surf, ac, false );
}

/*
================
RB_FinishStageTexturing
================
*/
void RB_FinishStageTexturing( const shaderStage_t *pStage, const drawSurf_t *surf, idDrawVert *ac ) {
	if ( R_TriHasPrimBatchMesh( surf->geo ) ) {
		RB_ARB2_DisableStageTexturing( pStage, surf );
		return;
	}

	// unset privatePolygonOffset if necessary
	if ( pStage->privatePolygonOffset && !surf->material->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}

	if ( pStage->texture.texgen == TG_DIFFUSE_CUBE || pStage->texture.texgen == TG_SKYBOX_CUBE
		|| pStage->texture.texgen == TG_WOBBLESKY_CUBE ) {
		glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );
	}

	if ( pStage->texture.texgen == TG_SCREEN ) {
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_Q );
	}
	if ( pStage->texture.texgen == TG_SCREEN2 ) {
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_Q );
	}

	if ( pStage->texture.texgen == TG_GLASSWARP ) {
		if ( tr.backEndRenderer == BE_ARB2 /*|| tr.backEndRenderer == BE_NV30*/ ) {
			GL_SelectTexture( 2 );
			globalImages->BindNull();

			GL_SelectTexture( 1 );
			if ( pStage->texture.hasMatrix ) {
				RB_LoadShaderTextureMatrix( surf->shaderRegisters, &pStage->texture );
			}
			glDisable( GL_TEXTURE_GEN_S );
			glDisable( GL_TEXTURE_GEN_T );
			glDisable( GL_TEXTURE_GEN_Q );
			glDisable( GL_FRAGMENT_PROGRAM_ARB );
			globalImages->BindNull();
			GL_SelectTexture( 0 );
		}
	}

	if ( pStage->texture.texgen == TG_REFLECT_CUBE ) {
		if ( tr.backEndRenderer == BE_ARB2 ) {
			// see if there is also a bump map specified
			const shaderStage_t *bumpStage = surf->material->GetBumpStage();
			if ( bumpStage ) {
				// per-pixel reflection mapping with bump mapping
				GL_SelectTexture( 1 );
				globalImages->BindNull();
				GL_SelectTexture( 0 );

				glDisableVertexAttribArrayARB( 9 );
				glDisableVertexAttribArrayARB( 10 );
			} else {
				// per-pixel reflection mapping without bump mapping
			}

			glDisableClientState( GL_NORMAL_ARRAY );
			glDisable( GL_FRAGMENT_PROGRAM_ARB );
			glDisable( GL_VERTEX_PROGRAM_ARB );
			// Fixme: Hack to get around an apparent bug in ATI drivers.  Should remove as soon as it gets fixed.
			glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
		} else {
			glDisable( GL_TEXTURE_GEN_S );
			glDisable( GL_TEXTURE_GEN_T );
			glDisable( GL_TEXTURE_GEN_R );
			glTexGenf( GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
			glTexGenf( GL_T, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
			glTexGenf( GL_R, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR );
			glDisableClientState( GL_NORMAL_ARRAY );

			glMatrixMode( GL_TEXTURE );
			glLoadIdentity();
			glMatrixMode( GL_MODELVIEW );
		}
	}

	if ( pStage->texture.hasMatrix ) {
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
	}
}

enum rbSoftParticleUniformIndex_t {
	RB_SOFT_PARTICLE_UNIFORM_STAGE_COLOR = 0,
	RB_SOFT_PARTICLE_UNIFORM_VERTEX_COLOR_MODE,
	RB_SOFT_PARTICLE_UNIFORM_DEPTH_PROJECTION,
	RB_SOFT_PARTICLE_UNIFORM_VIEWPORT_ORIGIN,
	RB_SOFT_PARTICLE_UNIFORM_INV_DEPTH_TEX_SIZE,
	RB_SOFT_PARTICLE_UNIFORM_FADE_DISTANCE,
	RB_SOFT_PARTICLE_UNIFORM_ADDITIVE_BLEND,
	RB_SOFT_PARTICLE_UNIFORM_COUNT
};

static newShaderStage_t rbSoftParticleStage;
static bool rbSoftParticleStageInitialized = false;

static void RB_InitSoftParticleStage( void ) {
	if ( rbSoftParticleStageInitialized ) {
		return;
	}

	memset( &rbSoftParticleStage, 0, sizeof( rbSoftParticleStage ) );
	rbSoftParticleStage.glslProgram = true;
	idStr::Copynz( rbSoftParticleStage.glslProgramName, "soft_particle.fs", sizeof( rbSoftParticleStage.glslProgramName ) );

	static const rbBuiltinUniformDef_t uniforms[RB_SOFT_PARTICLE_UNIFORM_COUNT] = {
		{ "stageColor", 4 },
		{ "vertexColorMode", 1 },
		{ "depthProjection", 2 },
		{ "viewportOrigin", 2 },
		{ "invDepthTexSize", 2 },
		{ "fadeDistance", 1 },
		{ "additiveBlend", 1 }
	};

	rbSoftParticleStage.numShaderParms = RB_SOFT_PARTICLE_UNIFORM_COUNT;
	for ( int i = 0; i < RB_SOFT_PARTICLE_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbSoftParticleStage.shaderParmNames[i], uniforms[i].name, sizeof( rbSoftParticleStage.shaderParmNames[i] ) );
		rbSoftParticleStage.shaderParmNumRegisters[i] = uniforms[i].components;
	}

	rbSoftParticleStage.numShaderTextures = 2;
	idStr::Copynz( rbSoftParticleStage.shaderTextureNames[0], "ParticleTexture", sizeof( rbSoftParticleStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbSoftParticleStage.shaderTextureNames[1], "SceneDepth", sizeof( rbSoftParticleStage.shaderTextureNames[1] ) );

	rbSoftParticleStageInitialized = true;
}

static bool RB_SoftParticleBlendSupported( int drawStateBits ) {
	const int blendBits = drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
	return blendBits == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
		|| blendBits == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
}

static bool RB_SoftParticleStageContractSupported( const drawSurf_t *surf, const shaderStage_t *pStage ) {
	if ( surf == NULL || pStage == NULL || surf->geo == NULL ) {
		return false;
	}
	if ( ( surf->dsFlags & DSF_BSE_EFFECT ) == 0 || ( surf->geo->surfaceFlags & STF_SOFT_PARTICLE_CANDIDATE ) == 0 ) {
		return false;
	}
	if ( R_TriHasPrimBatchMesh( surf->geo ) || pStage->newStage != NULL ) {
		return false;
	}
	if ( pStage->lighting != SL_AMBIENT || pStage->hasAlphaTest ) {
		return false;
	}
	if ( pStage->texture.image == NULL && pStage->texture.cinematic == NULL ) {
		return false;
	}
	if ( pStage->texture.texgen != TG_EXPLICIT && pStage->texture.texgen != TG_POT_CORRECTION ) {
		return false;
	}
	if ( !RB_SoftParticleBlendSupported( pStage->drawStateBits ) ) {
		return false;
	}

	const idMaterial *shader = surf->material;
	if ( shader == NULL || shader->GetSort() < SS_FAR || shader->GetSort() >= SS_POST_PROCESS ) {
		return false;
	}

	return true;
}

static bool RB_SoftParticleStageVisible( const shaderStage_t *pStage, const float *regs ) {
	if ( pStage == NULL ) {
		return false;
	}
	if ( regs != NULL && regs[ pStage->conditionRegister ] == 0.0f ) {
		return false;
	}

	const int blendBits = pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS );
	if ( regs != NULL ) {
		const float r = regs[ pStage->color.registers[0] ];
		const float g = regs[ pStage->color.registers[1] ];
		const float b = regs[ pStage->color.registers[2] ];
		const float a = regs[ pStage->color.registers[3] ];
		if ( blendBits == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE )
			&& r <= 0.0f && g <= 0.0f && b <= 0.0f ) {
			return false;
		}
		if ( blendBits == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
			&& a <= 0.0f ) {
			return false;
		}
	}

	return true;
}

bool RB_DrawSurfHasSoftParticleStage( const drawSurf_t *surf ) {
	if ( !r_softParticles.GetBool() || !glConfig.GLSLProgramAvailable ) {
		return false;
	}
	if ( surf == NULL || surf->material == NULL || surf->geo == NULL ) {
		return false;
	}
	const idMaterial *shader = surf->material;
	if ( !shader->HasAmbient() ) {
		return false;
	}

	const float *regs = surf->shaderRegisters;
	const int stageCount = shader->GetNumStages();
	for ( int stage = 0; stage < stageCount; ++stage ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		if ( RB_SoftParticleStageContractSupported( surf, pStage ) && RB_SoftParticleStageVisible( pStage, regs ) ) {
			return true;
		}
	}

	return false;
}

static bool RB_SoftParticleStageEligible( const drawSurf_t *surf, const shaderStage_t *pStage ) {
	if ( !r_softParticles.GetBool() || !glConfig.GLSLProgramAvailable ) {
		return false;
	}
	if ( globalImages == NULL || globalImages->currentDepthImage == NULL || backEnd.viewDef == NULL ) {
		return false;
	}

	return RB_SoftParticleStageContractSupported( surf, pStage );
}

static float RB_SoftParticleVertexColorModeValue( stageVertexColor_t vertexColor ) {
	switch ( vertexColor ) {
	case SVC_MODULATE:
		return 1.0f;
	case SVC_INVERSE_MODULATE:
		return 2.0f;
	case SVC_IGNORE:
	default:
		return 0.0f;
	}
}

static bool RB_TryDrawSoftParticleStage( const drawSurf_t *surf, const shaderStage_t *pStage, const float *regs, const srfTriangles_t *tri, idDrawVert *ac, int stage, const float color[4] ) {
	if ( !RB_SoftParticleStageEligible( surf, pStage ) ) {
		return false;
	}

	RB_InitSoftParticleStage();
	if ( !R_ValidateGLSLProgram( &rbSoftParticleStage ) ) {
		return false;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return false;
	}

	if ( !backEnd.currentDepthCopied ) {
		RB_CaptureCurrentDepthImage( viewportWidth, viewportHeight );
	}

	idImage *depthImage = globalImages->currentDepthImage;
	if ( depthImage == NULL || !backEnd.currentDepthCopied ) {
		return false;
	}

	const int depthTextureWidth = depthImage->GetOpts().width;
	const int depthTextureHeight = depthImage->GetOpts().height;
	if ( depthTextureWidth <= 0 || depthTextureHeight <= 0 ) {
		return false;
	}

	const bool useColorArray = pStage->vertexColor != SVC_IGNORE;
	if ( useColorArray ) {
		RB_SetStageVertexColorPointer( surf, stage, ac );
		glEnableClientState( GL_COLOR_ARRAY );
	} else {
		glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	}

	GL_SelectTexture( 0 );
	RB_BindVariableStageImage( &pStage->texture, regs );
	GL_State( pStage->drawStateBits );

	if ( !RB_PrepareStageTexturing( pStage, surf, ac ) ) {
		RB_FinishStageTexturing( pStage, surf, ac );
		if ( useColorArray ) {
			glDisableClientState( GL_COLOR_ARRAY );
		}
		return false;
	}

	GL_SelectTexture( 1 );
	depthImage->Bind();
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
	glTexParameteri( GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE );
	GL_SelectTexture( 0 );

	if ( glConfig.ARBVertexProgramAvailable ) {
		glDisable( GL_VERTEX_PROGRAM_ARB );
	}
	if ( glConfig.ARBFragmentProgramAvailable ) {
		glDisable( GL_FRAGMENT_PROGRAM_ARB );
	}
	glUseProgramObjectARB( (GLhandleARB)rbSoftParticleStage.glslProgramObject );

	if ( rbSoftParticleStage.shaderTextureLocations[0] >= 0 ) {
		glUniform1iARB( rbSoftParticleStage.shaderTextureLocations[0], 0 );
	}
	if ( rbSoftParticleStage.shaderTextureLocations[1] >= 0 ) {
		glUniform1iARB( rbSoftParticleStage.shaderTextureLocations[1], 1 );
	}

	const GLfloat depthProjection[2] = {
		backEnd.viewDef->projectionMatrix[10],
		backEnd.viewDef->projectionMatrix[14]
	};
	const GLfloat viewportOrigin[2] = {
		static_cast<GLfloat>( backEnd.viewDef->viewport.x1 ),
		static_cast<GLfloat>( backEnd.viewDef->viewport.y1 )
	};
	const GLfloat invDepthTexSize[2] = {
		1.0f / static_cast<GLfloat>( depthTextureWidth ),
		1.0f / static_cast<GLfloat>( depthTextureHeight )
	};
	const GLfloat fadeDistance = idMath::ClampFloat( 1.0f, 512.0f, r_softParticleFadeDistance.GetFloat() );
	const GLfloat vertexColorMode = RB_SoftParticleVertexColorModeValue( pStage->vertexColor );
	const GLfloat additiveBlend =
		( ( pStage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) )
		? 1.0f
		: 0.0f;

	if ( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_STAGE_COLOR] >= 0 ) {
		glUniform4fvARB( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_STAGE_COLOR], 1, color );
	}
	if ( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_VERTEX_COLOR_MODE] >= 0 ) {
		glUniform1fARB( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_VERTEX_COLOR_MODE], vertexColorMode );
	}
	if ( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_DEPTH_PROJECTION] >= 0 ) {
		glUniform2fvARB( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_DEPTH_PROJECTION], 1, depthProjection );
	}
	if ( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_VIEWPORT_ORIGIN] >= 0 ) {
		glUniform2fvARB( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_VIEWPORT_ORIGIN], 1, viewportOrigin );
	}
	if ( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_INV_DEPTH_TEX_SIZE] >= 0 ) {
		glUniform2fvARB( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_INV_DEPTH_TEX_SIZE], 1, invDepthTexSize );
	}
	if ( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_FADE_DISTANCE] >= 0 ) {
		glUniform1fARB( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_FADE_DISTANCE], fadeDistance );
	}
	if ( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_ADDITIVE_BLEND] >= 0 ) {
		glUniform1fARB( rbSoftParticleStage.shaderParmLocations[RB_SOFT_PARTICLE_UNIFORM_ADDITIVE_BLEND], additiveBlend );
	}

	RB_DrawElementsWithCounters( tri );

	glUseProgramObjectARB( 0 );
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	RB_FinishStageTexturing( pStage, surf, ac );

	if ( useColorArray ) {
		glDisableClientState( GL_COLOR_ARRAY );
	}

	return true;
}

enum rbRVSpecialDepthUniformIndex_t {
	RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE = 0,
	RB_RVSPECIAL_DEPTH_UNIFORM_COUNT
};

enum rbRVSpecialBlurUniformIndex_t {
	RB_RVSPECIAL_BLUR_UNIFORM_TEXTURE_SCALE = 0,
	RB_RVSPECIAL_BLUR_UNIFORM_SAMPLE_DIST,
	RB_RVSPECIAL_BLUR_UNIFORM_COUNT
};

enum rbRVSpecialMedLabsUniformIndex_t {
	RB_RVSPECIAL_MEDLABS_UNIFORM_RANGE = 0,
	RB_RVSPECIAL_MEDLABS_UNIFORM_FOCUS,
	RB_RVSPECIAL_MEDLABS_UNIFORM_SCROLL,
	RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_COLOR,
	RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_PERCENT,
	RB_RVSPECIAL_MEDLABS_UNIFORM_COUNT
};

enum rbRVSpecialALUniformIndex_t {
	RB_RVSPECIAL_AL_UNIFORM_DISTANCE_SCALE = 0,
	RB_RVSPECIAL_AL_UNIFORM_LIGHT_LOC,
	RB_RVSPECIAL_AL_UNIFORM_LIGHT_COLOR,
	RB_RVSPECIAL_AL_UNIFORM_LIGHT_SIZE,
	RB_RVSPECIAL_AL_UNIFORM_LIGHT_MIN_DISTANCE,
	RB_RVSPECIAL_AL_UNIFORM_COUNT
};

static newShaderStage_t rbRVSpecialDepthStage;
static newShaderStage_t rbRVSpecialBlurStage;
static newShaderStage_t rbRVSpecialMedLabsStage;
static newShaderStage_t rbRVSpecialALStage;
static bool rbRVSpecialStagesInitialized = false;
static bool rbRVSpecialBlurPrepared = false;
static bool rbRVSpecialALPrepared = false;
static bool rbRVSpecialCaptureUsesDiffuseImage = false;
static int rbRVSpecialActiveMask = 0;

static void RB_InitRVSpecialStages( void ) {
	if ( rbRVSpecialStagesInitialized ) {
		return;
	}

	memset( &rbRVSpecialDepthStage, 0, sizeof( rbRVSpecialDepthStage ) );
	rbRVSpecialDepthStage.glslProgram = true;
	idStr::Copynz( rbRVSpecialDepthStage.glslProgramName, "rvspecial_depth.fs", sizeof( rbRVSpecialDepthStage.glslProgramName ) );
	rbRVSpecialDepthStage.numShaderParms = RB_RVSPECIAL_DEPTH_UNIFORM_COUNT;
	idStr::Copynz( rbRVSpecialDepthStage.shaderParmNames[RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE], "distanceScale",
		sizeof( rbRVSpecialDepthStage.shaderParmNames[RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE] ) );
	rbRVSpecialDepthStage.shaderParmNumRegisters[RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE] = 1;
	rbRVSpecialDepthStage.numShaderTextures = 1;
	idStr::Copynz( rbRVSpecialDepthStage.shaderTextureNames[0], "Image", sizeof( rbRVSpecialDepthStage.shaderTextureNames[0] ) );

	memset( &rbRVSpecialBlurStage, 0, sizeof( rbRVSpecialBlurStage ) );
	rbRVSpecialBlurStage.glslProgram = true;
	idStr::Copynz( rbRVSpecialBlurStage.glslProgramName, "rvspecial_blur.fs", sizeof( rbRVSpecialBlurStage.glslProgramName ) );
	static const rbBuiltinUniformDef_t blurUniforms[RB_RVSPECIAL_BLUR_UNIFORM_COUNT] = {
		{ "textureScale", 2 },
		{ "sampleDist", 1 }
	};
	rbRVSpecialBlurStage.numShaderParms = RB_RVSPECIAL_BLUR_UNIFORM_COUNT;
	for ( int i = 0; i < RB_RVSPECIAL_BLUR_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbRVSpecialBlurStage.shaderParmNames[i], blurUniforms[i].name,
			sizeof( rbRVSpecialBlurStage.shaderParmNames[i] ) );
		rbRVSpecialBlurStage.shaderParmNumRegisters[i] = blurUniforms[i].components;
	}
	rbRVSpecialBlurStage.numShaderTextures = 1;
	idStr::Copynz( rbRVSpecialBlurStage.shaderTextureNames[0], "Image", sizeof( rbRVSpecialBlurStage.shaderTextureNames[0] ) );

	memset( &rbRVSpecialMedLabsStage, 0, sizeof( rbRVSpecialMedLabsStage ) );
	rbRVSpecialMedLabsStage.glslProgram = true;
	idStr::Copynz( rbRVSpecialMedLabsStage.glslProgramName, "rvspecial_medlabs.fs", sizeof( rbRVSpecialMedLabsStage.glslProgramName ) );
	static const rbBuiltinUniformDef_t medlabsUniforms[RB_RVSPECIAL_MEDLABS_UNIFORM_COUNT] = {
		{ "effectRange", 1 },
		{ "focus", 1 },
		{ "scroll", 1 },
		{ "approachColor", 4 },
		{ "approachPercent", 1 }
	};
	rbRVSpecialMedLabsStage.numShaderParms = RB_RVSPECIAL_MEDLABS_UNIFORM_COUNT;
	for ( int i = 0; i < RB_RVSPECIAL_MEDLABS_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbRVSpecialMedLabsStage.shaderParmNames[i], medlabsUniforms[i].name,
			sizeof( rbRVSpecialMedLabsStage.shaderParmNames[i] ) );
		rbRVSpecialMedLabsStage.shaderParmNumRegisters[i] = medlabsUniforms[i].components;
	}
	rbRVSpecialMedLabsStage.numShaderTextures = 2;
	idStr::Copynz( rbRVSpecialMedLabsStage.shaderTextureNames[0], "Depth", sizeof( rbRVSpecialMedLabsStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbRVSpecialMedLabsStage.shaderTextureNames[1], "Blur1", sizeof( rbRVSpecialMedLabsStage.shaderTextureNames[1] ) );

	memset( &rbRVSpecialALStage, 0, sizeof( rbRVSpecialALStage ) );
	rbRVSpecialALStage.glslProgram = true;
	idStr::Copynz( rbRVSpecialALStage.glslProgramName, "rvspecial_al.fs", sizeof( rbRVSpecialALStage.glslProgramName ) );
	static const rbBuiltinUniformDef_t alUniforms[RB_RVSPECIAL_AL_UNIFORM_COUNT] = {
		{ "distanceScale", 1 },
		{ "LightLoc", 3 },
		{ "LightColor", 4 },
		{ "LightSize", 1 },
		{ "LightMinDistance", 1 }
	};
	rbRVSpecialALStage.numShaderParms = RB_RVSPECIAL_AL_UNIFORM_COUNT;
	for ( int i = 0; i < RB_RVSPECIAL_AL_UNIFORM_COUNT; i++ ) {
		idStr::Copynz( rbRVSpecialALStage.shaderParmNames[i], alUniforms[i].name,
			sizeof( rbRVSpecialALStage.shaderParmNames[i] ) );
		rbRVSpecialALStage.shaderParmNumRegisters[i] = alUniforms[i].components;
	}
	rbRVSpecialALStage.numShaderTextures = 2;
	idStr::Copynz( rbRVSpecialALStage.shaderTextureNames[0], "RT", sizeof( rbRVSpecialALStage.shaderTextureNames[0] ) );
	idStr::Copynz( rbRVSpecialALStage.shaderTextureNames[1], "LightImage", sizeof( rbRVSpecialALStage.shaderTextureNames[1] ) );

	rbRVSpecialStagesInitialized = true;
}

static idImage *RB_CreateOrUpdateSpecialImage( const char *name, int width, int height, textureFormat_t format, textureFilter_t filter ) {
	idImageOpts opts;
	memset( &opts, 0, sizeof( opts ) );
	opts.textureType = TT_2D;
	opts.format = format;
	opts.width = width;
	opts.height = height;
	opts.numLevels = 1;
	opts.numMSAASamples = 0;
	opts.isPersistant = true;
	return tr.CreateImage( name, &opts, filter );
}

static bool RB_EnsureRVSpecialBlurResources( void ) {
	const int width = 256;
	const int height = 256;

	tr.specialBlurDepthImage = RB_CreateOrUpdateSpecialImage( "DepthTexture", width, height, FMT_RGBA16F, TF_LINEAR );
	tr.specialBlurDepthStencilImage = RB_CreateOrUpdateSpecialImage( "_rvspecialBlurDepthDS", width, height, FMT_DEPTH_STENCIL, TF_NEAREST );
	tr.specialBlurImage = RB_CreateOrUpdateSpecialImage( "BlurTexture1", width, height, FMT_RGBA16F, TF_LINEAR );
	if ( tr.specialBlurDepthImage == NULL || tr.specialBlurDepthStencilImage == NULL || tr.specialBlurImage == NULL ) {
		return false;
	}

	if ( tr.specialBlurDepthRenderTexture == NULL ) {
		tr.specialBlurDepthRenderTexture = tr.CreateRenderTexture( tr.specialBlurDepthImage, tr.specialBlurDepthStencilImage );
	} else if ( tr.specialBlurDepthRenderTexture->GetWidth() != width || tr.specialBlurDepthRenderTexture->GetHeight() != height ) {
		tr.ResizeRenderTexture( tr.specialBlurDepthRenderTexture, width, height );
	}

	if ( tr.specialBlurRenderTexture == NULL ) {
		tr.specialBlurRenderTexture = tr.CreateRenderTexture( tr.specialBlurImage, NULL );
	} else if ( tr.specialBlurRenderTexture->GetWidth() != width || tr.specialBlurRenderTexture->GetHeight() != height ) {
		tr.ResizeRenderTexture( tr.specialBlurRenderTexture, width, height );
	}

	return tr.specialBlurDepthRenderTexture != NULL && tr.specialBlurRenderTexture != NULL;
}

static bool RB_EnsureRVSpecialALResources( void ) {
	const int width = 512;
	const int height = 512;

	tr.specialALDepthImage = RB_CreateOrUpdateSpecialImage( "_rvspecialALDepth", width, height, FMT_RGBA16F, TF_NEAREST );
	tr.specialALDepthStencilImage = RB_CreateOrUpdateSpecialImage( "_rvspecialALDepthDS", width, height, FMT_DEPTH_STENCIL, TF_NEAREST );
	if ( tr.specialALDepthImage == NULL || tr.specialALDepthStencilImage == NULL ) {
		return false;
	}

	if ( tr.specialALDepthRenderTexture == NULL ) {
		tr.specialALDepthRenderTexture = tr.CreateRenderTexture( tr.specialALDepthImage, tr.specialALDepthStencilImage );
	} else if ( tr.specialALDepthRenderTexture->GetWidth() != width || tr.specialALDepthRenderTexture->GetHeight() != height ) {
		tr.ResizeRenderTexture( tr.specialALDepthRenderTexture, width, height );
	}

	if ( tr.specialALLightImage == NULL ) {
		tr.specialALLightImage = globalImages->ImageFromFile( "gfx/lights/round.tga", TF_LINEAR, TR_CLAMP, TD_DEFAULT );
	}

	return tr.specialALDepthRenderTexture != NULL && tr.specialALLightImage != NULL;
}

static void RB_RVSpecialRestoreDrawingView( void ) {
	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	backEnd.currentSpace = NULL;

	glViewport(
		tr.viewportOffset[0] + backEnd.viewDef->viewport.x1,
		tr.viewportOffset[1] + backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );
	glScissor(
		tr.viewportOffset[0] + backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		tr.viewportOffset[1] + backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
	backEnd.currentScissor = backEnd.viewDef->scissor;

	GL_State( GLS_DEPTHFUNC_EQUAL );
	if ( backEnd.viewDef->viewEntitys ) {
		glEnable( GL_DEPTH_TEST );
		glEnable( GL_STENCIL_TEST );
	} else {
		glDisable( GL_DEPTH_TEST );
		glDisable( GL_STENCIL_TEST );
	}

	backEnd.glState.faceCulling = -1;
	GL_Cull( CT_FRONT_SIDED );
	backEnd.glState.forceGlState = true;
}

static bool RB_SetRVSpecialOrthoForView( void );

static void RB_DestroyPostProcessRenderTexture( idRenderTexture *&renderTexture ) {
	if ( renderTexture == NULL ) {
		return;
	}
	tr.DestroyRenderTexture( renderTexture );
	renderTexture = NULL;
}

void RB_ShutdownScenePostProcess( void ) {
	RB_FreeSceneDepthAwarePresentProgram();

	RB_FreeGLSLProgram( &rbLightGridIndirectStage );
	RB_FreeGLSLProgram( &rbPlayerRimlightStage );
	RB_FreeGLSLProgram( &rbPlayerOutlineStage );
	RB_FreeGLSLProgram( &rbCelOutlineStage );
	RB_FreeGLSLProgram( &rbSSAOStage );
	RB_FreeGLSLProgram( &rbMotionBlurStage );
	RB_FreeGLSLProgram( &rbMotionVectorStage );
	RB_FreeGLSLProgram( &rbBloomExtractStage );
	RB_FreeGLSLProgram( &rbBloomDownsampleStage );
	RB_FreeGLSLProgram( &rbBloomBlurStage );
	RB_FreeGLSLProgram( &rbHDRLuminanceStage );
	RB_FreeGLSLProgram( &rbBloomCompositeStage );
	RB_FreeGLSLProgram( &rbResolutionScaleStage );
	RB_FreeGLSLProgram( &rbCRTStage );
	RB_FreeColorMappingProgram();
	RB_FreeGLSLProgram( &rbSoftParticleStage );
	RB_FreeGLSLProgram( &rbRVSpecialDepthStage );
	RB_FreeGLSLProgram( &rbRVSpecialBlurStage );
	RB_FreeGLSLProgram( &rbRVSpecialMedLabsStage );
	RB_FreeGLSLProgram( &rbRVSpecialALStage );

	RB_DestroyPostProcessRenderTexture( rbSceneRenderTexture );
	rbSceneRenderTextureSamples = -1;
	rbSceneColorImage = NULL;
	rbSceneDepthStencilImage = NULL;
	rbSceneRenderTargetPreserveDepthImage = NULL;
	rbSceneRenderTargetPreserveFarDepthFrame = -1;
	rbSceneRenderTargetPreserveFarDepthView = NULL;
	rbSceneRenderTargetPortalSkyFrame = -1;
	rbSceneRenderTargetPortalSkyViewport.Clear();
	rbSceneRenderTargetPortalSkyWidth = 0;
	rbSceneRenderTargetPortalSkyHeight = 0;
	rbSceneRenderTargetPreserveDepthFrame = -1;
	rbSceneRenderTargetPreserveDepthWidth = 0;
	rbSceneRenderTargetPreserveDepthHeight = 0;

	RB_ResetMotionBlurHistory();
	RB_DestroyPostProcessRenderTexture( rbMotionVectorRenderTexture );
	rbMotionVectorImage = NULL;
	rbMotionVectorPreviousState = NULL;
	rbMotionVectorDrewSurface = false;

	for ( int level = 0; level < RB_BLOOM_MAX_LEVELS; level++ ) {
		for ( int ping = 0; ping < 2; ping++ ) {
			RB_DestroyPostProcessRenderTexture( rbBloomRenderTextures[level][ping] );
			rbBloomImages[level][ping] = NULL;
		}
	}
	for ( int level = 0; level < RB_HDR_EXPOSURE_MAX_LEVELS; level++ ) {
		RB_DestroyPostProcessRenderTexture( rbHDRExposureRenderTextures[level] );
		rbHDRExposureImages[level] = NULL;
	}
	rbHDRExposureLevelCount = 0;
	rbHDRAdaptedExposure = 1.0f;
	rbHDRLastAverageLuminance = 1.0f;
	rbHDRLastTargetExposure = 1.0f;
	rbHDRLastAdaptationTime = -1.0f;
	rbHDRExposureInitialized = false;
	if ( rbHDRExposureReadbackPBOs[0] != 0 ) {
		if ( glDeleteBuffersARB != NULL ) {
			glDeleteBuffersARB( 2, rbHDRExposureReadbackPBOs );
		}
	}
	rbHDRExposureReadbackPBOs[0] = 0;
	rbHDRExposureReadbackPBOs[1] = 0;
	rbHDRExposureReadbackPrimed[0] = false;
	rbHDRExposureReadbackPrimed[1] = false;
	rbHDRExposureReadbackIndex = 0;
}

static void RB_RVSpecialBeginCapture( idRenderTexture *renderTexture, int width, int height ) {
	RB_BindPostProcessRenderTexture( renderTexture, width, height );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();

	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClearDepth( 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );

	GL_State( GLS_DEFAULT );
	glDisable( GL_BLEND );
	glDisable( GL_CULL_FACE );
	glEnable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glDepthFunc( GL_LEQUAL );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	backEnd.currentSpace = NULL;
}

static void RB_RVSpecialEndCapture( idRenderTexture *previousRenderTexture ) {
	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;

	glUseProgramObjectARB( 0 );
	RB_RestorePostProcessTarget( previousRenderTexture, viewportWidth, viewportHeight );

	const int maxStateUnits = Max( 0, Min( MAX_MULTITEXTURE_UNITS, Min( glConfig.maxTextureUnits, glConfig.maxTextureImageUnits ) ) );
	for ( int i = 0; i < maxStateUnits; i++ ) {
		GL_SelectTexture( i );
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_R );
		glDisable( GL_TEXTURE_GEN_Q );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
		glDisable( GL_TEXTURE_CUBE_MAP_EXT );
		glDisable( GL_TEXTURE_3D );
		glDisable( GL_TEXTURE_2D );
		backEnd.glState.tmu[i].textureType = TT_DISABLED;
		backEnd.glState.tmu[i].current2DMap = -1;
		backEnd.glState.tmu[i].current3DMap = -1;
		backEnd.glState.tmu[i].currentCubeMap = -1;
		globalImages->whiteImage->Bind();
	}

	GL_SelectTexture( 0 );
	backEnd.glState.forceGlState = true;
}

static bool RB_RVSpecialPrepareSolidStageTexturing( const drawSurf_t *surf, idDrawVert *ac, const shaderStage_t **diffuseStageOut ) {
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;

	if ( diffuseStageOut != NULL ) {
		*diffuseStageOut = NULL;
	}

	if ( !rbRVSpecialCaptureUsesDiffuseImage ) {
		globalImages->whiteImage->Bind();
		return true;
	}

	const int stageCount = shader->GetNumStages();
	for ( int stage = 0; stage < stageCount; stage++ ) {
		const shaderStage_t *pStage = shader->GetStage( stage );
		if ( pStage->lighting != SL_DIFFUSE || regs[ pStage->conditionRegister ] == 0.0f ) {
			continue;
		}

		pStage->texture.image->Bind();
		if ( diffuseStageOut != NULL ) {
			*diffuseStageOut = pStage;
		}
		return RB_PrepareStageTexturing( pStage, surf, ac );
	}

	globalImages->whiteImage->Bind();
	return true;
}

static bool RB_EnsurePackedClassicDrawCaches( const drawSurf_t *surf, bool needsLighting, bool createIndexCache ) {
#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
	const srfTriangles_t *tri = ( surf != NULL ) ? surf->geo : NULL;
	if ( tri == NULL || tri->primBatchMesh == NULL ) {
		return true;
	}

	srfTriangles_t *mutableTri = const_cast<srfTriangles_t *>( tri );
	const bool needsIndexCache = createIndexCache && r_useIndexBuffers.GetBool() && tri->numIndexes > 0;
	if ( mutableTri->ambientCache == NULL || ( needsIndexCache && mutableTri->indexCache == NULL ) ) {
		if ( !R_CreatePackedSurfaceFrameCaches( mutableTri, needsLighting, createIndexCache ) ) {
			return false;
		}
	}

	R_TouchVertexCache( mutableTri->ambientCache );
	if ( mutableTri->indexCache != NULL ) {
		R_TouchVertexCache( mutableTri->indexCache );
	}
#else
	(void)surf;
	(void)needsLighting;
	(void)createIndexCache;
#endif

	return true;
}

/*
=============================================================================================

FILL DEPTH BUFFER

=============================================================================================
*/


/*
==================
RB_T_FillDepthBuffer
==================
*/
static void RB_T_CaptureRVSpecialDepth( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = surf->geo;
	const idMaterial *shader = surf->material;
	const float *regs;
	const shaderStage_t *pStage = NULL;
	float color[4];

	if ( !shader->IsDrawn() || !tri->numIndexes || shader->Coverage() == MC_TRANSLUCENT ) {
		return;
	}
	if ( !RB_EnsurePackedClassicDrawCaches( surf, false, true ) ) {
		return;
	}
	if ( !tri->ambientCache ) {
		common->Printf( "RB_T_CaptureRVSpecialDepth: !tri->ambientCache\n" );
		return;
	}

	regs = surf->shaderRegisters;
	const int stageCount = shader->GetNumStages();
	for ( int stage = 0; stage < stageCount; stage++ ) {
		pStage = shader->GetStage( stage );
		if ( regs[ pStage->conditionRegister ] != 0.0f ) {
			break;
		}
	}
	if ( pStage == NULL || regs[ pStage->conditionRegister ] == 0.0f ) {
		return;
	}

	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	const bool useAlphaToCoverage = RB_UseAlphaToCoverage( shader );
	if ( useAlphaToCoverage ) {
		glEnable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}

	color[0] = 1.0f;
	color[1] = 1.0f;
	color[2] = 1.0f;
	color[3] = 1.0f;

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );

	bool drawSolid = ( shader->Coverage() == MC_OPAQUE );

	if ( shader->Coverage() == MC_PERFORATED ) {
		bool didDraw = false;

		glEnable( GL_ALPHA_TEST );
		for ( int stage = 0; stage < stageCount; stage++ ) {
			pStage = shader->GetStage( stage );
			if ( !pStage->hasAlphaTest || regs[ pStage->conditionRegister ] == 0.0f ) {
				continue;
			}
			if ( pStage->texture.dynamic == DI_REFLECTION_RENDER || pStage->texture.dynamic == DI_REFRACTION_RENDER ) {
				continue;
			}

			didDraw = true;
			color[3] = regs[ pStage->color.registers[3] ];
			if ( color[3] <= 0.0f ) {
				continue;
			}

			glColor4fv( color );
			glAlphaFunc( pStage->alphaTestMode, regs[ pStage->alphaTestRegister ] );
			pStage->texture.image->Bind();
			if ( !RB_PrepareStageTexturing( pStage, surf, ac, true ) ) {
				RB_FinishStageTexturing( pStage, surf, ac );
				continue;
			}

			RB_DrawElementsWithCounters( tri );
			RB_FinishStageTexturing( pStage, surf, ac );
		}
		glDisable( GL_ALPHA_TEST );

		if ( !didDraw ) {
			drawSolid = true;
		}
	}

	if ( drawSolid ) {
		const shaderStage_t *diffuseStage = NULL;
		glColor4fv( color );
		if ( RB_RVSpecialPrepareSolidStageTexturing( surf, ac, &diffuseStage ) ) {
			if ( R_TriHasPrimBatchMesh( tri ) ) {
				RB_ARB2_MD5R_DrawDepthElements( surf );
			} else {
				RB_DrawElementsWithCounters( tri );
			}
		}
		if ( diffuseStage != NULL ) {
			RB_FinishStageTexturing( diffuseStage, surf, ac );
		}
	}

	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	if ( useAlphaToCoverage ) {
		glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}
}

static bool RB_CaptureRVSpecialDepth( idRenderTexture *target, int width, int height, bool useDiffuseImage, float distanceScale ) {
	RB_InitRVSpecialStages();
	if ( !R_ValidateGLSLProgram( &rbRVSpecialDepthStage ) ) {
		return false;
	}

	const GLfloat safeDistanceScale = Max( distanceScale, 1.0f );
	idRenderTexture *previousRenderTexture = backEnd.renderTexture;
	rbRVSpecialCaptureUsesDiffuseImage = useDiffuseImage;

	RB_RVSpecialBeginCapture( target, width, height );

	glUseProgramObjectARB( (GLhandleARB)rbRVSpecialDepthStage.glslProgramObject );
	if ( rbRVSpecialDepthStage.shaderTextureLocations[0] >= 0 ) {
		glUniform1iARB( rbRVSpecialDepthStage.shaderTextureLocations[0], 0 );
	}
	if ( rbRVSpecialDepthStage.shaderParmLocations[RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE] >= 0 ) {
		glUniform1fARB( rbRVSpecialDepthStage.shaderParmLocations[RB_RVSPECIAL_DEPTH_UNIFORM_DISTANCE_SCALE], safeDistanceScale );
	}

	RB_RenderDrawSurfListWithFunctionIgnoreScissor(
		(drawSurf_t **)&backEnd.viewDef->drawSurfs[0],
		backEnd.viewDef->numDrawSurfs,
		RB_T_CaptureRVSpecialDepth );

	RB_RVSpecialEndCapture( previousRenderTexture );
	rbRVSpecialCaptureUsesDiffuseImage = false;
	return true;
}

static bool RB_PrepareRVSpecialBlurImage( void ) {
	if ( !rbRVSpecialBlurPrepared || tr.specialBlurDepthImage == NULL || tr.specialBlurRenderTexture == NULL ) {
		return false;
	}

	RB_InitRVSpecialStages();
	if ( !R_ValidateGLSLProgram( &rbRVSpecialBlurStage ) ) {
		return false;
	}

	idRenderTexture *previousRenderTexture = backEnd.renderTexture;
	const int blurWidth = tr.specialBlurRenderTexture->GetWidth();
	const int blurHeight = tr.specialBlurRenderTexture->GetHeight();
	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;

	RB_CaptureCurrentRenderImage( viewportWidth, viewportHeight );
	if ( !backEnd.currentRenderCopied || globalImages->currentRenderImage == NULL ) {
		return false;
	}

	RB_BindPostProcessRenderTexture( tr.specialBlurRenderTexture, blurWidth, blurHeight );
	RB_BeginFullscreenPostProcessPass( 0, 0, blurWidth, blurHeight );

	GL_SelectTexture( 0 );
	globalImages->currentRenderImage->Bind();
	glUseProgramObjectARB( (GLhandleARB)rbRVSpecialBlurStage.glslProgramObject );
	if ( rbRVSpecialBlurStage.shaderTextureLocations[0] >= 0 ) {
		glUniform1iARB( rbRVSpecialBlurStage.shaderTextureLocations[0], 0 );
	}

	const GLfloat textureScale[2] = { 1.0f, 1.0f };
	const GLfloat sampleDist = 0.00620f;
	if ( rbRVSpecialBlurStage.shaderParmLocations[RB_RVSPECIAL_BLUR_UNIFORM_TEXTURE_SCALE] >= 0 ) {
		glUniform2fvARB( rbRVSpecialBlurStage.shaderParmLocations[RB_RVSPECIAL_BLUR_UNIFORM_TEXTURE_SCALE], 1, textureScale );
	}
	if ( rbRVSpecialBlurStage.shaderParmLocations[RB_RVSPECIAL_BLUR_UNIFORM_SAMPLE_DIST] >= 0 ) {
		glUniform1fARB( rbRVSpecialBlurStage.shaderParmLocations[RB_RVSPECIAL_BLUR_UNIFORM_SAMPLE_DIST], sampleDist );
	}

	RB_DrawFullscreenPostProcessQuadUnitUV();

	glUseProgramObjectARB( 0 );
	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();
	RB_RestorePostProcessTarget(
		previousRenderTexture,
		backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
		backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
	backEnd.glState.forceGlState = true;
	return true;
}

static bool RB_CompositeRVSpecialBlur( void ) {
	if ( !rbRVSpecialBlurPrepared || tr.specialBlurDepthImage == NULL || tr.specialBlurImage == NULL ) {
		return false;
	}

	RB_InitRVSpecialStages();
	if ( !R_ValidateGLSLProgram( &rbRVSpecialMedLabsStage ) ) {
		return false;
	}

	const GLfloat effectRange = Max( tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][4], 0.01f );
	const GLfloat focus = idMath::ClampFloat( 0.0f, 1.0f, tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][5] );
	const GLfloat scroll = static_cast<GLfloat>( backEnd.viewDef->renderView.time ) * 0.001f * 0.25f;
	const GLfloat approachPercent = idMath::ClampFloat( 0.0f, 1.0f, tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][6] );
	const GLfloat approachColor[4] = {
		tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][0],
		tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][1],
		tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][2],
		tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][3]
	};

	backEnd.currentScissor = backEnd.viewDef->scissor;
	RB_BeginFullscreenPostProcessPass(
		backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );

	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	GL_SelectTexture( 0 );
	tr.specialBlurDepthImage->Bind();
	GL_SelectTexture( 1 );
	tr.specialBlurImage->Bind();
	GL_SelectTexture( 0 );

	glUseProgramObjectARB( (GLhandleARB)rbRVSpecialMedLabsStage.glslProgramObject );
	if ( rbRVSpecialMedLabsStage.shaderTextureLocations[0] >= 0 ) {
		glUniform1iARB( rbRVSpecialMedLabsStage.shaderTextureLocations[0], 0 );
	}
	if ( rbRVSpecialMedLabsStage.shaderTextureLocations[1] >= 0 ) {
		glUniform1iARB( rbRVSpecialMedLabsStage.shaderTextureLocations[1], 1 );
	}
	if ( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_RANGE] >= 0 ) {
		glUniform1fARB( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_RANGE], effectRange );
	}
	if ( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_FOCUS] >= 0 ) {
		glUniform1fARB( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_FOCUS], focus );
	}
	if ( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_SCROLL] >= 0 ) {
		glUniform1fARB( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_SCROLL], scroll );
	}
	if ( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_COLOR] >= 0 ) {
		glUniform4fvARB( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_COLOR], 1, approachColor );
	}
	if ( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_PERCENT] >= 0 ) {
		glUniform1fARB( rbRVSpecialMedLabsStage.shaderParmLocations[RB_RVSPECIAL_MEDLABS_UNIFORM_APPROACH_PERCENT], approachPercent );
	}

	RB_DrawFullscreenPostProcessQuadUnitUV();

	glUseProgramObjectARB( 0 );
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	RB_EndFullscreenPostProcessPass();
	backEnd.glState.forceGlState = true;
	return true;
}

static bool RB_SetRVSpecialOrthoForView( void ) {
	if ( backEnd.viewDef == NULL ) {
		return false;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return false;
	}

	glViewport(
		tr.viewportOffset[0] + backEnd.viewDef->viewport.x1,
		tr.viewportOffset[1] + backEnd.viewDef->viewport.y1,
		viewportWidth,
		viewportHeight );
	glScissor(
		tr.viewportOffset[0] + backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		tr.viewportOffset[1] + backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
	backEnd.currentScissor = backEnd.viewDef->scissor;

	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glOrtho( 0, viewportWidth, viewportHeight, 0, -1, 1 );
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	GL_Cull( CT_TWO_SIDED );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	return true;
}

static bool RB_DrawRVSpecialALLight( const idVec3 &origin, float size, const idVec3 &color ) {
	idPlane eye;
	idPlane clip;
	idVec3 ndc;
	idVec3 points[4];
	idVec3 eyePoint;
	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	const float distanceScale = 2000.0f;

	R_TransformModelToClip( origin, backEnd.viewDef->worldSpace.modelViewMatrix, backEnd.viewDef->projectionMatrix, eye, clip );
	if ( clip[3] <= 0.0f ) {
		return false;
	}

	const float lightDepth = -eye[2];
	if ( lightDepth <= 0.0f ) {
		return false;
	}

	const idVec3 right = backEnd.viewDef->renderView.viewaxis[1] * size;
	const idVec3 up = backEnd.viewDef->renderView.viewaxis[2] * size;
	points[0] = origin + right + up;
	points[1] = origin - right + up;
	points[2] = origin - right - up;
	points[3] = origin + right - up;

	float x1 = idMath::INFINITY;
	float y1 = idMath::INFINITY;
	float x2 = -idMath::INFINITY;
	float y2 = -idMath::INFINITY;

	for ( int i = 0; i < 4; i++ ) {
		R_TransformModelToClip( points[i], backEnd.viewDef->worldSpace.modelViewMatrix, backEnd.viewDef->projectionMatrix, eye, clip );
		if ( clip[3] <= 0.0f ) {
			return false;
		}

		R_TransformClipToDevice( clip, backEnd.viewDef, ndc );
		const float sx = ( ndc.x * 0.5f + 0.5f ) * viewportWidth;
		const float sy = ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * viewportHeight;
		x1 = Min( x1, sx );
		y1 = Min( y1, sy );
		x2 = Max( x2, sx );
		y2 = Max( y2, sy );
	}

	if ( x2 < 0.0f || y2 < 0.0f || x1 > viewportWidth || y1 > viewportHeight ) {
		return false;
	}

	R_LocalPointToGlobal( backEnd.viewDef->worldSpace.modelViewMatrix, origin, eyePoint );

	const GLfloat lightColor[4] = { color.x, color.y, color.z, 1.0f };
	if ( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_DISTANCE_SCALE] >= 0 ) {
		glUniform1fARB( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_DISTANCE_SCALE], distanceScale );
	}
	if ( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_LOC] >= 0 ) {
		glUniform3fvARB( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_LOC], 1, eyePoint.ToFloatPtr() );
	}
	if ( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_COLOR] >= 0 ) {
		glUniform4fvARB( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_COLOR], 1, lightColor );
	}
	if ( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_SIZE] >= 0 ) {
		glUniform1fARB( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_SIZE], size );
	}
	if ( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_MIN_DISTANCE] >= 0 ) {
		glUniform1fARB( rbRVSpecialALStage.shaderParmLocations[RB_RVSPECIAL_AL_UNIFORM_LIGHT_MIN_DISTANCE], lightDepth );
	}

	const float s1 = x1 / viewportWidth;
	const float s2 = x2 / viewportWidth;
	const float t1 = 1.0f - ( y1 / viewportHeight );
	const float t2 = 1.0f - ( y2 / viewportHeight );

	glBegin( GL_QUADS );
	glTexCoord2f( s1, t1 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 0.0f, 0.0f );
	glVertex2f( x1, y1 );
	glTexCoord2f( s2, t1 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 1.0f, 0.0f );
	glVertex2f( x2, y1 );
	glTexCoord2f( s2, t2 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 1.0f, 1.0f );
	glVertex2f( x2, y2 );
	glTexCoord2f( s1, t2 );
	glMultiTexCoord2fARB( GL_TEXTURE1, 0.0f, 1.0f );
	glVertex2f( x1, y2 );
	glEnd();

	return true;
}

void RB_DrawSpecialEffects( const void *data ) {
	const drawSurfsCommand_t *cmd = (const drawSurfsCommand_t *)data;

	backEnd.viewDef = cmd->viewDef;
	rbRVSpecialBlurPrepared = false;
	rbRVSpecialALPrepared = false;
	rbRVSpecialActiveMask = tr.specialEffectsEnabled;
	if ( r_forceSpecialEffects.GetInteger() > 0 ) {
		rbRVSpecialActiveMask = r_forceSpecialEffects.GetInteger();
	}

	if ( backEnd.viewDef == NULL || backEnd.viewDef->renderWorld == NULL || backEnd.viewDef->numDrawSurfs <= 0 ) {
		return;
	}
	if ( !glConfig.GLSLProgramAvailable ) {
		return;
	}

	if ( ( rbRVSpecialActiveMask & SPECIAL_EFFECT_BLUR ) != 0 && RB_EnsureRVSpecialBlurResources() ) {
		rbRVSpecialBlurPrepared = RB_CaptureRVSpecialDepth(
			tr.specialBlurDepthRenderTexture,
			tr.specialBlurDepthRenderTexture->GetWidth(),
			tr.specialBlurDepthRenderTexture->GetHeight(),
			false,
			Max( tr.specialEffectParms[ SPECIAL_EFFECT_BLUR ][7], 1.0f ) );
	}

	if ( ( rbRVSpecialActiveMask & SPECIAL_EFFECT_AL ) != 0 && RB_EnsureRVSpecialALResources() ) {
		rbRVSpecialALPrepared = RB_CaptureRVSpecialDepth(
			tr.specialALDepthRenderTexture,
			tr.specialALDepthRenderTexture->GetWidth(),
			tr.specialALDepthRenderTexture->GetHeight(),
			true,
			2000.0f );
	}
}

static void RB_DisplaySpecialEffects( const viewEntity_t *viewEnts, bool prePass ) {
	if ( backEnd.viewDef == NULL || !glConfig.GLSLProgramAvailable ) {
		return;
	}

	if ( prePass ) {
		// Legacy blur is authored as a fullscreen 2D overlay. The 3D pass only captures
		// its depth mask; the blur image itself is generated from the resolved scene when
		// the later HUD/UI view starts.
		if ( viewEnts == NULL && ( rbRVSpecialActiveMask & SPECIAL_EFFECT_BLUR ) != 0 ) {
			bool restoredView = false;
			if ( RB_PrepareRVSpecialBlurImage() ) {
				restoredView |= RB_CompositeRVSpecialBlur();
			}
			if ( restoredView ) {
				RB_RVSpecialRestoreDrawingView();
			}
		}
		return;
	}

	bool restoredView = false;

	if ( viewEnts != NULL && ( rbRVSpecialActiveMask & SPECIAL_EFFECT_AL ) != 0 && rbRVSpecialALPrepared && tr.primaryWorld != NULL ) {
		RB_InitRVSpecialStages();
		if ( R_ValidateGLSLProgram( &rbRVSpecialALStage ) && RB_SetRVSpecialOrthoForView() ) {
			GL_SelectTexture( 0 );
			tr.specialALDepthImage->Bind();
			GL_SelectTexture( 1 );
			tr.specialALLightImage->Bind();
			GL_SelectTexture( 0 );

			glUseProgramObjectARB( (GLhandleARB)rbRVSpecialALStage.glslProgramObject );
			if ( rbRVSpecialALStage.shaderTextureLocations[0] >= 0 ) {
				glUniform1iARB( rbRVSpecialALStage.shaderTextureLocations[0], 0 );
			}
			if ( rbRVSpecialALStage.shaderTextureLocations[1] >= 0 ) {
				glUniform1iARB( rbRVSpecialALStage.shaderTextureLocations[1], 1 );
			}

			for ( int i = 0; i < tr.primaryWorld->lightDefs.Num(); i++ ) {
				idRenderLightLocal *light = tr.primaryWorld->lightDefs[i];
				if ( light == NULL ) {
					continue;
				}

				idVec3 lightColor( light->parms.shaderParms[0], light->parms.shaderParms[1], light->parms.shaderParms[2] );
				if ( lightColor.LengthSqr() <= idMath::FLOAT_EPSILON ) {
					continue;
				}
				lightColor.Normalize();

				RB_DrawRVSpecialALLight( light->globalLightOrigin, 300.0f, lightColor );
			}

			glUseProgramObjectARB( 0 );
			GL_SelectTexture( 1 );
			globalImages->BindNull();
			GL_SelectTexture( 0 );
			globalImages->BindNull();
			restoredView = true;
		}
	}

	if ( restoredView ) {
		RB_RVSpecialRestoreDrawingView();
	}
}

void RB_T_FillDepthBuffer( const drawSurf_t *surf ) {
	int			stage;
	const idMaterial	*shader;
	const shaderStage_t *pStage;
	const float	*regs;
	float		color[4];
	const srfTriangles_t	*tri;

	tri = surf->geo;
	shader = surf->material;

	// update the clip plane if needed
	if ( backEnd.viewDef->numClipPlanes && surf->space != backEnd.currentSpace ) {
		GL_SelectTexture( 1 );
		
		idPlane	plane;

		R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.viewDef->clipPlanes[0], plane );
		plane[3] += 0.5;	// the notch is in the middle
		glTexGenfv( GL_S, GL_OBJECT_PLANE, plane.ToFloatPtr() );
		GL_SelectTexture( 0 );
	}

	if ( !shader->IsDrawn() ) {
		return;
	}

	// Portal-sky surfaces are a mask for the sky camera rendered before the
	// main scene. Keep those pixels at far depth when a scene-target present
	// needs to preserve the already-rendered sky color.
	if ( RB_MaterialIsPortalSkyForSSAO( shader )
		&& ( RB_SSAORequestedForCurrentView() || RB_ShouldPreserveSceneRenderTargetFarDepth( backEnd.viewDef ) ) ) {
		return;
	}

	// some deforms may disable themselves by setting numIndexes = 0
	if ( !tri->numIndexes ) {
		return;
	}

	// translucent surfaces don't put anything in the depth buffer and don't
	// test against it, which makes them fail the mirror clip plane operation
	if ( shader->Coverage() == MC_TRANSLUCENT ) {
		return;
	}

	if ( !RB_EnsurePackedClassicDrawCaches( surf, false, true ) ) {
		return;
	}

	if ( !tri->ambientCache ) {
		common->Printf( "RB_T_FillDepthBuffer: !tri->ambientCache\n" );
		return;
	}

	// get the expressions for conditionals / color / texcoords
	regs = surf->shaderRegisters;

	const bool useAlphaToCoverage = RB_UseAlphaToCoverage( shader );
	const int stageCount = shader->GetNumStages();

	// if all stages of a material have been conditioned off, don't do anything
	for ( stage = 0; stage < stageCount ; stage++ ) {
		pStage = shader->GetStage(stage);
		// check the stage enable condition
		if ( regs[ pStage->conditionRegister ] != 0 ) {
			break;
		}
	}
	if ( stage == stageCount ) {
		return;
	}

	// set polygon offset if necessary
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	if ( useAlphaToCoverage ) {
		glEnable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}

	// subviews will just down-modulate the color buffer by overbright
	if ( shader->GetSort() == SS_SUBVIEW ) {
		GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS );
		color[0] =
		color[1] = 
		color[2] = ( 1.0 / backEnd.overBright );
		color[3] = 1;
	} else {
		// others just draw black
		color[0] = 0;
		color[1] = 0;
		color[2] = 0;
		color[3] = 1;
	}

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );

	bool drawSolid = false;

	if ( shader->Coverage() == MC_OPAQUE ) {
		drawSolid = true;
	}

	// we may have multiple alpha tested stages
	if ( shader->Coverage() == MC_PERFORATED ) {
		// if the only alpha tested stages are condition register omitted,
		// draw a normal opaque surface
		bool	didDraw = false;

		glEnable( GL_ALPHA_TEST );
		// perforated surfaces may have multiple alpha tested stages
		for ( stage = 0; stage < stageCount ; stage++ ) {
			pStage = shader->GetStage(stage);

			if ( !pStage->hasAlphaTest ) {
				continue;
			}

			// check the stage enable condition
			if ( regs[ pStage->conditionRegister ] == 0 ) {
				continue;
			}

			// if we at least tried to draw an alpha tested stage,
			// we won't draw the opaque surface
			didDraw = true;

			// set the alpha modulate
			color[3] = regs[ pStage->color.registers[3] ];

			// skip the entire stage if alpha would be black
			if ( color[3] <= 0 ) {
				continue;
			}
			glColor4fv( color );

			glAlphaFunc( pStage->alphaTestMode, regs[ pStage->alphaTestRegister ] );

			// bind the texture
			pStage->texture.image->Bind();

			// set texture matrix and texGens
			if ( !RB_PrepareStageTexturing( pStage, surf, ac, true ) ) {
				RB_FinishStageTexturing( pStage, surf, ac );
				continue;
			}

			// draw it
			RB_DrawElementsWithCounters( tri );

			RB_FinishStageTexturing( pStage, surf, ac );
		}
		glDisable( GL_ALPHA_TEST );
		if ( !didDraw ) {
			drawSolid = true;
		}
	}

	// draw the entire surface solid
	if ( drawSolid ) {
		glColor4fv( color );
		globalImages->whiteImage->Bind();

		// draw it
		if ( R_TriHasPrimBatchMesh( tri ) ) {
			RB_ARB2_MD5R_DrawDepthElements( surf );
		} else {
			RB_DrawElementsWithCounters( tri );
		}
	}


	// reset polygon offset
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}

	// reset blending
	if ( shader->GetSort() == SS_SUBVIEW ) {
		GL_State( GLS_DEPTHFUNC_LESS );
	}

	if ( useAlphaToCoverage ) {
		glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}

}

/*
=====================
RB_STD_FillDepthBuffer

If we are rendering a subview with a near clip plane, use a second texture
to force the alpha test to fail when behind that clip plane
=====================
*/
void RB_STD_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	// if we are just doing 2D rendering, no need to fill the depth buffer
	if ( !backEnd.viewDef->viewEntitys ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_FillDepthBuffer ----------\n" );

	// enable the second texture for mirror plane clipping if needed
	if ( backEnd.viewDef->numClipPlanes ) {
		GL_SelectTexture( 1 );
		globalImages->alphaNotchImage->Bind();
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		glEnable( GL_TEXTURE_GEN_S );
		glTexCoord2f( 1, 0.5 );
	}

	// the first texture will be used for alpha tested surfaces
	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	// decal surfaces may enable polygon offset
	glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() );

	GL_State( GLS_DEPTHFUNC_LESS );

	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	glEnable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 1, 255 );

	RB_RenderDrawSurfListWithFunction( drawSurfs, numDrawSurfs, RB_T_FillDepthBuffer );

	if ( backEnd.viewDef->numClipPlanes ) {
		GL_SelectTexture( 1 );
		globalImages->BindNull();
		glDisable( GL_TEXTURE_GEN_S );
		GL_SelectTexture( 0 );
	}

}

typedef bool (*rbDrawSurfFilter_t)( const drawSurf_t *surf );

static bool RB_MaterialIsSkyForSSAODepth( const idMaterial *material ) {
	if ( material == NULL ) {
		return false;
	}
	if ( RB_MaterialIsPortalSkyForSSAO( material ) ) {
		return true;
	}

	return material->HasTexgen( TG_SKYBOX_CUBE ) || material->HasTexgen( TG_WOBBLESKY_CUBE );
}

static bool RB_SSAOWorldDepthSurfFilter( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL || surf->geo == NULL || surf->material == NULL ) {
		return false;
	}
	if ( ( surf->dsFlags & DSF_BSE_EFFECT ) != 0 ) {
		return false;
	}
	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		return false;
	}

	const idMaterial *material = surf->material;
	if ( !material->IsDrawn() || material->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	if ( material->GetSort() >= SS_POST_PROCESS || material->GetSort() == SS_SUBVIEW ) {
		return false;
	}
	if ( material->HasGui() || material->SuppressInSubview() || RB_MaterialIsSkyForSSAODepth( material ) ) {
		return false;
	}

	const idRenderEntityLocal *entityDef = surf->space->entityDef;
	if ( entityDef == NULL ) {
		return true;
	}

	const renderEntity_t &renderEntity = entityDef->parms;
	if ( renderEntity.remoteRenderView != NULL
		|| renderEntity.allowSurfaceInViewID != 0
		|| renderEntity.weaponDepthHackInViewID != 0
		|| renderEntity.modelDepthHack != 0.0f ) {
		return false;
	}
	return true;
}

static int RB_RenderDrawSurfListWithFilter( drawSurf_t **drawSurfs, int numDrawSurfs, void (*triFunc_)( const drawSurf_t * ), rbDrawSurfFilter_t filter ) {
	int rendered = 0;
	backEnd.currentSpace = NULL;

	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *drawSurf = drawSurfs[i];
		if ( filter != NULL && !filter( drawSurf ) ) {
			continue;
		}
		if ( drawSurf == NULL || drawSurf->space == NULL ) {
			continue;
		}

		if ( drawSurf->space != backEnd.currentSpace ) {
			glLoadMatrixf( drawSurf->space->modelViewMatrix );
		}

		if ( drawSurf->space->weaponDepthHack ) {
			RB_EnterWeaponDepthHack();
		}

		if ( drawSurf->space->modelDepthHack != 0.0f ) {
			RB_EnterModelDepthHack( drawSurf->space->modelDepthHack );
		}

		if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( drawSurf->scissorRect ) ) {
			backEnd.currentScissor = drawSurf->scissorRect;
			glScissor(
				backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
				backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
				backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
				backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
		}

		triFunc_( drawSurf );

		if ( drawSurf->space->weaponDepthHack || drawSurf->space->modelDepthHack != 0.0f ) {
			RB_LeaveDepthHack();
		}

		backEnd.currentSpace = drawSurf->space;
		rendered++;
	}

	return rendered;
}

static int RB_STD_FillDepthBufferFiltered( drawSurf_t **drawSurfs, int numDrawSurfs, rbDrawSurfFilter_t filter ) {
	if ( !backEnd.viewDef->viewEntitys ) {
		return 0;
	}

	if ( backEnd.viewDef->numClipPlanes ) {
		GL_SelectTexture( 1 );
		globalImages->alphaNotchImage->Bind();
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		glEnable( GL_TEXTURE_GEN_S );
		glTexCoord2f( 1, 0.5 );
	}

	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() );

	GL_State( GLS_DEPTHFUNC_LESS );
	glEnable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 1, 255 );

	const int rendered = RB_RenderDrawSurfListWithFilter( drawSurfs, numDrawSurfs, RB_T_FillDepthBuffer, filter );

	if ( backEnd.viewDef->numClipPlanes ) {
		GL_SelectTexture( 1 );
		globalImages->BindNull();
		glDisable( GL_TEXTURE_GEN_S );
		GL_SelectTexture( 0 );
	}

	return rendered;
}

static void RB_RestoreAfterSSAOWorldDepthCapture( void ) {
	// The SSAO depth snapshot replays alpha-tested material stages before the
	// real scene render. Scrub legacy material state so that replay cannot tint
	// or texture later world-model passes.
	glUseProgramObjectARB( 0 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, 0 );

	glDisable( GL_ALPHA_TEST );
	glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	glDisable( GL_POLYGON_OFFSET_FILL );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );

	const int maxStateUnits = Max( 0, Min( MAX_MULTITEXTURE_UNITS, Min( glConfig.maxTextureUnits, glConfig.maxTextureImageUnits ) ) );
	for ( int unit = maxStateUnits - 1; unit >= 0; unit-- ) {
		GL_SelectTexture( unit );
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_R );
		glDisable( GL_TEXTURE_GEN_Q );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
		GL_TexEnv( GL_MODULATE );
		globalImages->BindNull();
		if ( unit != 0 ) {
			glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		}
	}

	GL_SelectTexture( 0 );
	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	backEnd.glState.forceGlState = true;
}

static void RB_CaptureSSAOWorldDepthImage( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	rbSSAOWorldDepthFrame = -1;
	rbSSAOWorldDepthWidth = 0;
	rbSSAOWorldDepthHeight = 0;

	if ( !RB_SSAORequestedForCurrentView() || globalImages == NULL || backEnd.viewDef == NULL ) {
		return;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	idImage *worldDepthImage = RB_EnsureSSAODepthScratchImage( rbSSAOWorldDepthImage, "_ssaoWorldDepth", viewportWidth, viewportHeight );
	if ( worldDepthImage == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_CaptureSSAOWorldDepthImage ----------\n" );

	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	const int rendered = RB_STD_FillDepthBufferFiltered( drawSurfs, numDrawSurfs, RB_SSAOWorldDepthSurfFilter );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	if ( rendered > 0 ) {
		worldDepthImage->CopyDepthbuffer(
			backEnd.viewDef->viewport.x1,
			backEnd.viewDef->viewport.y1,
			viewportWidth,
			viewportHeight );
		rbSSAOWorldDepthFrame = backEnd.frameCount;
		rbSSAOWorldDepthWidth = viewportWidth;
		rbSSAOWorldDepthHeight = viewportHeight;
	}

	RB_RestoreAfterSSAOWorldDepthCapture();

	// Restore the view to a clean depth/stencil buffer before the normal renderer
	// either runs its full depth prepass or accepts the modern visible handoff.
	RB_BeginDrawingView();
}

/*
==================
RB_CelWorldDepthSurfFilter

World geometry only. Model entities are excluded because they are inked by the
outline shell instead, and letting them into the snapshot would draw a second
line along every silhouette the shell already covers.
==================
*/
static bool RB_CelWorldDepthSurfFilter( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL || surf->geo == NULL || surf->material == NULL ) {
		return false;
	}
	if ( ( surf->dsFlags & DSF_BSE_EFFECT ) != 0 || !R_CelSurfaceIsWorld( surf ) ) {
		return false;
	}

	const idMaterial *material = surf->material;
	if ( !material->IsDrawn() || material->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	if ( material->GetSort() >= SS_POST_PROCESS || material->GetSort() == SS_SUBVIEW ) {
		return false;
	}
	if ( material->HasGui() || material->SuppressInSubview() || RB_MaterialIsSkyForSSAODepth( material ) ) {
		return false;
	}

	return true;
}

/*
==================
RB_CaptureCelWorldDepthImage

Snapshots world-only depth before the real depth prepass, the same way SSAO
does. The pass runs at all only while r_celShadingWorld is enabled, so the cost
belongs entirely to the cel mode.
==================
*/
static void RB_CaptureCelWorldDepthImage( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	rbCelWorldDepthFrame = -1;
	rbCelWorldDepthWidth = 0;
	rbCelWorldDepthHeight = 0;

	if ( !RB_CelWorldOutlineRequestedForCurrentView() || globalImages == NULL || backEnd.viewDef == NULL ) {
		return;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	idImage *worldDepthImage = RB_EnsureSSAODepthScratchImage( rbCelWorldDepthImage, "_celWorldDepth", viewportWidth, viewportHeight );
	if ( worldDepthImage == NULL ) {
		return;
	}

	RB_LogComment( "---------- RB_CaptureCelWorldDepthImage ----------\n" );

	glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	const int rendered = RB_STD_FillDepthBufferFiltered( drawSurfs, numDrawSurfs, RB_CelWorldDepthSurfFilter );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	if ( rendered > 0 ) {
		worldDepthImage->CopyDepthbuffer(
			backEnd.viewDef->viewport.x1,
			backEnd.viewDef->viewport.y1,
			viewportWidth,
			viewportHeight );
		rbCelWorldDepthFrame = backEnd.frameCount;
		rbCelWorldDepthWidth = viewportWidth;
		rbCelWorldDepthHeight = viewportHeight;
	} else {
		RB_ReportCelWorldOutlineSkip( "no world surfaces passed the depth snapshot filter" );
	}

	RB_RestoreAfterSSAOWorldDepthCapture();
	RB_BeginDrawingView();
}

/*
=============================================================================================

SHADER PASSES

=============================================================================================
*/

/*
==================
RB_SetProgramEnvironment

Sets variables that can be used by all vertex programs
==================
*/
void RB_SetProgramEnvironment( void ) {
	float	parm[4];
	int		pot;

	if ( !glConfig.ARBVertexProgramAvailable ) {
		return;
	}

#if 0
	// screen power of two correction factor, one pixel in so we don't get a bilerp
	// of an uncopied pixel
	int	 w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	pot = globalImages->currentRenderImage->GetOpts().width;
	if ( w == pot ) {
		parm[0] = 1.0;
	} else {
		parm[0] = (float)(w-1) / pot;
	}

	int	 h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	pot = globalImages->currentRenderImage->GetOpts().height;
	if ( h == pot ) {
		parm[1] = 1.0;
	} else {
		parm[1] = (float)(h-1) / pot;
	}

	parm[2] = 0;
	parm[3] = 1;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 0, parm );
#else
	// screen power of two correction factor, assuming the copy to _currentRender
	// also copied an extra row and column for the bilerp
	int	 w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	pot = globalImages->currentRenderImage->GetOpts().width;
	parm[0] = (float)w / pot;

	int	 h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	pot = globalImages->currentRenderImage->GetOpts().height;
	parm[1] = (float)h / pot;

	parm[2] = 0.0f;
	parm[3] = 1.0f;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 0, parm );
#endif

	if ( glConfig.ARBFragmentProgramAvailable ) {
		glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, parm );

		// window coord to 0.0 to 1.0 conversion
		parm[0] = 1.0f / w;
		parm[1] = 1.0f / h;
		parm[2] = 0.0f;
		parm[3] = 1.0f;
		glProgramEnvParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 1, parm );
	}

	//
	// set eye position in global space
	//
	parm[0] = backEnd.viewDef->renderView.vieworg[0];
	parm[1] = backEnd.viewDef->renderView.vieworg[1];
	parm[2] = backEnd.viewDef->renderView.vieworg[2];
	parm[3] = 1.0;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 1, parm );


}

/*
==================
RB_SetProgramEnvironmentSpace

Sets variables related to the current space that can be used by all vertex programs
==================
*/
void RB_SetProgramEnvironmentSpace( void ) {
	if ( !glConfig.ARBVertexProgramAvailable ) {
		return;
	}

	const struct viewEntity_s *space = backEnd.currentSpace;
	float	parm[4];

	// set eye position in local space
	R_GlobalPointToLocal( space->modelMatrix, backEnd.viewDef->renderView.vieworg, *(idVec3 *)parm );
	parm[3] = 1.0;
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 5, parm );

	// we need the model matrix without it being combined with the view matrix
	// so we can transform local vectors to global coordinates
	parm[0] = space->modelMatrix[0];
	parm[1] = space->modelMatrix[4];
	parm[2] = space->modelMatrix[8];
	parm[3] = space->modelMatrix[12];
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 6, parm );
	parm[0] = space->modelMatrix[1];
	parm[1] = space->modelMatrix[5];
	parm[2] = space->modelMatrix[9];
	parm[3] = space->modelMatrix[13];
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 7, parm );
	parm[0] = space->modelMatrix[2];
	parm[1] = space->modelMatrix[6];
	parm[2] = space->modelMatrix[10];
	parm[3] = space->modelMatrix[14];
	glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, 8, parm );
}

/*
==================
RB_STD_T_RenderShaderPasses

This is also called for the generated 2D rendering
==================
*/
void RB_STD_T_RenderShaderPasses( const drawSurf_t *surf ) {
	int			stage;
	const idMaterial	*shader;
	const shaderStage_t *pStage;
	const float	*regs;
	float		color[4];
	const srfTriangles_t	*tri;

	tri = surf->geo;
	shader = surf->material;

	if ( !shader->HasAmbient() ) {
		return;
	}

	if ( shader->IsPortalSky() ) {
		return;
	}

	// change the matrix if needed
	if ( surf->space != backEnd.currentSpace ) {
		glLoadMatrixf( surf->space->modelViewMatrix );
		backEnd.currentSpace = surf->space;
		RB_SetProgramEnvironmentSpace();
	}

	// change the scissor if needed
	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
		backEnd.currentScissor = surf->scissorRect;
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1, 
			backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}

	// some deforms may disable themselves by setting numIndexes = 0
	if ( !tri->numIndexes ) {
		return;
	}

	if ( !RB_EnsurePackedClassicDrawCaches( surf, shader->ReceivesLighting(), true ) ) {
		return;
	}

	if ( !tri->ambientCache ) {
		common->Printf( "RB_T_RenderShaderPasses: !tri->ambientCache\n" );
		return;
	}

	// get the expressions for conditionals / color / texcoords
	regs = surf->shaderRegisters;

	// set face culling appropriately
	GL_Cull( shader->GetCullType() );

	// set polygon offset if necessary
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	const bool useAlphaToCoverage = RB_UseAlphaToCoverage( shader );
	if ( useAlphaToCoverage ) {
		glEnable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}
	
	if ( surf->space->weaponDepthHack ) {
		RB_EnterWeaponDepthHack();
	}

	if ( surf->space->modelDepthHack != 0.0f ) {
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
	}

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );
	bool resetTexCoords = false;

	const int stageCount = shader->GetNumStages();
	for ( stage = 0; stage < stageCount ; stage++ ) {
		pStage = shader->GetStage(stage);
		if ( backEnd.viewDef->isGlowView && !pStage->glowStage ) {
			continue;
		}

		// check the enable condition
		if ( regs[ pStage->conditionRegister ] == 0 ) {
			continue;
		}

		// skip the stages involved in lighting
		if ( pStage->lighting != SL_AMBIENT ) {
			continue;
		}

		// skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
		if ( ( pStage->drawStateBits & (GLS_SRCBLEND_BITS|GLS_DSTBLEND_BITS) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
			continue;
		}

		if ( resetTexCoords ) {
			glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );
			resetTexCoords = false;
		}

		if ( pStage->texture.texgen == TG_POT_CORRECTION && surf->dynamicTexCoords != NULL ) {
			glTexCoordPointer( 2, GL_FLOAT, 0, vertexCache.Position( surf->dynamicTexCoords ) );
			resetTexCoords = true;
		}

		// Fallback for materials that reference captured scene buffers but were not sorted as
		// post-process. Offscreen render-texture passes manage their own captures and must not
		// overwrite them here after clearing the destination render target.
		if ( !backEnd.currentRenderCopied && RB_AutomaticCurrentRenderCaptureAllowed() && RB_StageUsesCurrentRender( pStage ) ) {
			RB_CaptureCurrentRenderImage(
				backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
		}
		if ( !backEnd.currentDepthCopied && RB_AutomaticCurrentRenderCaptureAllowed() && RB_StageUsesCurrentDepth( pStage ) ) {
			RB_CaptureCurrentDepthImage(
				backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
		}

		// see if we are a new-style stage
		newShaderStage_t *newStage = pStage->newStage;
		if ( newStage ) {
			if ( newStage->customLighting ) {
				continue;
			}
			// This debug cvar suppresses world/material ambient programs only;
			// fullscreen post-process GLSL stages still need to run.
			if ( r_skipNewAmbient.GetBool() && shader->GetSort() < SS_POST_PROCESS ) {
				continue;
			}

			//--------------------------
			//
			// new style stages
			//
			//--------------------------

			if ( newStage->glslProgram ) {
				if ( !R_ValidateGLSLProgram( newStage ) ) {
					continue;
				}
				const bool useExplicitSMAAFullscreenQuad = RB_IsSMAAPostAAGLSLProgram( newStage );

				// GLSL stages in Quake 4 decal materials often rely on gl_Color
				// from per-vertex stage colors (for DecalLife/depth fade).
				float stageColor[4];
				stageColor[0] = regs[ pStage->color.registers[0] ];
				stageColor[1] = regs[ pStage->color.registers[1] ];
				stageColor[2] = regs[ pStage->color.registers[2] ];
				stageColor[3] = regs[ pStage->color.registers[3] ];
				bool useColorArray = false;
				if ( pStage->vertexColor == SVC_IGNORE || useExplicitSMAAFullscreenQuad ) {
					glColor4fv( stageColor );
				} else {
					RB_SetStageVertexColorPointer( surf, stage, ac );
					glEnableClientState( GL_COLOR_ARRAY );
					useColorArray = true;
				}

				GL_State( pStage->drawStateBits );
				glUseProgramObjectARB( (GLhandleARB)newStage->glslProgramObject );

				for ( int i = 0; i < newStage->numShaderParms; i++ ) {
					const int location = newStage->shaderParmLocations[i];
					if ( location < 0 ) {
						continue;
					}

					if ( RB_BindGLSLShaderParm( newStage->shaderParmBindings[i], location, pStage, NULL ) ) {
						continue;
					}

					const int numRegisters = newStage->shaderParmNumRegisters[i];
					if ( numRegisters <= 0 ) {
						continue;
					}

					float parm[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					for ( int j = 0; j < numRegisters && j < 4; j++ ) {
						parm[j] = regs[ newStage->shaderParmRegisters[i][j] ];
					}

					switch ( numRegisters ) {
					case 1:
						glUniform1fvARB( location, 1, parm );
						break;
					case 2:
						glUniform2fvARB( location, 1, parm );
						break;
					case 3:
						glUniform3fvARB( location, 1, parm );
						break;
					default:
						glUniform4fvARB( location, 1, parm );
						break;
					}
				}

				for ( int i = 0; i < newStage->numShaderTextures; i++ ) {
					idImage *image = RB_ResolveGLSLShaderTextureImage( newStage, i, NULL );
					if ( image == NULL ) {
						continue;
					}
					GL_SelectTexture( i );
					image->SetSamplerState( newStage->shaderTextureFilters[i], newStage->shaderTextureRepeats[i] );
					image->Bind();
					if ( newStage->shaderTextureLocations[i] >= 0 ) {
						glUniform1iARB( newStage->shaderTextureLocations[i], i );
					}
				}
				if ( useExplicitSMAAFullscreenQuad ) {
					RB_PoisonPostAAGLSLStateForValidation();
					RB_DrawSMAAExplicitFullscreenQuad();
				} else {
					// GL_SelectTexture also selects the client texcoord array; draw legacy
					// fullscreen/material geometry with gl_TexCoord[0] as the active lane.
					GL_SelectTexture( 0 );

					if ( !RB_PrepareStageTexturing( pStage, surf, ac ) ) {
						RB_FinishStageTexturing( pStage, surf, ac );
						for ( int i = 1; i < newStage->numShaderTextures; i++ ) {
							if ( RB_ResolveGLSLShaderTextureImage( newStage, i, NULL ) != NULL ) {
								GL_SelectTexture( i );
								globalImages->BindNull();
							}
						}
						GL_SelectTexture( 0 );
						glUseProgramObjectARB( 0 );
						if ( useColorArray ) {
							glDisableClientState( GL_COLOR_ARRAY );
						}
						continue;
					}
					RB_DrawElementsWithCounters( tri );
					RB_FinishStageTexturing( pStage, surf, ac );
				}

				for ( int i = 1; i < newStage->numShaderTextures; i++ ) {
					if ( RB_ResolveGLSLShaderTextureImage( newStage, i, NULL ) != NULL ) {
						GL_SelectTexture( i );
						globalImages->BindNull();
					}
				}

				GL_SelectTexture( 0 );
				glUseProgramObjectARB( 0 );
				if ( useColorArray ) {
					glDisableClientState( GL_COLOR_ARRAY );
				}
				continue;
			}

			// completely skip ARB program stages if we don't have the capability
			if ( tr.backEndRenderer != BE_ARB2 ) {
				continue;
			}

			bool usingPackedMaterialStage = false;
			if ( R_TriHasPrimBatchMesh( tri ) && newStage->md5rVertexProgram != 0 ) {
				usingPackedMaterialStage = RB_ARB2_PreparePackedMD5RProgramStageDraw( surf );
			}

			if ( !usingPackedMaterialStage ) {
				RB_SetStageVertexColorPointer( surf, stage, ac );
				glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET ) );
				glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET ) );
				glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, normal ) ) );

				glEnableClientState( GL_COLOR_ARRAY );
				glEnableVertexAttribArrayARB( 9 );
				glEnableVertexAttribArrayARB( 10 );
				glEnableClientState( GL_NORMAL_ARRAY );
			}

			GL_State( pStage->drawStateBits );

			int stageVertexProgram = newStage->vertexProgram;
			if ( usingPackedMaterialStage ) {
				stageVertexProgram = newStage->md5rVertexProgram;
			}

			bool vertexProgramEnabled = false;
			bool fragmentProgramEnabled = false;
			if ( stageVertexProgram != 0 ) {
				if ( !R_BindARBProgram( GL_VERTEX_PROGRAM_ARB, stageVertexProgram, "material stage vertex program", false ) ) {
					if ( usingPackedMaterialStage ) {
						RB_ARB2_ClearPreparedPackedMD5RDraw();
					} else {
						glDisableClientState( GL_COLOR_ARRAY );
						glDisableVertexAttribArrayARB( 9 );
						glDisableVertexAttribArrayARB( 10 );
						glDisableClientState( GL_NORMAL_ARRAY );
					}
					continue;
				}
				glEnable( GL_VERTEX_PROGRAM_ARB );
				if ( usingPackedMaterialStage ) {
					RB_ARB2_LoadMD5RLocalViewOrigin( surf );
					RB_ARB2_LoadMD5RMVPMatrix( surf );
					RB_ARB2_LoadMD5RProjectionMatrix();
					RB_ARB2_LoadMD5RModelViewMatrix( surf );
				}
				vertexProgramEnabled = true;
			}

			// megaTextures bind a lot of images and set a lot of parameters
			//if ( newStage->megaTexture ) {
			//	newStage->megaTexture->SetMappingForSurface( tri );
			//	idVec3	localViewer;
			//	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewer );
			//	newStage->megaTexture->BindForViewOrigin( localViewer );
			//}

			if ( newStage->fragmentProgram != 0 ) {
				if ( !R_BindARBProgram( GL_FRAGMENT_PROGRAM_ARB, newStage->fragmentProgram, "material stage fragment program", false ) ) {
					if ( vertexProgramEnabled ) {
						glDisable( GL_VERTEX_PROGRAM_ARB );
						glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
					}
					if ( usingPackedMaterialStage ) {
						RB_ARB2_ClearPreparedPackedMD5RDraw();
					} else {
						glDisableClientState( GL_COLOR_ARRAY );
						glDisableVertexAttribArrayARB( 9 );
						glDisableVertexAttribArrayARB( 10 );
						glDisableClientState( GL_NORMAL_ARRAY );
					}
					continue;
				}
				glEnable( GL_FRAGMENT_PROGRAM_ARB );
				fragmentProgramEnabled = true;
			}

			if ( !vertexProgramEnabled && !fragmentProgramEnabled ) {
				if ( usingPackedMaterialStage ) {
					RB_ARB2_ClearPreparedPackedMD5RDraw();
				} else {
					glDisableClientState( GL_COLOR_ARRAY );
					glDisableVertexAttribArrayARB( 9 );
					glDisableVertexAttribArrayARB( 10 );
					glDisableClientState( GL_NORMAL_ARRAY );
				}
				continue;
			}

			for ( int i = 0 ; i < newStage->numVertexParms ; i++ ) {
				float	parm[4];
				parm[0] = regs[ newStage->vertexParms[i][0] ];
				parm[1] = regs[ newStage->vertexParms[i][1] ];
				parm[2] = regs[ newStage->vertexParms[i][2] ];
				parm[3] = regs[ newStage->vertexParms[i][3] ];
				if ( vertexProgramEnabled ) {
					glProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, i, parm );
				}
				if ( fragmentProgramEnabled && newStage->numFragmentParms == 0 ) {
					glProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, i, parm );
				}
			}

			if ( fragmentProgramEnabled && newStage->numFragmentParms > 0 ) {
				for ( int i = 0 ; i < newStage->numFragmentParms ; i++ ) {
					float	parm[4];
					parm[0] = regs[ newStage->fragmentParms[i][0] ];
					parm[1] = regs[ newStage->fragmentParms[i][1] ];
					parm[2] = regs[ newStage->fragmentParms[i][2] ];
					parm[3] = regs[ newStage->fragmentParms[i][3] ];
					glProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, i, parm );
				}
			}

			if ( fragmentProgramEnabled ) {
				for ( int i = 0 ; i < newStage->numFragmentProgramImages ; i++ ) {
					if ( newStage->fragmentProgramImages[i] ) {
						GL_SelectTexture( i );
						newStage->fragmentProgramImages[i]->Bind();
					}
				}
			}

			// draw it
			RB_DrawElementsWithCounters( tri );

			if ( fragmentProgramEnabled ) {
				for ( int i = 1 ; i < newStage->numFragmentProgramImages ; i++ ) {
					if ( newStage->fragmentProgramImages[i] ) {
						GL_SelectTexture( i );
						globalImages->BindNull();
					}
				}
			}
			//if ( newStage->megaTexture ) {
			//	newStage->megaTexture->Unbind();
			//}

			GL_SelectTexture( 0 );

			if ( vertexProgramEnabled ) {
				glDisable( GL_VERTEX_PROGRAM_ARB );
			}
			if ( fragmentProgramEnabled ) {
				glDisable( GL_FRAGMENT_PROGRAM_ARB );
			}
			// Fixme: Hack to get around an apparent bug in ATI drivers.  Should remove as soon as it gets fixed.
			glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );

			if ( usingPackedMaterialStage ) {
				RB_ARB2_ClearPreparedPackedMD5RDraw();
			} else {
				glDisableClientState( GL_COLOR_ARRAY );
				glDisableVertexAttribArrayARB( 9 );
				glDisableVertexAttribArrayARB( 10 );
				glDisableClientState( GL_NORMAL_ARRAY );
			}
			continue;
		}

		//--------------------------
		//
		// old style stages
		//
		//--------------------------

		// Dynamic reflection/refraction stages exist only to refresh offscreen render targets.
		// The captured images are sampled by later stages via _reflectionRender/_refractionRender.
		if ( pStage->texture.dynamic == DI_REFLECTION_RENDER
			|| pStage->texture.dynamic == DI_REFRACTION_RENDER ) {
			continue;
		}

		// set the color
		color[0] = regs[ pStage->color.registers[0] ];
		color[1] = regs[ pStage->color.registers[1] ];
		color[2] = regs[ pStage->color.registers[2] ];
		color[3] = regs[ pStage->color.registers[3] ];

		// skip the entire stage if an add would be black
		if ( ( pStage->drawStateBits & (GLS_SRCBLEND_BITS|GLS_DSTBLEND_BITS) ) == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) 
			&& color[0] <= 0 && color[1] <= 0 && color[2] <= 0 ) {
			continue;
		}

		// skip the entire stage if a blend would be completely transparent
		if ( ( pStage->drawStateBits & (GLS_SRCBLEND_BITS|GLS_DSTBLEND_BITS) ) == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
			&& color[3] <= 0 ) {
			continue;
		}

		if ( RB_TryDrawSoftParticleStage( surf, pStage, regs, tri, ac, stage, color ) ) {
			continue;
		}

		const bool hasBakedDecalStageColor =
			( surf->decalColorCache != NULL && stage >= 0 && stage < surf->decalColorStageCount && surf->decalColorStride > 0 );

		// select the vertex color source
		if ( pStage->vertexColor == SVC_IGNORE ) {
			glColor4fv( color );
		} else {
			RB_SetStageVertexColorPointer( surf, stage, ac );
			glEnableClientState( GL_COLOR_ARRAY );

			if ( pStage->vertexColor == SVC_INVERSE_MODULATE ) {
				GL_TexEnv( GL_COMBINE_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_TEXTURE );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_PRIMARY_COLOR_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_ONE_MINUS_SRC_COLOR );
				glTexEnvi( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1 );
			}

			// for vertex color and modulated color, we need to enable a second
			// texture stage. Skip this when decal stages already baked stage
			// color into per-vertex data; applying both paths darkens decals.
			if ( !hasBakedDecalStageColor && ( color[0] != 1 || color[1] != 1 || color[2] != 1 || color[3] != 1 ) ) {
				GL_SelectTexture( 1 );

				globalImages->whiteImage->Bind();
				GL_TexEnv( GL_COMBINE_ARB );

				glTexEnvfv( GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color );

				glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_MODULATE );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PREVIOUS_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_RGB_ARB, GL_CONSTANT_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_RGB_ARB, GL_SRC_COLOR );
				glTexEnvi( GL_TEXTURE_ENV, GL_RGB_SCALE_ARB, 1 );

				glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_MODULATE );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_PREVIOUS_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE1_ALPHA_ARB, GL_CONSTANT_ARB );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA );
				glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND1_ALPHA_ARB, GL_SRC_ALPHA );
				glTexEnvi( GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1 );

				GL_SelectTexture( 0 );
			}
		}

		// bind the texture
		RB_BindVariableStageImage( &pStage->texture, regs );

		// set the state
		GL_State( pStage->drawStateBits );
		
		if ( !RB_PrepareStageTexturing( pStage, surf, ac ) ) {
			RB_FinishStageTexturing( pStage, surf, ac );
			if ( pStage->vertexColor != SVC_IGNORE ) {
				glDisableClientState( GL_COLOR_ARRAY );

				GL_SelectTexture( 1 );
				GL_TexEnv( GL_MODULATE );
				globalImages->BindNull();
				GL_SelectTexture( 0 );
				GL_TexEnv( GL_MODULATE );
			}
			continue;
		}

		// draw it
		RB_DrawElementsWithCounters( tri );

		RB_FinishStageTexturing( pStage, surf, ac );

		if ( pStage->vertexColor != SVC_IGNORE ) {
			glDisableClientState( GL_COLOR_ARRAY );

			GL_SelectTexture( 1 );
			GL_TexEnv( GL_MODULATE );
			globalImages->BindNull();
			GL_SelectTexture( 0 );
			GL_TexEnv( GL_MODULATE );
		}
	}

	// reset polygon offset
	if ( shader->TestMaterialFlag(MF_POLYGONOFFSET) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		RB_LeaveDepthHack();
	}

	if ( useAlphaToCoverage ) {
		glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}
}

/*
=====================
RB_STD_DrawShaderPasses

Draw non-light dependent passes
=====================
*/
static bool RB_STD_LightGridInlinePassAvailable( void );
static bool RB_STD_DrawLightGridInlineSurface( const drawSurf_t *surf );
static bool RB_LightGridUseDepthTextureCompare( void );
static bool rbLightGridInlineSubmittedThisView = false;

int RB_STD_DrawShaderPasses( drawSurf_t **drawSurfs, int numDrawSurfs, rbShaderPassSurfFilter_t filter = NULL ) {
	int				i;

	if ( drawSurfs == NULL || numDrawSurfs <= 0 ) {
		return 0;
	}

	// only obey skipAmbient if we are rendering a view
	if ( backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool() ) {
		return numDrawSurfs;
	}

	RB_LogComment( "---------- RB_STD_DrawShaderPasses ----------\n" );

	// if we are about to draw the first surface that needs
	// the rendering in a texture, copy it over
	if ( drawSurfs[0]->material->GetSort() >= SS_POST_PROCESS ) {
		const int lightGridReportFrames = r_lightGridReport.GetInteger();
		if ( lightGridReportFrames > 0 && ( backEnd.frameCount % lightGridReportFrames ) == 0 ) {
			idStr materialNames;
			const int sampleCount = Min( numDrawSurfs, 8 );
			for ( int surfIndex = 0; surfIndex < sampleCount; surfIndex++ ) {
				if ( surfIndex > 0 ) {
					materialNames.Append( ", " );
				}
				const idMaterial *material = drawSurfs[surfIndex] != NULL ? drawSurfs[surfIndex]->material : NULL;
				materialNames.Append( material != NULL ? material->GetName() : "<null>" );
			}
			common->Printf(
				"PostProcess surface pass: frame %i count=%i currentRender=%i renderTexture=%i first=%s sample=[%s]\n",
				backEnd.frameCount,
				numDrawSurfs,
				backEnd.currentRenderCopied ? 1 : 0,
				backEnd.renderTexture != NULL ? 1 : 0,
				drawSurfs[0]->material->GetName(),
				materialNames.c_str() );
		}
		if ( r_skipPostProcess.GetBool() ) {
			return 0;
		}

		bool needsCurrentDepth = false;
		for ( int surfIndex = 0; surfIndex < numDrawSurfs; surfIndex++ ) {
			if ( RB_MaterialUsesCurrentDepth( drawSurfs[surfIndex]->material ) ) {
				needsCurrentDepth = true;
				break;
			}
		}

		// Copy the current view for any post-process material sampling _currentRender.
		// Do not gate this on viewEntitys: world-only views may still contain post-process surfaces.
		// Offscreen render-texture passes capture _currentRender explicitly and must keep that copy.
		if ( RB_AutomaticCurrentRenderCaptureAllowed() ) {
			RB_CaptureCurrentRenderImage(
				backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
				backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
			if ( needsCurrentDepth ) {
				RB_CaptureCurrentDepthImage(
					backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
					backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
			}
		} else {
			// Offscreen fullscreen passes are explicitly managed by the caller. Mark the copy as
			// satisfied so SS_POST_PROCESS surfaces are allowed to draw in this view.
			backEnd.currentRenderCopied = true;
		}
	}

	GL_SelectTexture( 1 );
	globalImages->BindNull();

	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	RB_SetProgramEnvironment();

	// The post-ambient light-grid pass owns the complete receiver set. Inline
	// submission can only see surfaces that happen to pass the material-stage
	// checks during ambient drawing; one small submitted surface would otherwise
	// suppress the full pass and make most of the world miss baked lighting.
	const bool drawInlineLightGrid = false;

	// we don't use RB_RenderDrawSurfListWithFunction()
	// because we want to defer the matrix load because many
	// surfaces won't draw any ambient passes
	backEnd.currentSpace = NULL;
	for (i = 0  ; i < numDrawSurfs ; i++ ) {
		if ( filter != NULL && !filter( drawSurfs[i] ) ) {
			continue;
		}
		if ( drawSurfs[i]->material->SuppressInSubview() ) {
			continue;
		}

		if ( backEnd.viewDef->isXraySubview && drawSurfs[i]->space->entityDef ) {
			//if ( drawSurfs[i]->space->entityDef->parms.xrayIndex != 2 ) {
			//	continue;
			//}
		}

		if ( drawSurfs[i]->material->TestMaterialFlag( MF_NEED_CURRENT_RENDER )
			&& drawSurfs[i]->material->GetSort() < SS_POST_PROCESS
			&& !backEnd.currentRenderCopied ) {
			if ( RB_AutomaticCurrentRenderCaptureAllowed() ) {
				RB_CaptureCurrentRenderImage(
					backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1,
					backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
			} else {
				backEnd.currentRenderCopied = true;
			}
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if ( drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS
			&& !backEnd.currentRenderCopied ) {
			break;
		}

		RB_STD_T_RenderShaderPasses( drawSurfs[i] );
		if ( drawInlineLightGrid && RB_STD_DrawLightGridInlineSurface( drawSurfs[i] ) ) {
			rbLightGridInlineSubmittedThisView = true;
			backEnd.currentRenderCopied = false;
		}
	}

	GL_Cull( CT_FRONT_SIDED );
	glColor3f( 1, 1, 1 );

	return i;
}

static bool RB_GlowEnabledForCurrentView( void ) {
	return backEnd.viewDef != NULL
		&& !backEnd.viewDef->isSubview
		&& !backEnd.viewDef->isEditor
		&& !backEnd.viewDef->isGlowView
		&& backEnd.viewDef->viewEntitys != NULL
		&& !r_skipGlowOverlay.GetBool()
		&& r_glowStrength.GetFloat() > 0.0f
		&& globalImages->accumImage != NULL
		&& globalImages->glowScreenImage != NULL
		&& globalImages->glowCompositeImage != NULL;
}

static void RB_GlowSetScreenRect( void ) {
	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	const int viewportX = tr.viewportOffset[0] + backEnd.viewDef->viewport.x1;
	const int viewportY = tr.viewportOffset[1] + backEnd.viewDef->viewport.y1;
	glViewport( viewportX, viewportY, viewportWidth, viewportHeight );
	glScissor(
		viewportX + backEnd.viewDef->scissor.x1,
		viewportY + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
		backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
	backEnd.currentScissor = backEnd.viewDef->scissor;
}

static void RB_GlowBeginScreenPass( void ) {
	RB_GlowSetScreenRect();
	glMatrixMode( GL_MODELVIEW );
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0, 1, 0, 1, -4, 1 );
	GL_Cull( CT_TWO_SIDED );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	GL_SelectTexture( 1 );
	globalImages->BindNull();
	GL_SelectTexture( 0 );
	GL_TexEnv( GL_MODULATE );
}

static void RB_GlowEndScreenPass( void ) {
	glMatrixMode( GL_PROJECTION );
	glPopMatrix();
	glMatrixMode( GL_MODELVIEW );
	glPopMatrix();
	glEnable( GL_DEPTH_TEST );
	glEnable( GL_STENCIL_TEST );
	GL_Cull( CT_FRONT_SIDED );
}

static void RB_GlowDrawScreenQuad( float offsetX, float offsetY ) {
	glBegin( GL_QUADS );
	glTexCoord2f( 0.0f, 0.0f ); glVertex2f( offsetX, offsetY );
	glTexCoord2f( 1.0f, 0.0f ); glVertex2f( 1.0f + offsetX, offsetY );
	glTexCoord2f( 1.0f, 1.0f ); glVertex2f( 1.0f + offsetX, 1.0f + offsetY );
	glTexCoord2f( 0.0f, 1.0f ); glVertex2f( offsetX, 1.0f + offsetY );
	glEnd();
}

static void RB_GlowCopyViewToImage( idImage *image ) {
	if ( image == NULL ) {
		return;
	}
	const int width = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int height = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( width > 0 && height > 0 ) {
		image->CopyFramebuffer(
			tr.viewportOffset[0] + backEnd.viewDef->viewport.x1,
			tr.viewportOffset[1] + backEnd.viewDef->viewport.y1,
			width, height );
	}
}

static void RB_GlowDrawImage( idImage *image, int stateBits, float colorScale ) {
	if ( image == NULL ) {
		return;
	}
	GL_State( stateBits );
	GL_SelectTexture( 0 );
	image->Bind();
	glColor4f( colorScale, colorScale, colorScale, colorScale );
	RB_GlowDrawScreenQuad( 0.0f, 0.0f );
}

static void RB_GlowBlurPass( idImage *sourceImage, idImage *captureImage, bool horizontal ) {
	if ( sourceImage == NULL || captureImage == NULL ) {
		return;
	}
	const int steps = idMath::ClampInt( 0, 256, r_glowSteps.GetInteger() );
	const float startAlpha = idMath::ClampFloat( 0.0f, 8.0f, r_glowAlpha.GetFloat() );
	const float alphaChange = idMath::ClampFloat( 0.0f, 8.0f, r_glowAlphaChange.GetFloat() );
	const int blurExtent = horizontal ? sourceImage->GetUploadWidth() : sourceImage->GetUploadHeight();
	const float blurStep = 1.0f / (float)Max( 1, blurExtent );

	RB_GlowBeginScreenPass();
	GL_SelectTexture( 0 );
	sourceImage->Bind();
	GL_State( GLS_DEPTHFUNC_ALWAYS | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ZERO );
	glColor4f( 1, 1, 1, 1 );
	RB_GlowDrawScreenQuad( 0, 0 );
	if ( steps > 1 && startAlpha > 0.0f ) {
		float alpha = startAlpha;
		GL_State( GLS_DEPTHFUNC_ALWAYS | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE );
		for ( int step = 1; step < steps && alpha > 0.001f; step++ ) {
			const float offset = blurStep * (float)step;
			const float x = horizontal ? offset : 0.0f;
			const float y = horizontal ? 0.0f : offset;
			glColor4f( 1, 1, 1, alpha );
			RB_GlowDrawScreenQuad( x, y );
			RB_GlowDrawScreenQuad( -x, -y );
			alpha *= alphaChange;
		}
	}
	glColor4f( 1, 1, 1, 1 );
	RB_GlowCopyViewToImage( captureImage );
	RB_GlowEndScreenPass();
}

static void RB_STD_DrawGlowView( void ) {
	drawSurf_t **drawSurfs = (drawSurf_t **)&backEnd.viewDef->drawSurfs[0];
	const int numDrawSurfs = backEnd.viewDef->numDrawSurfs;
	if ( drawSurfs == NULL || numDrawSurfs <= 0 ) {
		return;
	}
	backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
	RB_BeginDrawingView();
	glClearColor( 0, 0, 0, 1 );
	glClear( GL_COLOR_BUFFER_BIT );
	RB_STD_FillDepthBuffer( drawSurfs, numDrawSurfs );
	glStencilFunc( GL_ALWAYS, 128, 255 );
	RB_STD_DrawShaderPasses( drawSurfs, numDrawSurfs );
}

static void RB_STD_GlowOverlay( void ) {
	if ( !RB_GlowEnabledForCurrentView() ) {
		return;
	}
	RB_LogComment( "---------- RB_STD_GlowOverlay ----------\n" );
	RB_GlowCopyViewToImage( globalImages->accumImage );

	viewDef_t glowView = *backEnd.viewDef;
	glowView.isGlowView = true;
	const viewDef_t *savedView = backEnd.viewDef;
	const int savedDepthFunc = backEnd.depthFunc;
	backEnd.viewDef = &glowView;
	RB_STD_DrawGlowView();
	backEnd.viewDef = savedView;
	backEnd.depthFunc = savedDepthFunc;

	RB_GlowCopyViewToImage( globalImages->glowScreenImage );
	RB_GlowBlurPass( globalImages->glowScreenImage, globalImages->glowCompositeImage, true );
	RB_GlowBlurPass( globalImages->glowCompositeImage, globalImages->glowCompositeImage, false );
	RB_GlowBeginScreenPass();
	RB_GlowDrawImage( globalImages->accumImage, GLS_DEPTHFUNC_ALWAYS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO, 1.0f );
	RB_GlowDrawImage( globalImages->glowCompositeImage, GLS_DEPTHFUNC_ALWAYS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE, r_glowStrength.GetFloat() );
	glColor4f( 1, 1, 1, 1 );
	RB_GlowEndScreenPass();
	backEnd.currentRenderCopied = false;
	backEnd.currentDepthCopied = false;
}



/*
==============================================================================

BACK END RENDERING OF STENCIL SHADOWS

==============================================================================
*/

/*
=====================
RB_T_Shadow

the shadow volumes face INSIDE
=====================
*/
// decided once per stencil shadow pass in RB_StencilShadowPass: requires the
// core GL 2.0 glStencilOpSeparate entry point (NOT the NVIDIA-only
// GL_EXT_stencil_two_side mechanism) and wrap stencil ops, without which the
// single-pass interleaving would not be order-equivalent to the legacy
// two-pass sequence on saturating INCR/DECR hardware
static bool rb_twoSidedStencilThisPass = false;

// space PP_LIGHT_ORIGIN was last uploaded for; tracked separately from
// backEnd.currentSpace because packed MD5R shadow surfaces advance
// currentSpace without uploading env[4], and their skinned palette rows
// overwrite env[3..5] outright
static const viewEntity_t *rb_shadowLightOriginSpace = NULL;

static void RB_T_Shadow( const drawSurf_t *surf ) {
	const srfTriangles_t	*tri;

	// set the light position if we are using a vertex program to project the rear surfaces
	if ( tr.backEndRendererHasVertexPrograms && r_useShadowVertexProgram.GetBool() ) {
		if ( R_TriHasPrimBatchMesh( surf->geo ) ) {
			rb_shadowLightOriginSpace = NULL;
		} else if ( surf->space != rb_shadowLightOriginSpace ) {
			idVec4 localLight;

			R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.vLight->globalLightOrigin, localLight.ToVec3() );
			localLight.w = 0.0f;
			glProgramEnvParameter4fvARB( GL_VERTEX_PROGRAM_ARB, PP_LIGHT_ORIGIN, localLight.ToFloatPtr() );
			rb_shadowLightOriginSpace = surf->space;
		}
	}

	tri = surf->geo;

	if ( !R_TriHasPrimBatchMesh( tri ) ) {
		if ( !tri->shadowCache ) {
			return;
		}

		glVertexPointer( 4, GL_FLOAT, sizeof( shadowCache_t ), vertexCache.Position(tri->shadowCache) );
	}

	// we always draw the sil planes, but we may not need to draw the front or rear caps
	int	numIndexes;
	bool external = false;

	if ( !r_useExternalShadows.GetInteger() ) {
		numIndexes = tri->numIndexes;
	} else if ( r_useExternalShadows.GetInteger() == 2 ) { // force to no caps for testing
		numIndexes = tri->numShadowIndexesNoCaps;
	} else if ( !(surf->dsFlags & DSF_VIEW_INSIDE_SHADOW) ) { 
		// if we aren't inside the shadow projection, no caps are ever needed needed
		numIndexes = tri->numShadowIndexesNoCaps;
		external = true;
	} else if ( !backEnd.vLight->viewInsideLight && !(surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE) ) {
		// if we are inside the shadow projection, but outside the light, and drawing
		// a non-infinite shadow, we can skip some caps
		if ( backEnd.vLight->viewSeesShadowPlaneBits & surf->geo->shadowCapPlaneBits ) {
			// we can see through a rear cap, so we need to draw it, but we can skip the
			// caps on the actual surface
			numIndexes = tri->numShadowIndexesNoFrontCaps;
		} else {
			// we don't need to draw any caps
			numIndexes = tri->numShadowIndexesNoCaps;
		}
		external = true;
	} else {
		// must draw everything
		numIndexes = tri->numIndexes;
	}

	// If this surface could not use external shadow optimizations, the caller will
	// have already forced the "no caps" index counts back to the full index count.
	// In that case treat it as an internal volume so we keep the robust stencil path.
	if ( numIndexes == tri->numIndexes ) {
		external = false;
	}

	// set depth bounds
	if( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) {
		glDepthBoundsEXT( surf->scissorRect.zmin, surf->scissorRect.zmax );
	}

	// debug visualization
	if ( r_showShadows.GetInteger() ) {
		if ( r_showShadows.GetInteger() == 3 ) {
			if ( external ) {
				glColor3f( 0.1/backEnd.overBright, 1/backEnd.overBright, 0.1/backEnd.overBright );
			} else {
				// these are the surfaces that require the reverse
				glColor3f( 1/backEnd.overBright, 0.1/backEnd.overBright, 0.1/backEnd.overBright );
			}
		} else {
			// draw different color for turboshadows
			if ( surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE ) {
				if ( numIndexes == tri->numIndexes ) {
					glColor3f( 1/backEnd.overBright, 0.1/backEnd.overBright, 0.1/backEnd.overBright );
				} else {
					glColor3f( 1/backEnd.overBright, 0.4/backEnd.overBright, 0.1/backEnd.overBright );
				}
			} else {
				if ( numIndexes == tri->numIndexes ) {
					glColor3f( 0.1/backEnd.overBright, 1/backEnd.overBright, 0.1/backEnd.overBright );
				} else if ( numIndexes == tri->numShadowIndexesNoFrontCaps ) {
					glColor3f( 0.1/backEnd.overBright, 1/backEnd.overBright, 0.6/backEnd.overBright );
				} else {
					glColor3f( 0.6/backEnd.overBright, 1/backEnd.overBright, 0.1/backEnd.overBright );
				}
			}
		}

		glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
		glDisable( GL_STENCIL_TEST );
		GL_Cull( CT_TWO_SIDED );
		RB_DrawShadowElementsWithCounters( surf, numIndexes );
		GL_Cull( CT_FRONT_SIDED );
		glEnable( GL_STENCIL_TEST );

		return;
	}

	if ( rb_twoSidedStencilThisPass ) {
		// collapse each cull-flipped draw pair into one no-cull draw with
		// per-face stencil ops; with wrap inc/dec the interleaved single-pass
		// deltas are order-equivalent to the two-pass sequence, so the
		// resulting stencil buffer is identical.
		// In non-mirror views CT_FRONT_SIDED culls GL_FRONT (idTech4 winding),
		// so the ops of the legacy CT_FRONT_SIDED draws belong to the GL_BACK
		// face; mirror views flip the faces exactly like GL_Cull does.
		const GLenum frontSidedFace = backEnd.viewDef->isMirror ? GL_FRONT : GL_BACK;	// rasterized by legacy CT_FRONT_SIDED draws
		const GLenum backSidedFace = backEnd.viewDef->isMirror ? GL_BACK : GL_FRONT;	// rasterized by legacy CT_BACK_SIDED draws

		GL_Cull( CT_TWO_SIDED );

		// patent-free work around
		if ( !external ) {
			// "preload" the stencil buffer with the number of volumes
			// that get clipped by the near or far clip plane
			glStencilOpSeparate( frontSidedFace, GL_KEEP, tr.stencilDecr, tr.stencilDecr );
			glStencilOpSeparate( backSidedFace, GL_KEEP, tr.stencilIncr, tr.stencilIncr );
			RB_DrawShadowElementsWithCounters( surf, numIndexes );
		}

		// traditional depth-pass stencil shadows
		glStencilOpSeparate( frontSidedFace, GL_KEEP, GL_KEEP, tr.stencilIncr );
		glStencilOpSeparate( backSidedFace, GL_KEEP, GL_KEEP, tr.stencilDecr );
		RB_DrawShadowElementsWithCounters( surf, numIndexes );
		return;
	}

	// patent-free work around
	if ( !external ) {
		// "preload" the stencil buffer with the number of volumes
		// that get clipped by the near or far clip plane
		glStencilOp( GL_KEEP, tr.stencilDecr, tr.stencilDecr );
		GL_Cull( CT_FRONT_SIDED );
		RB_DrawShadowElementsWithCounters( surf, numIndexes );
		glStencilOp( GL_KEEP, tr.stencilIncr, tr.stencilIncr );
		GL_Cull( CT_BACK_SIDED );
		RB_DrawShadowElementsWithCounters( surf, numIndexes );
	}

	// traditional depth-pass stencil shadows
	glStencilOp( GL_KEEP, GL_KEEP, tr.stencilIncr );
	GL_Cull( CT_FRONT_SIDED );
	RB_DrawShadowElementsWithCounters( surf, numIndexes );

	glStencilOp( GL_KEEP, GL_KEEP, tr.stencilDecr );
	GL_Cull( CT_BACK_SIDED );
	RB_DrawShadowElementsWithCounters( surf, numIndexes );
}

static int RB_STD_FindPostProcessStart( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	for ( int i = 0; i < numDrawSurfs; ++i ) {
		if ( drawSurfs[i]->material->GetSort() >= SS_POST_PROCESS ) {
			return i;
		}
	}
	return numDrawSurfs;
}

/*
=====================
RB_StencilShadowPass

Stencil test should already be enabled, and the stencil buffer should have
been set to 128 on any surfaces that might receive shadows
=====================
*/
void RB_StencilShadowPass( const drawSurf_t *drawSurfs ) {
	if ( !r_shadows.GetBool() ) {
		return;
	}

	if ( !drawSurfs ) {
		return;
	}

	RB_LogComment( "---------- RB_StencilShadowPass ----------\n" );

	globalImages->BindNull();
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );

	// for visualizing the shadows
	if ( r_showShadows.GetInteger() ) {
		if ( r_showShadows.GetInteger() == 2 ) {
			// draw filled in
			GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_LESS  );
		} else {
			// draw as lines, filling the depth buffer
			GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_POLYMODE_LINE | GLS_DEPTHFUNC_ALWAYS  );
		}
	} else {
		// don't write to the color buffer, just the stencil buffer
		GL_State( GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS );
	}

	if ( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() ) {
		glPolygonOffset( r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat() );
		glEnable( GL_POLYGON_OFFSET_FILL );
	}

	glStencilFunc( GL_ALWAYS, 1, 255 );

	if ( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) {
		glEnable( GL_DEPTH_BOUNDS_TEST_EXT );
	}

	rb_twoSidedStencilThisPass =
		r_useTwoSidedStencil.GetBool()
		&& glStencilOpSeparate != NULL
		&& tr.stencilIncr == GL_INCR_WRAP_EXT;

	// interaction and MD5R passes write env[4] directly between shadow passes
	rb_shadowLightOriginSpace = NULL;

	RB_RenderDrawSurfChainWithFunction( drawSurfs, RB_T_Shadow );

	GL_Cull( CT_FRONT_SIDED );

	if ( r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat() ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}

	if ( glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool() ) {
		glDisable( GL_DEPTH_BOUNDS_TEST_EXT );
	}

	glEnableClientState( GL_TEXTURE_COORD_ARRAY );

	glStencilFunc( GL_GEQUAL, 128, 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
}



/*
=============================================================================================

BLEND LIGHT PROJECTION

=============================================================================================
*/

/*
=====================
RB_T_BlendLight

=====================
*/
static void RB_T_BlendLight( const drawSurf_t *surf ) {
	const srfTriangles_t *tri;

	tri = surf->geo;
	if ( !RB_EnsurePackedClassicDrawCaches( surf, false, true ) ) {
		return;
	}

	if ( backEnd.currentSpace != surf->space ) {
		idPlane	lightProject[4];
		int		i;

		for ( i = 0 ; i < 4 ; i++ ) {
			R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i] );
		}

		GL_SelectTexture( 0 );
		glTexGenfv( GL_S, GL_OBJECT_PLANE, lightProject[0].ToFloatPtr() );
		glTexGenfv( GL_T, GL_OBJECT_PLANE, lightProject[1].ToFloatPtr() );
		glTexGenfv( GL_Q, GL_OBJECT_PLANE, lightProject[2].ToFloatPtr() );

		GL_SelectTexture( 1 );
		glTexGenfv( GL_S, GL_OBJECT_PLANE, lightProject[3].ToFloatPtr() );
	}

	// this gets used for both blend lights and shadow draws
	if ( tri->ambientCache ) {
		idDrawVert	*ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );
	} else if ( tri->shadowCache ) {
		shadowCache_t	*sc = (shadowCache_t *)vertexCache.Position( tri->shadowCache );
		glVertexPointer( 3, GL_FLOAT, sizeof( shadowCache_t ), RB_DrawVertAttributePointer( sc, offsetof( shadowCache_t, xyz ) ) );
	}

	RB_DrawElementsWithCounters( tri );
}


/*
=====================
RB_BlendLight

Dual texture together the falloff and projection texture with a blend
mode to the framebuffer, instead of interacting with the surface texture
=====================
*/
static void RB_BlendLight( const drawSurf_t *drawSurfs,  const drawSurf_t *drawSurfs2 ) {
	const idMaterial	*lightShader;
	const shaderStage_t	*stage;
	int					i;
	const float	*regs;

	if ( !drawSurfs ) {
		return;
	}
	if ( r_skipBlendLights.GetBool() ) {
		return;
	}
	RB_LogComment( "---------- RB_BlendLight ----------\n" );

	lightShader = backEnd.vLight->lightShader;
	regs = backEnd.vLight->shaderRegisters;

	// texture 1 will get the falloff texture
	GL_SelectTexture( 1 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glEnable( GL_TEXTURE_GEN_S );
	glTexCoord2f( 0, 0.5 );
	backEnd.vLight->falloffImage->Bind();

	// texture 0 will get the projected texture
	GL_SelectTexture( 0 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glEnable( GL_TEXTURE_GEN_S );
	glEnable( GL_TEXTURE_GEN_T );
	glEnable( GL_TEXTURE_GEN_Q );

	const int lightStageCount = lightShader->GetNumStages();
	for ( i = 0 ; i < lightStageCount ; i++ ) {
		stage = lightShader->GetStage(i);

		if ( !regs[ stage->conditionRegister ] ) {
			continue;
		}

		GL_State( GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL );

		GL_SelectTexture( 0 );
		stage->texture.image->Bind();

		if ( stage->texture.hasMatrix ) {
			RB_LoadShaderTextureMatrix( regs, &stage->texture );
		}

		// get the modulate values from the light, including alpha, unlike normal lights
		backEnd.lightColor[0] = regs[ stage->color.registers[0] ];
		backEnd.lightColor[1] = regs[ stage->color.registers[1] ];
		backEnd.lightColor[2] = regs[ stage->color.registers[2] ];
		backEnd.lightColor[3] = regs[ stage->color.registers[3] ];
		glColor4fv( backEnd.lightColor );

		RB_RenderDrawSurfChainWithFunction( drawSurfs, RB_T_BlendLight );
		RB_RenderDrawSurfChainWithFunction( drawSurfs2, RB_T_BlendLight );

		if ( stage->texture.hasMatrix ) {
			GL_SelectTexture( 0 );
			glMatrixMode( GL_TEXTURE );
			glLoadIdentity();
			glMatrixMode( GL_MODELVIEW );
		}
	}

	GL_SelectTexture( 1 );
	glDisable( GL_TEXTURE_GEN_S );
	globalImages->BindNull();

	GL_SelectTexture( 0 );
	glDisable( GL_TEXTURE_GEN_S );
	glDisable( GL_TEXTURE_GEN_T );
	glDisable( GL_TEXTURE_GEN_Q );
}


//========================================================================

/*
=====================
RB_T_BasicFog

=====================
*/
static void RB_T_BasicFog( const drawSurf_t *surf ) {
	if ( backEnd.currentSpace != surf->space ) {
		idPlane	local;

		GL_SelectTexture( 0 );

		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_DISTANCE_PLANE_S], local );
		local[3] += 0.5;
		glTexGenfv( GL_S, GL_OBJECT_PLANE, local.ToFloatPtr() );

//		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_DISTANCE_PLANE_T], local );
//		local[3] += 0.5;
local[0] = local[1] = local[2] = 0; local[3] = 0.5;
		glTexGenfv( GL_T, GL_OBJECT_PLANE, local.ToFloatPtr() );

		GL_SelectTexture( 1 );

		// GL_S is constant per viewer
		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_ENTER_PLANE_T], local );
		local[3] += FOG_ENTER;
		glTexGenfv( GL_T, GL_OBJECT_PLANE, local.ToFloatPtr() );

		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_ENTER_PLANE_S], local );
		glTexGenfv( GL_S, GL_OBJECT_PLANE, local.ToFloatPtr() );
	}

	if ( R_TriHasPrimBatchMesh( surf->geo ) ) {
		RB_ARB2_MD5R_DrawBasicFog( surf );
	} else {
		RB_T_RenderTriangleSurface( surf );
	}
}



/*
==================
RB_FogPass
==================
*/
static void RB_FogPass( const drawSurf_t *drawSurfs,  const drawSurf_t *drawSurfs2 ) {
	const srfTriangles_t*frustumTris;
	drawSurf_t			ds;
	const idMaterial	*lightShader;
	const shaderStage_t	*stage;
	const float			*regs;

	RB_LogComment( "---------- RB_FogPass ----------\n" );

	// create a surface for the light frustom triangles, which are oriented drawn side out
	frustumTris = backEnd.vLight->frustumTris;

	// if we ran out of vertex cache memory, skip it
	if ( !frustumTris->ambientCache ) {
		return;
	}
	memset( &ds, 0, sizeof( ds ) );
	ds.space = &backEnd.viewDef->worldSpace;
	ds.geo = frustumTris;
	ds.scissorRect = backEnd.viewDef->scissor;

	// find the current color and density of the fog
	lightShader = backEnd.vLight->lightShader;
	regs = backEnd.vLight->shaderRegisters;
	// assume fog shaders have only a single stage
	stage = lightShader->GetStage(0);

	backEnd.lightColor[0] = regs[ stage->color.registers[0] ];
	backEnd.lightColor[1] = regs[ stage->color.registers[1] ];
	backEnd.lightColor[2] = regs[ stage->color.registers[2] ];
	backEnd.lightColor[3] = regs[ stage->color.registers[3] ];

	glColor3fv( backEnd.lightColor );

	// calculate the falloff planes
	const float a = RB_FogDistanceScale( backEnd.lightColor[3] );

	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );

	// The fog pass is fixed-function. Reassert the classic state here so
	// hybrid ARB2 / MD5R stage work can't leak texture-combine or program
	// bindings that turn colored fog volumes black or invisible.
	glUseProgramObjectARB( 0 );
	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, 0 );

	// texture 0 is the falloff image
	GL_SelectTexture( 0 );
	GL_TexEnv( GL_MODULATE );
	globalImages->fogImage->Bind();
	//GL_Bind( tr.whiteImage );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	// Fog uses the light's current color; stale per-vertex color arrays can
	// zero the fog contribution and turn colored fog volumes black.
	glDisableClientState( GL_COLOR_ARRAY );
	glDisable( GL_TEXTURE_GEN_R );
	glDisable( GL_TEXTURE_GEN_Q );
	glEnable( GL_TEXTURE_GEN_S );
	glEnable( GL_TEXTURE_GEN_T );
	glTexCoord2f( 0.5f, 0.5f );		// make sure Q is set

	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[2];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[6];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[10];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[14];

	fogTexGenPlanes[FOG_DISTANCE_PLANE_T][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[0];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_T][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[4];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_T][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[8];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_T][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[12];


	// texture 1 is the entering plane fade correction
	GL_SelectTexture( 1 );
	GL_TexEnv( GL_MODULATE );
	globalImages->fogEnterImage->Bind();
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisable( GL_TEXTURE_GEN_R );
	glDisable( GL_TEXTURE_GEN_Q );
	glEnable( GL_TEXTURE_GEN_S );
	glEnable( GL_TEXTURE_GEN_T );

	// T will get a texgen for the fade plane, which is always the "top" plane on unrotated lights
	fogTexGenPlanes[FOG_ENTER_PLANE_T][0] = 0.001f * backEnd.vLight->fogPlane[0];
	fogTexGenPlanes[FOG_ENTER_PLANE_T][1] = 0.001f * backEnd.vLight->fogPlane[1];
	fogTexGenPlanes[FOG_ENTER_PLANE_T][2] = 0.001f * backEnd.vLight->fogPlane[2];
	fogTexGenPlanes[FOG_ENTER_PLANE_T][3] = 0.001f * backEnd.vLight->fogPlane[3];

	// S is based on the view origin
	const float s = backEnd.viewDef->renderView.vieworg * fogTexGenPlanes[FOG_ENTER_PLANE_T].Normal()
		+ fogTexGenPlanes[FOG_ENTER_PLANE_T][3];

	fogTexGenPlanes[FOG_ENTER_PLANE_S][0] = 0;
	fogTexGenPlanes[FOG_ENTER_PLANE_S][1] = 0;
	fogTexGenPlanes[FOG_ENTER_PLANE_S][2] = 0;
	fogTexGenPlanes[FOG_ENTER_PLANE_S][3] = FOG_ENTER + s;

	glTexCoord2f( FOG_ENTER + s, FOG_ENTER );


	// draw it
	RB_RenderDrawSurfChainWithFunction( drawSurfs, RB_T_BasicFog );
	RB_RenderDrawSurfChainWithFunction( drawSurfs2, RB_T_BasicFog );

	// the light frustum bounding planes aren't in the depth buffer, so use depthfunc_less instead
	// of depthfunc_equal
	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS );
	GL_Cull( CT_BACK_SIDED );
	RB_RenderDrawSurfChainWithFunction( &ds, RB_T_BasicFog );
	GL_Cull( CT_FRONT_SIDED );

	GL_SelectTexture( 1 );
	glDisable( GL_TEXTURE_GEN_S );
	glDisable( GL_TEXTURE_GEN_T );
	globalImages->BindNull();

	GL_SelectTexture( 0 );
	glDisable( GL_TEXTURE_GEN_S );
	glDisable( GL_TEXTURE_GEN_T );
}


/*
==================
RB_STD_FogAllLights
==================
*/
void RB_STD_FogAllLights( void ) {
	viewLight_t	*vLight;

	if ( r_skipFogLights.GetBool() || r_showOverDraw.GetInteger() != 0 
		 || backEnd.viewDef->isXraySubview /* dont fog in xray mode*/
		 ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_FogAllLights ----------\n" );

	glDisable( GL_STENCIL_TEST );

	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		if ( !vLight->lightShader->IsFogLight() && !vLight->lightShader->IsBlendLight() ) {
			continue;
		}

#if 0 // _D3XP disabled that
		if ( r_ignore.GetInteger() ) {
			// we use the stencil buffer to guarantee that no pixels will be
			// double fogged, which happens in some areas that are thousands of
			// units from the origin
			backEnd.currentScissor = vLight->scissorRect;
			if ( r_useScissor.GetBool() ) {
				glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1, 
					backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
					backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
					backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
			}
			glClear( GL_STENCIL_BUFFER_BIT );

			glEnable( GL_STENCIL_TEST );

			// only pass on the cleared stencil values
			glStencilFunc( GL_EQUAL, 128, 255 );

			// when we pass the stencil test and depth test and are going to draw,
			// increment the stencil buffer so we don't ever draw on that pixel again
			glStencilOp( GL_KEEP, GL_KEEP, GL_INCR );
		}
#endif

		if ( vLight->lightShader->IsFogLight() ) {
			RB_FogPass( vLight->globalInteractions, vLight->localInteractions );
		} else if ( vLight->lightShader->IsBlendLight() ) {
			RB_BlendLight( vLight->globalInteractions, vLight->localInteractions );
		}
		glDisable( GL_STENCIL_TEST );
	}

	glEnable( GL_STENCIL_TEST );
}

//=========================================================================================

/*
==================
RB_STD_LightScale

Perform extra blending passes to multiply the entire buffer by
a floating point value
==================
*/
void RB_STD_LightScale( void ) {
	float	v, f;

	if ( backEnd.overBright == 1.0f ) {
		return;
	}

	if ( r_skipLightScale.GetBool() ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_LightScale ----------\n" );

	// the scissor may be smaller than the viewport for subviews
	if ( r_useScissor.GetBool() ) {
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1, 
			backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1, 
			backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
			backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
		backEnd.currentScissor = backEnd.viewDef->scissor;
	}

	// full screen blends
	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity(); 
    glOrtho( 0, 1, 0, 1, -1, 1 );

	GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_SRC_COLOR );
	GL_Cull( CT_TWO_SIDED );	// so mirror views also get it
	globalImages->BindNull();
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );

	v = 1;
	while ( idMath::Fabs( v - backEnd.overBright ) > 0.01 ) {	// a little extra slop
		f = backEnd.overBright / v;
		f /= 2;
		if ( f > 1 ) {
			f = 1;
		}
		glColor3f( f, f, f );
		v = v * f * 2;

		glBegin( GL_QUADS );
		glVertex2f( 0,0 );	
		glVertex2f( 0,1 );
		glVertex2f( 1,1 );	
		glVertex2f( 1,0 );	
		glEnd();
	}


	glPopMatrix();
	glEnable( GL_DEPTH_TEST );
	glMatrixMode( GL_MODELVIEW );
	GL_Cull( CT_FRONT_SIDED );
}

/*
==================
RB_STD_ForceAmbient

Lift the final scene toward a minimum brightness floor.
==================
*/
static void RB_STD_ForceAmbient( void ) {
	const bool useSimpleInteraction = !r_testARBProgram.GetBool() &&
		( r_useSimpleInteraction.GetBool() || glConfig.preferSimpleInteraction );
	const GLuint interactionVertexProgram = r_testARBProgram.GetBool() ? VPROG_TEST : ( useSimpleInteraction ? VPROG_SIMPLE_INTERACTION : VPROG_INTERACTION );
	const GLuint interactionFragmentProgram = r_testARBProgram.GetBool() ? FPROG_TEST : ( useSimpleInteraction ? FPROG_SIMPLE_INTERACTION : FPROG_INTERACTION );
	const bool interactionRescueActive =
		tr.backEndRenderer == BE_ARB2 &&
		( glConfig.disableARB2Interactions ||
			!R_IsARBProgramValid( GL_VERTEX_PROGRAM_ARB, interactionVertexProgram ) ||
			!R_IsARBProgramValid( GL_FRAGMENT_PROGRAM_ARB, interactionFragmentProgram ) );
	const float ambientFloor = interactionRescueActive ? 0.20f : 0.0f;
	const float ambient = idMath::ClampFloat( 0.0f, 1.0f, Max( r_forceAmbient.GetFloat(), ambientFloor ) );
	if ( ambient <= 0.0f || !backEnd.viewDef->viewEntitys ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_ForceAmbient ----------\n" );

	// the scissor may be smaller than the viewport for subviews
	if ( r_useScissor.GetBool() ) {
		glScissor( backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
			backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
			backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
			backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
		backEnd.currentScissor = backEnd.viewDef->scissor;
	}

	glLoadIdentity();
	glMatrixMode( GL_PROJECTION );
	glPushMatrix();
	glLoadIdentity();
	glOrtho( 0, 1, 0, 1, -1, 1 );

	// This blend computes: dst = dst + ambient * ( 1 - dst ).
	GL_State( GLS_SRCBLEND_ONE_MINUS_DST_COLOR | GLS_DSTBLEND_ONE );
	GL_Cull( CT_TWO_SIDED );
	globalImages->BindNull();
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glColor3f( ambient, ambient, ambient );

	glBegin( GL_QUADS );
	glVertex2f( 0, 0 );
	glVertex2f( 0, 1 );
	glVertex2f( 1, 1 );
	glVertex2f( 1, 0 );
	glEnd();

	glColor3f( 1.0f, 1.0f, 1.0f );
	glPopMatrix();
	glEnable( GL_DEPTH_TEST );
	glMatrixMode( GL_MODELVIEW );
	GL_Cull( CT_FRONT_SIDED );
}

static void RB_LightGridModelMatrixRows( const float modelMatrix[16], float row0[4], float row1[4], float row2[4] ) {
	row0[0] = modelMatrix[0];
	row0[1] = modelMatrix[4];
	row0[2] = modelMatrix[8];
	row0[3] = modelMatrix[12];

	row1[0] = modelMatrix[1];
	row1[1] = modelMatrix[5];
	row1[2] = modelMatrix[9];
	row1[3] = modelMatrix[13];

	row2[0] = modelMatrix[2];
	row2[1] = modelMatrix[6];
	row2[2] = modelMatrix[10];
	row2[3] = modelMatrix[14];
}

static void RB_LightGridVertexColorParams( const stageVertexColor_t vertexColor, float params[2] ) {
	params[0] = 0.0f;
	params[1] = 1.0f;

	if ( vertexColor == SVC_MODULATE ) {
		params[0] = 1.0f;
		params[1] = 0.0f;
	} else if ( vertexColor == SVC_INVERSE_MODULATE ) {
		params[0] = -1.0f;
		params[1] = 1.0f;
	}
}

static bool RB_LightGridUsesReceiverOnlySubmission( const int debugMode ) {
	return debugMode == 0 ||
		debugMode == 1 ||
		debugMode == 2 ||
		debugMode == 3 ||
		debugMode == 5 ||
		debugMode == 6 ||
		debugMode == 7;
}

struct rbLightGridPortalBlend_t {
	const LightGrid *	neighborLightGrid;
	idPlane				portalPlane;
	idBounds			portalBounds;
	float				blendDistance;
};

static bool RB_LightGridIsUsable( const LightGrid &candidate ) {
	return candidate.IsUsable();
}

static bool RB_LightGridMaterialHasActiveColorMaskStage( const idMaterial *shader, const float *regs ) {
	if ( shader == NULL ) {
		return false;
	}

	const int stageCount = shader->GetNumStages();
	for ( int stageIndex = 0; stageIndex < stageCount; stageIndex++ ) {
		const shaderStage_t *stage = shader->GetStage( stageIndex );
		if ( stage != NULL &&
			( regs == NULL || regs[stage->conditionRegister] != 0.0f ) &&
			( stage->drawStateBits & GLS_COLORMASK ) != 0 ) {
			return true;
		}
	}

	return false;
}

static bool RB_SurfaceCanReceiveLightGrid( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->material == NULL || surf->space == NULL || surf->geo == NULL ) {
		return false;
	}
	if ( !surf->material->IsDrawn() || !surf->material->ReceivesLighting() || surf->material->GetSort() != SS_OPAQUE ) {
		return false;
	}
	if ( surf->material->IsPortalSky() || surf->material->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	if ( surf->decalColorCache != NULL || surf->material->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		return false;
	}
	// First-person scope/glass surfaces can use color-mask stages to author
	// alpha/display behavior. The additive indirect pass cannot preserve those
	// masks, so let the weapon's regular material and GUI light own them.
	if ( surf->space->weaponDepthHack && RB_LightGridMaterialHasActiveColorMaskStage( surf->material, surf->shaderRegisters ) ) {
		return false;
	}
	return true;
}

static const LightGrid *RB_CurrentViewLightGrid( void );

static bool RB_SurfaceHasLightGrid( const drawSurf_t *surf, const LightGrid *&lightGrid ) {
	lightGrid = NULL;

	if ( surf == NULL ) {
		return false;
	}
	// Keep the indirect light-grid pass on stable world/entity receivers only.
	// Shot-created decals and depth-hacked weapon/effect surfaces use different
	// color/depth paths and can leave this pass binding invalid state after fire.
	if ( !RB_SurfaceCanReceiveLightGrid( surf ) ) {
		return false;
	}
	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		return false;
	}

	if ( surf->area == NULL ) {
		return false;
	}

	const LightGrid &candidate = surf->area->lightGrid;
	if ( !RB_LightGridIsUsable( candidate ) ) {
		return false;
	}

	lightGrid = &candidate;
	return true;
}

static int RB_CurrentViewLightGridArea( idRenderWorldLocal *world ) {
	if ( world == NULL || backEnd.viewDef == NULL ) {
		return -1;
	}

	int areaNum = backEnd.viewDef->areaNum;
	if ( areaNum < 0 || areaNum >= world->numPortalAreas ) {
		areaNum = world->PointInArea( backEnd.viewDef->initialViewAreaOrigin );
	}
	if ( areaNum < 0 || areaNum >= world->numPortalAreas ) {
		areaNum = world->PointInArea( backEnd.viewDef->renderView.vieworg );
	}
	if ( areaNum < 0 || areaNum >= world->numPortalAreas ) {
		return -1;
	}

	return areaNum;
}

static const LightGrid *RB_CurrentViewLightGrid( void ) {
	if ( backEnd.viewDef == NULL || backEnd.viewDef->renderWorld == NULL ) {
		return NULL;
	}

	idRenderWorldLocal *world = backEnd.viewDef->renderWorld;
	const int areaNum = RB_CurrentViewLightGridArea( world );
	if ( areaNum < 0 ) {
		return NULL;
	}

	const LightGrid &candidate = world->portalAreas[ areaNum ].lightGrid;
	if ( !RB_LightGridIsUsable( candidate ) ) {
		return NULL;
	}

	return &candidate;
}

static bool RB_LightGridSurfaceWorldBounds( const drawSurf_t *surf, idBounds &worldBounds ) {
	if ( surf == NULL || surf->space == NULL || surf->geo == NULL || surf->geo->bounds.IsCleared() ) {
		return false;
	}

	idVec3 localPoints[8];
	surf->geo->bounds.ToPoints( localPoints );
	worldBounds.Clear();
	for ( int i = 0; i < 8; i++ ) {
		idVec3 worldPoint;
		R_LocalPointToGlobal( surf->space->modelMatrix, localPoints[i], worldPoint );
		worldBounds.AddPoint( worldPoint );
	}

	return !worldBounds.IsCleared();
}

static bool RB_SurfaceHasLightGridPortalBlend( const drawSurf_t *surf, rbLightGridPortalBlend_t &blend ) {
	blend.neighborLightGrid = NULL;
	blend.blendDistance = 0.0f;
	blend.portalPlane.Zero();
	blend.portalBounds.Clear();

	if ( surf == NULL || surf->area == NULL || backEnd.viewDef == NULL || backEnd.viewDef->renderWorld == NULL ) {
		return false;
	}

	const float blendDistance = r_lightGridPortalBlend.GetFloat();
	if ( blendDistance <= 0.0f ) {
		return false;
	}

	idBounds surfaceBounds;
	if ( !RB_LightGridSurfaceWorldBounds( surf, surfaceBounds ) ) {
		return false;
	}

	idRenderWorldLocal *world = backEnd.viewDef->renderWorld;
	float bestWeight = 0.0f;
	for ( const portal_t *portal = surf->area->portals; portal != NULL; portal = portal->next ) {
		if ( portal->w == NULL || portal->w->GetNumPoints() < 3 ) {
			continue;
		}
		if ( portal->doublePortal != NULL && ( portal->doublePortal->blockingBits & PS_BLOCK_VIEW ) ) {
			continue;
		}
		if ( portal->intoArea < 0 || portal->intoArea >= world->numPortalAreas ) {
			continue;
		}
		portalArea_t &neighborArea = world->portalAreas[ portal->intoArea ];
		if ( neighborArea.viewCount != tr.viewCount ) {
			continue;
		}
		if ( backEnd.viewDef->connectedAreas != NULL && !backEnd.viewDef->connectedAreas[ portal->intoArea ] ) {
			continue;
		}
		if ( !RB_LightGridIsUsable( neighborArea.lightGrid ) ) {
			continue;
		}

		idBounds portalBounds;
		portal->w->GetBounds( portalBounds );
		portalBounds.ExpandSelf( blendDistance );
		if ( !surfaceBounds.IntersectsBounds( portalBounds ) ) {
			continue;
		}

		const float planeDistance = idMath::Fabs( surfaceBounds.PlaneDistance( portal->plane ) );
		if ( planeDistance > blendDistance ) {
			continue;
		}

		const float weight = 1.0f - idMath::ClampFloat( 0.0f, 1.0f, planeDistance / blendDistance );
		if ( weight <= bestWeight ) {
			continue;
		}

		bestWeight = weight;
		blend.neighborLightGrid = &neighborArea.lightGrid;
		blend.portalPlane = portal->plane;
		blend.portalBounds = portalBounds;
		blend.blendDistance = blendDistance;
	}

	return blend.neighborLightGrid != NULL;
}

static bool RB_SurfaceHasViewWeaponLightGrid( const drawSurf_t *surf, const LightGrid *&lightGrid ) {
	lightGrid = NULL;

	if ( !RB_SurfaceCanReceiveLightGrid( surf ) ) {
		return false;
	}
	if ( !surf->space->weaponDepthHack ) {
		return false;
	}

	const LightGrid *candidate = RB_CurrentViewLightGrid();
	if ( candidate == NULL ) {
		return false;
	}

	lightGrid = candidate;
	return true;
}

static const int LIGHTGRID_RESIDENCY_UNTOUCHED = -0x40000000;

static idRenderWorldLocal *rbLightGridResidencyWorld = NULL;
static idList<int> rbLightGridResidencyLastTouched;
static int rbLightGridResidencyFrame = 0;

static void RB_EnsureLightGridResidencyState( idRenderWorldLocal *world ) {
	if ( rbLightGridResidencyWorld == world && rbLightGridResidencyLastTouched.Num() == world->numPortalAreas ) {
		return;
	}

	rbLightGridResidencyWorld = world;
	rbLightGridResidencyLastTouched.SetNum( world->numPortalAreas );
	for ( int areaIndex = 0; areaIndex < rbLightGridResidencyLastTouched.Num(); areaIndex++ ) {
		rbLightGridResidencyLastTouched[areaIndex] = LIGHTGRID_RESIDENCY_UNTOUCHED;
	}
}

static void RB_LoadLightGridResidencyImage( idImage *image ) {
	if ( image != NULL && !image->IsLoaded() ) {
		image->ActuallyLoadImage( true );
	}
}

static void RB_LoadLightGridResidencyImages( LightGrid &lightGrid ) {
	RB_LoadLightGridResidencyImage( lightGrid.irradianceImage );
	RB_LoadLightGridResidencyImage( lightGrid.visibilityImage );
	RB_LoadLightGridResidencyImage( lightGrid.probeImage );
}

static void RB_PurgeLightGridResidencyImage( idImage *image ) {
	if ( image != NULL && image->IsLoaded() ) {
		image->PurgeImage();
	}
}

static void RB_PurgeLightGridResidencyImages( LightGrid &lightGrid ) {
	RB_PurgeLightGridResidencyImage( lightGrid.irradianceImage );
	RB_PurgeLightGridResidencyImage( lightGrid.visibilityImage );
	RB_PurgeLightGridResidencyImage( lightGrid.probeImage );
}

static void RB_TouchLightGridResidencyArea( idRenderWorldLocal *world, int areaIndex, int frameIndex ) {
	if ( areaIndex < 0 || areaIndex >= world->numPortalAreas ) {
		return;
	}

	if ( rbLightGridResidencyLastTouched[areaIndex] == frameIndex ) {
		return;
	}

	LightGrid &lightGrid = world->portalAreas[areaIndex].lightGrid;
	if ( !RB_LightGridIsUsable( lightGrid ) ) {
		return;
	}

	rbLightGridResidencyLastTouched[areaIndex] = frameIndex;
	if ( !world->EnsureLightGridAreaImages( areaIndex ) ) {
		return;
	}
	RB_LoadLightGridResidencyImages( lightGrid );
}

static void RB_TouchLightGridResidencyAreaAndNeighbors( idRenderWorldLocal *world, int areaIndex, int frameIndex ) {
	if ( areaIndex < 0 || areaIndex >= world->numPortalAreas ) {
		return;
	}

	RB_TouchLightGridResidencyArea( world, areaIndex, frameIndex );

	for ( const portal_t *portal = world->portalAreas[areaIndex].portals; portal != NULL; portal = portal->next ) {
		if ( portal->doublePortal != NULL && ( portal->doublePortal->blockingBits & PS_BLOCK_VIEW ) ) {
			continue;
		}
		if ( portal->intoArea < 0 || portal->intoArea >= world->numPortalAreas ) {
			continue;
		}
		if ( backEnd.viewDef != NULL && backEnd.viewDef->connectedAreas != NULL && !backEnd.viewDef->connectedAreas[ portal->intoArea ] ) {
			continue;
		}

		RB_TouchLightGridResidencyArea( world, portal->intoArea, frameIndex );
	}
}

static void RB_TouchLightGridResidencyReference( idRenderWorldLocal *world, const LightGrid *lightGrid, int frameIndex ) {
	if ( lightGrid == NULL ) {
		return;
	}
	RB_TouchLightGridResidencyArea( world, lightGrid->area, frameIndex );
}

static void RB_TouchLightGridResidencyDrawSurfs( idRenderWorldLocal *world, int frameIndex ) {
	if ( backEnd.viewDef == NULL || backEnd.viewDef->drawSurfs == NULL ) {
		return;
	}

	for ( int i = 0; i < backEnd.viewDef->numDrawSurfs; i++ ) {
		drawSurf_t *surf = backEnd.viewDef->drawSurfs[i];
		if ( surf == NULL || surf->material == NULL ) {
			continue;
		}
		if ( surf->material->GetSort() >= SS_POST_PROCESS || surf->material->SuppressInSubview() ) {
			continue;
		}

		const LightGrid *lightGrid = NULL;
		if ( RB_SurfaceHasLightGrid( surf, lightGrid ) ) {
			RB_TouchLightGridResidencyReference( world, lightGrid, frameIndex );
		}
		if ( RB_SurfaceHasViewWeaponLightGrid( surf, lightGrid ) ) {
			RB_TouchLightGridResidencyReference( world, lightGrid, frameIndex );

			rbLightGridPortalBlend_t portalBlend;
			if ( RB_SurfaceHasLightGridPortalBlend( surf, portalBlend ) ) {
				RB_TouchLightGridResidencyReference( world, portalBlend.neighborLightGrid, frameIndex );
			}
		}
	}
}

static void RB_UpdateLightGridImageResidency( idRenderWorldLocal *world ) {
	if ( world == NULL || world->portalAreas == NULL ) {
		return;
	}

	RB_EnsureLightGridResidencyState( world );

	const int frameIndex = ++rbLightGridResidencyFrame;
	for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
		if ( world->portalAreas[areaIndex].viewCount != tr.viewCount ) {
			continue;
		}
		RB_TouchLightGridResidencyAreaAndNeighbors( world, areaIndex, frameIndex );
	}

	const int viewArea = RB_CurrentViewLightGridArea( world );
	RB_TouchLightGridResidencyAreaAndNeighbors( world, viewArea, frameIndex );
	RB_TouchLightGridResidencyDrawSurfs( world, frameIndex );

	const int residencyFrames = r_lightGridResidencyFrames.GetInteger();
	if ( residencyFrames <= 0 ) {
		return;
	}
	for ( int areaIndex = 0; areaIndex < world->numPortalAreas; areaIndex++ ) {
		portalArea_t &area = world->portalAreas[ areaIndex ];
		const int lastTouchedFrame = rbLightGridResidencyLastTouched[areaIndex];
		if ( lastTouchedFrame != LIGHTGRID_RESIDENCY_UNTOUCHED && frameIndex - lastTouchedFrame <= residencyFrames ) {
			continue;
		}

		RB_PurgeLightGridResidencyImages( area.lightGrid );
	}
}

static void RB_LightGridSetIdentityTextureMatrix( idVec4 matrix[2] ) {
	matrix[0].Set( 1.0f, 0.0f, 0.0f, 0.0f );
	matrix[1].Set( 0.0f, 1.0f, 0.0f, 0.0f );
}

static bool RB_LightGridMaterialStageIsActive( const shaderStage_t *stage, const float *regs ) {
	return stage != NULL && ( regs == NULL || regs[ stage->conditionRegister ] != 0.0f );
}

static int RB_LightGridStageBlendBits( const shaderStage_t *stage ) {
	return stage != NULL ? ( stage->drawStateBits & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) : 0;
}

static bool RB_LightGridAmbientStageCanProvideAlbedo( const idMaterial *shader, const shaderStage_t *stage, const float *regs ) {
	if ( shader == NULL || stage == NULL ) {
		return false;
	}
	if ( stage->lighting != SL_AMBIENT || !RB_LightGridMaterialStageIsActive( stage, regs ) ) {
		return false;
	}
	if ( shader->IsPortalSky() || shader->TestMaterialFlag( MF_SKY ) || shader->GetSort() >= SS_FAR ) {
		return false;
	}
	if ( stage->texture.image == NULL || stage->newStage != NULL ) {
		return false;
	}
	if ( stage->texture.texgen != TG_EXPLICIT && stage->texture.texgen != TG_POT_CORRECTION ) {
		return false;
	}
	return RB_LightGridStageBlendBits( stage ) == 0;
}

static bool RB_LightGridStageCanProvideAlbedo( const idMaterial *shader, const shaderStage_t *stage, const float *regs ) {
	if ( stage == NULL ) {
		return false;
	}
	if ( stage->lighting == SL_DIFFUSE && RB_LightGridMaterialStageIsActive( stage, regs ) ) {
		return true;
	}
	return RB_LightGridAmbientStageCanProvideAlbedo( shader, stage, regs );
}

static bool RB_LightGridHasActiveAlbedoStage( const idMaterial *shader, const float *regs ) {
	if ( shader == NULL ) {
		return false;
	}

	const int stageCount = shader->GetNumStages();
	for ( int stageIndex = 0; stageIndex < stageCount; stageIndex++ ) {
		const shaderStage_t *stage = shader->GetStage( stageIndex );
		if ( RB_LightGridStageCanProvideAlbedo( shader, stage, regs ) ) {
			return true;
		}
	}

	return false;
}

struct rbLightGridDrawStats_t {
	int nullInput;
	int noAlbedo;
	int cacheFail;
	int emptyGeometry;
	int noIrradiance;
	int ensureFail;
	int defaultIrradiance;
	int badAtlas;
	int stageReject;
	int stageSubmit;
};

struct rbLightGridAlbedoBinding_t {
	const shaderStage_t *stage;
	int stageIndex;
	idImage *image;
	idVec4 matrix[2];
	float color[4];
	float vertexColorParams[2];
};

static void RB_LightGridInitAlbedoBinding( rbLightGridAlbedoBinding_t &binding ) {
	memset( &binding, 0, sizeof( binding ) );
	binding.stage = NULL;
	binding.stageIndex = -1;
	binding.image = globalImages != NULL ? globalImages->whiteImage : NULL;
	RB_LightGridSetIdentityTextureMatrix( binding.matrix );
	binding.color[0] = 0.55f;
	binding.color[1] = 0.55f;
	binding.color[2] = 0.55f;
	binding.color[3] = 1.0f;
	binding.vertexColorParams[0] = 0.0f;
	binding.vertexColorParams[1] = 1.0f;
}

static bool RB_LightGridFindRepresentativeAlbedo( const drawSurf_t *surf, rbLightGridAlbedoBinding_t &binding ) {
	RB_LightGridInitAlbedoBinding( binding );
	const idMaterial *shader = surf != NULL ? surf->material : NULL;
	const float *regs = surf != NULL ? surf->shaderRegisters : NULL;
	if ( shader == NULL || regs == NULL ) {
		return false;
	}

	const int stageCount = shader->GetNumStages();
	for ( int stageIndex = 0; stageIndex < stageCount; stageIndex++ ) {
		const shaderStage_t *stage = shader->GetStage( stageIndex );
		if ( !RB_LightGridStageCanProvideAlbedo( shader, stage, regs ) ) {
			continue;
		}

		idImage *diffuseImage = globalImages->whiteImage;
		idVec4 diffuseMatrix[2];
		float diffuseColor[4];
		R_SetDrawInteraction( stage, regs, &diffuseImage, diffuseMatrix, diffuseColor );
		if ( diffuseImage == NULL ) {
			diffuseImage = globalImages->whiteImage;
		}
		if ( diffuseColor[0] <= 0.0f && diffuseColor[1] <= 0.0f && diffuseColor[2] <= 0.0f ) {
			diffuseColor[0] = 1.0f;
			diffuseColor[1] = 1.0f;
			diffuseColor[2] = 1.0f;
		}

		binding.stage = stage;
		binding.stageIndex = stageIndex;
		binding.image = diffuseImage;
		binding.matrix[0] = diffuseMatrix[0];
		binding.matrix[1] = diffuseMatrix[1];
		binding.color[0] = diffuseColor[0];
		binding.color[1] = diffuseColor[1];
		binding.color[2] = diffuseColor[2];
		binding.color[3] = diffuseColor[3];
		RB_LightGridVertexColorParams( stage->vertexColor, binding.vertexColorParams );
		if ( stage->lighting == SL_DIFFUSE ) {
			idVec4 flatDiffuseParams;
			RB_ApplyFlatDiffuseStage( surf, &binding.image, binding.color, flatDiffuseParams );
		}
		return true;
	}

	return false;
}

static bool RB_STD_DrawLightGridAlbedoStage( const drawSurf_t *surf, const shaderStage_t *albedoStage, int albedoStageIndex, idImage *bumpImage, const idVec4 bumpMatrix[2], const float *regs, const srfTriangles_t *tri, idDrawVert *ac ) {
	if ( albedoStage == NULL || tri == NULL ) {
		return false;
	}

	idImage *diffuseImage = globalImages->whiteImage;
	idVec4 diffuseMatrix[2];
	float diffuseColor[4];
	R_SetDrawInteraction( albedoStage, regs, &diffuseImage, diffuseMatrix, diffuseColor );
	if ( diffuseImage == NULL ) {
		diffuseImage = globalImages->whiteImage;
	}
	if ( bumpImage == NULL || r_skipBump.GetBool() ) {
		bumpImage = globalImages->flatNormalMap;
	}
	if ( diffuseColor[0] <= 0.0f && diffuseColor[1] <= 0.0f && diffuseColor[2] <= 0.0f ) {
		diffuseColor[0] = 1.0f;
		diffuseColor[1] = 1.0f;
		diffuseColor[2] = 1.0f;
	}
	idVec4 flatDiffuseParams;
	flatDiffuseParams.Zero();
	if ( albedoStage->lighting == SL_DIFFUSE ) {
		RB_ApplyFlatDiffuseStage( surf, &diffuseImage, diffuseColor, flatDiffuseParams );
	}

	float vertexColorParams[2];
	RB_LightGridVertexColorParams( albedoStage->vertexColor, vertexColorParams );

	const bool useVertexColorArray = albedoStage->vertexColor != SVC_IGNORE;
	if ( useVertexColorArray ) {
		RB_SetStageVertexColorPointer( surf, albedoStageIndex, ac );
		glEnableClientState( GL_COLOR_ARRAY );
	} else {
		glDisableClientState( GL_COLOR_ARRAY );
		glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	}

	GL_SelectTexture( 0 );
	if ( albedoStage->texture.texgen == TG_POT_CORRECTION && surf->dynamicTexCoords != NULL ) {
		glTexCoordPointer( 2, GL_FLOAT, 0, vertexCache.Position( surf->dynamicTexCoords ) );
	} else {
		glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );
	}
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_BUMP_MATRIX_S], 1, bumpMatrix[0].ToFloatPtr() );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_BUMP_MATRIX_T], 1, bumpMatrix[1].ToFloatPtr() );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DIFFUSE_MATRIX_S], 1, diffuseMatrix[0].ToFloatPtr() );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DIFFUSE_MATRIX_T], 1, diffuseMatrix[1].ToFloatPtr() );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DIFFUSE_COLOR], 1, diffuseColor );
	glUniform2fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_VERTEX_COLOR_PARAMS], 1, vertexColorParams );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_FLAT_DIFFUSE_PARAMS], 1, flatDiffuseParams.ToFloatPtr() );

	GL_SelectTextureNoClient( 0 );
	bumpImage->Bind();
	GL_SelectTextureNoClient( 1 );
	diffuseImage->Bind();

	RB_DrawElementsWithCounters( tri );
	if ( useVertexColorArray ) {
		glDisableClientState( GL_COLOR_ARRAY );
	}
	return true;
}

static bool RB_STD_DrawLightGridSurface( const drawSurf_t *surf, const LightGrid &lightGrid, const rbLightGridPortalBlend_t *portalBlend = NULL, bool invertPortalBlend = false, rbLightGridDrawStats_t *drawStats = NULL ) {
	const srfTriangles_t *tri = surf->geo;
	const idMaterial *shader = surf->material;
	const float *regs = surf->shaderRegisters;
	const int debugMode = r_lightGridDebug.GetInteger();
	const bool receiverOnlySubmission = RB_LightGridUsesReceiverOnlySubmission( debugMode );
	if ( tri == NULL || shader == NULL || regs == NULL ) {
		if ( drawStats != NULL ) {
			drawStats->nullInput++;
		}
		return false;
	}
	if ( !receiverOnlySubmission && !RB_LightGridHasActiveAlbedoStage( shader, regs ) ) {
		if ( drawStats != NULL ) {
			drawStats->noAlbedo++;
		}
		return false;
	}
	if ( !RB_EnsurePackedClassicDrawCaches( surf, true, true ) ) {
		if ( drawStats != NULL ) {
			drawStats->cacheFail++;
		}
		return false;
	}

	if ( tri->numIndexes <= 0 || tri->ambientCache == NULL ) {
		if ( drawStats != NULL ) {
			drawStats->emptyGeometry++;
		}
		return false;
	}

	idImage *irradianceImage = lightGrid.irradianceImage;
	if ( irradianceImage == NULL ) {
		if ( drawStats != NULL ) {
			drawStats->noIrradiance++;
		}
		return false;
	}

	if ( backEnd.viewDef != NULL && backEnd.viewDef->renderWorld != NULL ) {
		if ( !backEnd.viewDef->renderWorld->EnsureLightGridAreaImages( lightGrid.area ) ) {
			if ( drawStats != NULL ) {
				drawStats->ensureFail++;
			}
			return false;
		}
	}

	if ( !irradianceImage->IsLoaded() ) {
		irradianceImage->ActuallyLoadImage( true );
	}
	if ( irradianceImage->IsDefaulted() ) {
		if ( drawStats != NULL ) {
			drawStats->defaultIrradiance++;
		}
		return false;
	}

	const int atlasWidth = irradianceImage->GetOpts().width;
	const int atlasHeight = irradianceImage->GetOpts().height;
	if ( atlasWidth <= 0 || atlasHeight <= 0 ) {
		if ( drawStats != NULL ) {
			drawStats->badAtlas++;
		}
		return false;
	}

	idImage *visibilityImage = lightGrid.visibilityImage;
	if ( visibilityImage != NULL ) {
		if ( !visibilityImage->IsLoaded() ) {
			visibilityImage->ActuallyLoadImage( true );
		}
		if ( visibilityImage->IsDefaulted() ) {
			visibilityImage = NULL;
		}
		if ( visibilityImage != NULL && ( visibilityImage->GetOpts().width != atlasWidth || visibilityImage->GetOpts().height != atlasHeight ) ) {
			visibilityImage = NULL;
		}
	}

	idImage *probeImage = lightGrid.probeImage;
	const int probeImageWidth = Max( lightGrid.lightGridBounds[0] * lightGrid.lightGridBounds[2], 1 );
	const int probeImageHeight = Max( lightGrid.lightGridBounds[1], 1 );
	if ( probeImage != NULL ) {
		if ( !probeImage->IsLoaded() ) {
			probeImage->ActuallyLoadImage( true );
		}
		if ( probeImage->IsDefaulted() ) {
			probeImage = NULL;
		}
		if ( probeImage != NULL && ( probeImage->GetOpts().width != probeImageWidth || probeImage->GetOpts().height != probeImageHeight ) ) {
			probeImage = NULL;
		}
	}

	float row0[4];
	float row1[4];
	float row2[4];
	RB_LightGridModelMatrixRows( surf->space->modelMatrix, row0, row1, row2 );

	const float lightGridOrigin[4] = {
		lightGrid.lightGridOrigin[0], lightGrid.lightGridOrigin[1], lightGrid.lightGridOrigin[2], 0.0f
	};
	const float lightGridSize[4] = {
		lightGrid.lightGridSize[0], lightGrid.lightGridSize[1], lightGrid.lightGridSize[2], 0.0f
	};
	const float lightGridBounds[4] = {
		static_cast<float>( lightGrid.lightGridBounds[0] ),
		static_cast<float>( lightGrid.lightGridBounds[1] ),
		static_cast<float>( lightGrid.lightGridBounds[2] ),
		0.0f
	};
	const float atlasInfo[4] = {
		1.0f / static_cast<float>( atlasWidth ),
		1.0f / static_cast<float>( atlasHeight ),
		static_cast<float>( lightGrid.imageSingleProbeSize ),
		static_cast<float>( lightGrid.imageBorderSize )
	};
	const float visibilityInfo[4] = {
		lightGrid.visibilityMaxDistance > 0.0f ? lightGrid.visibilityMaxDistance : 4096.0f,
		3.0f,
		idMath::ClampFloat( 0.0f, 1.0f, r_lightGridVisibilityFloor.GetFloat() ),
		2.0f
	};
	const float probeInfo[4] = {
		lightGrid.relocationMaxDistance > 0.0f ? lightGrid.relocationMaxDistance : 48.0f,
		probeImage != NULL ? 1.0f : 0.0f,
		0.0f,
		0.0f
	};
	const bool usePortalBlend = portalBlend != NULL && portalBlend->blendDistance > 0.0f && !portalBlend->portalBounds.IsCleared();
	const float lightGridIntensity = idMath::ClampFloat( 0.0f, 16.0f, r_lightGridIntensity.GetFloat() );
	const float blendInfo[4] = {
		lightGridIntensity,
		usePortalBlend ? ( invertPortalBlend ? -1.0f : 1.0f ) : 0.0f,
		usePortalBlend ? portalBlend->blendDistance : 0.0f,
		0.0f
	};
	const float portalPlane[4] = {
		usePortalBlend ? portalBlend->portalPlane[0] : 0.0f,
		usePortalBlend ? portalBlend->portalPlane[1] : 0.0f,
		usePortalBlend ? portalBlend->portalPlane[2] : 0.0f,
		usePortalBlend ? portalBlend->portalPlane[3] : 0.0f
	};
	const float portalBoundsMin[4] = {
		usePortalBlend ? portalBlend->portalBounds[0][0] : 0.0f,
		usePortalBlend ? portalBlend->portalBounds[0][1] : 0.0f,
		usePortalBlend ? portalBlend->portalBounds[0][2] : 0.0f,
		0.0f
	};
	const float portalBoundsMax[4] = {
		usePortalBlend ? portalBlend->portalBounds[1][0] : 0.0f,
		usePortalBlend ? portalBlend->portalBounds[1][1] : 0.0f,
		usePortalBlend ? portalBlend->portalBounds[1][2] : 0.0f,
		0.0f
	};
	const float debugInfo[4] = {
		static_cast<float>( r_lightGridDebug.GetInteger() ),
		0.18f,
		0.18f,
		0.18f
	};
	const bool useDepthTextureCompare = debugMode != 3 && RB_LightGridUseDepthTextureCompare();
	const float depthInfo[4] = {
		useDepthTextureCompare ? 1.0f / static_cast<float>( rbLightGridDepthCompareWidth ) : 1.0f,
		useDepthTextureCompare ? 1.0f / static_cast<float>( rbLightGridDepthCompareHeight ) : 1.0f,
		idMath::ClampFloat( 0.0f, 0.1f, r_lightGridDepthTolerance.GetFloat() ),
		useDepthTextureCompare ? 1.0f : 0.0f
	};
	const float depthViewport[4] = {
		backEnd.viewDef != NULL ? static_cast<float>( tr.viewportOffset[0] + backEnd.viewDef->viewport.x1 ) : 0.0f,
		backEnd.viewDef != NULL ? static_cast<float>( tr.viewportOffset[1] + backEnd.viewDef->viewport.y1 ) : 0.0f,
		0.0f,
		0.0f
	};
	const float colorInfo[4] = {
		idMath::ClampFloat( 0.25f, 4.0f, r_lightGridIrradianceGamma.GetFloat() ),
		idMath::ClampFloat( 0.0f, 16.0f, r_lightGridMaxContribution.GetFloat() ),
		RB_IsSceneRenderTexture( backEnd.renderTexture ) ? 1.0f : 0.0f,
		0.0f
	};
	idVec4 flatDiffuseParams;
	flatDiffuseParams.Zero();

	const bool useAlphaToCoverage = RB_UseAlphaToCoverage( shader );
	if ( useAlphaToCoverage ) {
		glEnable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}

	GL_Cull( shader->GetCullType() );
	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glEnable( GL_POLYGON_OFFSET_FILL );
		glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset() );
	}

	if ( surf->space->weaponDepthHack ) {
		RB_EnterWeaponDepthHack();
	}
	if ( surf->space->modelDepthHack != 0.0f ) {
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
	}

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );
	glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, normal ) ) );
	glEnableClientState( GL_NORMAL_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );

	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );
	GL_SelectTexture( 1 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET ) );
	GL_SelectTexture( 2 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET ) );
	GL_SelectTexture( 0 );

	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW0], 1, row0 );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW1], 1, row1 );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_MODEL_MATRIX_ROW2], 1, row2 );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_LIGHTGRID_ORIGIN], 1, lightGridOrigin );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_LIGHTGRID_SIZE], 1, lightGridSize );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_LIGHTGRID_BOUNDS], 1, lightGridBounds );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_ATLAS_INFO], 1, atlasInfo );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_VISIBILITY_INFO], 1, visibilityInfo );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_PROBE_INFO], 1, probeInfo );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_BLEND_INFO], 1, blendInfo );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_PORTAL_PLANE], 1, portalPlane );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_PORTAL_BOUNDS_MIN], 1, portalBoundsMin );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_PORTAL_BOUNDS_MAX], 1, portalBoundsMax );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DEBUG_INFO], 1, debugInfo );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DEPTH_INFO], 1, depthInfo );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DEPTH_VIEWPORT], 1, depthViewport );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_COLOR_INFO], 1, colorInfo );
	glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_FLAT_DIFFUSE_PARAMS], 1, flatDiffuseParams.ToFloatPtr() );

	GL_SelectTextureNoClient( 2 );
	irradianceImage->SetSamplerState( TF_LINEAR, TR_CLAMP );
	irradianceImage->Bind();
	GL_SelectTextureNoClient( 3 );
	if ( visibilityImage != NULL ) {
		visibilityImage->SetSamplerState( TF_LINEAR, TR_CLAMP );
		visibilityImage->Bind();
	} else {
		globalImages->whiteImage->Bind();
	}
	GL_SelectTextureNoClient( 4 );
	if ( probeImage != NULL ) {
		probeImage->SetSamplerState( TF_LINEAR, TR_CLAMP );
		probeImage->Bind();
	} else {
		globalImages->blackImage->Bind();
	}
	GL_SelectTextureNoClient( 5 );
	if ( useDepthTextureCompare && globalImages->currentDepthImage != NULL ) {
		globalImages->currentDepthImage->SetSamplerState( TF_NEAREST, TR_CLAMP );
		globalImages->currentDepthImage->Bind();
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE );
		glTexParameteri( GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE );
	} else {
		globalImages->whiteImage->Bind();
	}

	if ( receiverOnlySubmission ) {
		idVec4 identityMatrix[2];
		RB_LightGridSetIdentityTextureMatrix( identityMatrix );
		rbLightGridAlbedoBinding_t albedoBinding;
		RB_LightGridFindRepresentativeAlbedo( surf, albedoBinding );
		const bool useVertexColorArray = albedoBinding.stage != NULL && albedoBinding.stage->vertexColor != SVC_IGNORE && albedoBinding.stageIndex >= 0;
		idVec4 representativeFlatDiffuseParams;
		representativeFlatDiffuseParams.Zero();
		if ( albedoBinding.stage != NULL && albedoBinding.stage->lighting == SL_DIFFUSE ) {
			RB_GetFlatDiffuseParams( surf, representativeFlatDiffuseParams );
		}

		glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_BUMP_MATRIX_S], 1, identityMatrix[0].ToFloatPtr() );
		glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_BUMP_MATRIX_T], 1, identityMatrix[1].ToFloatPtr() );
		glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DIFFUSE_MATRIX_S], 1, albedoBinding.matrix[0].ToFloatPtr() );
		glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DIFFUSE_MATRIX_T], 1, albedoBinding.matrix[1].ToFloatPtr() );
		glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DIFFUSE_COLOR], 1, albedoBinding.color );
		glUniform2fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_VERTEX_COLOR_PARAMS], 1, albedoBinding.vertexColorParams );
		glUniform4fvARB( rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_FLAT_DIFFUSE_PARAMS], 1, representativeFlatDiffuseParams.ToFloatPtr() );

		GL_SelectTexture( 0 );
		if ( albedoBinding.stage != NULL && albedoBinding.stage->texture.texgen == TG_POT_CORRECTION && surf->dynamicTexCoords != NULL ) {
			glTexCoordPointer( 2, GL_FLOAT, 0, vertexCache.Position( surf->dynamicTexCoords ) );
		} else {
			glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );
		}

		if ( useVertexColorArray ) {
			RB_SetStageVertexColorPointer( surf, albedoBinding.stageIndex, ac );
			glEnableClientState( GL_COLOR_ARRAY );
		} else {
			glDisableClientState( GL_COLOR_ARRAY );
			glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
		}

		GL_SelectTextureNoClient( 0 );
		globalImages->flatNormalMap->Bind();
		GL_SelectTextureNoClient( 1 );
		albedoBinding.image->Bind();

		RB_DrawElementsWithCounters( tri );
		if ( useVertexColorArray ) {
			glDisableClientState( GL_COLOR_ARRAY );
		}

		GL_SelectTextureNoClient( 2 );
		globalImages->BindNull();
		GL_SelectTextureNoClient( 3 );
		globalImages->BindNull();
		GL_SelectTextureNoClient( 4 );
		globalImages->BindNull();
		GL_SelectTextureNoClient( 5 );
		globalImages->BindNull();
		GL_SelectTextureNoClient( 1 );
		globalImages->BindNull();
		GL_SelectTextureNoClient( 0 );
		globalImages->BindNull();

		GL_SelectTexture( 2 );
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		GL_SelectTexture( 1 );
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		GL_SelectTexture( 0 );
		glDisableClientState( GL_COLOR_ARRAY );
		glDisableClientState( GL_NORMAL_ARRAY );

		if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
			RB_LeaveDepthHack();
		}
		if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
			glDisable( GL_POLYGON_OFFSET_FILL );
		}
		if ( useAlphaToCoverage ) {
			glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
		}

		if ( drawStats != NULL ) {
			drawStats->stageSubmit++;
		}
		return true;
	}

	idImage *currentBumpImage = globalImages->flatNormalMap;
	idVec4 currentBumpMatrix[2];
	RB_LightGridSetIdentityTextureMatrix( currentBumpMatrix );
	bool submitted = false;
	int stageRejects = 0;
	int stageSubmits = 0;
	const int stageCount = shader->GetNumStages();
	for ( int stageIndex = 0; stageIndex < stageCount; stageIndex++ ) {
		const shaderStage_t *stage = shader->GetStage( stageIndex );
		if ( stage->lighting == SL_BUMP ) {
			if ( !r_skipBump.GetBool() && RB_LightGridMaterialStageIsActive( stage, regs ) ) {
				R_SetDrawInteraction( stage, regs, &currentBumpImage, currentBumpMatrix, NULL );
				if ( currentBumpImage == NULL ) {
					currentBumpImage = globalImages->flatNormalMap;
				}
			}
			continue;
		}
		if ( !RB_LightGridStageCanProvideAlbedo( shader, stage, regs ) ) {
			stageRejects++;
			continue;
		}

		submitted = true;
		stageSubmits++;
		RB_STD_DrawLightGridAlbedoStage( surf, stage, stageIndex, currentBumpImage, currentBumpMatrix, regs, tri, ac );
	}

	GL_SelectTextureNoClient( 2 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 3 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 4 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 5 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 1 );
	globalImages->BindNull();
	GL_SelectTextureNoClient( 0 );
	globalImages->BindNull();

	GL_SelectTexture( 2 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 1 );
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	GL_SelectTexture( 0 );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );

	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		RB_LeaveDepthHack();
	}
	if ( shader->TestMaterialFlag( MF_POLYGONOFFSET ) ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	if ( useAlphaToCoverage ) {
		glDisable( GL_SAMPLE_ALPHA_TO_COVERAGE );
	}

	if ( drawStats != NULL ) {
		drawStats->stageReject += stageRejects;
		drawStats->stageSubmit += stageSubmits;
	}

	return submitted;
}

static bool RB_STD_LightGridInlinePassAvailable( void ) {
	return r_useLightGrid.GetBool() && glConfig.GLSLProgramAvailable && rbLightGridIndirectStage.glslProgramObject != 0;
}

static bool RB_LightGridUseDepthTextureCompare( void ) {
	return rbLightGridDepthCompareAvailable && globalImages != NULL && globalImages->currentDepthImage != NULL;
}

static void RB_PrepareLightGridDepthTexture( void ) {
	rbLightGridDepthCompareAvailable = false;
	rbLightGridDepthCompareWidth = 0;
	rbLightGridDepthCompareHeight = 0;

	if ( !r_useLightGrid.GetBool() || !glConfig.GLSLProgramAvailable || backEnd.viewDef == NULL || globalImages == NULL || globalImages->currentDepthImage == NULL ) {
		return;
	}

	RB_InitLightGridIndirectStage();
	if ( rbLightGridIndirectStage.glslProgramObject == 0 ) {
		return;
	}

	const int viewportWidth = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int viewportHeight = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
	if ( viewportWidth <= 0 || viewportHeight <= 0 ) {
		return;
	}

	RB_CaptureCurrentDepthImage( viewportWidth, viewportHeight );
	if ( !backEnd.currentDepthCopied ) {
		return;
	}

	idImage *depthImage = globalImages->currentDepthImage;
	const idImageOpts &depthOpts = depthImage->GetOpts();
	rbLightGridDepthCompareWidth = depthOpts.width > 0 ? depthOpts.width : viewportWidth;
	rbLightGridDepthCompareHeight = depthOpts.height > 0 ? depthOpts.height : viewportHeight;
	rbLightGridDepthCompareAvailable = rbLightGridDepthCompareWidth > 0 && rbLightGridDepthCompareHeight > 0;
}

static void RB_STD_BindLightGridProgram( void ) {
	glUseProgramObjectARB( (GLhandleARB)rbLightGridIndirectStage.glslProgramObject );
	for ( int i = 0; i < rbLightGridIndirectStage.numShaderTextures; i++ ) {
		if ( rbLightGridIndirectStage.shaderTextureLocations[i] >= 0 ) {
			glUniform1iARB( rbLightGridIndirectStage.shaderTextureLocations[i], i );
		}
	}
}

static void RB_STD_SetLightGridDrawState( const bool inlineSurface ) {
	const int debugMode = r_lightGridDebug.GetInteger();
	const bool debugIrradianceReplace = debugMode == 2 || debugMode == 4 || debugMode == 5 || debugMode == 6 || debugMode == 7;
	const bool debugCoverageNoDepth = debugMode == 3;
	const bool debugDepthTexture = debugMode == 6 || debugMode == 7;
	const bool depthTextureCompare = !debugCoverageNoDepth && RB_LightGridUseDepthTextureCompare();
	const bool disableHardwareDepth = debugCoverageNoDepth || ( depthTextureCompare && debugDepthTexture );
	const float lightGridDepthBiasFactor = inlineSurface ? 0.0f : r_lightGridDepthBiasFactor.GetFloat();
	const float lightGridDepthBiasUnits = inlineSurface ? 0.0f : r_lightGridDepthBiasUnits.GetFloat();
	const bool useLightGridDepthBias = !debugCoverageNoDepth && !depthTextureCompare && ( lightGridDepthBiasFactor != 0.0f || lightGridDepthBiasUnits != 0.0f );
	const int blendState = debugIrradianceReplace ? ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO ) : ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
	const int depthState = disableHardwareDepth ? GLS_DEPTHFUNC_ALWAYS : ( useLightGridDepthBias ? GLS_DEPTHFUNC_LESS : GLS_DEPTHFUNC_EQUAL );

	GL_ClearStateDelta();
	GL_State( blendState | GLS_DEPTHMASK | depthState );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glBlendEquation( GL_FUNC_ADD );
	if ( debugIrradianceReplace ) {
		glDisable( GL_BLEND );
		glBlendFunc( GL_ONE, GL_ZERO );
	} else {
		glEnable( GL_BLEND );
		glBlendFunc( GL_ONE, GL_ONE );
	}
	glDepthMask( GL_FALSE );
	if ( disableHardwareDepth ) {
		glDisable( GL_DEPTH_TEST );
	} else {
		glEnable( GL_DEPTH_TEST );
	}
	if ( useLightGridDepthBias ) {
		glPolygonOffset( lightGridDepthBiasFactor, lightGridDepthBiasUnits );
		glEnable( GL_POLYGON_OFFSET_FILL );
	}
	glDisable( GL_STENCIL_TEST );
}

static void RB_STD_FinishLightGridDrawState( const bool inlineSurface ) {
	const int debugMode = r_lightGridDebug.GetInteger();
	const bool debugCoverageNoDepth = debugMode == 3;
	const bool debugDepthTexture = debugMode == 6 || debugMode == 7;
	const bool depthTextureCompare = !debugCoverageNoDepth && RB_LightGridUseDepthTextureCompare();
	const bool disableHardwareDepth = debugCoverageNoDepth || ( depthTextureCompare && debugDepthTexture );
	const bool useLightGridDepthBias =
		!inlineSurface &&
		!debugCoverageNoDepth &&
		!depthTextureCompare &&
		( r_lightGridDepthBiasFactor.GetFloat() != 0.0f || r_lightGridDepthBiasUnits.GetFloat() != 0.0f );

	glUseProgramObjectARB( 0 );
	if ( useLightGridDepthBias ) {
		glDisable( GL_POLYGON_OFFSET_FILL );
	}
	if ( disableHardwareDepth ) {
		glEnable( GL_DEPTH_TEST );
	}
	glEnable( GL_BLEND );
	GL_SelectTexture( 0 );
	GL_Cull( CT_FRONT_SIDED );
	GL_ClearStateDelta();
}

static bool RB_STD_DrawLightGridInlineSurface( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->material == NULL ) {
		return false;
	}
	if ( surf->material->GetSort() >= SS_POST_PROCESS || surf->material->SuppressInSubview() ) {
		return false;
	}

	const int debugMode = r_lightGridDebug.GetInteger();
	const bool receiverOnlySubmission = RB_LightGridUsesReceiverOnlySubmission( debugMode );
	const LightGrid *lightGrid = NULL;
	bool viewWeaponLightGrid = false;
	if ( !RB_SurfaceHasLightGrid( surf, lightGrid ) ) {
		if ( !RB_SurfaceHasViewWeaponLightGrid( surf, lightGrid ) ) {
			return false;
		}
		viewWeaponLightGrid = true;
	}
	if ( !receiverOnlySubmission && !RB_LightGridHasActiveAlbedoStage( surf->material, surf->shaderRegisters ) ) {
		return false;
	}

	glDepthRange( 0.0, 1.0 );
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
	glLoadMatrixf( surf->space->modelViewMatrix );
	backEnd.currentSpace = surf->space;
	if ( r_useScissor.GetBool() && !backEnd.currentScissor.Equals( surf->scissorRect ) ) {
		backEnd.currentScissor = surf->scissorRect;
		glScissor(
			tr.viewportOffset[0] + backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
			tr.viewportOffset[1] + backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
			backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
			backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
	}

	RB_STD_SetLightGridDrawState( true );
	RB_STD_BindLightGridProgram();

	bool submitted = false;
	if ( viewWeaponLightGrid ) {
		rbLightGridPortalBlend_t portalBlend;
		if ( RB_SurfaceHasLightGridPortalBlend( surf, portalBlend ) ) {
			submitted |= RB_STD_DrawLightGridSurface( surf, *lightGrid, &portalBlend, true );
			submitted |= RB_STD_DrawLightGridSurface( surf, *portalBlend.neighborLightGrid, &portalBlend, false );
		} else {
			submitted |= RB_STD_DrawLightGridSurface( surf, *lightGrid );
		}
	} else {
		submitted |= RB_STD_DrawLightGridSurface( surf, *lightGrid );
	}

	RB_STD_FinishLightGridDrawState( true );
	return submitted;
}

static void RB_STD_LightGridIndirect( void ) {
	if ( !r_useLightGrid.GetBool() || r_skipDiffuse.GetBool() ) {
		return;
	}
	if ( !glConfig.GLSLProgramAvailable || backEnd.viewDef == NULL || !backEnd.viewDef->viewEntitys ) {
		return;
	}

	// without a single usable grid in the world this pass draws nothing;
	// skip the program bind, residency walk, and the two numDrawSurfs filter
	// loops outright (stock content ships no baked grids).
	idRenderWorldLocal *gridWorld = backEnd.viewDef->renderWorld;
	if ( gridWorld == NULL || !gridWorld->AnyLightGridAvailable() ) {
		return;
	}

	RB_InitLightGridIndirectStage();
	if ( !R_ValidateGLSLProgram( &rbLightGridIndirectStage ) ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_LightGridIndirect ----------\n" );

	// Restore whole-view scissor after per-light/decal passes so the overlay
	// is clipped only by the current view, not by the last submitted light.
	glViewport(
		tr.viewportOffset[0] + backEnd.viewDef->viewport.x1,
		tr.viewportOffset[1] + backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );
	if ( r_useScissor.GetBool() ) {
		glScissor(
			tr.viewportOffset[0] + backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
			tr.viewportOffset[1] + backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
			backEnd.viewDef->scissor.x2 - backEnd.viewDef->scissor.x1 + 1,
			backEnd.viewDef->scissor.y2 - backEnd.viewDef->scissor.y1 + 1 );
		backEnd.currentScissor = backEnd.viewDef->scissor;
	}
	glDepthRange( 0.0, 1.0 );
	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );

	const int debugMode = r_lightGridDebug.GetInteger();
	const bool receiverOnlySubmission = RB_LightGridUsesReceiverOnlySubmission( debugMode );

	RB_STD_SetLightGridDrawState( false );
	RB_STD_BindLightGridProgram();

	RB_UpdateLightGridImageResidency( backEnd.viewDef->renderWorld );

	const int reportFrames = r_lightGridReport.GetInteger();
	const bool reportStats = reportFrames > 0 && ( backEnd.frameCount % reportFrames ) == 0;
	int worldConsidered = 0;
	int worldPostSkipped = 0;
	int worldNoGrid = 0;
	int worldNoAlbedo = 0;
	int worldGrid = 0;
	int worldSubmitted = 0;
	int weaponGrid = 0;
	int weaponSubmitted = 0;
	rbLightGridDrawStats_t drawStats;
	memset( &drawStats, 0, sizeof( drawStats ) );

	backEnd.currentSpace = NULL;
	for ( int i = 0; i < backEnd.viewDef->numDrawSurfs; i++ ) {
		drawSurf_t *surf = backEnd.viewDef->drawSurfs[i];
		worldConsidered++;
		if ( surf == NULL || surf->material == NULL ) {
			continue;
		}
		if ( surf->material->GetSort() >= SS_POST_PROCESS || surf->material->SuppressInSubview() ) {
			worldPostSkipped++;
			continue;
		}

		const LightGrid *lightGrid = NULL;
		if ( !RB_SurfaceHasLightGrid( surf, lightGrid ) ) {
			worldNoGrid++;
			continue;
		}
		if ( !receiverOnlySubmission && !RB_LightGridHasActiveAlbedoStage( surf->material, surf->shaderRegisters ) ) {
			worldNoAlbedo++;
			continue;
		}

		worldGrid++;
		RB_SimpleSurfaceSetup( surf );
		if ( RB_STD_DrawLightGridSurface( surf, *lightGrid, NULL, false, reportStats ? &drawStats : NULL ) ) {
			worldSubmitted++;
		}
	}

	RB_LogComment( "---------- RB_STD_ViewWeaponLightGridIndirect ----------\n" );

	backEnd.currentSpace = NULL;
	for ( int i = 0; i < backEnd.viewDef->numDrawSurfs; i++ ) {
		drawSurf_t *surf = backEnd.viewDef->drawSurfs[i];
		if ( surf == NULL || surf->material == NULL ) {
			continue;
		}
		if ( surf->material->GetSort() >= SS_POST_PROCESS || surf->material->SuppressInSubview() ) {
			continue;
		}

		const LightGrid *lightGrid = NULL;
		if ( !RB_SurfaceHasViewWeaponLightGrid( surf, lightGrid ) ) {
			continue;
		}

		weaponGrid++;
		RB_SimpleSurfaceSetup( surf );
		bool submitted = false;
		rbLightGridPortalBlend_t portalBlend;
		if ( RB_SurfaceHasLightGridPortalBlend( surf, portalBlend ) ) {
			submitted |= RB_STD_DrawLightGridSurface( surf, *lightGrid, &portalBlend, true, reportStats ? &drawStats : NULL );
			submitted |= RB_STD_DrawLightGridSurface( surf, *portalBlend.neighborLightGrid, &portalBlend, false, reportStats ? &drawStats : NULL );
		} else {
			submitted |= RB_STD_DrawLightGridSurface( surf, *lightGrid, NULL, false, reportStats ? &drawStats : NULL );
		}
		if ( submitted ) {
			weaponSubmitted++;
		}
	}

	if ( reportStats ) {
		common->Printf(
			"LightGrid receiver stats: frame %i debug=%i debugLoc=%i world considered=%i postSkip=%i noGrid=%i noAlbedo=%i grid=%i submitted=%i weaponGrid=%i weaponSubmitted=%i draw null=%i noAlb=%i cache=%i empty=%i noIrr=%i ensure=%i default=%i badAtlas=%i stageReject=%i stageSubmit=%i\n",
			backEnd.frameCount,
			r_lightGridDebug.GetInteger(),
			rbLightGridIndirectStage.shaderParmLocations[RB_LIGHTGRID_UNIFORM_DEBUG_INFO],
			worldConsidered,
			worldPostSkipped,
			worldNoGrid,
			worldNoAlbedo,
			worldGrid,
			worldSubmitted,
			weaponGrid,
			weaponSubmitted,
			drawStats.nullInput,
			drawStats.noAlbedo,
			drawStats.cacheFail,
			drawStats.emptyGeometry,
			drawStats.noIrradiance,
			drawStats.ensureFail,
			drawStats.defaultIrradiance,
			drawStats.badAtlas,
			drawStats.stageReject,
			drawStats.stageSubmit );
	}

	if ( worldSubmitted > 0 || weaponSubmitted > 0 ) {
		backEnd.currentRenderCopied = false;
	}

	RB_STD_FinishLightGridDrawState( false );
}

/*
=========================================================================================

	Player visibility effects

	Presentation-only overlays driven by renderEntity_t: an additive brightskin
	wash, an additive rimlight, and a silhouette outline. Only the multiplayer
	game sets them, and only on other players, so the pass costs one walk of the
	surface list in every other view.

	The outline is a shell pushed away from the silhouette and masked against the
	silhouette itself in the stencil buffer. Without that mask the shell covers
	the whole body instead of ringing it, and overlapping surfaces blend their
	shells over each other.

	REF_OUTLINE_NODEPTH splits the outline in two. A see-through entity has its
	outline drawn through the geometry in front of it, and its silhouette mask is
	built separately from the depth tested one, because a mask that records
	occluded pixels would erase the ring off anything standing in front of it.
	Only the outline follows the flag; the rimlight and the brightskin stay depth
	tested, because a ring is a position marker where a shaded body is a wallhack.

	REF_OUTLINE_THROUGH_WORLD goes further: the entity is forced into every view
	whether or not the portal flood reached it, so an ally on the far side of the
	level still gets a ring. Those surfaces arrive on their own list and are drawn
	by the outline pass alone - they are in no depth buffer, so they cannot be lit,
	shadowed, or depth tested at all.

=========================================================================================
*/

// Stencil bit used to mask the outline shell against the body it belongs to.
// The pass runs after the interactions, where RB_STD_DrawView has already
// neutralized the shadow stencil test, and only this single bit is ever cleared
// or written so any remaining shadow counts survive untouched.
static const GLuint RB_PLAYER_OUTLINE_STENCIL_BIT = 1;

// Matches the value RB_DrawView and the modern executor install before their
// own stencil clears, so restoring it leaves shared GL state exactly as found.
static GLint RB_PlayerVisibilitySafeStencilClearValue( void ) {
	const int stencilBits = idMath::ClampInt( 1, 30, ( glConfig.stencilBits > 0 ) ? glConfig.stencilBits : 8 );
	return 1 << ( stencilBits - 1 );
}

static bool RB_PlayerVisibilityEffectsSurfaceAllowed( const drawSurf_t *surf ) {
	if ( surf == NULL || surf->space == NULL || surf->space->entityDef == NULL || surf->geo == NULL || surf->material == NULL ) {
		return false;
	}
	if ( ( surf->dsFlags & DSF_BSE_EFFECT ) != 0 ) {
		return false;
	}
	if ( surf->geo->numIndexes <= 0 ) {
		return false;
	}
	// Every pass here re-draws against the depth buffer the scene already filled,
	// which only lines up for spaces submitted with the unmodified projection.
	// Players never own a depth hack, so this only rejects surfaces that would
	// have compared against the wrong depths.
	if ( surf->space->weaponDepthHack || surf->space->modelDepthHack != 0.0f ) {
		return false;
	}

	const idMaterial *shader = surf->material;
	if ( !shader->IsDrawn() || shader->Coverage() == MC_TRANSLUCENT ) {
		return false;
	}
	if ( shader->IsPortalSky() || shader->SuppressInSubview() || shader->GetSort() >= SS_POST_PROCESS || shader->HasGui() ) {
		return false;
	}

	return true;
}

// Player outline width ladder, matching cl_player_outline_width. The cel outline
// runs its own, wider ladder ( CEL_MIN/MAX_OUTLINE_WIDTH ), so the shell helpers
// the two passes share take an already clamped width rather than imposing this
// one on both.
static const float RB_PLAYER_OUTLINE_MIN_WIDTH = 0.5f;
static const float RB_PLAYER_OUTLINE_MAX_WIDTH = 6.0f;

static bool RB_PlayerVisibilityEffectsHasOutline( const renderEntity_t &renderEntity ) {
	return renderEntity.outlineColor[3] > 0.0f && renderEntity.outlineWidth > 0.0f;
}

static float RB_PlayerVisibilityOutlineWidth( const renderEntity_t &renderEntity ) {
	return idMath::ClampFloat( RB_PLAYER_OUTLINE_MIN_WIDTH, RB_PLAYER_OUTLINE_MAX_WIDTH, renderEntity.outlineWidth );
}

// A see-through entity has its outline drawn through the geometry in front of it.
// The outline alone: a ring is a position marker, while a rimlight or brightskin
// through a wall shades the whole body and reads as a full wallhack.
//
// THROUGH_WORLD implies NODEPTH and the implication is drawn here rather than
// asked of the caller: such an entity may not be in the depth buffer at all, so
// there is nothing a depth test could compare it against.
static bool RB_PlayerVisibilityIsSeeThrough( const renderEntity_t &renderEntity ) {
	return ( renderEntity.outlineFlags & ( REF_OUTLINE_NODEPTH | REF_OUTLINE_THROUGH_WORLD ) ) != 0;
}

// Surfaces the front end forced into the view for their ring alone. They live on
// their own list precisely so no other pass can pick them up, so reaching them
// takes an explicit ask. A view that never built the list reads as empty.
static drawSurf_t **RB_PlayerVisibilityOutlineOnlySurfaces( int &count ) {
	if ( backEnd.viewDef->outlineDrawSurfs == NULL || backEnd.viewDef->numOutlineDrawSurfs <= 0 ) {
		count = 0;
		return NULL;
	}

	count = backEnd.viewDef->numOutlineDrawSurfs;
	return backEnd.viewDef->outlineDrawSurfs;
}

// Alpha-tested surfaces do grow a shell.
//
// They were excluded for a while on the reasoning that a shell samples no texture,
// so a perforated surface would ink its whole quad. That reasoning holds for an
// alpha-tested card - a grate, a fence - and not at all for a character: Quake 4
// player bodies are perforated for small details, but the mesh is still
// player-shaped, so the shell follows the silhouette correctly and the only thing
// lost is an inner ring around holes too small to see. Measured on q4dm1, the
// exclusion cut peak outline coverage from 8019 pixels to 962 - it removed the
// body ring from every player and left only the head and the world weapon.
static bool RB_PlayerVisibilityOutlineShellAllowed( const drawSurf_t *surf ) {
	return surf->material->Coverage() != MC_TRANSLUCENT;
}

static bool RB_PlayerVisibilityEffectsHasRimlight( const renderEntity_t &renderEntity ) {
	return renderEntity.rimlightColor[3] > 0.0f;
}

static bool RB_PlayerVisibilityEffectsHasBrightSkin( const renderEntity_t &renderEntity ) {
	return renderEntity.brightSkinColor[3] > 0.0f;
}

static void RB_PlayerVisibilityApplyScissor( const idScreenRect &rect ) {
	if ( !r_useScissor.GetBool() || backEnd.currentScissor.Equals( rect ) ) {
		return;
	}

	backEnd.currentScissor = rect;
	glScissor(
		backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
		backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
		backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
		backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
}

// The outline shell reaches outside the surface scissor rect, which is sized to
// the unexpanded model, so the outline passes clip against the whole view
// instead of clipping their own edges away.
static void RB_PlayerVisibilitySetSurfaceSetup( const drawSurf_t *surf, const float *modelViewMatrix, const bool useSurfaceScissor ) {
	glLoadMatrixf( modelViewMatrix );
	backEnd.currentSpace = NULL;

	RB_PlayerVisibilityApplyScissor( useSurfaceScissor ? surf->scissorRect : backEnd.viewDef->scissor );
}

static bool RB_PlayerVisibilityPrepareSurface( const drawSurf_t *surf, idDrawVert *&ac ) {
	if ( !RB_EnsurePackedClassicDrawCaches( surf, false, true ) ) {
		return false;
	}

	const srfTriangles_t *tri = surf->geo;
	if ( tri->ambientCache == NULL ) {
		return false;
	}

	ac = static_cast<idDrawVert *>( vertexCache.Position( tri->ambientCache ) );

	GL_SelectTexture( 0 );
	glVertexPointer( 3, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, xyz ) ) );
	glTexCoordPointer( 2, GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, st ) ) );
	glDisableClientState( GL_COLOR_ARRAY );
	return true;
}

static const idBounds &RB_PlayerVisibilityModelBounds( const drawSurf_t *surf ) {
	const idRenderEntityLocal *entityDef = surf->space->entityDef;
	if ( !entityDef->referenceBounds.IsCleared() ) {
		return entityDef->referenceBounds;
	}
	if ( !entityDef->parms.bounds.IsCleared() ) {
		return entityDef->parms.bounds;
	}
	return surf->geo->bounds;
}

static void RB_PlayerVisibilityBuildExpandedModelViewMatrix( const drawSurf_t *surf, const float scale, float out[16] ) {
	const float *modelView = surf->space->modelViewMatrix;
	memcpy( out, modelView, sizeof( float ) * 16 );
	if ( scale == 1.0f ) {
		return;
	}

	// Scale about the centre of the model bounds, not the model origin. Player
	// models keep their origin at the feet, so scaling about it would slide the
	// shell upwards: a thick band above the head and no outline at the legs.
	const idVec3 recenter = RB_PlayerVisibilityModelBounds( surf ).GetCenter() * ( 1.0f - scale );

	for ( int column = 0; column < 3; column++ ) {
		const int offset = column * 4;
		out[offset + 0] *= scale;
		out[offset + 1] *= scale;
		out[offset + 2] *= scale;
		out[offset + 3] *= scale;
	}
	for ( int row = 0; row < 4; row++ ) {
		out[12 + row] = modelView[12 + row]
			+ modelView[row] * recenter.x
			+ modelView[4 + row] * recenter.y
			+ modelView[8 + row] * recenter.z;
	}
}

static float RB_PlayerVisibilityWorldUnitsPerPixel( const drawSurf_t *surf ) {
	const int viewportHeight = Max( 1, backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
	const float distance = Max( 1.0f, surf->space->distanceToCamera );
	const float fovY = idMath::ClampFloat( 1.0f, 179.0f, backEnd.viewDef->renderView.fov_y );
	const float worldHeightAtDistance = 2.0f * idMath::Tan( DEG2RAD( fovY * 0.5f ) ) * distance;

	return worldHeightAtDistance / static_cast<float>( viewportHeight );
}

// Fallback width control for the fixed-function shell. A uniform scale can only
// approximate a constant pixel width - parts of the model further from the
// bounds centre expand more - so the GLSL path is preferred whenever it loads.
//
// The width arrives already clamped by whichever policy asked for it. Re-clamping
// here to the player ladder is what silently held the cel outline to 6 pixels on
// a fixed-function driver while the GLSL path honoured all 8.
static float RB_PlayerVisibilityOutlineScale( const drawSurf_t *surf, const float requestedWidth ) {
	const float width = Max( 0.0f, requestedWidth );
	const float radius = RB_PlayerVisibilityModelBounds( surf ).GetRadius();
	if ( radius <= 1.0f ) {
		return 1.02f;
	}

	const float expansion = width * RB_PlayerVisibilityWorldUnitsPerPixel( surf );

	return idMath::ClampFloat( 1.001f, 2.0f, 1.0f + expansion / radius );
}

static void RB_PlayerVisibilityModelRows( const viewEntity_t *space, idVec4 rows[3] ) {
	rows[0].Set( space->modelMatrix[0], space->modelMatrix[4], space->modelMatrix[8], space->modelMatrix[12] );
	rows[1].Set( space->modelMatrix[1], space->modelMatrix[5], space->modelMatrix[9], space->modelMatrix[13] );
	rows[2].Set( space->modelMatrix[2], space->modelMatrix[6], space->modelMatrix[10], space->modelMatrix[14] );
}

static void RB_PlayerVisibilitySetRimlightUniforms( const drawSurf_t *surf, const renderEntity_t &renderEntity ) {
	idVec4 rows[3];
	RB_PlayerVisibilityModelRows( surf->space, rows );

	const idVec3 &viewOrg = backEnd.viewDef->renderView.vieworg;
	const float viewOrigin[4] = { viewOrg.x, viewOrg.y, viewOrg.z, 1.0f };
	// exponent, scale, floor, unused. The default squared falloff keeps the band
	// tight enough to read as a rim rather than as a second brightskin; raising
	// the floor trades that back towards a brightskin on purpose.
	const float rimParams[4] = {
		idMath::ClampFloat( RB_PLAYER_RIMLIGHT_MIN_POWER, RB_PLAYER_RIMLIGHT_MAX_POWER, r_playerRimlightPower.GetFloat() ),
		1.0f,
		idMath::ClampFloat( RB_PLAYER_RIMLIGHT_MIN_FLOOR, RB_PLAYER_RIMLIGHT_MAX_FLOOR, r_playerRimlightFloor.GetFloat() ),
		0.0f
	};

	if ( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_MODEL_MATRIX_ROW0] >= 0 ) {
		glUniform4fvARB( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_MODEL_MATRIX_ROW0], 1, rows[0].ToFloatPtr() );
	}
	if ( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_MODEL_MATRIX_ROW1] >= 0 ) {
		glUniform4fvARB( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_MODEL_MATRIX_ROW1], 1, rows[1].ToFloatPtr() );
	}
	if ( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_MODEL_MATRIX_ROW2] >= 0 ) {
		glUniform4fvARB( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_MODEL_MATRIX_ROW2], 1, rows[2].ToFloatPtr() );
	}
	if ( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_VIEW_ORIGIN] >= 0 ) {
		glUniform4fvARB( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_VIEW_ORIGIN], 1, viewOrigin );
	}
	if ( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_COLOR] >= 0 ) {
		glUniform4fvARB( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_COLOR], 1, renderEntity.rimlightColor.ToFloatPtr() );
	}
	if ( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_PARAMS] >= 0 ) {
		glUniform4fvARB( rbPlayerRimlightStage.shaderParmLocations[RB_PLAYER_RIMLIGHT_UNIFORM_PARAMS], 1, rimParams );
	}
}

static void RB_PlayerVisibilitySetOutlineUniforms( const renderEntity_t &renderEntity ) {
	const int viewportWidth = Max( 1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1 );
	const int viewportHeight = Max( 1, backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
	const float outlineParams[4] = {
		RB_PlayerVisibilityOutlineWidth( renderEntity ),
		2.0f / static_cast<float>( viewportWidth ),
		2.0f / static_cast<float>( viewportHeight ),
		0.0f
	};

	if ( rbPlayerOutlineStage.shaderParmLocations[RB_PLAYER_OUTLINE_UNIFORM_COLOR] >= 0 ) {
		glUniform4fvARB( rbPlayerOutlineStage.shaderParmLocations[RB_PLAYER_OUTLINE_UNIFORM_COLOR], 1, renderEntity.outlineColor.ToFloatPtr() );
	}
	if ( rbPlayerOutlineStage.shaderParmLocations[RB_PLAYER_OUTLINE_UNIFORM_PARAMS] >= 0 ) {
		glUniform4fvARB( rbPlayerOutlineStage.shaderParmLocations[RB_PLAYER_OUTLINE_UNIFORM_PARAMS], 1, outlineParams );
	}
}

// Marks the pixels the body itself covers, so the shell pass can stay outside
// them. A depth tested outline only has to avoid the visible part of the body;
// a see-through outline has to avoid all of it.
static bool RB_PlayerVisibilityMaskOutlineSurface( const drawSurf_t *surf ) {
	const renderEntity_t &renderEntity = surf->space->entityDef->parms;
	if ( !RB_PlayerVisibilityEffectsHasOutline( renderEntity ) ) {
		return false;
	}

	idDrawVert *ac = NULL;
	if ( !RB_PlayerVisibilityPrepareSurface( surf, ac ) ) {
		return false;
	}

	RB_PlayerVisibilitySetSurfaceSetup( surf, surf->space->modelViewMatrix, false );

	const int depthFunc = RB_PlayerVisibilityIsSeeThrough( renderEntity ) ? GLS_DEPTHFUNC_ALWAYS : GLS_DEPTHFUNC_EQUAL;
	GL_State( GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | depthFunc );
	GL_Cull( surf->material->GetCullType() );
	glDisableClientState( GL_NORMAL_ARRAY );

	RB_DrawElementsWithCounters( surf->geo );
	return true;
}

static bool RB_PlayerVisibilityDrawOutlineSurface( const drawSurf_t *surf, const bool useGLSL, const bool maskSilhouette ) {
	const renderEntity_t &renderEntity = surf->space->entityDef->parms;
	if ( !RB_PlayerVisibilityEffectsHasOutline( renderEntity ) || !RB_PlayerVisibilityOutlineShellAllowed( surf ) ) {
		return false;
	}

	idDrawVert *ac = NULL;
	if ( !RB_PlayerVisibilityPrepareSurface( surf, ac ) ) {
		return false;
	}

	if ( useGLSL ) {
		// Screen space normal extrusion holds the outline at the requested pixel
		// width whatever the distance, which is what cl_player_outline_width
		// documents.
		RB_PlayerVisibilitySetSurfaceSetup( surf, surf->space->modelViewMatrix, false );
		glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, normal ) ) );
		glEnableClientState( GL_NORMAL_ARRAY );
		RB_PlayerVisibilitySetOutlineUniforms( renderEntity );
	} else {
		float modelViewMatrix[16];
		RB_PlayerVisibilityBuildExpandedModelViewMatrix( surf, RB_PlayerVisibilityOutlineScale( surf, RB_PlayerVisibilityOutlineWidth( renderEntity ) ), modelViewMatrix );
		RB_PlayerVisibilitySetSurfaceSetup( surf, modelViewMatrix, false );
		GL_SelectTexture( 0 );
		globalImages->whiteImage->Bind();
		glColor4fv( renderEntity.outlineColor.ToFloatPtr() );
		glDisableClientState( GL_NORMAL_ARRAY );
	}

	// A see-through outline relies entirely on the silhouette mask to stay out of
	// the body, so without one it has to keep depth testing instead. A surface
	// forced in for its ring alone cannot fall back that way - it is in no depth
	// buffer, so a depth test would reject all of it - which is why the pass
	// declines to draw those at all when there is no stencil to mask with.
	const bool ignoreDepth = maskSilhouette && RB_PlayerVisibilityIsSeeThrough( renderEntity );
	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA
		| ( ignoreDepth ? GLS_DEPTHFUNC_ALWAYS : GLS_DEPTHFUNC_LESS ) );
	// Draw the far side of the shell. Its depth sits behind the body, so the
	// interior is rejected by the depth test as well as by the stencil mask, and
	// the visible band is exactly the part that pokes past the silhouette.
	GL_Cull( CT_BACK_SIDED );

	RB_DrawElementsWithCounters( surf->geo );
	return true;
}

static bool RB_PlayerVisibilityDrawRimlightSurface( const drawSurf_t *surf ) {
	const renderEntity_t &renderEntity = surf->space->entityDef->parms;
	if ( !RB_PlayerVisibilityEffectsHasRimlight( renderEntity ) ) {
		return false;
	}

	idDrawVert *ac = NULL;
	if ( !RB_PlayerVisibilityPrepareSurface( surf, ac ) ) {
		return false;
	}

	RB_PlayerVisibilitySetSurfaceSetup( surf, surf->space->modelViewMatrix, true );
	// The rimlight stays depth tested even for a see-through entity. Only the
	// outline is meant to read through geometry: it is a thin band that says
	// "someone is there", where a rimlight through a wall shades the whole body
	// and turns an ally marker into a full wallhack.
	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
	GL_Cull( surf->material->GetCullType() );

	glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, normal ) ) );
	glEnableClientState( GL_NORMAL_ARRAY );
	RB_PlayerVisibilitySetRimlightUniforms( surf, renderEntity );

	RB_DrawElementsWithCounters( surf->geo );
	return true;
}

static bool RB_PlayerVisibilityDrawBrightSkinSurface( const drawSurf_t *surf ) {
	const renderEntity_t &renderEntity = surf->space->entityDef->parms;
	if ( !RB_PlayerVisibilityEffectsHasBrightSkin( renderEntity ) ) {
		return false;
	}

	idDrawVert *ac = NULL;
	if ( !RB_PlayerVisibilityPrepareSurface( surf, ac ) ) {
		return false;
	}

	RB_PlayerVisibilitySetSurfaceSetup( surf, surf->space->modelViewMatrix, true );
	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
	GL_Cull( surf->material->GetCullType() );
	GL_SelectTexture( 0 );
	globalImages->whiteImage->Bind();

	idVec4 color = renderEntity.brightSkinColor;
	color[0] *= color[3];
	color[1] *= color[3];
	color[2] *= color[3];
	glColor4fv( color.ToFloatPtr() );
	glDisableClientState( GL_NORMAL_ARRAY );

	RB_DrawElementsWithCounters( surf->geo );
	return true;
}

struct rbPlayerVisibilityWork_t {
	bool	brightSkin;
	bool	rimlight;
	bool	outlineDepthTested;
	bool	outlineSeeThrough;
};

static bool RB_PlayerVisibilityGatherWork( drawSurf_t **drawSurfs, int numDrawSurfs, rbPlayerVisibilityWork_t &work ) {
	work.brightSkin = false;
	work.rimlight = false;
	work.outlineDepthTested = false;
	work.outlineSeeThrough = false;

	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *surf = drawSurfs[i];
		if ( !RB_PlayerVisibilityEffectsSurfaceAllowed( surf ) ) {
			continue;
		}

		const renderEntity_t &renderEntity = surf->space->entityDef->parms;
		work.brightSkin = work.brightSkin || RB_PlayerVisibilityEffectsHasBrightSkin( renderEntity );
		work.rimlight = work.rimlight || RB_PlayerVisibilityEffectsHasRimlight( renderEntity );
		if ( RB_PlayerVisibilityEffectsHasOutline( renderEntity ) ) {
			if ( RB_PlayerVisibilityIsSeeThrough( renderEntity ) ) {
				work.outlineSeeThrough = true;
			} else {
				work.outlineDepthTested = true;
			}
		}

		if ( work.brightSkin && work.rimlight && work.outlineDepthTested && work.outlineSeeThrough ) {
			break;
		}
	}

	// Anything the front end forced in exists only to be outlined, and only ever
	// see-through, so its presence alone is see-through work.
	int numOutlineOnly = 0;
	RB_PlayerVisibilityOutlineOnlySurfaces( numOutlineOnly );
	if ( numOutlineOnly > 0 ) {
		work.outlineSeeThrough = true;
	}

	return work.brightSkin || work.rimlight || work.outlineDepthTested || work.outlineSeeThrough;
}

static bool RB_PlayerVisibilityDrawBrightSkinPass( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	bool submitted = false;

	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *surf = drawSurfs[i];
		if ( RB_PlayerVisibilityEffectsSurfaceAllowed( surf ) && RB_PlayerVisibilityDrawBrightSkinSurface( surf ) ) {
			submitted = true;
		}
	}

	return submitted;
}

static bool RB_PlayerVisibilityDrawRimlightPass( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	RB_InitPlayerRimlightStage();
	// Like every other programmable effect in this backend the rimlight is
	// simply unavailable without GLSL. The fixed-function pipeline cannot
	// evaluate a per-pixel view/normal term, and faking it with a scaled hull
	// produced a pass that never survived its own depth test.
	if ( !glConfig.GLSLProgramAvailable || !R_ValidateGLSLProgram( &rbPlayerRimlightStage ) ) {
		// Say so once. The outline and the brightskin still draw, so the symptom
		// is a rimlight strength that appears to do nothing at all, which is not
		// a state anyone should have to guess at from a silent frame.
		static bool reportedUnavailable = false;
		if ( !reportedUnavailable ) {
			reportedUnavailable = true;
			common->Printf( "player rimlight unavailable: %s\n",
				glConfig.GLSLProgramAvailable ? "glprogs/player_rimlight.* failed to build" : "no GLSL support" );
		}
		return false;
	}

	bool submitted = false;

	glUseProgramObjectARB( (GLhandleARB)rbPlayerRimlightStage.glslProgramObject );
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *surf = drawSurfs[i];
		if ( RB_PlayerVisibilityEffectsSurfaceAllowed( surf ) && RB_PlayerVisibilityDrawRimlightSurface( surf ) ) {
			submitted = true;
		}
	}
	glUseProgramObjectARB( 0 );

	return submitted;
}

// Draws one depth group's shells against a mask built for that group alone.
//
// A see-through body marks every pixel it covers, occluded ones included, so a
// single shared mask let a teammate hidden behind a wall eat the ring off an
// enemy standing in front of it - and with teammate outlines always see-through,
// that is the ordinary case in any team mode with both outlines on. Splitting the
// mask per group fixes it. The see-through group still masks against every
// outlined body, because a shell that already ignores depth has nothing else
// keeping it off the players it overlaps.
static void RB_PlayerVisibilityMaskOutlineList( drawSurf_t **surfs, int numSurfs, const bool seeThroughGroup ) {
	for ( int i = 0; i < numSurfs; i++ ) {
		const drawSurf_t *surf = surfs[i];
		if ( !RB_PlayerVisibilityEffectsSurfaceAllowed( surf ) ) {
			continue;
		}
		if ( !seeThroughGroup && RB_PlayerVisibilityIsSeeThrough( surf->space->entityDef->parms ) ) {
			continue;
		}
		RB_PlayerVisibilityMaskOutlineSurface( surf );
	}
}

static bool RB_PlayerVisibilityDrawOutlineList( drawSurf_t **surfs, int numSurfs, const bool seeThroughGroup,
		const bool useGLSLOutline, const bool maskSilhouette ) {
	bool submitted = false;

	for ( int i = 0; i < numSurfs; i++ ) {
		const drawSurf_t *surf = surfs[i];
		if ( !RB_PlayerVisibilityEffectsSurfaceAllowed( surf ) ) {
			continue;
		}
		if ( RB_PlayerVisibilityIsSeeThrough( surf->space->entityDef->parms ) != seeThroughGroup ) {
			continue;
		}
		if ( RB_PlayerVisibilityDrawOutlineSurface( surf, useGLSLOutline, maskSilhouette ) ) {
			submitted = true;
		}
	}

	return submitted;
}

static bool RB_PlayerVisibilityDrawOutlineGroup( drawSurf_t **drawSurfs, int numDrawSurfs, const bool seeThroughGroup,
		const bool useGLSLOutline, const bool maskSilhouette ) {
	// Surfaces forced in for their ring alone are always see-through, so only this
	// group ever looks at them - and only with a stencil buffer to mask against.
	// They are in no depth buffer, so the no-stencil fallback other shells use
	// (keep depth testing) would reject every pixel, and drawing them unmasked
	// would paint the far side of the body as a solid blob through the wall. Both
	// are worse than no ring, so the pass declines instead of degrading.
	int numOutlineOnly = 0;
	drawSurf_t **outlineOnly = NULL;
	if ( seeThroughGroup && maskSilhouette ) {
		outlineOnly = RB_PlayerVisibilityOutlineOnlySurfaces( numOutlineOnly );
	}

	bool submitted = false;

	if ( maskSilhouette ) {
		// Reserve the mask bit over the whole view before any silhouette is
		// marked. glClear honours the scissor and the stencil write mask, so
		// this only touches RB_PLAYER_OUTLINE_STENCIL_BIT inside the view.
		RB_PlayerVisibilityApplyScissor( backEnd.viewDef->scissor );
		glEnable( GL_STENCIL_TEST );
		glStencilMask( RB_PLAYER_OUTLINE_STENCIL_BIT );
		glClearStencil( 0 );
		glClear( GL_STENCIL_BUFFER_BIT );
		glStencilFunc( GL_ALWAYS, RB_PLAYER_OUTLINE_STENCIL_BIT, RB_PLAYER_OUTLINE_STENCIL_BIT );
		glStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );

		RB_PlayerVisibilityMaskOutlineList( drawSurfs, numDrawSurfs, seeThroughGroup );
		RB_PlayerVisibilityMaskOutlineList( outlineOnly, numOutlineOnly, seeThroughGroup );

		// Paint the shell only where the silhouette bit is clear, and claim the
		// bit as we go so overlapping surfaces never blend their shells over
		// each other.
		glStencilFunc( GL_NOTEQUAL, RB_PLAYER_OUTLINE_STENCIL_BIT, RB_PLAYER_OUTLINE_STENCIL_BIT );
	}

	if ( useGLSLOutline ) {
		glUseProgramObjectARB( (GLhandleARB)rbPlayerOutlineStage.glslProgramObject );
	}
	if ( RB_PlayerVisibilityDrawOutlineList( drawSurfs, numDrawSurfs, seeThroughGroup, useGLSLOutline, maskSilhouette ) ) {
		submitted = true;
	}
	if ( RB_PlayerVisibilityDrawOutlineList( outlineOnly, numOutlineOnly, seeThroughGroup, useGLSLOutline, maskSilhouette ) ) {
		submitted = true;
	}
	if ( useGLSLOutline ) {
		glUseProgramObjectARB( 0 );
	}

	return submitted;
}

static bool RB_PlayerVisibilityDrawOutlinePass( drawSurf_t **drawSurfs, int numDrawSurfs, const rbPlayerVisibilityWork_t &work ) {
	RB_InitPlayerOutlineStage();
	const bool useGLSLOutline = glConfig.GLSLProgramAvailable && R_ValidateGLSLProgram( &rbPlayerOutlineStage );
	// No stencil buffer means no silhouette mask. The depth tested outline still
	// works - the body's own depth rejects the shell - so only the see-through
	// variant has to give something up. The groups still run separately, which
	// costs nothing there: without a mask to build, each still draws every one of
	// its own surfaces exactly once.
	const bool maskSilhouette = glConfig.stencilBits > 0;
	bool submitted = false;

	// Depth tested first, so a see-through ring that overlaps one lands on top -
	// the group that is meant to cut through geometry wins the pixel.
	if ( work.outlineDepthTested
		&& RB_PlayerVisibilityDrawOutlineGroup( drawSurfs, numDrawSurfs, false, useGLSLOutline, maskSilhouette ) ) {
		submitted = true;
	}
	if ( work.outlineSeeThrough
		&& RB_PlayerVisibilityDrawOutlineGroup( drawSurfs, numDrawSurfs, true, useGLSLOutline, maskSilhouette ) ) {
		submitted = true;
	}

	if ( maskSilhouette ) {
		glDisable( GL_STENCIL_TEST );
		glStencilMask( 0xff );
		glClearStencil( RB_PlayerVisibilitySafeStencilClearValue() );
	}

	return submitted;
}

static void RB_STD_DrawPlayerVisibilityEffects( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	if ( !backEnd.viewDef->viewEntitys || drawSurfs == NULL || numDrawSurfs <= 0 || r_skipPlayerVisibilityEffects.GetBool() ) {
		return;
	}

	// One scan decides which passes have anything to draw, so a view without
	// visibility effects - every singleplayer view, and multiplayer with the
	// cvars off - never touches GL state at all.
	rbPlayerVisibilityWork_t work;
	if ( !RB_PlayerVisibilityGatherWork( drawSurfs, numDrawSurfs, work ) ) {
		return;
	}

	RB_LogComment( "---------- RB_STD_DrawPlayerVisibilityEffects ----------\n" );

	bool submitted = false;

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );
	glEnable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );

	// Body overlays first, then the outline: the outline owns the stencil for
	// the rest of the pass and only covers pixels outside the bodies anyway.
	if ( work.brightSkin && RB_PlayerVisibilityDrawBrightSkinPass( drawSurfs, numDrawSurfs ) ) {
		submitted = true;
	}
	if ( work.rimlight && RB_PlayerVisibilityDrawRimlightPass( drawSurfs, numDrawSurfs ) ) {
		submitted = true;
	}
	if ( ( work.outlineDepthTested || work.outlineSeeThrough )
		&& RB_PlayerVisibilityDrawOutlinePass( drawSurfs, numDrawSurfs, work ) ) {
		submitted = true;
	}

	glDisableClientState( GL_NORMAL_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	glEnable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 128, 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	GL_Cull( CT_FRONT_SIDED );
	GL_ClearStateDelta();
	backEnd.currentSpace = NULL;

	if ( submitted ) {
		backEnd.currentRenderCopied = false;
	}
}

/*
=============================================================================================

CEL OUTLINES

Silhouette shells around cel-shaded model entities. The extrusion program, the
stencil silhouette mask and the geometry plumbing are all shared with the
multiplayer visibility outline above, so the two cannot drift apart visually;
only the policy that picks colour and width differs. World geometry is inked by
RB_STD_CelWorldOutline in screen space instead.

=============================================================================================
*/

static void RB_CelSetOutlineUniforms( const drawSurf_t *surf, const idVec4 &color ) {
	const int viewportWidth = Max( 1, backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1 );
	const int viewportHeight = Max( 1, backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1 );
	const float outlineParams[4] = {
		R_CelOutlineWidthForSurface( surf ),
		2.0f / static_cast<float>( viewportWidth ),
		2.0f / static_cast<float>( viewportHeight ),
		0.0f
	};

	if ( rbPlayerOutlineStage.shaderParmLocations[RB_PLAYER_OUTLINE_UNIFORM_COLOR] >= 0 ) {
		glUniform4fvARB( rbPlayerOutlineStage.shaderParmLocations[RB_PLAYER_OUTLINE_UNIFORM_COLOR], 1, color.ToFloatPtr() );
	}
	if ( rbPlayerOutlineStage.shaderParmLocations[RB_PLAYER_OUTLINE_UNIFORM_PARAMS] >= 0 ) {
		glUniform4fvARB( rbPlayerOutlineStage.shaderParmLocations[RB_PLAYER_OUTLINE_UNIFORM_PARAMS], 1, outlineParams );
	}
}

// Marks the pixels the model itself covers so the shell pass can stay outside
// them and the ink reads as a single clean band.
static bool RB_CelMaskOutlineSurface( const drawSurf_t *surf ) {
	idDrawVert *ac = NULL;
	if ( !RB_PlayerVisibilityPrepareSurface( surf, ac ) ) {
		return false;
	}

	RB_PlayerVisibilitySetSurfaceSetup( surf, surf->space->modelViewMatrix, false );

	GL_State( GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_EQUAL );
	GL_Cull( surf->material->GetCullType() );
	glDisableClientState( GL_NORMAL_ARRAY );

	RB_DrawElementsWithCounters( surf->geo );
	return true;
}

static bool RB_CelDrawOutlineSurface( const drawSurf_t *surf, const bool useGLSL ) {
	if ( !RB_PlayerVisibilityOutlineShellAllowed( surf ) ) {
		return false;
	}

	idVec4 color;
	R_CelOutlineColorForSurface( surf, color );
	if ( color.w <= 0.0f ) {
		return false;
	}

	idDrawVert *ac = NULL;
	if ( !RB_PlayerVisibilityPrepareSurface( surf, ac ) ) {
		return false;
	}

	if ( useGLSL ) {
		// Screen-space normal extrusion holds the ink at the requested pixel
		// width whatever the distance, which is what r_celOutlineWidth promises.
		RB_PlayerVisibilitySetSurfaceSetup( surf, surf->space->modelViewMatrix, false );
		glNormalPointer( GL_FLOAT, sizeof( idDrawVert ), RB_DrawVertAttributePointer( ac, offsetof( idDrawVert, normal ) ) );
		glEnableClientState( GL_NORMAL_ARRAY );
		RB_CelSetOutlineUniforms( surf, color );
	} else {
		float modelViewMatrix[16];
		RB_PlayerVisibilityBuildExpandedModelViewMatrix( surf,
			RB_PlayerVisibilityOutlineScale( surf, R_CelOutlineWidthForSurface( surf ) ), modelViewMatrix );
		RB_PlayerVisibilitySetSurfaceSetup( surf, modelViewMatrix, false );
		GL_SelectTexture( 0 );
		globalImages->whiteImage->Bind();
		glColor4fv( color.ToFloatPtr() );
		glDisableClientState( GL_NORMAL_ARRAY );
	}

	GL_State( GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS );
	// Draw the far side of the shell: its depth sits behind the model, so the
	// interior is rejected by the depth test as well as by the stencil mask and
	// the visible band is exactly the part that pokes past the silhouette.
	GL_Cull( CT_BACK_SIDED );

	RB_DrawElementsWithCounters( surf->geo );
	return true;
}

static bool RB_CelHasOutlineWork( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		if ( R_CelOutlineSurfaceActive( drawSurfs[i] ) ) {
			return true;
		}
	}

	return false;
}

static void RB_STD_DrawCelOutlines( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	if ( !backEnd.viewDef->viewEntitys || drawSurfs == NULL || numDrawSurfs <= 0 || !R_CelOutlineEnabled() ) {
		return;
	}

	// One scan up front, so a view with nothing to outline never touches GL
	// state and the disabled mode really is free.
	if ( !RB_CelHasOutlineWork( drawSurfs, numDrawSurfs ) ) {
		return;
	}

	RB_InitPlayerOutlineStage();
	const bool useGLSLOutline = glConfig.GLSLProgramAvailable && R_ValidateGLSLProgram( &rbPlayerOutlineStage );
	const bool maskSilhouette = glConfig.stencilBits > 0;

	RB_LogComment( "---------- RB_STD_DrawCelOutlines ----------\n" );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( backEnd.viewDef->projectionMatrix );
	glMatrixMode( GL_MODELVIEW );
	GL_SelectTexture( 0 );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );
	glEnable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );

	if ( maskSilhouette ) {
		// Reserve the mask bit over the whole view before any silhouette is
		// marked. glClear honours the scissor and the stencil write mask, so
		// only RB_PLAYER_OUTLINE_STENCIL_BIT inside the view is touched.
		RB_PlayerVisibilityApplyScissor( backEnd.viewDef->scissor );
		glEnable( GL_STENCIL_TEST );
		glStencilMask( RB_PLAYER_OUTLINE_STENCIL_BIT );
		glClearStencil( 0 );
		glClear( GL_STENCIL_BUFFER_BIT );
		glStencilFunc( GL_ALWAYS, RB_PLAYER_OUTLINE_STENCIL_BIT, RB_PLAYER_OUTLINE_STENCIL_BIT );
		glStencilOp( GL_KEEP, GL_KEEP, GL_REPLACE );

		for ( int i = 0; i < numDrawSurfs; i++ ) {
			const drawSurf_t *surf = drawSurfs[i];
			if ( R_CelOutlineSurfaceActive( surf ) ) {
				RB_CelMaskOutlineSurface( surf );
			}
		}

		// Paint the shell only where the silhouette bit is clear, claiming the
		// bit as we go so overlapping models never blend their shells over each
		// other.
		glStencilFunc( GL_NOTEQUAL, RB_PLAYER_OUTLINE_STENCIL_BIT, RB_PLAYER_OUTLINE_STENCIL_BIT );
	}

	bool submitted = false;

	if ( useGLSLOutline ) {
		glUseProgramObjectARB( (GLhandleARB)rbPlayerOutlineStage.glslProgramObject );
	}
	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *surf = drawSurfs[i];
		if ( R_CelOutlineSurfaceActive( surf ) && RB_CelDrawOutlineSurface( surf, useGLSLOutline ) ) {
			submitted = true;
		}
	}
	if ( useGLSLOutline ) {
		glUseProgramObjectARB( 0 );
	}

	if ( maskSilhouette ) {
		glDisable( GL_STENCIL_TEST );
		glStencilMask( 0xff );
		glClearStencil( RB_PlayerVisibilitySafeStencilClearValue() );
	}

	glDisableClientState( GL_NORMAL_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glColor4f( 1.0f, 1.0f, 1.0f, 1.0f );
	GL_SelectTexture( 0 );
	globalImages->BindNull();
	glEnable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 128, 255 );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	GL_Cull( CT_FRONT_SIDED );
	GL_ClearStateDelta();
	backEnd.currentSpace = NULL;

	if ( submitted ) {
		backEnd.currentRenderCopied = false;
	}
}

//=========================================================================================

static bool rbARB2InteractionBypassFrameBreadcrumbsComplete = false;

static bool RB_ARB2InteractionBypassActive( void ) {
	return tr.backEndRenderer == BE_ARB2 && glConfig.disableARB2Interactions;
}

static void RB_RecordARB2InteractionBypassFramePhase( rendererStartupPhase_t phase ) {
	if ( !RB_ARB2InteractionBypassActive() ) {
		rbARB2InteractionBypassFrameBreadcrumbsComplete = false;
		return;
	}
	if ( rbARB2InteractionBypassFrameBreadcrumbsComplete ) {
		return;
	}

	R_RecordRendererStartupPhase( phase );
	if ( phase == RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_FRAME_TAIL ) {
		rbARB2InteractionBypassFrameBreadcrumbsComplete = true;
	}
}

/*
=============
RB_STD_DrawView

=============
*/
void	RB_STD_DrawView( void ) {
	drawSurf_t	 **drawSurfs;
	int			numDrawSurfs;

	RB_LogComment( "---------- RB_STD_DrawView ----------\n" );

	backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;

	drawSurfs = (drawSurf_t **)&backEnd.viewDef->drawSurfs[0];
	numDrawSurfs = backEnd.viewDef->numDrawSurfs;
	rbLightGridInlineSubmittedThisView = false;
	rbLightGridDepthCompareAvailable = false;
	rbLightGridDepthCompareWidth = 0;
	rbLightGridDepthCompareHeight = 0;

	RB_MarkPortalSkyBackdropForSceneTarget( backEnd.viewDef );

	rbSceneScaleState_t sceneScaleState;
	RB_ClearSceneScaleState( sceneScaleState );
	const bool rootSceneRenderTargetRequested = RB_SceneRenderTargetRequested();
	const bool inlineSubviewSceneRenderTargetRequested = RB_InlineSubviewSceneRenderTargetRequested();
	const viewDef_t *portalSkySceneTargetView = RB_PortalSkySceneTargetView( backEnd.viewDef );
	if ( portalSkySceneTargetView != NULL ) {
		RB_MarkSceneRenderTargetPreserveFarDepth( portalSkySceneTargetView );
	}
	const viewDef_t *sceneTargetView = inlineSubviewSceneRenderTargetRequested
		? backEnd.viewDef->superView
		: backEnd.viewDef;
	if ( ( rootSceneRenderTargetRequested || inlineSubviewSceneRenderTargetRequested )
		&& RB_EnsureSceneRenderTexture( sceneTargetView ) ) {
		backEnd.renderTexture = rbSceneRenderTexture;
		RB_BeginSceneSupersampling( sceneScaleState, sceneTargetView );
	}

	// If we have a backend rendertexture, assign it here.
	if (backEnd.renderTexture)
	{
		backEnd.renderTexture->MakeCurrent();
	}

	RB_DisplaySpecialEffects( backEnd.viewDef->viewEntitys, true );

	// clear the z buffer, set the projection matrix, etc
	RB_BeginDrawingView();
	RB_CaptureSSAOWorldDepthImage( drawSurfs, numDrawSurfs );
	RB_CaptureCelWorldDepthImage( drawSurfs, numDrawSurfs );

	// decide how much overbrighting we are going to do
	RB_DetermineLightScale();

	// fill the depth buffer and clear color buffer to black except on
	// subviews
	if ( R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_DEPTH, backEnd.viewDef ) ) {
		R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_DEPTH );
	} else {
		RB_STD_FillDepthBuffer( drawSurfs, numDrawSurfs );
	}
	RB_PrepareLightGridDepthTexture();
	RB_DisplaySpecialEffects( backEnd.viewDef->viewEntitys, false );

	// main light renderer
	if ( R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_ARB2_INTERACTION, backEnd.viewDef ) ) {
		R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_ARB2_INTERACTION );
		if ( R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_SHADOW_MAP, backEnd.viewDef ) ) {
			R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_SHADOW_MAP );
		}
		if ( R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_STENCIL_SHADOW, backEnd.viewDef ) ) {
			R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_STENCIL_SHADOW );
		}
	} else {
		RB_ARB2_DrawInteractions();
	}

	// one-shot once a view has actually lit something, so a reporter log says
	// how many surfaces took the Apple GL 2.1 GLSL corridor instead of only
	// which fallback pair was armed
	RB_ReportAppleGL21RouteCounters();

	if ( RB_ARB2InteractionBypassActive() ) {
		RB_RecordARB2InteractionBypassFramePhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_LIGHT_SCALE_SKIPPED );
	} else {
		RB_RecordARB2InteractionBypassFramePhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_LIGHT_SCALE );

		// disable stencil shadow test
		glStencilFunc( GL_ALWAYS, 128, 255 );

		// uplight the entire screen to crutch up not having better blending range
		RB_STD_LightScale();
	}

	if ( r_portalsDistanceCull.GetBool() && backEnd.viewDef->viewEntitys && backEnd.viewDef->renderWorld != NULL ) {
		backEnd.viewDef->renderWorld->RenderPortalFades();
	}

	// Draw the non-light dependent base shading that retail Q4 submits before
	// fog: opaque, decal, and far-sort surfaces. Medium/close translucent
	// surfaces are held until after fog so additive BSE layers are not painted
	// over by fog volumes that only owned the opaque scene depth.
	const int processed = RB_STD_FindPostProcessStart( drawSurfs, numDrawSurfs );
	const bool ambientLegacySkipped = R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_AMBIENT, backEnd.viewDef );
	const bool preFogFeedback = ambientLegacySkipped && RB_HasLegacyFeedbackDrawSurfs( drawSurfs, processed, RB_DrawSurfIsPreFogMaterialPass );
	const bool postFogFeedback = ambientLegacySkipped && RB_HasLegacyFeedbackDrawSurfs( drawSurfs, processed, RB_DrawSurfIsPostFogMaterialPass );
	if ( ambientLegacySkipped ) {
		if ( preFogFeedback || postFogFeedback ) {
			R_ModernGLExecutor_ComposeVisibleSceneForPost();
			backEnd.currentRenderCopied = false;
			backEnd.currentDepthCopied = false;
			if ( preFogFeedback ) {
				RB_STD_DrawShaderPasses( drawSurfs, processed, RB_DrawSurfNeedsPreFogLegacyFeedback );
			}
		}
		R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_AMBIENT );
	} else if ( processed > 0 ) {
		// Keep authored post-process surfaces out of the ambient/material pass.
		// Some pre-post materials legitimately populate _currentRender; if the
		// full list is submitted here, SS_POST_PROCESS surfaces can consume that
		// stale pre-light-grid copy before the indirect overlay has been added.
		RB_STD_DrawShaderPasses( drawSurfs, processed, RB_DrawSurfIsPreFogMaterialPass );
	}

	// Modern visible color/depth must be handed back before legacy overlay
	// passes, otherwise the later composition overwrites those contributions.
	R_ModernGLExecutor_ComposeVisibleSceneForPost();

	// Add precomputed indirect diffuse from irradiance-volume atlases after
	// ambient/material shading. This matches the render graph and keeps the
	// baked contribution visible instead of letting later material passes bury
	// it.
	if ( R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_LIGHT_GRID, backEnd.viewDef ) ) {
		R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_LIGHT_GRID );
	} else {
		RB_STD_LightGridIndirect();
	}

	R_ModernGLExecutor_SubmitForwardPlusDecalOverlay( backEnd.viewDef );

	RB_RecordARB2InteractionBypassFramePhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_AMBIENT_RESCUE );

	// Apply a configurable brightness floor after ambient/material passes.
	RB_STD_ForceAmbient();

	// Player readability overlays go on top of the finished shading. The light
	// grid overlay and the ambient floor both lift the whole frame, so running
	// this earlier washed the outline out by exactly the amount the player had
	// turned their brightness up.
	RB_STD_DrawPlayerVisibilityEffects( drawSurfs, processed );

	// Ink the cel silhouettes after every lighting contribution, so the ambient
	// floor and the indirect overlay cannot wash the outline back out.
	RB_STD_DrawCelOutlines( drawSurfs, processed );

	RB_RecordARB2InteractionBypassFramePhase( RENDERER_STARTUP_PHASE_ARB2_INTERACTION_BYPASS_FRAME_TAIL );

	// fob and blend lights
	if ( R_ModernGLExecutor_LegacyPassCanSkipForView( RENDER_PASS_FOG_BLEND, backEnd.viewDef ) ) {
		R_ModernGLExecutor_RecordLegacyPassSkipped( RENDER_PASS_FOG_BLEND );
	} else {
		RB_STD_FogAllLights();
		backEnd.currentRenderCopied = false;
		backEnd.currentDepthCopied = false;
	}

	if ( ambientLegacySkipped ) {
		if ( postFogFeedback ) {
			backEnd.currentRenderCopied = false;
			backEnd.currentDepthCopied = false;
			RB_STD_DrawShaderPasses( drawSurfs, processed, RB_DrawSurfNeedsPostFogLegacyFeedback );
		}
	} else if ( processed > 0 ) {
		RB_STD_DrawShaderPasses( drawSurfs, processed, RB_DrawSurfIsPostFogMaterialPass );
	}

	RB_CaptureSceneRenderTargetPreserveDepthImage();

	// Apply SSAO before bloom and tonemapping so indirect shadowing modulates the lit scene.
	RB_STD_SSAO();

	// Apply camera motion blur before bloom.
	RB_STD_MotionBlur();

	// Apply scene bloom before authored post-process overlays that sample _currentRender.
	RB_STD_Bloom();

	// World cel edges land after bloom so the ink stays crisp instead of being
	// smeared by the blur of whatever it borders.
	RB_STD_CelWorldOutline();

	// now draw any post-processing effects using _currentRender
	if ( processed < numDrawSurfs ) {
		RB_STD_DrawShaderPasses( drawSurfs+processed, numDrawSurfs-processed );
	}

	// Isolate and blur authored glowStage passes while the complete scene is
	// still in the active scene target; presentation happens below.
	RB_STD_GlowOverlay();

	RB_RenderDebugTools( drawSurfs, numDrawSurfs );

	if ( rootSceneRenderTargetRequested && RB_IsSceneRenderTexture( backEnd.renderTexture ) ) {
		RB_PresentSceneRenderTargetToBackBuffer( sceneScaleState );
	}
	RB_RestoreSceneSupersampling( sceneScaleState );

	if ( inlineSubviewSceneRenderTargetRequested
		&& RB_IsSceneRenderTexture( backEnd.renderTexture ) ) {
		backEnd.renderTexture = NULL;
		idRenderTexture::BindNull();
	}

// jmarshall - stupid OpenGL
	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO);
// jmarshall end
}
