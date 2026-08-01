// Copyright (C) 2026 DarkMatter Productions
//

/*
===============================================================================

	Vulkan interaction pass (Phase F1,
	docs/dev/plans/2026-07-19-vulkan-phase-f.md) + stencil shadow volumes
	(Phase G1, docs/dev/plans/2026-07-20-vulkan-phase-g.md) + fog and
	blend lights (Phase G2, same plan doc).

	Per-light bump/diffuse/specular interactions for every view light,
	drawn between the depth fill and the ambient walks. Lights the
	shadow-map path (Phase F2) admits keep the receiver pipelines; every
	OTHER light that carries shadow surfs stamps stencil volumes and draws
	its interactions under the GEQUAL/128 exit contract — with the retail
	default r_useShadowMap 0 that is EVERY shadow-casting light.

	GL-free ports from TUs excluded from the vk module build:
	- RB_ARB2_DrawInteractions light loop (draw_arb2.cpp:11458-11666):
	  skip fog/blend/empty lights; opaque interactions (local then global)
	  additive at depth EQUAL with writes off; translucent at depth LESS.
	- The stencil-path per-light block (draw_arb2.cpp:11599-11649):
	  scissored stencil clear to 128, then globalShadows →
	  localInteractions → localShadows → globalInteractions (stencil
	  ownership: noSelfShadow receivers are lit before the local volumes
	  join), translucent GEQUAL under r_stencilTranslucentShadows.
	- RB_StencilShadowPass + RB_T_Shadow (draw_common.cpp:7194-7444):
	  two-sided single-pass wrap-op sequences, the per-surface
	  index-count/external selection ladder, the GEQUAL/128/KEEP exit.
	- RB_CreateSingleDrawInteractionsFiltered decomposition walk +
	  R_SetDrawInteraction + RB_SubmittInteraction (tr_render.cpp:782-1033):
	  light-stage × surface-stage pairing into drawInteraction_t, with the
	  r_skipBump/Diffuse/Specular substitutions and the both-black skip.
	- RB_DetermineLightScale (tr_render.cpp:675-726).
	- RB_BakeTextureMatrixIntoTexgen (draw_common.cpp:4365-4400).
	- RB_STD_FogAllLights + RB_FogPass + RB_T_BasicFog (draw_common.cpp:
	  7593-7816): fog and blend lights draw AFTER the pre-fog ambient walk
	  (RB_STD_DrawView:9806), stencil disabled for the whole pass; fog =
	  eye-depth/fog-plane texgen over the light's interaction chains at
	  depth EQUAL plus the frustumTris cap at LEQUAL back-sided.
	- RB_BlendLight + RB_T_BlendLight (draw_common.cpp:7462-7582): the
	  light-shader stage walk projecting lightProject S/T/Q + falloff,
	  per-stage blend bits and RGBA color.

	Per-draw data rides the shared 128B push block (MVP + vertex-color
	packing + ambient direction; the volume pipeline reuses it for MVP +
	local light origin); everything else streams through the executor's
	dynamic uniform ring as a std140 block on set 6. The six texture slots
	bind cached per-image single-sampler sets (0=specular table, 1=bump,
	2=falloff, 3=light projection, 4=diffuse, 5=specular).

===============================================================================
*/

#ifdef OPENQ4_RENDERER_VK_MODULE

#include "../../idlib/precompiled.h"
#pragma hdrstop

#include "../tr_local.h"
#include "../Model_local.h"

#undef snprintf
#undef vsnprintf
#ifndef INT_MAX
#define INT_MAX		2147483647
#endif
#ifndef INT_MIN
#define INT_MIN		( -2147483647 - 1 )
#endif
#ifndef UINT_MAX
#define UINT_MAX	0xffffffffu
#endif
#include <cstdio>
#include "volk.h"

#include "VulkanDevice.h"
#include "vk_ShadowMap.h"

// vk_GuiExecutor.cpp narrow accessors (vkExec stays file-static there)
VkCommandBuffer VK_Exec_ActiveCmd( void );
int VK_Exec_ActiveFrameSlot( void );
bool VK_Exec_BindTriGeometry( VkCommandBuffer cmd, int slot, const srfTriangles_t *tri );
bool VK_Exec_BindShadowGeometry( VkCommandBuffer cmd, int slot, const srfTriangles_t *tri );
bool VK_Exec_BindRawShadowGeometry( VkCommandBuffer cmd, int slot,
		const shadowCache_t *verts, int numVerts,
		const glIndex_t *indexes, int numIndexes );
void VK_Exec_SetSurfScissor( VkCommandBuffer cmd, const viewDef_t *viewDef, const drawSurf_t *drawSurf, int fbHeight );
void VK_BuildSurfMVP( const viewDef_t *viewDef, const drawSurf_t *drawSurf, float outMvp[ 16 ] );
VkPipeline VK_Exec_InteractionPipeline( void );
VkPipelineLayout VK_Exec_InteractionPipelineLayout( void );
VkPipeline VK_Exec_ShadowInteractionPipeline( void );
VkPipeline VK_Exec_PointShadowInteractionPipeline( void );
VkPipelineLayout VK_Exec_ShadowInteractionPipelineLayout( void );
VkPipeline VK_Exec_StencilShadowPipeline( void );
VkPipelineLayout VK_Exec_BasePipelineLayout( void );
VkPipeline VK_Exec_FogPipeline( void );
VkPipeline VK_Exec_BlendLightPipeline( int stateBits );
VkPipelineLayout VK_Exec_FogBlendPipelineLayout( void );
VkDescriptorSet VK_Exec_ShadowDescriptorSet( void );
VkDescriptorSet VK_Exec_ImageDescriptor( unsigned int texnum, bool require2D );
VkDescriptorSet VK_Exec_InteractionUniformSet( void );
int VK_Exec_InteractionUniformAlloc( const void *data, int bytes );
int VK_Exec_ShadowUniformAlloc( const void *data, int bytes );
int VK_Exec_ActiveFramebufferWidth( void );
int VK_Exec_ActiveFramebufferHeight( void );
bool VK_Exec_ActiveTargetHasStencil( void );

/*
====================
Per-draw GPU blocks
====================
*/

// mirror of the shared 128B push block ({mat4; vec4 a,b,c,d}):
// a = (vertexColorModulate, vertexColorAdd, ambientLight, unused),
// b = tangent-space ambient light direction (cube-quantized), c/d unused
typedef struct vkInteractionPush_s {
	float			mvp[ 16 ];
	float			a[ 4 ];
	float			b[ 4 ];
	float			c[ 4 ];
	float			d[ 4 ];
} vkInteractionPush_t;

// std140 mirror of the set-6 InteractionBlock (15 vec4 = 240 bytes,
// inside the 256B ring slice)
typedef struct vkInteractionBlock_s {
	float			localLightOrigin[ 4 ];
	float			localViewOrigin[ 4 ];
	float			lightProjectionS[ 4 ];
	float			lightProjectionT[ 4 ];
	float			lightProjectionQ[ 4 ];
	float			lightFalloffS[ 4 ];
	float			bumpMatrixS[ 4 ];
	float			bumpMatrixT[ 4 ];
	float			diffuseMatrixS[ 4 ];
	float			diffuseMatrixT[ 4 ];
	float			specularMatrixS[ 4 ];
	float			specularMatrixT[ 4 ];
	float			diffuseColor[ 4 ];
	float			specularColor[ 4 ];
	float			flatDiffuseParams[ 4 ];
} vkInteractionBlock_t;

// std140 mirror of the projected set-7 ShadowBlock (29 vec4 = 464 bytes in
// its own 512B ring slice). Every cascade row is localized per receiver
// space; the atlas rects already include the ownership block origin and the
// Vulkan inverted-v tile convention.
typedef struct vkShadowBlock_s {
	float			shadowRow0[ SHADOWMAP_PROJECTED_MAX_CASCADES ][ 4 ];
	float			shadowRow1[ SHADOWMAP_PROJECTED_MAX_CASCADES ][ 4 ];
	float			shadowRow2[ SHADOWMAP_PROJECTED_MAX_CASCADES ][ 4 ];
	float			shadowRow3[ SHADOWMAP_PROJECTED_MAX_CASCADES ][ 4 ];
	float			atlasRects[ SHADOWMAP_PROJECTED_MAX_CASCADES ][ 4 ];
	float			splitDepths[ 4 ];
	float			cascadeBiasScale[ 4 ];
	float			texelDepthBias[ 4 ];
	float			normalOffsetWorld[ 4 ];
	float			viewDepthRow[ 4 ];	// -modelView row 2
	float			biasParams[ 4 ];	// x: constant, y: normal, z: cascade blend, w: cascade count
	float			texelSize[ 4 ];		// x,y: 1 / atlas dimensions
	float			filterParams[ 4 ];	// x: radius, y: taps, z: mode, w: hardware compare
	float			pcssParams[ 4 ];	// x: light radius, y: max radius, z: effective radius, w: receiver-plane bias
} vkShadowBlock_t;

// std140 mirror of the point variant's set-7 ShadowBlock (Phase F2b,
// 7 vec4 = 112 bytes; rewritten per space — the rows are the model matrix)
typedef struct vkPointShadowBlock_s {
	float			modelRow0[ 4 ];		// model -> world matrix rows
	float			modelRow1[ 4 ];
	float			modelRow2[ 4 ];
	float			lightOriginFar[ 4 ];	// xyz: world light origin, w: far envelope
	float			biasParams[ 4 ];	// x: constant bias, y: normal bias, z: texel depth bias, w: per-distance normal-offset factor
	float			filterParams[ 4 ];	// x: radius, y: taps, z: mode, w: cube texel scale
	float			samplingParams[ 4 ];	// x: hardware compare enabled
} vkPointShadowBlock_t;

static_assert( sizeof( vkShadowBlock_t ) == 464,
		"projected shadow std140 block must remain 29 vec4s" );
static_assert( sizeof( vkPointShadowBlock_t ) == 112,
		"point shadow std140 block must remain 7 vec4s" );
static_assert( sizeof( vkInteractionBlock_t ) == 240,
		"interaction std140 block must remain 15 vec4s" );
static_assert( sizeof( vkShadowBlock_t ) <= 512 &&
		sizeof( vkPointShadowBlock_t ) <= 512,
		"set-7 shadow blocks must fit the fixed 512-byte descriptor range" );

// std140 mirror of the fog/blend set-2 BlendLightBlock (5 vec4 = 80 bytes,
// inside the 256B ring slice; rewritten per space — the planes are
// model-local — and per light stage for the color)
typedef struct vkBlendLightBlock_s {
	float			lightProjectS[ 4 ];
	float			lightProjectT[ 4 ];
	float			lightProjectQ[ 4 ];
	float			lightFalloffS[ 4 ];
	float			color[ 4 ];
} vkBlendLightBlock_t;

// per-view pass state (space/depth-range tracking mirrors the Draw3DView
// walks; reset per VK_Interactions_DrawLights / VK_Fog_DrawAllLights call)
typedef struct vkInterPass_s {
	const viewDef_t *	viewDef;
	VkCommandBuffer		cmd;
	int					slot;
	int					fbWidth;
	int					fbHeight;
	VkViewport			viewport;
	VkPipelineLayout	layout;
	VkDescriptorSet		specTableSet;
	const viewEntity_t *currentSpace;
	bool				weaponDepthRange;
	float				mvp[ 16 ];
	float				ambientDir[ 3 ];
	int					lightCount;
	int					drawCount;

	// Phase F2a/F2b shadow-map receivers
	bool				shadowPassPrepared;	// shadow maps rendered for this view
	VkPipeline			pipelineUnshadowed;
	VkPipeline			pipelineShadowed;		// projected receiver (atlas)
	VkPipeline			pipelinePointShadowed;	// point receiver (cube)
	VkPipelineLayout	layoutShadowed;
	VkDescriptorSet		shadowSetAtlas;		// the executor's atlas set (projected lights)
	VkDescriptorSet		shadowSet;			// active light's set-7 set (atlas or cube)
	int					shadowMode;			// 0 = unshadowed, 1 = projected, 2 = point
	bool				shadowActive;		// a shadow pipeline is bound
	const vkShadowLightState_t *shadowState;	// current light's shadow state (shadowActive only)
	const vkShadowPassState_t *shadowPassState;	// active LOCAL/GLOBAL resource state
	int					shadowSliceOffset;	// ring offset of the current space's shadow block, -1 = unset
	int					shadowLightCount;
	int					shadowDrawCount;
	int					elidedLightCount;	// diagnostic count; conservative VK policy expects zero

	// Phase G1 stencil shadow volumes
	VkPipeline			pipelineStencilShadow;	// vec4 volume stream, color writes off
	VkPipelineLayout	layoutStencilShadow;	// the base 128B-push layout
	int					stencilLightCount;		// lights that took the stencil path
	int					volumeDrawCount;		// volume draws (preload + z-pass)
	int					volumePreloadCount;		// z-fail preload draws (internal volumes)
	int					volumeSkipCount;		// prim-batch / cache-less shadow surfs skipped

	// Phase G2 fog/blend lights (a separate pass invocation between the
	// two ambient walks; reuses the space/viewport tracking above)
	VkPipeline			pipelineFog;
	VkPipelineLayout	layoutFogBlend;
	const float *		blendTextureMatrix;	// current blend stage's texture matrix, NULL = none
	int					blendSliceOffset;	// ring offset of the current space's blend block, -1 = unset
	float				fogColor[ 4 ];		// current fog light's stage-0 color (alpha pinned 1)
	float				blendColor[ 4 ];	// current blend stage's RGBA color
	int					fogLightCount;
	int					blendLightCount;
	int					fogDrawCount;
	int					blendDrawCount;
	int					fogSkipCount;		// prim-batch / cache-less fog+blend surfs skipped
} vkInterPass_t;

static vkInterPass_t interPass;

/*
====================
VK_ResolveCustomLightingTexture

Resolve the named texture ABI used by the two shipped custom-lighting
guides without involving an OpenGL shader object.  The authored material
stage owns the surface images; the light falloff/projection and stock
lookup images remain drawInteraction_t/global semantics exactly like the
GL custom-stage path.
====================
*/
static idImage *VK_ResolveCustomLightingTexture( const newShaderStage_t *newStage,
												 const char *name,
												 const drawInteraction_t *din ) {
	if ( newStage == NULL || name == NULL ) {
		return NULL;
	}

	for ( int slot = 0 ; slot < newStage->numShaderTextures ; slot++ ) {
		if ( idStr::Icmp( newStage->shaderTextureNames[ slot ], name ) != 0 ) {
			continue;
		}

		switch ( newStage->shaderTextureBindings[ slot ] ) {
			case GLSL_SHADERTEXTURE_LIGHT_FALLOFF:
				return din != NULL && din->lightFalloffImage != NULL
					? din->lightFalloffImage : globalImages->whiteImage;
			case GLSL_SHADERTEXTURE_LIGHT_IMAGE:
				return din != NULL && din->lightImage != NULL
					? din->lightImage : globalImages->whiteImage;
			case GLSL_SHADERTEXTURE_AMBIENT_NORMAL_MAP:
				return globalImages->ambientNormalMap != NULL
					? globalImages->ambientNormalMap : globalImages->defaultImage;
			case GLSL_SHADERTEXTURE_NORMAL_CUBE_MAP:
				return globalImages->normalCubeMapImage != NULL
					? globalImages->normalCubeMapImage : globalImages->defaultImage;
			case GLSL_SHADERTEXTURE_SPECULAR_TABLE:
				return globalImages->specularTableImage != NULL
					? globalImages->specularTableImage : globalImages->defaultImage;
			case GLSL_SHADERTEXTURE_IMAGE:
			default:
				return newStage->shaderTextureImages[ slot ];
		}
	}

	return NULL;
}

/*
====================
VK_CustomLightingScaleBias

Read the authored vScaleBias register pair used by Parallaxbump.glsl.
Unknown/missing components remain zero, which is the neutral offset.
====================
*/
static void VK_CustomLightingScaleBias( const newShaderStage_t *newStage,
										const float *surfaceRegs,
										float scaleBias[ 2 ] ) {
	scaleBias[ 0 ] = 0.0f;
	scaleBias[ 1 ] = 0.0f;
	if ( newStage == NULL || surfaceRegs == NULL ) {
		return;
	}

	for ( int parm = 0 ; parm < newStage->numShaderParms ; parm++ ) {
		if ( idStr::Icmp( newStage->shaderParmNames[ parm ], "vScaleBias" ) != 0
				|| newStage->shaderParmBindings[ parm ] != GLSL_SHADERPARM_REGISTERS ) {
			continue;
		}

		const int count = Min( 2, newStage->shaderParmNumRegisters[ parm ] );
		for ( int component = 0 ; component < count ; component++ ) {
			scaleBias[ component ] =
				surfaceRegs[ newStage->shaderParmRegisters[ parm ][ component ] ];
		}
		return;
	}
}

