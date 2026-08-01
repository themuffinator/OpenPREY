/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

===========================================================================
*/

#include "tr_local.h"
#include "Model_local.h"

static void R_InitPreyBeamVert( idDrawVert &vert, float s, float t ) {
	vert.Clear();
	vert.st.Set( s, t );
	vert.color[0] = vert.color[1] = vert.color[2] = vert.color[3] = 255;
}

void hhRenderModelBeam::InitFromFile( const char *fileName ) {
	name = fileName;
	declBeam = static_cast<const hhDeclBeam *>( declManager->FindType( DECL_BEAM, fileName ) );
}

void hhRenderModelBeam::LoadModel( void ) {
	idRenderModelStatic::LoadModel();
}

dynamicModel_t hhRenderModelBeam::IsDynamicModel( void ) const {
	return DM_CONTINUOUS;
}

idRenderModel *hhRenderModelBeam::InstantiateDynamicModel( const renderEntity_s *renderEntity, const viewDef_s *view, idRenderModel *cachedModel ) {
	delete cachedModel;
	if ( renderEntity == NULL || view == NULL || declBeam == NULL || renderEntity->beamNodes == NULL || declBeam->numNodes < 2 ) {
		return NULL;
	}

	idRenderModelStatic *staticModel = new idRenderModelStatic;
	staticModel->InitEmpty( "_PreyBeam_Snapshot_" );
	modelSurface_t surface = {};
	int surfaceId = 0;

	for ( int beamIndex = 0; beamIndex < declBeam->numBeams; ++beamIndex ) {
		const int segmentCount = declBeam->numNodes - 1;
		srfTriangles_t *tri = R_AllocStaticTriSurf();
		R_AllocStaticTriSurfVerts( tri, 4 * segmentCount );
		R_AllocStaticTriSurfIndexes( tri, 6 * segmentCount );
		for ( int segment = 0; segment < segmentCount; ++segment ) {
			R_InitPreyBeamVert( tri->verts[4 * segment + 0], 0.0f, 0.0f );
			R_InitPreyBeamVert( tri->verts[4 * segment + 1], 0.0f, 1.0f );
			R_InitPreyBeamVert( tri->verts[4 * segment + 2], 1.0f, 0.0f );
			R_InitPreyBeamVert( tri->verts[4 * segment + 3], 1.0f, 1.0f );
			tri->indexes[6 * segment + 0] = 4 * segment + 0;
			tri->indexes[6 * segment + 1] = 4 * segment + 2;
			tri->indexes[6 * segment + 2] = 4 * segment + 1;
			tri->indexes[6 * segment + 3] = 4 * segment + 2;
			tri->indexes[6 * segment + 4] = 4 * segment + 3;
			tri->indexes[6 * segment + 5] = 4 * segment + 1;
		}
		tri->numVerts = 4 * segmentCount;
		tri->numIndexes = 6 * segmentCount;
		surface.id = surfaceId++;
		surface.shader = declBeam->shader[beamIndex];
		surface.geometry = tri;
		staticModel->AddSurface( surface );
	}

	for ( int beamIndex = 0; beamIndex < declBeam->numBeams; ++beamIndex ) {
		for ( int endpoint = 0; endpoint < 2; ++endpoint ) {
			if ( declBeam->quadShader[beamIndex][endpoint] == NULL ) {
				continue;
			}
			srfTriangles_t *tri = R_AllocStaticTriSurf();
			R_AllocStaticTriSurfVerts( tri, 4 );
			R_AllocStaticTriSurfIndexes( tri, 6 );
			R_InitPreyBeamVert( tri->verts[0], 0.0f, 0.0f );
			R_InitPreyBeamVert( tri->verts[1], 0.0f, 1.0f );
			R_InitPreyBeamVert( tri->verts[2], 1.0f, 0.0f );
			R_InitPreyBeamVert( tri->verts[3], 1.0f, 1.0f );
			const glIndex_t indexes[6] = { 0, 2, 1, 2, 3, 1 };
			memcpy( tri->indexes, indexes, sizeof( indexes ) );
			tri->numVerts = 4;
			tri->numIndexes = 6;
			surface.id = surfaceId++;
			surface.shader = declBeam->quadShader[beamIndex][endpoint];
			surface.geometry = tri;
			staticModel->AddSurface( surface );
		}
	}

	int surfaceIndex = 0;
	for ( int beamIndex = 0; beamIndex < declBeam->numBeams; ++beamIndex ) {
		UpdateSurface( renderEntity, view, beamIndex, &renderEntity->beamNodes[beamIndex],
			const_cast<modelSurface_t *>( staticModel->Surface( surfaceIndex++ ) ) );
	}
	for ( int beamIndex = 0; beamIndex < declBeam->numBeams; ++beamIndex ) {
		for ( int endpoint = 0; endpoint < 2; ++endpoint ) {
			if ( declBeam->quadShader[beamIndex][endpoint] != NULL ) {
				UpdateQuadSurface( renderEntity, view, beamIndex, endpoint, &renderEntity->beamNodes[beamIndex],
					const_cast<modelSurface_t *>( staticModel->Surface( surfaceIndex++ ) ) );
			}
		}
	}

	staticModel->bounds = Bounds( renderEntity );
	return staticModel;
}