/*
====================
VK_ReportCustomLightingFamily

One-shot diagnostics establish that an active shipped guide reached its
native Vulkan interaction implementation without producing per-draw spam.
====================
*/
static void VK_ReportCustomLightingFamily( vkGLSLProgramFamily_t family ) {
	static bool reportedCustomLit = false;
	static bool reportedParallax = false;

	if ( family == VK_GLSL_PROGRAM_FAMILY_CUSTOM_LIT && !reportedCustomLit ) {
		reportedCustomLit = true;
		common->Printf( "Vulkan: native Customlit per-light interactions active\n" );
	} else if ( family == VK_GLSL_PROGRAM_FAMILY_PARALLAX_BUMP && !reportedParallax ) {
		reportedParallax = true;
		common->Printf( "Vulkan: native Parallaxbump per-light interactions active\n" );
	}
}

/*
====================
RB_BakeTextureMatrixIntoTexgen

Port of the excluded draw_common.cpp implementation: folds a light-stage
texture matrix into the S/T texgen planes (Q passes through the multiply
untouched). The GL version reads backEnd.lightTextureMatrix directly; this
one honors the parameter (every caller passes that global).
====================
*/
void RB_BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float textureMatrix[16] ) {
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

	myGlMultMatrix( genMatrix, textureMatrix, final );

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
====================
VK_DetermineLightScale

Port of RB_DetermineLightScale (tr_render.cpp:675). The vk module always
runs the ARB2-shaped front-end path (backEndRendererMaxLight = 999), so
lightScale normally stays r_lightScale and overBright 1.0; the GL-only
disableARB2Interactions driver-quirk branch has no vk analog.
====================
*/
static void VK_DetermineLightScale( void ) {
	viewLight_t			*vLight;
	const idMaterial	*shader;
	float				max;
	int					i, j, numStages;
	const shaderStage_t	*stage;

	// the light scale will be based on the largest color component of any
	// surface that will be drawn; if there are no lights, this stays 1.0
	max = 1.0;

	for ( vLight = backEnd.viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		// lights with no surfaces or shaderparms may still be present
		// for debug display
		if ( !vLight->localInteractions && !vLight->globalInteractions
			&& !vLight->translucentInteractions ) {
			continue;
		}

		shader = vLight->lightShader;
		numStages = shader->GetNumStages();
		for ( i = 0 ; i < numStages ; i++ ) {
			stage = shader->GetStage( i );
			for ( j = 0 ; j < 3 ; j++ ) {
				float	v = r_lightScale.GetFloat() * vLight->shaderRegisters[ stage->color.registers[j] ];
				if ( v > max ) {
					max = v;
				}
			}
		}
	}

	backEnd.pc.maxLightValue = max;
	if ( max <= tr.backEndRendererMaxLight ) {
		backEnd.lightScale = r_lightScale.GetFloat();
		backEnd.overBright = 1.0;
	} else {
		backEnd.lightScale = r_lightScale.GetFloat() * tr.backEndRendererMaxLight / max;
		backEnd.overBright = max / tr.backEndRendererMaxLight;
	}
}

/*
====================
VK_SetDrawInteraction

Port of R_SetDrawInteraction (tr_render.cpp:782): stage texture matrix as
two S/T rows (scroll offsets wrapped at ±40), stage color clamped [0,1].
====================
*/
static void VK_SetDrawInteraction( const shaderStage_t *surfaceStage, const float *surfaceRegs,
								   idImage **image, idVec4 matrix[2], float color[4] ) {
	*image = surfaceStage->texture.image;
	if ( surfaceStage->texture.hasMatrix ) {
		matrix[0][0] = surfaceRegs[surfaceStage->texture.matrix[0][0]];
		matrix[0][1] = surfaceRegs[surfaceStage->texture.matrix[0][1]];
		matrix[0][2] = 0;
		matrix[0][3] = surfaceRegs[surfaceStage->texture.matrix[0][2]];

		matrix[1][0] = surfaceRegs[surfaceStage->texture.matrix[1][0]];
		matrix[1][1] = surfaceRegs[surfaceStage->texture.matrix[1][1]];
		matrix[1][2] = 0;
		matrix[1][3] = surfaceRegs[surfaceStage->texture.matrix[1][2]];

		// we attempt to keep scrolls from generating incredibly large
		// texture values, but center rotations and center scales can still
		// generate offsets that need to be > 1
		if ( matrix[0][3] < -40 || matrix[0][3] > 40 ) {
			matrix[0][3] -= (int)matrix[0][3];
		}
		if ( matrix[1][3] < -40 || matrix[1][3] > 40 ) {
			matrix[1][3] -= (int)matrix[1][3];
		}
	} else {
		matrix[0][0] = 1;
		matrix[0][1] = 0;
		matrix[0][2] = 0;
		matrix[0][3] = 0;

		matrix[1][0] = 0;
		matrix[1][1] = 1;
		matrix[1][2] = 0;
		matrix[1][3] = 0;
	}

	if ( color ) {
		for ( int i = 0 ; i < 4 ; i++ ) {
			color[i] = surfaceRegs[surfaceStage->color.registers[i]];
			// clamp here, so cards with greater range don't look different
			if ( color[i] < 0 ) {
				color[i] = 0;
			} else if ( color[i] > 1.0 ) {
				color[i] = 1.0;
			}
		}
	}
}

/*
====================
VK_DrawSingleInteraction

The Vulkan analog of RB_ARB2_DrawInteraction: streams the interaction
block into the uniform ring, binds the six cached image sets + the ring
set, pushes the 128B block, and draws the bound light-tris geometry.
====================
*/
static void VK_DrawSingleInteractionMode( const drawInteraction_t *din,
										  bool parallax,
										  float parallaxScale,
										  float parallaxBias ) {
	if ( din->bumpImage == NULL || din->lightFalloffImage == NULL || din->lightImage == NULL
			|| din->diffuseImage == NULL || din->specularImage == NULL ) {
		return;
	}

	// shadowed lights bind set 7 (atlas + shadow block) and pass a second
	// dynamic offset; a missing per-space shadow slice (ring overflow) skips
	// the draw exactly like the interaction-slice failure below
	const bool shadowDraw = interPass.shadowActive;
	if ( shadowDraw && interPass.shadowSliceOffset < 0 ) {
		return;
	}
	const int setCount = shadowDraw ? 8 : 7;
	const VkPipelineLayout layout = shadowDraw ? interPass.layoutShadowed : interPass.layout;

	VkDescriptorSet sets[ 8 ];
	sets[ 0 ] = interPass.specTableSet;
	sets[ 1 ] = VK_Exec_ImageDescriptor( din->bumpImage->GetDeviceHandle(), true );
	sets[ 2 ] = VK_Exec_ImageDescriptor( din->lightFalloffImage->GetDeviceHandle(), true );
	sets[ 3 ] = VK_Exec_ImageDescriptor( din->lightImage->GetDeviceHandle(), true );
	sets[ 4 ] = VK_Exec_ImageDescriptor( din->diffuseImage->GetDeviceHandle(), true );
	sets[ 5 ] = VK_Exec_ImageDescriptor( din->specularImage->GetDeviceHandle(), true );
	sets[ 6 ] = VK_Exec_InteractionUniformSet();
	sets[ 7 ] = interPass.shadowSet;
	for ( int i = 0 ; i < setCount ; i++ ) {
		if ( sets[ i ] == VK_NULL_HANDLE ) {
			return;
		}
	}

	vkInteractionBlock_t block;
	memcpy( block.localLightOrigin, din->localLightOrigin.ToFloatPtr(), sizeof( block.localLightOrigin ) );
	memcpy( block.localViewOrigin, din->localViewOrigin.ToFloatPtr(), sizeof( block.localViewOrigin ) );
	memcpy( block.lightProjectionS, din->lightProjection[0].ToFloatPtr(), sizeof( block.lightProjectionS ) );
	memcpy( block.lightProjectionT, din->lightProjection[1].ToFloatPtr(), sizeof( block.lightProjectionT ) );
	memcpy( block.lightProjectionQ, din->lightProjection[2].ToFloatPtr(), sizeof( block.lightProjectionQ ) );
	memcpy( block.lightFalloffS, din->lightProjection[3].ToFloatPtr(), sizeof( block.lightFalloffS ) );
	memcpy( block.bumpMatrixS, din->bumpMatrix[0].ToFloatPtr(), sizeof( block.bumpMatrixS ) );
	memcpy( block.bumpMatrixT, din->bumpMatrix[1].ToFloatPtr(), sizeof( block.bumpMatrixT ) );
	memcpy( block.diffuseMatrixS, din->diffuseMatrix[0].ToFloatPtr(), sizeof( block.diffuseMatrixS ) );
	memcpy( block.diffuseMatrixT, din->diffuseMatrix[1].ToFloatPtr(), sizeof( block.diffuseMatrixT ) );
	memcpy( block.specularMatrixS, din->specularMatrix[0].ToFloatPtr(), sizeof( block.specularMatrixS ) );
	memcpy( block.specularMatrixT, din->specularMatrix[1].ToFloatPtr(), sizeof( block.specularMatrixT ) );
	// the shader doubles the specular term (ARB2 doubles the env constant)
	memcpy( block.diffuseColor, din->diffuseColor.ToFloatPtr(), sizeof( block.diffuseColor ) );
	memcpy( block.specularColor, din->specularColor.ToFloatPtr(), sizeof( block.specularColor ) );
	memcpy( block.flatDiffuseParams, din->flatDiffuseParams.ToFloatPtr(), sizeof( block.flatDiffuseParams ) );

	const int uboOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );
	if ( uboOffset < 0 ) {
		return;
	}

	// SVC packing exactly like the stock interaction.vfp (ICM_PACKED)
	vkInteractionPush_t push;
	memset( &push, 0, sizeof( push ) );
	memcpy( push.mvp, interPass.mvp, sizeof( push.mvp ) );
	switch ( din->vertexColor ) {
		case SVC_MODULATE:
			push.a[ 0 ] = 1.0f;
			push.a[ 1 ] = 0.0f;
			break;
		case SVC_INVERSE_MODULATE:
			push.a[ 0 ] = -1.0f;
			push.a[ 1 ] = 1.0f;
			break;
		default:	// SVC_IGNORE
			push.a[ 0 ] = 0.0f;
			push.a[ 1 ] = 1.0f;
			break;
	}
	push.a[ 2 ] = din->ambientLight ? 1.0f : 0.0f;
	push.b[ 0 ] = interPass.ambientDir[ 0 ];
	push.b[ 1 ] = interPass.ambientDir[ 1 ];
	push.b[ 2 ] = interPass.ambientDir[ 2 ];
	push.c[ 0 ] = parallaxScale;
	push.c[ 1 ] = parallaxBias;
	push.c[ 2 ] = parallax ? 1.0f : 0.0f;

	// dynamic offsets consume in set order: set 6 interaction slice, then
	// (shadowed only) set 7 binding 1 shadow slice
	uint32_t dynamicOffsets[ 2 ];
	dynamicOffsets[ 0 ] = (uint32_t)uboOffset;
	dynamicOffsets[ 1 ] = (uint32_t)interPass.shadowSliceOffset;
	vkCmdBindDescriptorSets( interPass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
			0, (uint32_t)setCount, sets, shadowDraw ? 2 : 1, dynamicOffsets );
	vkCmdPushConstants( interPass.cmd, layout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
	vkCmdDrawIndexed( interPass.cmd, (uint32_t)din->surf->geo->numIndexes, 1, 0, 0, 0 );
	interPass.drawCount++;
	if ( shadowDraw ) {
		interPass.shadowDrawCount++;
	}
}

static void VK_DrawSingleInteraction( const drawInteraction_t *din ) {
	VK_DrawSingleInteractionMode( din, false, 0.0f, 0.0f );
}

/*
====================
VK_SubmitInteraction

Port of RB_SubmittInteraction (tr_render.cpp:836): blackImage defaults for
missing diffuse/specular (and the r_skip* debug substitutions), flat
normal map for skipped bump, and the skip-if-nothing-would-draw rule.
====================
*/
static void VK_SubmitInteraction( drawInteraction_t *din ) {
	if ( !din->bumpImage ) {
		return;
	}

	if ( !din->diffuseImage || r_skipDiffuse.GetBool() ) {
		din->diffuseImage = globalImages->blackImage;
	}
	if ( !din->specularImage || r_skipSpecular.GetBool() || din->ambientLight ) {
		din->specularImage = globalImages->blackImage;
	}
	if ( !din->bumpImage || r_skipBump.GetBool() ) {
		din->bumpImage = globalImages->flatNormalMap;
	}

	// if we wouldn't draw anything, don't call the Draw function
	if (
		( ( din->diffuseColor[0] > 0 ||
		din->diffuseColor[1] > 0 ||
		din->diffuseColor[2] > 0 ) && din->diffuseImage != globalImages->blackImage )
		|| ( ( din->specularColor[0] > 0 ||
		din->specularColor[1] > 0 ||
		din->specularColor[2] > 0 ) && din->specularImage != globalImages->blackImage ) ) {
		VK_DrawSingleInteraction( din );
	}
}

/*
====================
VK_SubmitCustomLightingInteraction

Submit an authored Raven customLighting stage through the shared interaction
pipelines.  RB_ARB2_DrawCustomGLSLInteractionStage does not apply the stock
r_skipBump/r_skipDiffuse/r_skipSpecular substitutions to these stages, so the
Vulkan translation must leave all three authored maps intact as well.
====================
*/
static void VK_SubmitCustomLightingInteraction( drawInteraction_t *din,
												bool parallax,
												const float scaleBias[ 2 ] ) {
	if ( din->bumpImage == NULL || din->diffuseImage == NULL
			|| din->specularImage == NULL ) {
		return;
	}

	VK_DrawSingleInteractionMode( din, parallax, scaleBias[ 0 ], scaleBias[ 1 ] );
}

/*
====================
VK_DrawCustomLightingStage

Translate one active glsl/Customlit or glsl/Parallaxbump stage into the
existing per-light drawInteraction_t ABI.  All six guide textures are
resolved by their authored semantic names; the stock interaction pipelines
therefore preserve unshadowed, projected-shadow, point-shadow, and stencil
receiver behavior without a parallel lighting implementation.
====================
*/
static void VK_DrawCustomLightingStage( const shaderStage_t *surfaceStage,
										const float *surfaceRegs,
										const float lightColor[ 4 ],
										const drawInteraction_t *lightInter ) {
	if ( surfaceStage == NULL || surfaceStage->newStage == NULL
			|| surfaceRegs == NULL || lightInter == NULL ) {
		return;
	}

	const newShaderStage_t *newStage = surfaceStage->newStage;
	if ( !newStage->customLighting || !newStage->glslProgram ) {
		return;
	}
	if ( surfaceRegs[ surfaceStage->conditionRegister ] == 0.0f ) {
		return;
	}

	const vkGLSLProgramFamily_t family =
		R_GetGLSLProgramFamily( newStage->glslProgramName );
	if ( family != VK_GLSL_PROGRAM_FAMILY_CUSTOM_LIT
			&& family != VK_GLSL_PROGRAM_FAMILY_PARALLAX_BUMP ) {
		return;
	}

	drawInteraction_t customInter = *lightInter;
	customInter.bumpImage =
		VK_ResolveCustomLightingTexture( newStage, "NormalMap", &customInter );
	customInter.diffuseImage =
		VK_ResolveCustomLightingTexture( newStage, "DiffuseMap", &customInter );
	customInter.specularImage =
		VK_ResolveCustomLightingTexture( newStage, "SpecularMap", &customInter );

	idImage *semanticFalloff =
		VK_ResolveCustomLightingTexture( newStage, "LightFalloffImage", &customInter );
	if ( semanticFalloff != NULL ) {
		customInter.lightFalloffImage = semanticFalloff;
	}
	idImage *semanticLight =
		VK_ResolveCustomLightingTexture( newStage, "LightImage", &customInter );
	if ( semanticLight != NULL ) {
		customInter.lightImage = semanticLight;
	}

	idImage *unusedImage = NULL;
	idVec4 textureMatrix[ 2 ];
	float surfaceColor[ 4 ];
	VK_SetDrawInteraction( surfaceStage, surfaceRegs, &unusedImage,
		textureMatrix, surfaceColor );
	customInter.bumpMatrix[ 0 ] = textureMatrix[ 0 ];
	customInter.bumpMatrix[ 1 ] = textureMatrix[ 1 ];
	customInter.diffuseMatrix[ 0 ] = textureMatrix[ 0 ];
	customInter.diffuseMatrix[ 1 ] = textureMatrix[ 1 ];
	customInter.specularMatrix[ 0 ] = textureMatrix[ 0 ];
	customInter.specularMatrix[ 1 ] = textureMatrix[ 1 ];
	for ( int component = 0 ; component < 4 ; component++ ) {
		customInter.diffuseColor[ component ] =
			surfaceColor[ component ] * lightColor[ component ];
		customInter.specularColor[ component ] =
			surfaceColor[ component ] * lightColor[ component ];
	}
	customInter.vertexColor = surfaceStage->vertexColor;
	RB_ApplyFlatDiffuseStage( customInter.surf, &customInter.diffuseImage,
		customInter.diffuseColor.ToFloatPtr(), customInter.flatDiffuseParams );

	const bool parallax = family == VK_GLSL_PROGRAM_FAMILY_PARALLAX_BUMP;
	float scaleBias[ 2 ];
	VK_CustomLightingScaleBias( newStage, surfaceRegs, scaleBias );
	VK_ReportCustomLightingFamily( family );
	VK_SubmitCustomLightingInteraction( &customInter, parallax, scaleBias );
}

/*
====================
VK_Inter_WriteShadowSlice

Per-space shadow block. Projected lights use a shadow-only 512B slice and the
GL contract (draw_arb2.cpp:8552-8581): every cascade's world clip planes are
localized to the surface's model space CPU-side, with composed atlas rects,
split depths, and per-cascade bias/normal-offset state. Point lights
(RB_GLSLPointShadowMap_
DrawInteraction contract): the model matrix rows (RB_ShadowMapModelMatrixRows),
the global light origin + far envelope, and the point bias scalars. Returns
the dynamic offset or -1 on ring overflow.
====================
*/
static int VK_Inter_WriteShadowSlice( const viewEntity_t *space ) {
	const vkShadowLightState_t *state = interPass.shadowState;
	const vkShadowPassState_t *passState = interPass.shadowPassState;
	if ( state == NULL || passState == NULL || space == NULL ) {
		return -1;
	}

	if ( state->pointLight ) {
		vkPointShadowBlock_t pointBlock;
		memset( &pointBlock, 0, sizeof( pointBlock ) );
		const float *m = space->modelMatrix;
		for ( int i = 0 ; i < 4 ; i++ ) {
			pointBlock.modelRow0[ i ] = m[ i * 4 + 0 ];
			pointBlock.modelRow1[ i ] = m[ i * 4 + 1 ];
			pointBlock.modelRow2[ i ] = m[ i * 4 + 2 ];
		}
		pointBlock.lightOriginFar[ 0 ] = state->pointLightOrigin[ 0 ];
		pointBlock.lightOriginFar[ 1 ] = state->pointLightOrigin[ 1 ];
		pointBlock.lightOriginFar[ 2 ] = state->pointLightOrigin[ 2 ];
		pointBlock.lightOriginFar[ 3 ] = state->pointFar;
		// the depth-compare path uses r_shadowMapPointBias directly; the GL
		// storage-step floor only applies to the packed-color fallback
		pointBlock.biasParams[ 0 ] = r_shadowMapPointBias.GetFloat();
		pointBlock.biasParams[ 1 ] = r_shadowMapPointNormalBias.GetFloat();
		pointBlock.biasParams[ 2 ] = state->texelDepthBias;
		pointBlock.biasParams[ 3 ] = state->normalOffsetWorld;
		pointBlock.filterParams[ 0 ] =
				Max( 0.0f, r_shadowMapPointFilterRadius.GetFloat() );
		pointBlock.filterParams[ 1 ] = (float)idMath::ClampInt( 1, 13,
				r_shadowMapPointFilterTaps.GetInteger() );
		pointBlock.filterParams[ 2 ] = (float)idMath::ClampInt( 0, 1,
				r_shadowMapPointFilterMode.GetInteger() );
		pointBlock.filterParams[ 3 ] =
				2.0f / (float)Max( 1, state->tileSize );
		pointBlock.samplingParams[ 0 ] =
				r_shadowMapPointDepthCompare.GetBool() ? 1.0f : 0.0f;
		return VK_Exec_ShadowUniformAlloc( &pointBlock,
				sizeof( pointBlock ) );
	}

	vkShadowBlock_t block;
	memset( &block, 0, sizeof( block ) );
	const shadowMapProjectedLightState_t &projected =
			state->projectedState;
	const int cascadeCount = idMath::ClampInt( 1,
			SHADOWMAP_PROJECTED_MAX_CASCADES, projected.cascadeCount );
	for ( int cascadeIndex = 0 ; cascadeIndex < cascadeCount ;
			cascadeIndex++ ) {
		idPlane localPlane;
		R_GlobalPlaneToLocal( space->modelMatrix,
				projected.clipPlanes[ cascadeIndex ][ 0 ], localPlane );
		memcpy( block.shadowRow0[ cascadeIndex ],
				localPlane.ToFloatPtr(), sizeof( block.shadowRow0[ 0 ] ) );
		R_GlobalPlaneToLocal( space->modelMatrix,
				projected.clipPlanes[ cascadeIndex ][ 1 ], localPlane );
		memcpy( block.shadowRow1[ cascadeIndex ],
				localPlane.ToFloatPtr(), sizeof( block.shadowRow1[ 0 ] ) );
		R_GlobalPlaneToLocal( space->modelMatrix,
				projected.clipPlanes[ cascadeIndex ][ 2 ], localPlane );
		memcpy( block.shadowRow2[ cascadeIndex ],
				localPlane.ToFloatPtr(), sizeof( block.shadowRow2[ 0 ] ) );
		R_GlobalPlaneToLocal( space->modelMatrix,
				projected.clipPlanes[ cascadeIndex ][ 3 ], localPlane );
		memcpy( block.shadowRow3[ cascadeIndex ],
				localPlane.ToFloatPtr(), sizeof( block.shadowRow3[ 0 ] ) );
	}

	memcpy( block.atlasRects, passState->atlasRects,
			sizeof( block.atlasRects ) );
	memcpy( block.splitDepths, projected.splitDepths,
			sizeof( block.splitDepths ) );
	memcpy( block.cascadeBiasScale, projected.biasScale,
			sizeof( block.cascadeBiasScale ) );
	memcpy( block.texelDepthBias, projected.texelDepthBias,
			sizeof( block.texelDepthBias ) );
	const float normalOffsetScale =
			Max( 0.0f, r_shadowMapNormalOffsetScale.GetFloat() );
	for ( int cascadeIndex = 0 ;
			cascadeIndex < SHADOWMAP_PROJECTED_MAX_CASCADES ;
			cascadeIndex++ ) {
		block.normalOffsetWorld[ cascadeIndex ] =
				projected.worldTexelSize[ cascadeIndex ]
				* normalOffsetScale;
	}

	// Column-major matrix row 2 negated, so dot(localPosition,row) is the
	// positive eye-space depth used by the shared cascade selection policy.
	const float *modelView = space->modelViewMatrix;
	block.viewDepthRow[ 0 ] = -modelView[ 2 ];
	block.viewDepthRow[ 1 ] = -modelView[ 6 ];
	block.viewDepthRow[ 2 ] = -modelView[ 10 ];
	block.viewDepthRow[ 3 ] = -modelView[ 14 ];
	block.biasParams[ 0 ] = r_shadowMapBias.GetFloat();
	block.biasParams[ 1 ] = r_shadowMapNormalBias.GetFloat();
	block.biasParams[ 2 ] = idMath::ClampFloat( 0.0f, 0.5f,
			r_shadowMapCascadeBlend.GetFloat() );
	block.biasParams[ 3 ] = (float)cascadeCount;
	block.texelSize[ 0 ] = state->invAtlasSize[ 0 ];
	block.texelSize[ 1 ] = state->invAtlasSize[ 1 ];
	const shadowMapProjectedFilterSettings_t filterSettings =
			R_ShadowMapProjectedFilterSettings( state->vLight );
	block.filterParams[ 0 ] = filterSettings.filterRadius;
	block.filterParams[ 1 ] = (float)filterSettings.filterTaps;
	block.filterParams[ 2 ] = (float)filterSettings.filterMode;
	// PCSS-lite requires native blocker depths, irrespective of the general
	// projected compare preference.
	block.filterParams[ 3 ] =
			( r_shadowMapDepthCompare.GetBool() &&
			  filterSettings.filterMode != 2 ) ? 1.0f : 0.0f;
	block.pcssParams[ 0 ] = filterSettings.pcssLightRadius;
	block.pcssParams[ 1 ] = filterSettings.pcssMaxRadius;
	block.pcssParams[ 2 ] = filterSettings.effectiveFilterRadius;
	block.pcssParams[ 3 ] =
			r_shadowMapReceiverPlaneBias.GetBool() ? 1.0f : 0.0f;

	return VK_Exec_ShadowUniformAlloc( &block, sizeof( block ) );
}

/*
====================
VK_Inter_SelectShadowMode

Binds the projected-shadowed, point-shadowed, or unshadowed interaction
pipeline for the next chain and selects the ownership pass's set-7
descriptor set (the shared atlas set, or that pass's cube set). Entering a
shadowed receiver pass invalidates the space tracking so its per-space
shadow slice is rewritten even when the light and space are unchanged.
====================
*/
static void VK_Inter_SelectShadowMode( const vkShadowLightState_t *state,
		const vkShadowPassState_t *passState ) {
	int wantMode = 0;
	if ( state != NULL && passState != NULL ) {
		wantMode = state->pointLight ? 2 : 1;
	} else {
		state = NULL;
		passState = NULL;
	}
	// Defensive downgrade only: the per-light admission block validates these
	// resources before a required receiver chain can reach this selector.
	if ( wantMode == 1 && ( interPass.pipelineShadowed == VK_NULL_HANDLE || interPass.shadowSetAtlas == VK_NULL_HANDLE ) ) {
		wantMode = 0;
		state = NULL;
		passState = NULL;
	}
	if ( wantMode == 2 && ( interPass.pipelinePointShadowed == VK_NULL_HANDLE
			|| passState->pointSet == VK_NULL_HANDLE ) ) {
		wantMode = 0;
		state = NULL;
		passState = NULL;
	}

	if ( wantMode != interPass.shadowMode ) {
		interPass.shadowMode = wantMode;
		VkPipeline pipeline = interPass.pipelineUnshadowed;
		if ( wantMode == 1 ) {
			pipeline = interPass.pipelineShadowed;
		} else if ( wantMode == 2 ) {
			pipeline = interPass.pipelinePointShadowed;
		}
		vkCmdBindPipeline( interPass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	}
	interPass.shadowActive = wantMode != 0;
	interPass.shadowState = state;
	interPass.shadowPassState = passState;
	interPass.shadowSet = ( wantMode == 2 ) ? passState->pointSet : interPass.shadowSetAtlas;
	if ( interPass.shadowActive ) {
		interPass.currentSpace = NULL;
		interPass.shadowSliceOffset = -1;
	}
}

/*
====================
VK_Inter_StencilClear

Per-light stencil clear (draw_arb2.cpp:11600-11608 contract):
vkCmdClearAttachments uses vLight->scissorRect when r_useScissor is on,
or RB_BeginDrawingView's view scissor when it is off. The selected rect is
converted from GL bottom-left to Vulkan top-left and clamped to the render
area. GL clears to the view-level latch (128, R_SafeStencilClearValue).
====================
*/
static void VK_Inter_StencilClear( const viewLight_t *vLight ) {
	const viewDef_t *viewDef = interPass.viewDef;
	const idScreenRect &rect = r_useScissor.GetBool()
			? vLight->scissorRect : viewDef->scissor;
	if ( rect.IsEmpty() ) {
		// A degenerate selected scissor clears (and later draws) nothing.
		return;
	}

	const int scX = viewDef->viewport.x1 + rect.x1;
	const int scYGL = viewDef->viewport.y1 + rect.y1;
	const int scW = rect.x2 - rect.x1 + 1;
	const int scH = rect.y2 - rect.y1 + 1;

	int x0 = scX > 0 ? scX : 0;
	int y0 = interPass.fbHeight - scYGL - scH;
	if ( y0 < 0 ) {
		y0 = 0;
	}
	int x1 = scX + scW;
	if ( x1 > interPass.fbWidth ) {
		x1 = interPass.fbWidth;
	}
	int y1 = interPass.fbHeight - scYGL;
	if ( y1 > interPass.fbHeight ) {
		y1 = interPass.fbHeight;
	}
	if ( x1 <= x0 || y1 <= y0 ) {
		return;
	}

	VkClearAttachment clearAtt;
	memset( &clearAtt, 0, sizeof( clearAtt ) );
	clearAtt.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
	clearAtt.clearValue.depthStencil.stencil = 128;
	VkClearRect clearRect;
	memset( &clearRect, 0, sizeof( clearRect ) );
	clearRect.rect.offset.x = x0;
	clearRect.rect.offset.y = y0;
	clearRect.rect.extent.width = (uint32_t)( x1 - x0 );
	clearRect.rect.extent.height = (uint32_t)( y1 - y0 );
	clearRect.layerCount = 1;
	vkCmdClearAttachments( interPass.cmd, 1, &clearAtt, 1, &clearRect );
}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
static ID_INLINE int VK_Inter_MD5RBlendIndex( dword packedBlendIndices, int component ) {
	return static_cast<int>( ( packedBlendIndices >> ( component * 8 ) ) & 0xFFu );
}

static ID_INLINE idVec3 VK_Inter_MD5RTransformPosition( const float *transform, const idVec4 &position ) {
	return idVec3(
		transform[ 0 ] * position.x + transform[ 1 ] * position.y + transform[ 2 ] * position.z + transform[ 3 ] * position.w,
		transform[ 4 ] * position.x + transform[ 5 ] * position.y + transform[ 6 ] * position.z + transform[ 7 ] * position.w,
		transform[ 8 ] * position.x + transform[ 9 ] * position.y + transform[ 10 ] * position.z + transform[ 11 ] * position.w );
}

// CPU equivalent of md5rshadow1.vp/md5rshadow4.vp. The packed volume's
// homogeneous shadow selector is preserved separately; skinning always uses a
// positional w of one, matching the four-bone program's explicit R4.w = 1 and
// the one-bone buffer contract.
static bool VK_Inter_MD5RSkinShadowPosition( const rvMD5RVertexBufferDesc &vertexBuffer,
		int sourceVertexIndex, const rvMD5RPrimBatch &primBatch,
		const srfTriangles_t *tri, int transformBase, idVec3 &skinnedPosition ) {
	if ( tri == NULL || tri->skinToModelTransforms == NULL || tri->numSkinToModelTransforms <= 0
			|| sourceVertexIndex < 0 || sourceVertexIndex >= vertexBuffer.numVertices
			|| vertexBuffer.positions.Num() != vertexBuffer.numVertices ) {
		return false;
	}

	const bool hasBlendIndices = vertexBuffer.loadVertexFormat.hasBlendIndex
			&& vertexBuffer.blendIndices.Num() == vertexBuffer.numVertices;
	const bool hasBlendWeights = vertexBuffer.loadVertexFormat.hasBlendWeight
			&& vertexBuffer.blendWeights.Num() == vertexBuffer.numVertices;
	const int transformCount = Max( primBatch.numTransforms, 1 );
	const dword packedBlendIndices = hasBlendIndices ? vertexBuffer.blendIndices[ sourceVertexIndex ] : 0u;

	idVec4 position = vertexBuffer.positions[ sourceVertexIndex ];
	position.w = 1.0f;

	idVec4 weights;
	weights.Zero();
	weights.x = 1.0f;
	if ( hasBlendWeights ) {
		weights = vertexBuffer.blendWeights[ sourceVertexIndex ];
		// md5rshadow4.vp stores xyz and reconstructs the final influence.
		weights.w = 1.0f - weights.x - weights.y - weights.z;
	}

	skinnedPosition.Zero();
	const int influenceCount = hasBlendWeights ? 4 : 1;
	for ( int influence = 0 ; influence < influenceCount ; influence++ ) {
		const int localTransform = hasBlendIndices ? VK_Inter_MD5RBlendIndex( packedBlendIndices, influence ) : 0;
		if ( localTransform < 0 || localTransform >= transformCount ) {
			return false;
		}
		const int transformIndex = transformBase + localTransform;
		if ( transformIndex < 0 || transformIndex >= tri->numSkinToModelTransforms ) {
			return false;
		}
		const float weight = weights[ influence ];
		if ( weight != 0.0f ) {
			skinnedPosition += weight * VK_Inter_MD5RTransformPosition(
					tri->skinToModelTransforms + transformIndex * 16, position );
		}
	}
	return true;
}

static float VK_Inter_MD5RShadowVertexW( const rvMD5RVertexBufferDesc &vertexBuffer, int vertexIndex ) {
	const bool oneBoneSelector = vertexBuffer.loadVertexFormat.hasBlendIndex
			&& !vertexBuffer.loadVertexFormat.hasBlendWeight
			&& vertexBuffer.blendIndices.Num() == vertexBuffer.numVertices;
	if ( oneBoneSelector ) {
		// md5rshadow1.vp reads the selector from normalized attrib[1].w.
		return (float)( ( vertexBuffer.blendIndices[ vertexIndex ] >> 24 ) & 0xFFu ) * ( 1.0f / 255.0f );
	}
	return vertexBuffer.positions[ vertexIndex ].w;
}

static bool VK_Inter_PackedShadowHeaderValid( const srfTriangles_t *tri, int numPrimBatches ) {
	if ( tri == NULL || tri->indexes == NULL || numPrimBatches <= 0
			|| tri->numIndexes <= 0 || tri->numAllocedIndices < 0 ) {
		return false;
	}
	const int64_t headerWords = static_cast<int64_t>( numPrimBatches ) * 2;
	const int64_t requiredWords = static_cast<int64_t>( tri->numIndexes ) + headerWords;
	if ( requiredWords > tri->numAllocedIndices ) {
		return false;
	}
	int64_t total = 0;
	for ( int batch = 0 ; batch < numPrimBatches ; batch++ ) {
		const int64_t noCaps = tri->indexes[ batch * 2 + 0 ];
		const int64_t withCaps = tri->indexes[ batch * 2 + 1 ];
		if ( noCaps < 0 || withCaps < 0 || ( noCaps % 3 ) != 0 || ( withCaps % 3 ) != 0
				|| noCaps > withCaps || withCaps > tri->numIndexes - total ) {
			return false;
		}
		total += withCaps;
	}
	return total == tri->numIndexes;
}

// Decode, CPU-skin, upload, and draw the packed shadow batches. This is a
// correctness fallback for Vulkan until packed MD5R vertex buffers become
// first-class Vulkan resources; unlike the former skip it preserves moving
// character shadows and the retail cap/no-cap selection.
static bool VK_Inter_DrawPackedShadowSurface( const drawSurf_t *surf, bool drawCaps,
		bool external, VkStencilFaceFlags frontSidedFace, VkStencilFaceFlags backSidedFace ) {
	typedef struct vkPackedShadowBatchRange_s {
		int vertexStart;
		int vertexCount;
		int sourceIndexStart;
		int sourceIndexCount;
		int selectedIndexCount;
		int transformBase;
		int transformCount;
		int destVertexStart;
		int destIndexStart;
	} vkPackedShadowBatchRange_t;

	const srfTriangles_t *tri = surf != NULL ? surf->geo : NULL;
	const rvMD5RMesh *mesh = R_MD5R_GetMeshForTri( tri );
	const rvMD5RVertexBufferDesc *vertexBuffer = R_MD5R_GetShadowVertexBufferForTri( tri );
	if ( tri == NULL || mesh == NULL || vertexBuffer == NULL || mesh->primBatches.Num() <= 0
			|| vertexBuffer->numVertices <= 0 || vertexBuffer->positions.Num() != vertexBuffer->numVertices ) {
		return false;
	}
	const bool hasBlendIndices = vertexBuffer->loadVertexFormat.hasBlendIndex;
	const bool hasBlendWeights = vertexBuffer->loadVertexFormat.hasBlendWeight;
	if ( hasBlendWeights && !hasBlendIndices ) {
		return false;
	}
	if ( hasBlendIndices && vertexBuffer->blendIndices.Num() != vertexBuffer->numVertices ) {
		return false;
	}
	if ( hasBlendWeights && vertexBuffer->blendWeights.Num() != vertexBuffer->numVertices ) {
		return false;
	}
	const bool skinPackedVertices = hasBlendIndices
		&& tri->skinToModelTransforms != NULL
		&& tri->numSkinToModelTransforms > 0;

	const int numBatches = mesh->primBatches.Num();
	const bool headered = VK_Inter_PackedShadowHeaderValid( tri, numBatches );
	const rvMD5RIndexBufferDesc *indexBuffer = headered ? NULL : R_MD5R_GetShadowIndexBufferForTri( tri );
	if ( !headered && ( indexBuffer == NULL || indexBuffer->numIndices <= 0
			|| indexBuffer->indices.Num() != indexBuffer->numIndices ) ) {
		return false;
	}

	// Retail packed MD5R volumes expose only sidewalls and cap-inclusive
	// partitions. The caller folds the classic "no front caps" selection
	// into the cap-inclusive stream explicitly, avoiding ambiguous equal
	// counts after the front end disables external-shadow optimization.
	const glIndex_t *header = headered ? tri->indexes : NULL;
	const glIndex_t *headeredIndexes = headered ? tri->indexes + 2 * numBatches : NULL;
	idTempArray<vkPackedShadowBatchRange_t> ranges( static_cast<unsigned int>( numBatches ) );
	int64_t totalVertexCount = 0;
	int64_t totalSelectedIndexCount = 0;
	int64_t headeredSourceIndex = 0;
	int64_t transformBase = 0;

	// Validate every batch and build all draw ranges before recording the
	// first stencil write. This keeps malformed packed data and ring failures
	// from leaving a partially stamped volume.
	for ( int batchIndex = 0 ; batchIndex < numBatches ; batchIndex++ ) {
		const rvMD5RPrimBatch &batch = mesh->primBatches[ batchIndex ];
		vkPackedShadowBatchRange_t &range = ranges[ batchIndex ];
		memset( &range, 0, sizeof( range ) );
		if ( batch.numTransforms < 0 ) {
			return false;
		}
		const int transformCount = Max( batch.numTransforms, 1 );
		const int vertexStart = batch.shadowVolGeoSpec.vertexStart;
		const int vertexCount = batch.shadowVolGeoSpec.vertexCount;
		if ( !batch.hasShadowGeoSpec || vertexStart < 0 || vertexCount <= 0
				|| vertexStart > vertexBuffer->numVertices
				|| vertexCount > vertexBuffer->numVertices - vertexStart ) {
			return false;
		}

		int64_t indexCount = 0;
		int64_t totalIndexCount = 0;
		int64_t sourceIndexStart = 0;
		if ( headered ) {
			const int64_t noCaps = header[ batchIndex * 2 + 0 ];
			totalIndexCount = header[ batchIndex * 2 + 1 ];
			indexCount = drawCaps ? totalIndexCount : noCaps;
			sourceIndexStart = headeredSourceIndex;
			if ( totalIndexCount > tri->numIndexes - headeredSourceIndex ) {
				return false;
			}
			headeredSourceIndex += totalIndexCount;
		} else {
			if ( batch.shadowVolGeoSpec.primitiveCount < 0
					|| batch.numShadowPrimitivesNoCaps < 0 ) {
				return false;
			}
			totalIndexCount = static_cast<int64_t>( batch.shadowVolGeoSpec.primitiveCount ) * 3;
			const int64_t noCaps = static_cast<int64_t>( batch.numShadowPrimitivesNoCaps ) * 3;
			sourceIndexStart = batch.shadowVolGeoSpec.indexStart;
			if ( sourceIndexStart < 0 || totalIndexCount < 0 || noCaps < 0
					|| noCaps > totalIndexCount
					|| sourceIndexStart > indexBuffer->numIndices
					|| totalIndexCount > indexBuffer->numIndices - sourceIndexStart ) {
				return false;
			}
			indexCount = drawCaps ? totalIndexCount : noCaps;
		}

		if ( skinPackedVertices
				&& ( transformBase > tri->numSkinToModelTransforms
					|| transformCount > tri->numSkinToModelTransforms - transformBase ) ) {
			return false;
		}
		if ( totalVertexCount > INT_MAX - vertexCount
				|| totalSelectedIndexCount > INT_MAX - indexCount
				|| transformBase > INT_MAX - transformCount ) {
			return false;
		}

		const glIndex_t *sourceIndexes = headered
				? headeredIndexes + sourceIndexStart
				: indexBuffer->indices.Ptr() + sourceIndexStart;
		for ( int64_t index = 0 ; index < indexCount ; index++ ) {
			const int64_t sourceIndex = sourceIndexes[ index ];
			if ( sourceIndex < vertexStart
					|| sourceIndex >= static_cast<int64_t>( vertexStart ) + vertexCount ) {
				return false;
			}
		}

		range.vertexStart = vertexStart;
		range.vertexCount = vertexCount;
		range.sourceIndexStart = static_cast<int>( sourceIndexStart );
		range.sourceIndexCount = static_cast<int>( totalIndexCount );
		range.selectedIndexCount = static_cast<int>( indexCount );
		range.transformBase = static_cast<int>( transformBase );
		range.transformCount = transformCount;
		range.destVertexStart = static_cast<int>( totalVertexCount );
		range.destIndexStart = static_cast<int>( totalSelectedIndexCount );

		totalVertexCount += vertexCount;
		totalSelectedIndexCount += indexCount;
		transformBase += transformCount;
	}
	if ( totalSelectedIndexCount <= 0 || totalVertexCount <= 0
			|| ( headered && headeredSourceIndex != tri->numIndexes ) ) {
		return false;
	}

	idTempArray<shadowCache_t> verts( static_cast<unsigned int>( totalVertexCount ) );
	idTempArray<glIndex_t> indexes( static_cast<unsigned int>( totalSelectedIndexCount ) );

	for ( int batchIndex = 0 ; batchIndex < numBatches ; batchIndex++ ) {
		const rvMD5RPrimBatch &batch = mesh->primBatches[ batchIndex ];
		const vkPackedShadowBatchRange_t &range = ranges[ batchIndex ];
		for ( int vertex = 0 ; vertex < range.vertexCount ; vertex++ ) {
			const int sourceVertex = range.vertexStart + vertex;
			idVec3 position = vertexBuffer->positions[ sourceVertex ].ToVec3();
			if ( skinPackedVertices
					&& !VK_Inter_MD5RSkinShadowPosition( *vertexBuffer, sourceVertex, batch,
							tri, range.transformBase, position ) ) {
				return false;
			}
			verts[ range.destVertexStart + vertex ].xyz.Set(
					position.x, position.y, position.z,
					VK_Inter_MD5RShadowVertexW( *vertexBuffer, sourceVertex ) );
		}

		const glIndex_t *sourceIndexes = headered
				? headeredIndexes + range.sourceIndexStart
				: indexBuffer->indices.Ptr() + range.sourceIndexStart;
		for ( int index = 0 ; index < range.selectedIndexCount ; index++ ) {
			const int rebased = sourceIndexes[ index ] - range.vertexStart;
			indexes[ range.destIndexStart + index ] =
					range.destVertexStart + rebased;
		}
	}

	if ( !VK_Exec_BindRawShadowGeometry( interPass.cmd, interPass.slot,
			verts.Ptr(), static_cast<int>( totalVertexCount ),
			indexes.Ptr(), static_cast<int>( totalSelectedIndexCount ) ) ) {
		return false;
	}

	bool drewAnything = false;
	for ( int batchIndex = 0 ; batchIndex < numBatches ; batchIndex++ ) {
		const vkPackedShadowBatchRange_t &range = ranges[ batchIndex ];
		if ( range.selectedIndexCount <= 0 ) {
			continue;
		}
		const uint32_t drawCount = r_singleTriangle.GetBool()
				? static_cast<uint32_t>( Min( 3, range.selectedIndexCount ) )
				: static_cast<uint32_t>( range.selectedIndexCount );
		if ( !external ) {
			vkCmdSetStencilOp( interPass.cmd, frontSidedFace, VK_STENCIL_OP_KEEP,
					VK_STENCIL_OP_DECREMENT_AND_WRAP, VK_STENCIL_OP_DECREMENT_AND_WRAP, VK_COMPARE_OP_ALWAYS );
			vkCmdSetStencilOp( interPass.cmd, backSidedFace, VK_STENCIL_OP_KEEP,
					VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_COMPARE_OP_ALWAYS );
			vkCmdDrawIndexed( interPass.cmd, drawCount, 1,
					static_cast<uint32_t>( range.destIndexStart ), 0, 0 );
			interPass.volumeDrawCount++;
			interPass.volumePreloadCount++;
			backEnd.pc.c_shadowElements++;
			backEnd.pc.c_shadowIndexes += range.selectedIndexCount;
			backEnd.pc.c_shadowVertexes += range.vertexCount;
		}

		vkCmdSetStencilOp( interPass.cmd, frontSidedFace, VK_STENCIL_OP_KEEP,
				VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS );
		vkCmdSetStencilOp( interPass.cmd, backSidedFace, VK_STENCIL_OP_KEEP,
				VK_STENCIL_OP_DECREMENT_AND_WRAP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS );
		vkCmdDrawIndexed( interPass.cmd, drawCount, 1,
				static_cast<uint32_t>( range.destIndexStart ), 0, 0 );
		interPass.volumeDrawCount++;
		backEnd.pc.c_shadowElements++;
		backEnd.pc.c_shadowIndexes += range.selectedIndexCount;
		backEnd.pc.c_shadowVertexes += range.vertexCount;
		drewAnything = true;
	}
	return drewAnything;
}
#endif

/*
====================
VK_StencilShadowPass

Port of RB_StencilShadowPass + RB_T_Shadow (draw_common.cpp:7381-7444,
:7207-7362) in the two-sided single-pass formulation ONLY: wrap ops and
separate per-face stencil state are core Vulkan, so the GL capability gate
(glStencilOpSeparate && GL_INCR_WRAP, :7420-7423) is unconditionally
satisfied and the cull-flipped two-pass fallback never runs.

Enter/exit stencil contract: the caller latched GEQUAL/128/KEEP after the
per-light clear; this pass flips the per-face ops to ALWAYS + wrap writes
for the volume draws and restores GEQUAL/128/KEEP (the GL exit at
:7442-7443) so the light's interactions draw under the exit state.

Face mapping (derivation, following the E/F cull-mapping precedent): the
executor's negative-height viewport preserves GL winding parity under
VK_FRONT_FACE_COUNTER_CLOCKWISE, so CT_FRONT_SIDED maps to
VK_CULL_MODE_FRONT_BIT in non-mirror views (the Draw3DView depth fill /
GL_Cull's glCullFace(GL_FRONT) convention) — i.e. a triangle GL classifies
GL_BACK is a Vulkan back face. RB_T_Shadow assigns the legacy
CT_FRONT_SIDED ops to frontSidedFace = isMirror ? GL_FRONT : GL_BACK
(:7321), so those ops land on VK_STENCIL_FACE_BACK_BIT in non-mirror views
and flip for mirrors.

Documented Phase G1 gap:
- r_showShadows debug visualization: Phase I rendertools.
====================
*/
static bool VK_StencilShadowPass( const drawSurf_t *drawSurfs ) {
	if ( drawSurfs == NULL ) {
		return true;
	}
	if ( !r_shadows.GetBool() ) {
		return false;
	}

	VkCommandBuffer cmd = interPass.cmd;
	bool complete = true;

	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, interPass.pipelineStencilShadow );

	// GL_State(GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK |
	// GLS_DEPTHFUNC_LESS): color writes are off in the pipeline; depth
	// tests LEQUAL with writes off
	vkCmdSetDepthTestEnable( cmd, VK_TRUE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );

	// glPolygonOffset(r_shadowPolygonFactor, -r_shadowPolygonOffset): the
	// defaults (0, -1) give constant +1, slope 0 — bias IS on by default.
	// GL units map to the Vulkan constant factor and GL factor to the
	// slope factor (the Phase E polygon-offset mapping)
	const bool shadowBias = r_shadowPolygonFactor.GetFloat() != 0.0f || r_shadowPolygonOffset.GetFloat() != 0.0f;
	if ( shadowBias ) {
		vkCmdSetDepthBiasEnable( cmd, VK_TRUE );
		vkCmdSetDepthBias( cmd, -r_shadowPolygonOffset.GetFloat(), 0.0f, r_shadowPolygonFactor.GetFloat() );
	}

	// GL_Cull(CT_TWO_SIDED): both faces rasterize in one draw
	vkCmdSetCullMode( cmd, VK_CULL_MODE_NONE );

	const bool useDepthBounds =
			vkCtx.depthBoundsSupported && r_useDepthBoundsTest.GetBool();
	if ( useDepthBounds ) {
		vkCmdSetDepthBoundsTestEnable( cmd, VK_TRUE );
	}

	// see the face-mapping derivation above
	const VkStencilFaceFlags frontSidedFace = interPass.viewDef->isMirror ? VK_STENCIL_FACE_FRONT_BIT : VK_STENCIL_FACE_BACK_BIT;
	const VkStencilFaceFlags backSidedFace = interPass.viewDef->isMirror ? VK_STENCIL_FACE_BACK_BIT : VK_STENCIL_FACE_FRONT_BIT;

	for ( const drawSurf_t *surf = drawSurfs ; surf ; surf = surf->nextOnLight ) {
		const srfTriangles_t *tri = surf->geo;
		if ( tri == NULL ) {
			interPass.volumeSkipCount++;
			complete = false;
			continue;
		}
		const bool packedPrimBatches = R_TriHasPrimBatchMesh( tri );
		if ( tri->numIndexes <= 0 ) {
			continue;
		}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		// Packed MD5R volumes are decoded and streamed after the retail
		// cap/no-cap count is selected below. Classic volumes retain the
		// shadowCache/index binding path.
		if ( !packedPrimBatches
				&& ( tri->shadowCache == NULL || tri->indexes == NULL
					|| !VK_Exec_BindShadowGeometry( cmd, interPass.slot, tri ) ) ) {
			interPass.volumeSkipCount++;
			complete = false;
			continue;
		}
#else
		if ( packedPrimBatches || tri->shadowCache == NULL || tri->indexes == NULL
				|| !VK_Exec_BindShadowGeometry( cmd, interPass.slot, tri ) ) {
			interPass.volumeSkipCount++;
			complete = false;
			continue;
		}
#endif
		VK_Exec_SetSurfScissor( cmd, interPass.viewDef, surf, interPass.fbHeight );

		// space change: MVP (depth hacks included) + weapon depth-range,
		// sharing the pass tracking so the interleaved volume/interaction
		// chains never rebuild redundantly (VK_BuildSurfMVP is the same
		// function both walks use)
		if ( surf->space != interPass.currentSpace ) {
			interPass.currentSpace = surf->space;
			VK_BuildSurfMVP( interPass.viewDef, surf, interPass.mvp );
			const bool wantWeaponRange = surf->space->weaponDepthHack;
			if ( wantWeaponRange != interPass.weaponDepthRange ) {
				interPass.weaponDepthRange = wantWeaponRange;
				interPass.viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;
				vkCmdSetViewport( cmd, 0, 1, &interPass.viewport );
			}
		}

		// the local light origin rides the shared push block (the env[4]
		// PP_LIGHT_ORIGIN contract, w = 0; draw_common.cpp:7211-7222).
		// With r_useShadowVertexProgram 0 the front-end bakes CPU-projected
		// caches whose w==0 verts are ALREADY light-relative directions —
		// push a zero origin so the shader's subtract becomes the
		// fixed-function pass-through
		vkInteractionPush_t push;
		memset( &push, 0, sizeof( push ) );
		memcpy( push.mvp, interPass.mvp, sizeof( push.mvp ) );
		if ( packedPrimBatches || r_useShadowVertexProgram.GetBool() ) {
			idVec4 localLight;
			R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.vLight->globalLightOrigin, localLight.ToVec3() );
			localLight.w = 0.0f;
			memcpy( push.a, localLight.ToFloatPtr(), sizeof( push.a ) );
		}
		vkCmdPushConstants( cmd, interPass.layoutStencilShadow,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );

		// we always draw the sil planes, but we may not need to draw the
		// front or rear caps (RB_T_Shadow :7238-7268, verbatim)
		int numIndexes;
		bool external = false;
		bool packedCapInclusive = true;

		if ( !r_useExternalShadows.GetInteger() ) {
			numIndexes = tri->numIndexes;
		} else if ( r_useExternalShadows.GetInteger() == 2 ) { // force to no caps for testing
			numIndexes = tri->numShadowIndexesNoCaps;
			packedCapInclusive = false;
		} else if ( !( surf->dsFlags & DSF_VIEW_INSIDE_SHADOW ) ) {
			// if we aren't inside the shadow projection, no caps are ever needed
			numIndexes = tri->numShadowIndexesNoCaps;
			external = true;
			packedCapInclusive = false;
		} else if ( !backEnd.vLight->viewInsideLight && !( tri->shadowCapPlaneBits & SHADOW_CAP_INFINITE ) ) {
			// if we are inside the shadow projection, but outside the light,
			// and drawing a non-infinite shadow, we can skip some caps
			if ( backEnd.vLight->viewSeesShadowPlaneBits & tri->shadowCapPlaneBits ) {
				// we can see through a rear cap, so we need to draw it, but
				// we can skip the caps on the actual surface
				numIndexes = tri->numShadowIndexesNoFrontCaps;
			} else {
				// we don't need to draw any caps
				numIndexes = tri->numShadowIndexesNoCaps;
				packedCapInclusive = false;
			}
			external = true;
		} else {
			// must draw everything
			numIndexes = tri->numIndexes;
		}

		// If this surface could not use external shadow optimizations, the
		// front end already forced the "no caps" index counts back to the
		// full count; treat it as internal to keep the robust stencil path
		if ( numIndexes == tri->numIndexes ) {
			external = false;
			packedCapInclusive = true;
		}
		if ( numIndexes <= 0 ) {
			continue;
		}

		if ( useDepthBounds ) {
			const float minDepth = idMath::ClampFloat( 0.0f, 1.0f,
					surf->scissorRect.zmin );
			const float maxDepth = idMath::ClampFloat( minDepth, 1.0f,
					surf->scissorRect.zmax );
			vkCmdSetDepthBounds( cmd, minDepth, maxDepth );
		}

#if defined( _MD5R_SUPPORT ) || defined( Q4SDK_MD5R )
		if ( packedPrimBatches ) {
			if ( !VK_Inter_DrawPackedShadowSurface( surf, packedCapInclusive, external,
					frontSidedFace, backSidedFace ) ) {
				interPass.volumeSkipCount++;
				complete = false;
			}
			continue;
		}
#endif

		// patent-free work around: "preload" the stencil buffer with the
		// number of volumes clipped by the near or far plane (z-fail ops),
		// then the traditional depth-pass draw. With wrap inc/dec the
		// interleaved single-pass deltas are order-equivalent to the legacy
		// two-pass sequence (draw_common.cpp:7313-7340). GL op order is
		// (fail, zfail, zpass); Vulkan takes (fail, pass, depthFail)
		if ( !external ) {
			vkCmdSetStencilOp( cmd, frontSidedFace, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_DECREMENT_AND_WRAP,
					VK_STENCIL_OP_DECREMENT_AND_WRAP, VK_COMPARE_OP_ALWAYS );
			vkCmdSetStencilOp( cmd, backSidedFace, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_WRAP,
					VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_COMPARE_OP_ALWAYS );
			vkCmdDrawIndexed( cmd, (uint32_t)numIndexes, 1, 0, 0, 0 );
			interPass.volumeDrawCount++;
			interPass.volumePreloadCount++;
			backEnd.pc.c_shadowElements++;
			backEnd.pc.c_shadowIndexes += numIndexes;
			backEnd.pc.c_shadowVertexes += tri->numVerts;
		}

		// traditional depth-pass stencil shadows
		vkCmdSetStencilOp( cmd, frontSidedFace, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_INCREMENT_AND_WRAP,
				VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS );
		vkCmdSetStencilOp( cmd, backSidedFace, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_DECREMENT_AND_WRAP,
				VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS );
		vkCmdDrawIndexed( cmd, (uint32_t)numIndexes, 1, 0, 0, 0 );
		interPass.volumeDrawCount++;
		backEnd.pc.c_shadowElements++;
		backEnd.pc.c_shadowIndexes += numIndexes;
		backEnd.pc.c_shadowVertexes += tri->numVerts;
	}

	// exit contract (GL :7430-7443): bias off, GEQUAL/128 with ops KEEP for
	// the light's interactions (reference/masks stay latched from the light
	// entry); cull is re-set per surface by every consumer, so the
	// CT_FRONT_SIDED restore needs no explicit call
	if ( shadowBias ) {
		vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	}
	if ( useDepthBounds ) {
		vkCmdSetDepthBoundsTestEnable( cmd, VK_FALSE );
		vkCmdSetDepthBounds( cmd, 0.0f, 1.0f );
	}
	vkCmdSetStencilOp( cmd, VK_STENCIL_FACE_FRONT_AND_BACK, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
			VK_STENCIL_OP_KEEP, VK_COMPARE_OP_GREATER_OR_EQUAL );
	return complete;
}

/*
====================
VK_CreateSingleDrawInteractions

Port of RB_CreateSingleDrawInteractionsFiltered (tr_render.cpp:875, no
stage filter, no packed prim-batch path): per-surface geometry/scissor/
space handling in the executor's conventions, then the light-stage ×
surface-stage decomposition into primitive drawInteraction_t draws.

The chain surfaces' geo is the light-tris subset — its own culled index
list over the ambient surface's shared idDrawVert cache.
====================
*/
static void VK_CreateSingleDrawInteractions( const drawSurf_t *surf ) {
	const idMaterial	*surfaceShader = surf->material;
	const float			*surfaceRegs = surf->shaderRegisters;
	const viewLight_t	*vLight = backEnd.vLight;
	const idMaterial	*lightShader = vLight->lightShader;
	const float			*lightRegs = vLight->shaderRegisters;
	drawInteraction_t	inter;

	if ( r_skipInteractions.GetBool() || surf->geo == NULL || surf->geo->ambientCache == NULL ) {
		return;
	}
	if ( surf->geo->numIndexes <= 0
			|| ( surf->geo->indexes == NULL && surf->geo->indexCache == NULL ) ) {
		return;
	}

	if ( !VK_Exec_BindTriGeometry( interPass.cmd, interPass.slot, surf->geo ) ) {
		return;
	}
	VK_Exec_SetSurfScissor( interPass.cmd, interPass.viewDef, surf, interPass.fbHeight );

	// space change: rebuild the MVP (depth hacks included), the weapon
	// depth-range window, and the shadowed lights' per-space shadow slice,
	// mirroring the Draw3DView walks
	if ( surf->space != interPass.currentSpace ) {
		interPass.currentSpace = surf->space;
		VK_BuildSurfMVP( interPass.viewDef, surf, interPass.mvp );
		const bool wantWeaponRange = surf->space->weaponDepthHack;
		if ( wantWeaponRange != interPass.weaponDepthRange ) {
			interPass.weaponDepthRange = wantWeaponRange;
			interPass.viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;
			vkCmdSetViewport( interPass.cmd, 0, 1, &interPass.viewport );
		}
		if ( interPass.shadowActive ) {
			interPass.shadowSliceOffset = VK_Inter_WriteShadowSlice( surf->space );
		}
	}

	// material cull with the mirror swap (GL_Cull contract)
	switch ( surfaceShader->GetCullType() ) {
		case CT_TWO_SIDED:
			vkCmdSetCullMode( interPass.cmd, VK_CULL_MODE_NONE );
			break;
		case CT_BACK_SIDED:
			vkCmdSetCullMode( interPass.cmd, interPass.viewDef->isMirror ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT );
			break;
		default:
			vkCmdSetCullMode( interPass.cmd, interPass.viewDef->isMirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT );
			break;
	}

	// Quake 4 applies decal polygon offset in interaction passes as well
	// (RB_ARB2_DrawInteraction does this per draw; the material is shared
	// by every primitive interaction of the surface)
	const bool polygonOffset = surfaceShader->TestMaterialFlag( MF_POLYGONOFFSET );
	if ( polygonOffset ) {
		vkCmdSetDepthBiasEnable( interPass.cmd, VK_TRUE );
		vkCmdSetDepthBias( interPass.cmd, r_offsetUnits.GetFloat() * surfaceShader->GetPolygonOffset(), 0.0f, r_offsetFactor.GetFloat() );
	}

	inter.surf = surf;
	inter.lightFalloffImage = vLight->falloffImage;
	inter.vertexColor = SVC_IGNORE;

	R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, inter.localLightOrigin.ToVec3() );
	R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, inter.localViewOrigin.ToVec3() );
	inter.localLightOrigin[3] = 0;
	inter.localViewOrigin[3] = 1;
	inter.ambientLight = lightShader->IsAmbientLight();

	// the base projections may be modified by texture matrix on light stages
	idPlane lightProject[4];
	for ( int i = 0 ; i < 4 ; i++ ) {
		R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[i], lightProject[i] );
	}

	const int lightStageCount = lightShader->GetNumStages();
	const int surfaceStageCount = surfaceShader->GetNumStages();
	for ( int lightStageNum = 0 ; lightStageNum < lightStageCount ; lightStageNum++ ) {
		const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

		// ignore stages that fail the condition
		if ( !lightRegs[ lightStage->conditionRegister ] ) {
			continue;
		}

		inter.lightImage = lightStage->texture.image;

		memcpy( inter.lightProjection, lightProject, sizeof( inter.lightProjection ) );
		// now multiply the texgen by the light texture matrix
		if ( lightStage->texture.hasMatrix ) {
			RB_GetShaderTextureMatrix( lightRegs, &lightStage->texture, backEnd.lightTextureMatrix );
			RB_BakeTextureMatrixIntoTexgen( reinterpret_cast<class idPlane *>(inter.lightProjection), backEnd.lightTextureMatrix );
		}

		inter.bumpImage = NULL;
		inter.specularImage = NULL;
		inter.diffuseImage = NULL;
		inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
		inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;
		inter.flatDiffuseParams.Zero();

		float lightColor[4];

		// backEnd.lightScale is calculated so that lightColor[] will never
		// exceed tr.backEndRendererMaxLight
		lightColor[0] = backEnd.lightScale * lightRegs[ lightStage->color.registers[0] ];
		lightColor[1] = backEnd.lightScale * lightRegs[ lightStage->color.registers[1] ];
		lightColor[2] = backEnd.lightScale * lightRegs[ lightStage->color.registers[2] ];
		lightColor[3] = lightRegs[ lightStage->color.registers[3] ];

		// go through the individual stages
		for ( int surfaceStageNum = 0 ; surfaceStageNum < surfaceStageCount ; surfaceStageNum++ ) {
			const shaderStage_t	*surfaceStage = surfaceShader->GetStage( surfaceStageNum );

			switch( surfaceStage->lighting ) {
				case SL_AMBIENT: {
					// ignore ambient stages while drawing interactions
					break;
				}
				case SL_BUMP: {
					// ignore stage that fails the condition
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					// draw any previous interaction
					VK_SubmitInteraction( &inter );
					inter.diffuseImage = NULL;
					inter.specularImage = NULL;
					VK_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.bumpImage, inter.bumpMatrix, NULL );
					break;
				}
				case SL_DIFFUSE: {
					// ignore stage that fails the condition
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					if ( inter.diffuseImage ) {
						VK_SubmitInteraction( &inter );
					}
					VK_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.diffuseImage,
											inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr() );
					RB_ApplyFlatDiffuseStage( surf, &inter.diffuseImage,
						inter.diffuseColor.ToFloatPtr(), inter.flatDiffuseParams );
					inter.diffuseColor[0] *= lightColor[0];
					inter.diffuseColor[1] *= lightColor[1];
					inter.diffuseColor[2] *= lightColor[2];
					inter.diffuseColor[3] *= lightColor[3];
					inter.vertexColor = surfaceStage->vertexColor;
					break;
				}
				case SL_SPECULAR: {
					// ignore stage that fails the condition
					if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
						break;
					}
					if ( inter.specularImage ) {
						VK_SubmitInteraction( &inter );
					}
					VK_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.specularImage,
											inter.specularMatrix, inter.specularColor.ToFloatPtr() );
					inter.specularColor[0] *= lightColor[0];
					inter.specularColor[1] *= lightColor[1];
					inter.specularColor[2] *= lightColor[2];
					inter.specularColor[3] *= lightColor[3];
					inter.vertexColor = surfaceStage->vertexColor;
					break;
				}
				case SL_INTERACTION: {
					if ( surfaceRegs[ surfaceStage->conditionRegister ] != 0.0f ) {
						static bool warnedUnsupportedPreyInteraction = false;
						if ( !warnedUnsupportedPreyInteraction ) {
							common->Warning( "Vulkan: custom Prey ARB interaction stages are unsupported; use renderer-gl" );
							warnedUnsupportedPreyInteraction = true;
						}
					}
					break;
				}
			}
		}

		// draw the final interaction
		VK_SubmitInteraction( &inter );

		// Quake 4's two shipped customLighting guide families are ambient
		// material stages that execute once for every active light stage.
		// They use their own named surface images but the same origins,
		// projection planes, light color, shadows, and additive state as the
		// standard interaction decomposition above.
		for ( int surfaceStageNum = 0 ; surfaceStageNum < surfaceStageCount ; surfaceStageNum++ ) {
			VK_DrawCustomLightingStage( surfaceShader->GetStage( surfaceStageNum ),
				surfaceRegs, lightColor, &inter );
		}
	}

	if ( polygonOffset ) {
		vkCmdSetDepthBiasEnable( interPass.cmd, VK_FALSE );
	}
}