idBounds hhRenderModelBeam::Bounds( const renderEntity_s *renderEntity ) const {
	idBounds result;
	result.Clear();
	if ( renderEntity == NULL || renderEntity->beamNodes == NULL || declBeam == NULL ) {
		return idBounds( vec3_origin ).Expand( 8.0f );
	}
	for ( int beamIndex = 0; beamIndex < declBeam->numBeams; ++beamIndex ) {
		const hhBeamNodes_t &beam = renderEntity->beamNodes[beamIndex];
		for ( int node = 0; node < declBeam->numNodes; ++node ) {
			result.AddPoint( beam.nodes[node] );
		}
		result.ExpandSelf( declBeam->thickness[beamIndex] * 0.5f );
		for ( int endpoint = 0; endpoint < 2; ++endpoint ) {
			if ( declBeam->quadShader[beamIndex][endpoint] != NULL ) {
				result.AddBounds( idBounds( endpoint == 0 ? beam.nodes[0] : beam.nodes[declBeam->numNodes - 1] ).Expand( declBeam->quadSize[beamIndex][endpoint] * 0.5f ) );
			}
		}
	}
	return result;
}

void hhRenderModelBeam::UpdateSurface( const renderEntity_s *renderEntity, const viewDef_s *view, int beamIndex, const hhBeamNodes_t *beam, modelSurface_t *surface ) {
	srfTriangles_t *tri = surface->geometry;
	idVec3 up;
	renderEntity->axis.ProjectVector( view->renderView.viewaxis[2], up );
	const idVec3 halfWidth = up * declBeam->thickness[beamIndex] * 0.5f;
	for ( int segment = 0; segment < declBeam->numNodes - 1; ++segment ) {
		tri->verts[4 * segment + 0].xyz = beam->nodes[segment] - halfWidth;
		tri->verts[4 * segment + 1].xyz = beam->nodes[segment] + halfWidth;
		tri->verts[4 * segment + 2].xyz = beam->nodes[segment + 1] - halfWidth;
		tri->verts[4 * segment + 3].xyz = beam->nodes[segment + 1] + halfWidth;
	}
	R_BoundTriSurf( tri );
}

void hhRenderModelBeam::UpdateQuadSurface( const renderEntity_s *renderEntity, const viewDef_s *view, int beamIndex, int endpoint, const hhBeamNodes_t *beam, modelSurface_t *surface ) {
	idVec3 up;
	idVec3 right;
	renderEntity->axis.ProjectVector( view->renderView.viewaxis[2], up );
	renderEntity->axis.ProjectVector( view->renderView.viewaxis[1], right );
	const float halfSize = declBeam->quadSize[beamIndex][endpoint] * 0.5f;
	up *= halfSize;
	right *= halfSize;
	const idVec3 center = endpoint == 0 ? beam->nodes[0] : beam->nodes[declBeam->numNodes - 1];
	srfTriangles_t *tri = surface->geometry;
	tri->verts[0].xyz = center - right - up;
	tri->verts[1].xyz = center - right + up;
	tri->verts[2].xyz = center + right - up;
	tri->verts[3].xyz = center + right + up;
	R_BoundTriSurf( tri );
}