/*
====================
VK_DrawInteractionChain
====================
*/
static void VK_DrawInteractionChain( const drawSurf_t *surf ) {
	for ( ; surf ; surf = surf->nextOnLight ) {
		VK_CreateSingleDrawInteractions( surf );
	}
}

/*
====================
VK_Interactions_DrawLights

The Phase F1 light loop (RB_ARB2_DrawInteractions skeleton): skip fog and
blend lights (fogging is a later phase) and lights with no interactions;
draw local then global interactions additively at depth EQUAL with depth
writes off, then translucent interactions at depth LESS. All lights draw
unshadowed — with the Phase E pipelines stencil-free this is exactly the
GL branch for lights without shadow surfaces.

Called from VK_GuiExecutor_Draw3DView between the depth fill and the
ambient walks; exits with depth bias off and the depth-range baseline
(maxDepth 1.0) restored.
====================
*/
void VK_Interactions_DrawLights( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->viewLights == NULL ) {
		return;
	}
	if ( r_skipInteractions.GetBool() ) {
		return;
	}

	VkCommandBuffer cmd = VK_Exec_ActiveCmd();
	if ( cmd == VK_NULL_HANDLE ) {
		return;
	}
	VkPipeline pipeline = VK_Exec_InteractionPipeline();
	if ( pipeline == VK_NULL_HANDLE ) {
		return;
	}

	memset( &interPass, 0, sizeof( interPass ) );
	interPass.viewDef = viewDef;
	interPass.cmd = cmd;
	interPass.slot = VK_Exec_ActiveFrameSlot();
	interPass.fbWidth = VK_Exec_ActiveFramebufferWidth();
	interPass.fbHeight = VK_Exec_ActiveFramebufferHeight();
	interPass.layout = VK_Exec_InteractionPipelineLayout();
	interPass.pipelineUnshadowed = pipeline;
	interPass.shadowSliceOffset = -1;

	// Phase G1: the stencil volume pipeline serves every shadow-casting
	// light the shadow-map path does not admit. Never create or use it for
	// a render target that has no stencil attachment.
	const bool activeTargetHasStencil = VK_Exec_ActiveTargetHasStencil();
	interPass.pipelineStencilShadow = activeTargetHasStencil
		? VK_Exec_StencilShadowPipeline() : VK_NULL_HANDLE;
	interPass.layoutStencilShadow = VK_Exec_BasePipelineLayout();

	// Phase F2a/F2b: classify + tile the view's shadow-map lights (CPU),
	// then render the atlas + point cubes in a frame-scope interruption
	// BEFORE any batch state is set (the resume path re-establishes the
	// executor baseline). Any mapped admission failure leaves the retained
	// volume available for the same-frame stencil path below.
	const bool stencilFallbackAvailable =
			activeTargetHasStencil &&
			interPass.pipelineStencilShadow != VK_NULL_HANDLE;
	if ( VK_ShadowMap_PrepareViewLights(
			viewDef, stencilFallbackAvailable ) > 0 ) {
		interPass.pipelineShadowed = VK_Exec_ShadowInteractionPipeline();
		interPass.pipelinePointShadowed = VK_Exec_PointShadowInteractionPipeline();
		interPass.layoutShadowed = VK_Exec_ShadowInteractionPipelineLayout();
		if ( interPass.layoutShadowed != VK_NULL_HANDLE
				&& ( interPass.pipelineShadowed != VK_NULL_HANDLE || interPass.pipelinePointShadowed != VK_NULL_HANDLE ) ) {
			if ( VK_ShadowMap_RenderAtlas( viewDef ) ) {
				interPass.shadowSetAtlas = VK_Exec_ShadowDescriptorSet();
				interPass.shadowPassPrepared = interPass.shadowSetAtlas != VK_NULL_HANDLE;
			}
		}
		if ( !interPass.shadowPassPrepared ) {
			// Phase F3: the prepared lights cannot be consumed this view
			// (missing pipelines/layout/descriptors). Drop their mapped state;
			// their retained volume remains available below.
			VK_ShadowMap_AbandonPreparedLights();
		}
	}

	// the specular table rides slot 0 for every draw (ARB2 binds it once
	// on unit 6); without a device-resident table the pass cannot draw
	interPass.specTableSet = VK_Exec_ImageDescriptor( globalImages->specularTableImage->GetDeviceHandle(), true );
	if ( interPass.specTableSet == VK_NULL_HANDLE ) {
		return;
	}

	// ambient lights: both GL paths sample the ambient normal-map cube and
	// decode rgb*2-1. R_AmbientNormalImage stores x in the alpha channel
	// (the red slot holds 255), so the decoded tangent-space constant is
	// (1, q(y), q(z)) with 8-bit quantization — reproduce it exactly
	interPass.ambientDir[ 0 ] = 1.0f;
	interPass.ambientDir[ 1 ] = ( (float)(byte)( 255 * tr.ambientLightVector[1] ) / 255.0f ) * 2.0f - 1.0f;
	interPass.ambientDir[ 2 ] = ( (float)(byte)( 255 * tr.ambientLightVector[2] ) / 255.0f ) * 2.0f - 1.0f;

	VK_DetermineLightScale();

	// GL bottom-left viewport -> Vulkan negative-height viewport (the same
	// baseline Draw3DView established; re-issued only for depth-range hacks)
	const int vpX = viewDef->viewport.x1;
	const int vpYGL = viewDef->viewport.y1;
	const int vpW = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int vpH = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	interPass.viewport.x = (float)vpX;
	interPass.viewport.y = (float)( interPass.fbHeight - vpYGL );
	interPass.viewport.width = (float)vpW;
	interPass.viewport.height = -(float)vpH;
	interPass.viewport.minDepth = 0.0f;
	interPass.viewport.maxDepth = 1.0f;

	// batch state: one pipeline (ONE/ONE additive), depth test on with
	// writes off (GLS_DEPTHMASK), bias off until a decal material needs it.
	// The viewport is issued unconditionally: the depth-fill walk may have
	// left a weapon depth-range (maxDepth 0.5) latched, and the pass's
	// tracking assumes the baseline at entry
	vkCmdSetViewport( cmd, 0, 1, &interPass.viewport );
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	vkCmdSetDepthTestEnable( cmd, VK_TRUE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );

	for ( viewLight_t *vLight = viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		// do fogging later
		if ( vLight->lightShader->IsFogLight() ) {
			continue;
		}
		if ( vLight->lightShader->IsBlendLight() ) {
			continue;
		}

		if ( !vLight->localInteractions && !vLight->globalInteractions
			&& !vLight->translucentInteractions ) {
			continue;
		}

		interPass.lightCount++;

		// Phase F2a/F2b ownership parity: localInteractions select the LOCAL
		// map (global casters only), while globalInteractions select the
		// GLOBAL map (global + local casters). A pass-specific allocation or
		// render failure selects the retained stencil fallback for the whole
		// light whenever volume geometry exists.
		const vkShadowLightState_t *shadowState = NULL;
		const vkShadowPassState_t *localShadowState = NULL;
		const vkShadowPassState_t *globalShadowState = NULL;
		if ( interPass.shadowPassPrepared ) {
			shadowState = VK_ShadowMap_LightState( vLight );
			if ( shadowState != NULL ) {
				localShadowState = VK_ShadowMap_PassState(
						shadowState, VK_SHADOW_RECEIVER_LOCAL );
				globalShadowState = VK_ShadowMap_PassState(
						shadowState, VK_SHADOW_RECEIVER_GLOBAL );

				const bool classReceiverReady = shadowState->pointLight
					? interPass.pipelinePointShadowed != VK_NULL_HANDLE
					: ( interPass.pipelineShadowed != VK_NULL_HANDLE
						&& interPass.shadowSetAtlas != VK_NULL_HANDLE );
				if ( !classReceiverReady ) {
					VK_ShadowMap_MarkStencilFallbackSticky( vLight );
					shadowState = NULL;
					localShadowState = NULL;
					globalShadowState = NULL;
				} else if ( shadowState->pointLight ) {
					// Point resources carry one descriptor per ownership
					// cube, so validate them independently.
					if ( localShadowState != NULL
							&& localShadowState->pointSet == VK_NULL_HANDLE ) {
						VK_ShadowMap_MarkStencilFallbackSticky( vLight );
						localShadowState = NULL;
					}
					if ( globalShadowState != NULL
							&& globalShadowState->pointSet == VK_NULL_HANDLE ) {
						VK_ShadowMap_MarkStencilFallbackSticky( vLight );
						globalShadowState = NULL;
					}
				}
			}
			if ( localShadowState != NULL || globalShadowState != NULL ) {
				interPass.shadowLightCount++;
				// F3 elision evidence: the front end skipped this light's
				// stencil volume generation entirely
				if ( vLight->globalShadows == NULL && vLight->localShadows == NULL ) {
					interPass.elidedLightCount++;
				}
			}
		}

		// Determine shadow need per retail ownership chain. A LOCAL receiver
		// only sees global casters; a GLOBAL receiver sees both. This matters
		// when a map resource failed or a target lacks stencil: receivers that
		// genuinely need unavailable shadow data must fail closed instead of
		// silently receiving an unshadowed light.
		const int incompleteMapMask =
				vLight->shadowMapIncompleteMapMask
				| vLight->shadowMapPrelightMapMissingMask;
		const int incompleteStencilMask =
				vLight->shadowMapIncompleteStencilMask
				| ( vLight->shadowMapPrelightStencilRequiredMask
					& ~vLight->shadowMapPrelightStencilReadyMask );
		const int hybridIncompleteMask =
				vLight->shadowMapHybridIncompleteMask
				| vLight->shadowMapPrelightMapMissingMask;
		const bool hasGlobalStencilSupplement =
				vLight->globalShadowMapStencilSupplements != NULL;
		const bool hasLocalStencilSupplement =
				vLight->localShadowMapStencilSupplements != NULL;
		const bool hasGlobalCasters = vLight->globalShadows != NULL
				|| vLight->globalShadowMapCasters != NULL
				|| vLight->globalShadowMapDynamicCasters != NULL;
		const bool hasLocalCasters = vLight->localShadows != NULL
				|| vLight->localShadowMapCasters != NULL
				|| vLight->localShadowMapDynamicCasters != NULL;
		const bool shadowingEnabled = r_shadows.GetBool()
				&& vLight->lightShader->LightCastsShadows()
				&& ( hasGlobalCasters || hasLocalCasters
					|| incompleteMapMask != 0 );
		const bool localReceiverNeedsShadow = shadowingEnabled
				&& vLight->localInteractions != NULL
				&& ( hasGlobalCasters
					|| ( incompleteMapMask
						& SHADOWMAP_RECEIVER_MASK_LOCAL ) != 0 );
		const bool globalReceiverNeedsShadow = shadowingEnabled
				&& vLight->globalInteractions != NULL
				&& ( hasGlobalCasters || hasLocalCasters
					|| ( incompleteMapMask
						& SHADOWMAP_RECEIVER_MASK_GLOBAL ) != 0 );
		const bool translucentReceiverNeedsShadow = shadowingEnabled
				&& vLight->translucentInteractions != NULL
				&& ( hasGlobalCasters || hasLocalCasters
					|| ( incompleteMapMask
						& SHADOWMAP_RECEIVER_MASK_GLOBAL ) != 0 )
				&& ( globalShadowState != NULL
					? r_shadowMapTranslucentReceivers.GetBool()
					: r_stencilTranslucentShadows.GetBool() );
		const bool localMapNeedsSupplement =
				localReceiverNeedsShadow &&
				localShadowState != NULL &&
				( incompleteMapMask &
					SHADOWMAP_RECEIVER_MASK_LOCAL ) != 0;
		const bool globalOpaqueMapNeedsSupplement =
				globalReceiverNeedsShadow &&
				globalShadowState != NULL &&
				( incompleteMapMask &
					SHADOWMAP_RECEIVER_MASK_GLOBAL ) != 0;
		const bool translucentMapNeedsSupplement =
				translucentReceiverNeedsShadow &&
				globalShadowState != NULL &&
				( incompleteMapMask &
					SHADOWMAP_RECEIVER_MASK_GLOBAL ) != 0;
		const bool localReceiverNeedsFallback =
				localReceiverNeedsShadow &&
				localShadowState == NULL;
		const bool globalOpaqueReceiverNeedsFallback =
				globalReceiverNeedsShadow &&
				globalShadowState == NULL;
		const bool translucentReceiverNeedsFallback =
				translucentReceiverNeedsShadow &&
				globalShadowState == NULL;
		const bool globalReceiverNeedsFallback =
				globalOpaqueReceiverNeedsFallback ||
				translucentReceiverNeedsFallback;
		const bool localReceiverNeedsStencil =
				localReceiverNeedsFallback ||
				localMapNeedsSupplement;
		const bool globalOpaqueReceiverNeedsStencil =
				globalOpaqueReceiverNeedsFallback ||
				globalOpaqueMapNeedsSupplement;
		const bool translucentReceiverNeedsStencil =
				translucentReceiverNeedsFallback ||
				translucentMapNeedsSupplement;
		const bool globalReceiverNeedsStencil =
				globalOpaqueReceiverNeedsStencil ||
				translucentReceiverNeedsStencil;
		const bool missingRequiredShadow =
				localReceiverNeedsStencil ||
				globalReceiverNeedsStencil;
		const bool localStencilOwnershipComplete =
				localMapNeedsSupplement
					? ( ( hybridIncompleteMask &
							SHADOWMAP_RECEIVER_MASK_LOCAL ) == 0
						&& hasGlobalStencilSupplement )
					: ( ( incompleteStencilMask &
							SHADOWMAP_RECEIVER_MASK_LOCAL ) == 0 );
		const bool globalStencilOwnershipComplete =
				( globalOpaqueMapNeedsSupplement ||
					translucentMapNeedsSupplement )
					? ( ( hybridIncompleteMask &
							SHADOWMAP_RECEIVER_MASK_GLOBAL ) == 0
						&& ( hasGlobalStencilSupplement ||
							hasLocalStencilSupplement ) )
					: ( ( incompleteStencilMask &
							SHADOWMAP_RECEIVER_MASK_GLOBAL ) == 0 );
		const bool localEmptyFallback =
				localReceiverNeedsFallback &&
				localStencilOwnershipComplete &&
				vLight->globalShadows == NULL;
		const bool globalEmptyFallback =
				globalReceiverNeedsFallback &&
				globalStencilOwnershipComplete &&
				vLight->globalShadows == NULL &&
				vLight->localShadows == NULL;
		const bool stencilResourcesReady =
				activeTargetHasStencil &&
				interPass.pipelineStencilShadow != VK_NULL_HANDLE;
		const bool localStencilFallback =
				localReceiverNeedsStencil &&
				!localEmptyFallback &&
				localStencilOwnershipComplete &&
				stencilResourcesReady;
		const bool globalStencilFallback =
				globalReceiverNeedsStencil &&
				!globalEmptyFallback &&
				globalStencilOwnershipComplete &&
				stencilResourcesReady;
		const bool anyStencilFallback =
				localStencilFallback ||
				globalStencilFallback;
		const bool unresolvedBeforeSubmit =
				( localReceiverNeedsStencil &&
					!localEmptyFallback &&
					!localStencilFallback )
				|| ( globalReceiverNeedsStencil &&
					!globalEmptyFallback &&
					!globalStencilFallback );
		if ( missingRequiredShadow && unresolvedBeforeSubmit ) {
			static bool warnedFailClosedShadow = false;
			if ( !warnedFailClosedShadow ) {
				warnedFailClosedShadow = true;
				common->Warning( "Vulkan: required shadow resource unavailable; affected light receivers are skipped fail-closed" );
			}
		}

		bool globalOpaqueReceiverDrawn = false;
		bool localReceiverDrewWithStencil = false;
		bool globalStencilPassComplete = false;
		if ( anyStencilFallback ) {
			interPass.stencilLightCount++;

			// LOCAL and GLOBAL can independently require a full fallback or a
			// map supplement. Clear and rebuild stencil per receiver so a
			// supplement-only chain can never be contaminated by full-volume
			// casters that are already represented in the filtered map.
			if ( localStencilFallback ) {
				VK_Inter_SelectShadowMode( NULL, NULL );
				VK_Inter_StencilClear( vLight );
				vkCmdSetStencilTestEnable( cmd, VK_TRUE );
				vkCmdSetStencilOp( cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
						VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
						VK_STENCIL_OP_KEEP, VK_COMPARE_OP_GREATER_OR_EQUAL );
				vkCmdSetStencilCompareMask( cmd,
						VK_STENCIL_FACE_FRONT_AND_BACK, 255 );
				vkCmdSetStencilWriteMask( cmd,
						VK_STENCIL_FACE_FRONT_AND_BACK, 255 );
				vkCmdSetStencilReference( cmd,
						VK_STENCIL_FACE_FRONT_AND_BACK, 128 );
				const drawSurf_t *localGlobalVolumes =
						localMapNeedsSupplement
							? vLight->globalShadowMapStencilSupplements
							: vLight->globalShadows;
				const bool localVolumePassComplete =
						VK_StencilShadowPass( localGlobalVolumes );
				vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
						interPass.pipelineUnshadowed );
				vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_EQUAL );
				if ( localVolumePassComplete ) {
					VK_Inter_SelectShadowMode(
						localMapNeedsSupplement ? shadowState : NULL,
						localMapNeedsSupplement ? localShadowState : NULL );
					vkCmdSetStencilTestEnable( cmd, VK_TRUE );
					VK_DrawInteractionChain( vLight->localInteractions );
					localReceiverDrewWithStencil = true;
				}
			}

			if ( !localReceiverDrewWithStencil &&
					( localEmptyFallback ||
						!localReceiverNeedsStencil ) ) {
				vkCmdSetStencilTestEnable( cmd, VK_FALSE );
				VK_Inter_SelectShadowMode(
					localEmptyFallback ? NULL : shadowState,
					localEmptyFallback ? NULL : localShadowState );
				VK_DrawInteractionChain( vLight->localInteractions );
			}

			if ( globalStencilFallback ) {
				VK_Inter_SelectShadowMode( NULL, NULL );
				VK_Inter_StencilClear( vLight );
				vkCmdSetStencilTestEnable( cmd, VK_TRUE );
				vkCmdSetStencilOp( cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
						VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
						VK_STENCIL_OP_KEEP, VK_COMPARE_OP_GREATER_OR_EQUAL );
				vkCmdSetStencilCompareMask( cmd,
						VK_STENCIL_FACE_FRONT_AND_BACK, 255 );
				vkCmdSetStencilWriteMask( cmd,
						VK_STENCIL_FACE_FRONT_AND_BACK, 255 );
				vkCmdSetStencilReference( cmd,
						VK_STENCIL_FACE_FRONT_AND_BACK, 128 );
				const drawSurf_t *globalGlobalVolumes =
						( globalOpaqueMapNeedsSupplement ||
							translucentMapNeedsSupplement )
							? vLight->globalShadowMapStencilSupplements
							: vLight->globalShadows;
				const drawSurf_t *globalLocalVolumes =
						( globalOpaqueMapNeedsSupplement ||
							translucentMapNeedsSupplement )
							? vLight->localShadowMapStencilSupplements
							: vLight->localShadows;
				const bool globalVolumePassComplete =
						VK_StencilShadowPass( globalGlobalVolumes );
				const bool localVolumePassComplete =
						VK_StencilShadowPass( globalLocalVolumes );
				vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
						interPass.pipelineUnshadowed );
				vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_EQUAL );
				globalStencilPassComplete =
						globalVolumePassComplete &&
						localVolumePassComplete;
				if ( globalOpaqueReceiverNeedsStencil &&
						globalStencilPassComplete ) {
					VK_Inter_SelectShadowMode(
						globalOpaqueMapNeedsSupplement
							? shadowState : NULL,
						globalOpaqueMapNeedsSupplement
							? globalShadowState : NULL );
					vkCmdSetStencilTestEnable( cmd, VK_TRUE );
					VK_DrawInteractionChain( vLight->globalInteractions );
					globalOpaqueReceiverDrawn = true;
				}
			}

			// A late geometry bind/stream failure can invalidate only the
			// ownership accumulated so far. Never consume a partial stencil
			// buffer: keep any already-complete receiver draw, then use a
			// valid ownership map, an independently proven empty fallback, or
			// skip only the affected receiver chain fail-closed.
			vkCmdSetStencilTestEnable( cmd, VK_FALSE );
			if ( !globalOpaqueReceiverDrawn &&
					( globalEmptyFallback ||
						!globalOpaqueReceiverNeedsStencil ) ) {
				VK_Inter_SelectShadowMode(
					globalEmptyFallback ? NULL : shadowState,
					globalEmptyFallback ? NULL : globalShadowState );
				VK_DrawInteractionChain( vLight->globalInteractions );
				globalOpaqueReceiverDrawn = true;
			}

			const bool runtimeMissingRequiredShadow =
					( localStencilFallback &&
						!localReceiverDrewWithStencil )
					|| ( globalStencilFallback &&
						!globalStencilPassComplete );
			if ( runtimeMissingRequiredShadow ) {
				static bool warnedIncompleteStencilSubmission = false;
				if ( !warnedIncompleteStencilSubmission ) {
					warnedIncompleteStencilSubmission = true;
					common->Warning( "Vulkan: stencil shadow volume submission incomplete; affected light receivers are skipped fail-closed" );
				}
			}
		} else {
			// Opaque interactions test EQUAL against the depth fill. Select
			// each ownership map immediately before its receiver chain so a
			// shared model space still receives the pass-specific atlas rect
			// or point-cube descriptor.
			vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_EQUAL );
			if ( localEmptyFallback ||
					!localReceiverNeedsStencil ) {
				VK_Inter_SelectShadowMode(
					localEmptyFallback ? NULL : shadowState,
					localEmptyFallback ? NULL : localShadowState );
				VK_DrawInteractionChain( vLight->localInteractions );
			}
			if ( globalEmptyFallback ||
					!globalOpaqueReceiverNeedsStencil ) {
				VK_Inter_SelectShadowMode(
					globalEmptyFallback ? NULL : shadowState,
					globalEmptyFallback ? NULL : globalShadowState );
				VK_DrawInteractionChain( vLight->globalInteractions );
				globalOpaqueReceiverDrawn = true;
			}
		}

		if ( !r_skipTranslucent.GetBool() ) {
			const bool translucentUsesStencilFallback =
					translucentReceiverNeedsStencil &&
					globalStencilFallback &&
					globalStencilPassComplete;
			const bool translucentUsesEmptyFallback =
					translucentReceiverNeedsFallback &&
					globalEmptyFallback;
			const bool drawTranslucentReceiver =
					!translucentReceiverNeedsStencil ||
					translucentUsesStencilFallback ||
					translucentUsesEmptyFallback;
			// Translucent receivers keep the GLOBAL pass state, matching the
			// GL pair where the global opaque pass is the last mapped state.
			if ( translucentUsesStencilFallback ) {
				VK_Inter_SelectShadowMode(
					translucentMapNeedsSupplement
						? shadowState : NULL,
					translucentMapNeedsSupplement
						? globalShadowState : NULL );
				vkCmdSetStencilTestEnable( cmd, VK_TRUE );
			} else if ( drawTranslucentReceiver &&
					translucentReceiverNeedsShadow &&
					globalShadowState != NULL ) {
				VK_Inter_SelectShadowMode( shadowState, globalShadowState );
			} else {
				VK_Inter_SelectShadowMode( NULL, NULL );
				vkCmdSetStencilTestEnable( cmd, VK_FALSE );
			}
			// GLS_DEPTHFUNC_LESS maps to glDepthFunc(GL_LEQUAL)
			vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
			if ( drawTranslucentReceiver ) {
				VK_DrawInteractionChain( vLight->translucentInteractions );
			}
		}

		// stencil reset: the next light (and the ambient walks) start
		// stencil-free
		if ( anyStencilFallback ) {
			vkCmdSetStencilTestEnable( cmd, VK_FALSE );
		}
	}

	// restore the depth-range baseline for the ambient walks
	if ( interPass.weaponDepthRange ) {
		interPass.viewport.maxDepth = 1.0f;
		vkCmdSetViewport( cmd, 0, 1, &interPass.viewport );
	}

	// one-shot bring-up evidence that the interaction pass emitted real work
	static bool loggedFirstInteractionPass = false;
	if ( !loggedFirstInteractionPass && interPass.drawCount > 0 ) {
		loggedFirstInteractionPass = true;
		common->Printf( "Vulkan: first interaction pass drew %d interactions across %d lights\n",
				interPass.drawCount, interPass.lightCount );
	}

	// one-shot bring-up evidence that shadow-receiving interactions drew
	static bool loggedFirstShadowReceivers = false;
	if ( !loggedFirstShadowReceivers && interPass.shadowDrawCount > 0 ) {
		loggedFirstShadowReceivers = true;
		common->Printf( "Vulkan: first shadow-receiving interaction pass drew %d shadowed interactions across %d shadow lights\n",
				interPass.shadowDrawCount, interPass.shadowLightCount );
	}

	// one-shot bring-up evidence that stencil shadow volumes drew (Phase G1)
	static bool loggedFirstStencilShadowPass = false;
	if ( !loggedFirstStencilShadowPass && interPass.volumeDrawCount > 0 ) {
		loggedFirstStencilShadowPass = true;
		common->Printf( "Vulkan: first stencil shadow pass: %d volumes (%d preload), %d lights\n",
				interPass.volumeDrawCount, interPass.volumePreloadCount, interPass.stencilLightCount );
		if ( interPass.volumeSkipCount > 0 ) {
			common->Printf( "Vulkan: stencil shadow pass skipped %d prim-batch/cache-less volumes\n",
					interPass.volumeSkipCount );
		}
	}

	// one-shot bring-up evidence the front end elided stencil volume
	// generation for shadow-mapped lights (Phase F3
	// RB_ShadowMapResourcesKnownGood honesty)
	static bool loggedFirstElision = false;
	if ( !loggedFirstElision && interPass.elidedLightCount > 0 ) {
		loggedFirstElision = true;
		common->Printf( "Vulkan: stencil elision active: %d of %d shadow-mapped lights carry no stencil volumes\n",
				interPass.elidedLightCount, interPass.shadowLightCount );
	}
}

/*
===============================================================================

	Phase G2: fog and blend lights.

===============================================================================
*/

// the fog texgen plane scratch (indexed by fogPlaneIndex_t) is extern in
// tr_local.h and owned by draw_common.cpp, which the vk module build
// excludes — the module's definition lives here
idPlane fogTexGenPlanes[4];

/*
====================
VK_FogDistanceScale

Port of RB_FogDistanceScale (draw_common.cpp:240): fog alphas up to 1.0
select the default 500-unit ramp; larger alphas ARE the fog distance.
====================
*/
static float VK_FogDistanceScale( float alpha ) {
	if ( alpha <= 1.0f ) {
		return -0.5f / DEFAULT_FOG_DISTANCE;
	}
	return -0.5f / alpha;
}

/*
====================
VK_T_BasicFog

Port of RB_T_BasicFog (draw_common.cpp:7593): on space change the four
texgen rows localize (tex0 S gains the +0.5 center bias, tex0 T is forced
to the constant 0.5 row in-shader, tex1 T gains +FOG_ENTER, tex1 S — the
zero-normal viewer-distance plane — transforms to itself) and ride the
128B push block alongside the MVP and fog color; the push persists across
the space's surfaces exactly like GL's latched texgen planes.

Documented Phase G2 gaps (mirroring the interaction walk):
- MD5R packed prim-batch surfaces (RB_ARB2_MD5R_DrawBasicFog) skip with a
  counter; the packed vertex-program path is Phase I.
- the shadowCache vertex fallback for cache-less light tris
  (draw_common.cpp:7491-7494 is shared with RB_T_BlendLight) has no vk
  analog; fog/blend chains carry ambient-cached tris in practice.
====================
*/
static void VK_T_BasicFog( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = surf->geo;

	if ( tri == NULL || R_TriHasPrimBatchMesh( tri ) || tri->ambientCache == NULL ) {
		interPass.fogSkipCount++;
		return;
	}
	if ( tri->numIndexes <= 0 || tri->indexes == NULL ) {
		return;
	}

	if ( !VK_Exec_BindTriGeometry( interPass.cmd, interPass.slot, tri ) ) {
		return;
	}
	VK_Exec_SetSurfScissor( interPass.cmd, interPass.viewDef, surf, interPass.fbHeight );

	// space change: MVP (depth hacks included) + weapon depth-range + the
	// localized fog planes into the push block
	if ( surf->space != interPass.currentSpace ) {
		interPass.currentSpace = surf->space;
		VK_BuildSurfMVP( interPass.viewDef, surf, interPass.mvp );
		const bool wantWeaponRange = surf->space->weaponDepthHack;
		if ( wantWeaponRange != interPass.weaponDepthRange ) {
			interPass.weaponDepthRange = wantWeaponRange;
			interPass.viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;
			vkCmdSetViewport( interPass.cmd, 0, 1, &interPass.viewport );
		}

		vkInteractionPush_t push;
		memset( &push, 0, sizeof( push ) );
		memcpy( push.mvp, interPass.mvp, sizeof( push.mvp ) );

		idPlane local;
		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_DISTANCE_PLANE_S], local );
		local[3] += 0.5f;
		memcpy( push.a, local.ToFloatPtr(), sizeof( push.a ) );

		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_ENTER_PLANE_T], local );
		local[3] += FOG_ENTER;
		memcpy( push.b, local.ToFloatPtr(), sizeof( push.b ) );

		// constant per viewer: the zero-normal plane localizes to itself
		R_GlobalPlaneToLocal( surf->space->modelMatrix, fogTexGenPlanes[FOG_ENTER_PLANE_S], local );
		memcpy( push.c, local.ToFloatPtr(), sizeof( push.c ) );

		memcpy( push.d, interPass.fogColor, sizeof( push.d ) );

		vkCmdPushConstants( interPass.cmd, interPass.layoutFogBlend,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );
	}

	vkCmdDrawIndexed( interPass.cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );
	interPass.fogDrawCount++;
}

/*
====================
VK_FogPass

Port of RB_FogPass (draw_common.cpp:7633): stage-0 fog color and density,
the view-space eye-depth S plane + scaled fogPlane texgen rows, then the
light's interaction chains at depth EQUAL followed by the frustumTris cap
at LEQUAL with back-sided cull under the full view scissor. The GL
fixed-function hardening (programs off, color arrays off) is inherent
here — the fog pipeline reads position only.
====================
*/
static void VK_FogPass( const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2 ) {
	const viewLight_t *vLight = backEnd.vLight;
	drawSurf_t ds;

	// create a surface for the light frustum triangles, which are oriented
	// drawn side out; if we ran out of vertex cache memory, skip it
	const srfTriangles_t *frustumTris = vLight->frustumTris;
	if ( frustumTris == NULL || frustumTris->ambientCache == NULL ) {
		return;
	}
	memset( &ds, 0, sizeof( ds ) );
	ds.space = &backEnd.viewDef->worldSpace;
	ds.geo = frustumTris;
	ds.scissorRect = backEnd.viewDef->scissor;

	// find the current color and density of the fog; assume fog shaders
	// have only a single stage
	const idMaterial *lightShader = vLight->lightShader;
	const float *regs = vLight->shaderRegisters;
	const shaderStage_t *stage = lightShader->GetStage( 0 );

	// glColor3fv: RGB from the stage registers, alpha pins to 1
	interPass.fogColor[0] = regs[ stage->color.registers[0] ];
	interPass.fogColor[1] = regs[ stage->color.registers[1] ];
	interPass.fogColor[2] = regs[ stage->color.registers[2] ];
	interPass.fogColor[3] = 1.0f;

	// calculate the falloff planes
	const float a = VK_FogDistanceScale( regs[ stage->color.registers[3] ] );

	// tex0 S: eye depth off the view-space Z row (T is the constant 0.5
	// row in-shader; the GL FOG_DISTANCE_PLANE_T is computed but always
	// overridden per surface, so it is not carried)
	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[2];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[6];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[10];
	fogTexGenPlanes[FOG_DISTANCE_PLANE_S][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[14];

	// tex1 T: the fade plane, scaled so one texel ~ 1000 units
	fogTexGenPlanes[FOG_ENTER_PLANE_T][0] = 0.001f * vLight->fogPlane[0];
	fogTexGenPlanes[FOG_ENTER_PLANE_T][1] = 0.001f * vLight->fogPlane[1];
	fogTexGenPlanes[FOG_ENTER_PLANE_T][2] = 0.001f * vLight->fogPlane[2];
	fogTexGenPlanes[FOG_ENTER_PLANE_T][3] = 0.001f * vLight->fogPlane[3];

	// tex1 S is based on the view origin
	const float s = backEnd.viewDef->renderView.vieworg * fogTexGenPlanes[FOG_ENTER_PLANE_T].Normal()
		+ fogTexGenPlanes[FOG_ENTER_PLANE_T][3];

	fogTexGenPlanes[FOG_ENTER_PLANE_S][0] = 0;
	fogTexGenPlanes[FOG_ENTER_PLANE_S][1] = 0;
	fogTexGenPlanes[FOG_ENTER_PLANE_S][2] = 0;
	fogTexGenPlanes[FOG_ENTER_PLANE_S][3] = FOG_ENTER + s;

	// texture 0 = _fog, texture 1 = _fogEnter
	VkDescriptorSet sets[ 2 ];
	sets[ 0 ] = VK_Exec_ImageDescriptor( globalImages->fogImage->GetDeviceHandle(), true );
	sets[ 1 ] = VK_Exec_ImageDescriptor( globalImages->fogEnterImage->GetDeviceHandle(), true );
	if ( sets[ 0 ] == VK_NULL_HANDLE || sets[ 1 ] == VK_NULL_HANDLE ) {
		return;
	}

	VkCommandBuffer cmd = interPass.cmd;
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, interPass.pipelineFog );
	vkCmdBindDescriptorSets( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, interPass.layoutFogBlend,
			0, 2, sets, 0, NULL );

	// GLS_DEPTHMASK | SRC_ALPHA/ONE_MINUS blend | GLS_DEPTHFUNC_EQUAL;
	// the chains draw under the CT_FRONT_SIDED baseline
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_EQUAL );
	vkCmdSetCullMode( cmd, interPass.viewDef->isMirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT );

	// draw it: global then local chains (each chain walk resets the space
	// tracking exactly like RB_RenderDrawSurfChainWithFunction)
	interPass.currentSpace = NULL;
	for ( const drawSurf_t *surf = drawSurfs ; surf ; surf = surf->nextOnLight ) {
		VK_T_BasicFog( surf );
	}
	interPass.currentSpace = NULL;
	for ( const drawSurf_t *surf = drawSurfs2 ; surf ; surf = surf->nextOnLight ) {
		VK_T_BasicFog( surf );
	}

	// the light frustum bounding planes aren't in the depth buffer, so use
	// depthfunc_less (GLS_DEPTHFUNC_LESS -> glDepthFunc(GL_LEQUAL)) instead
	// of depthfunc_equal, and CT_BACK_SIDED cull with the mirror swap
	vkCmdSetDepthCompareOp( cmd, VK_COMPARE_OP_LESS_OR_EQUAL );
	vkCmdSetCullMode( cmd, interPass.viewDef->isMirror ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT );
	interPass.currentSpace = NULL;
	VK_T_BasicFog( &ds );
	vkCmdSetCullMode( cmd, interPass.viewDef->isMirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT );
}

/*
====================
VK_T_BlendLight

Port of RB_T_BlendLight (draw_common.cpp:7462): on space change the four
lightProject planes localize (with the stage texture matrix folded into
S/T via RB_BakeTextureMatrixIntoTexgen — the vk equivalent of GL's texture
matrix) and stream with the stage color as a set-2 ring slice; the MVP
rides the push block. Shares VK_T_BasicFog's prim-batch/cache-less gaps.
====================
*/
static void VK_T_BlendLight( const drawSurf_t *surf ) {
	const srfTriangles_t *tri = surf->geo;

	if ( tri == NULL || R_TriHasPrimBatchMesh( tri ) || tri->ambientCache == NULL ) {
		interPass.fogSkipCount++;
		return;
	}
	if ( tri->numIndexes <= 0 || tri->indexes == NULL ) {
		return;
	}

	if ( !VK_Exec_BindTriGeometry( interPass.cmd, interPass.slot, tri ) ) {
		return;
	}
	VK_Exec_SetSurfScissor( interPass.cmd, interPass.viewDef, surf, interPass.fbHeight );

	if ( surf->space != interPass.currentSpace ) {
		interPass.currentSpace = surf->space;
		VK_BuildSurfMVP( interPass.viewDef, surf, interPass.mvp );
		const bool wantWeaponRange = surf->space->weaponDepthHack;
		if ( wantWeaponRange != interPass.weaponDepthRange ) {
			interPass.weaponDepthRange = wantWeaponRange;
			interPass.viewport.maxDepth = wantWeaponRange ? 0.5f : 1.0f;
			vkCmdSetViewport( interPass.cmd, 0, 1, &interPass.viewport );
		}

		idPlane lightProject[4];
		for ( int i = 0 ; i < 4 ; i++ ) {
			R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i] );
		}
		if ( interPass.blendTextureMatrix != NULL ) {
			RB_BakeTextureMatrixIntoTexgen( lightProject, interPass.blendTextureMatrix );
		}

		vkBlendLightBlock_t block;
		memset( &block, 0, sizeof( block ) );
		memcpy( block.lightProjectS, lightProject[0].ToFloatPtr(), sizeof( block.lightProjectS ) );
		memcpy( block.lightProjectT, lightProject[1].ToFloatPtr(), sizeof( block.lightProjectT ) );
		memcpy( block.lightProjectQ, lightProject[2].ToFloatPtr(), sizeof( block.lightProjectQ ) );
		memcpy( block.lightFalloffS, lightProject[3].ToFloatPtr(), sizeof( block.lightFalloffS ) );
		memcpy( block.color, interPass.blendColor, sizeof( block.color ) );
		interPass.blendSliceOffset = VK_Exec_InteractionUniformAlloc( &block, sizeof( block ) );

		vkInteractionPush_t push;
		memset( &push, 0, sizeof( push ) );
		memcpy( push.mvp, interPass.mvp, sizeof( push.mvp ) );
		vkCmdPushConstants( interPass.cmd, interPass.layoutFogBlend,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( push ), &push );

		// rebind set 2 at the space's slice (a ring overflow skips the
		// space's draws)
		if ( interPass.blendSliceOffset >= 0 ) {
			VkDescriptorSet uniformSet = VK_Exec_InteractionUniformSet();
			uint32_t dynamicOffset = (uint32_t)interPass.blendSliceOffset;
			if ( uniformSet != VK_NULL_HANDLE ) {
				vkCmdBindDescriptorSets( interPass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
						interPass.layoutFogBlend, 2, 1, &uniformSet, 1, &dynamicOffset );
			} else {
				interPass.blendSliceOffset = -1;
			}
		}
	}
	if ( interPass.blendSliceOffset < 0 ) {
		return;
	}

	vkCmdDrawIndexed( interPass.cmd, (uint32_t)tri->numIndexes, 1, 0, 0, 0 );
	interPass.blendDrawCount++;
}

/*
====================
VK_BlendLight

Port of RB_BlendLight (draw_common.cpp:7508): every light-shader stage
(condition-register gated) projects its texture through lightProject[0..2]
with the falloff through lightProject[3], modulated by the stage's RGBA
color and blended by the stage's blend keyword at depth EQUAL with writes
off. GL's empty-chain early-out checks the FIRST list only — preserved.
Stage alpha-test bits have no analog here (unused by light materials).
====================
*/
static void VK_BlendLight( const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2 ) {
	const viewLight_t *vLight = backEnd.vLight;
	float textureMatrix[16];

	if ( !drawSurfs ) {
		return;
	}
	if ( r_skipBlendLights.GetBool() ) {
		return;
	}
	if ( vLight->falloffImage == NULL ) {
		return;
	}

	// texture 1 gets the falloff texture (T pins to 0.5 in-shader)
	VkDescriptorSet falloffSet = VK_Exec_ImageDescriptor( vLight->falloffImage->GetDeviceHandle(), true );
	if ( falloffSet == VK_NULL_HANDLE ) {
		return;
	}

	const idMaterial *lightShader = vLight->lightShader;
	const float *regs = vLight->shaderRegisters;

	const int lightStageCount = lightShader->GetNumStages();
	for ( int i = 0 ; i < lightStageCount ; i++ ) {
		const shaderStage_t *stage = lightShader->GetStage(i);

		if ( !regs[ stage->conditionRegister ] ) {
			continue;
		}
		if ( stage->texture.image == NULL ) {
			continue;
		}

		// GL_State( GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL ):
		// the stage blend keyword selects the pipeline; depth writes off +
		// EQUAL ride the dynamic state (re-asserted per stage — a fog cap
		// may have left LEQUAL latched)
		VkPipeline pipeline = VK_Exec_BlendLightPipeline( stage->drawStateBits );
		VkDescriptorSet sets[ 2 ];
		sets[ 0 ] = VK_Exec_ImageDescriptor( stage->texture.image->GetDeviceHandle(), true );
		sets[ 1 ] = falloffSet;
		if ( pipeline == VK_NULL_HANDLE || sets[ 0 ] == VK_NULL_HANDLE ) {
			continue;
		}

		vkCmdBindPipeline( interPass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
		vkCmdBindDescriptorSets( interPass.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, interPass.layoutFogBlend,
				0, 2, sets, 0, NULL );
		vkCmdSetDepthCompareOp( interPass.cmd, VK_COMPARE_OP_EQUAL );
		// blend lights draw under the CT_FRONT_SIDED baseline
		vkCmdSetCullMode( interPass.cmd, interPass.viewDef->isMirror ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT );

		if ( stage->texture.hasMatrix ) {
			RB_GetShaderTextureMatrix( regs, &stage->texture, textureMatrix );
			interPass.blendTextureMatrix = textureMatrix;
		} else {
			interPass.blendTextureMatrix = NULL;
		}

		// get the modulate values from the light, including alpha, unlike
		// normal lights
		interPass.blendColor[0] = regs[ stage->color.registers[0] ];
		interPass.blendColor[1] = regs[ stage->color.registers[1] ];
		interPass.blendColor[2] = regs[ stage->color.registers[2] ];
		interPass.blendColor[3] = regs[ stage->color.registers[3] ];

		interPass.currentSpace = NULL;
		interPass.blendSliceOffset = -1;
		for ( const drawSurf_t *surf = drawSurfs ; surf ; surf = surf->nextOnLight ) {
			VK_T_BlendLight( surf );
		}
		interPass.currentSpace = NULL;
		interPass.blendSliceOffset = -1;
		for ( const drawSurf_t *surf = drawSurfs2 ; surf ; surf = surf->nextOnLight ) {
			VK_T_BlendLight( surf );
		}
	}
}

/*
====================
VK_Fog_DrawAllLights

Port of RB_STD_FogAllLights (draw_common.cpp:7762): fog and blend lights
draw between the two ambient walks (RB_STD_DrawView:9806), over the
lights' interaction chains only — translucentInteractions are never
fogged. Stencil stays disabled for the whole pass (GL :7773; the stencil
buffer still carries the volumes' 128 baseline and must not gate fog);
the D3XP-disabled anti-double-fog stencil guard (GL :7782-7805 in #if 0)
is not ported.

Called from VK_GuiExecutor_Draw3DView between the ambient walks; exits
with depth bias off and the depth-range baseline (maxDepth 1.0) restored.
====================
*/
void VK_Fog_DrawAllLights( const viewDef_t *viewDef ) {
	if ( viewDef == NULL || viewDef->viewLights == NULL ) {
		return;
	}
	if ( r_skipFogLights.GetBool() || r_showOverDraw.GetInteger() != 0
		 || viewDef->isXraySubview /* dont fog in xray mode*/
		 ) {
		return;
	}

	// keep the common no-fog view zero-cost: no state is touched unless a
	// fog or blend light exists
	viewLight_t *vLight;
	for ( vLight = viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		if ( vLight->lightShader->IsFogLight() || vLight->lightShader->IsBlendLight() ) {
			break;
		}
	}
	if ( vLight == NULL ) {
		return;
	}

	VkCommandBuffer cmd = VK_Exec_ActiveCmd();
	if ( cmd == VK_NULL_HANDLE ) {
		return;
	}

	memset( &interPass, 0, sizeof( interPass ) );
	interPass.viewDef = viewDef;
	interPass.cmd = cmd;
	interPass.slot = VK_Exec_ActiveFrameSlot();
	interPass.fbWidth = VK_Exec_ActiveFramebufferWidth();
	interPass.fbHeight = VK_Exec_ActiveFramebufferHeight();
	interPass.layoutFogBlend = VK_Exec_FogBlendPipelineLayout();
	interPass.pipelineFog = VK_Exec_FogPipeline();
	interPass.blendSliceOffset = -1;
	if ( interPass.layoutFogBlend == VK_NULL_HANDLE ) {
		return;
	}

	// GL bottom-left viewport -> Vulkan negative-height viewport, issued
	// unconditionally: the pre-fog ambient walk may have left a weapon
	// depth-range (maxDepth 0.5) latched (the interaction-pass convention)
	const int vpX = viewDef->viewport.x1;
	const int vpYGL = viewDef->viewport.y1;
	const int vpW = viewDef->viewport.x2 - viewDef->viewport.x1 + 1;
	const int vpH = viewDef->viewport.y2 - viewDef->viewport.y1 + 1;
	interPass.viewport.x = (float)vpX;
	interPass.viewport.y = (float)( interPass.fbHeight - vpYGL );
	interPass.viewport.width = (float)vpW;
	interPass.viewport.height = -(float)vpH;
	interPass.viewport.minDepth = 0.0f;
	interPass.viewport.maxDepth = 1.0f;
	vkCmdSetViewport( cmd, 0, 1, &interPass.viewport );

	// batch state: depth test on with writes off (GLS_DEPTHMASK); stencil
	// disabled for the whole pass; bias off
	vkCmdSetDepthTestEnable( cmd, VK_TRUE );
	vkCmdSetDepthWriteEnable( cmd, VK_FALSE );
	vkCmdSetDepthBiasEnable( cmd, VK_FALSE );
	vkCmdSetStencilTestEnable( cmd, VK_FALSE );
	vkCmdSetFrontFace( cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE );

	for ( vLight = viewDef->viewLights ; vLight ; vLight = vLight->next ) {
		backEnd.vLight = vLight;

		if ( !vLight->lightShader->IsFogLight() && !vLight->lightShader->IsBlendLight() ) {
			continue;
		}

		if ( vLight->lightShader->IsFogLight() ) {
			if ( interPass.pipelineFog == VK_NULL_HANDLE ) {
				continue;
			}
			interPass.fogLightCount++;
			VK_FogPass( vLight->globalInteractions, vLight->localInteractions );
		} else if ( vLight->lightShader->IsBlendLight() ) {
			interPass.blendLightCount++;
			VK_BlendLight( vLight->globalInteractions, vLight->localInteractions );
		}
	}

	// restore the depth-range baseline for the post-fog ambient walk
	if ( interPass.weaponDepthRange ) {
		interPass.viewport.maxDepth = 1.0f;
		vkCmdSetViewport( cmd, 0, 1, &interPass.viewport );
	}

	// one-shot bring-up evidence that the fog/blend pass emitted real work
	static bool loggedFirstFogPass = false;
	if ( !loggedFirstFogPass && ( interPass.fogDrawCount > 0 || interPass.blendDrawCount > 0 ) ) {
		loggedFirstFogPass = true;
		common->Printf( "Vulkan: first fog/blend pass: %d fog, %d blend lights\n",
				interPass.fogLightCount, interPass.blendLightCount );
		if ( interPass.fogSkipCount > 0 ) {
			common->Printf( "Vulkan: fog/blend pass skipped %d prim-batch/cache-less surfaces\n",
					interPass.fogSkipCount );
		}
	}
}

#endif /* OPENQ4_RENDERER_VK_MODULE */
