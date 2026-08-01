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
#include "Model_local.h"

static const char *parametricParticle_SnapshotName = "_ParametricParticle_Snapshot_";

/*
====================
idRenderModelPrt::idRenderModelPrt
====================
*/
idRenderModelPrt::idRenderModelPrt() {
	particleSystem = NULL;
}

/*
====================
idRenderModelPrt::InitFromFile
====================
*/
void idRenderModelPrt::InitFromFile( const char *fileName ) {
	name = fileName;
	particleSystem = static_cast<const idDeclParticle *>( declManager->FindType( DECL_PARTICLE, fileName ) );
}

/*
=================
idRenderModelPrt::TouchData
=================
*/
void idRenderModelPrt::TouchData( void ) {
	particleSystem = static_cast<const idDeclParticle *>( declManager->FindType( DECL_PARTICLE, name ) );
}

/*
====================
idRenderModelPrt::InstantiateDynamicModel
====================
*/
idRenderModel *idRenderModelPrt::InstantiateDynamicModel( const struct renderEntity_s *renderEntity, const struct viewDef_s *viewDef, idRenderModel *cachedModel ) {
	idRenderModelStatic *staticModel;

	if ( cachedModel != NULL && !r_useCachedDynamicModels.GetBool() ) {
		delete cachedModel;
		cachedModel = NULL;
	}
	if ( renderEntity == NULL || viewDef == NULL || r_skipParticles.GetBool() || particleSystem == NULL ) {
		delete cachedModel;
		return NULL;
	}

	if ( cachedModel != NULL ) {
		assert( dynamic_cast<idRenderModelStatic *>( cachedModel ) != NULL );
		assert( idStr::Icmp( cachedModel->Name(), parametricParticle_SnapshotName ) == 0 );
		staticModel = static_cast<idRenderModelStatic *>( cachedModel );
	} else {
		staticModel = new idRenderModelStatic;
		staticModel->InitEmpty( parametricParticle_SnapshotName );
	}

	particleGen_t g;
	g.renderEnt = renderEntity;
	g.renderView = &viewDef->renderView;
	g.origin.Zero();
	g.axis.Identity();

	for ( int stageNum = 0; stageNum < particleSystem->stages.Num(); ++stageNum ) {
		idParticleStage *stage = particleSystem->stages[ stageNum ];
		if ( stage->material == NULL || stage->cycleMsec == 0 ) {
			continue;
		}
		if ( stage->hidden ) {
			staticModel->DeleteSurfaceWithId( stageNum );
			continue;
		}

		idRandom steppingRandom;
		idRandom previousCycleRandom;
		const int stageAge = g.renderView->time + renderEntity->shaderParms[ SHADERPARM_TIMEOFFSET ] * 1000 - stage->timeOffset * 1000;
		const int stageCycle = stageAge / stage->cycleMsec;
		const int diversitySeed = static_cast<int>( renderEntity->shaderParms[ SHADERPARM_DIVERSITY ] * idRandom::MAX_RAND );
		steppingRandom.SetSeed( ( ( stageCycle << 10 ) & idRandom::MAX_RAND ) ^ diversitySeed );
		previousCycleRandom.SetSeed( ( ( ( stageCycle - 1 ) << 10 ) & idRandom::MAX_RAND ) ^ diversitySeed );

		const int count = stage->totalParticles * stage->NumQuadsPerParticle();
		int surfaceNum;
		modelSurface_t *surf;
		if ( staticModel->FindSurfaceWithId( stageNum, surfaceNum ) ) {
			surf = &staticModel->surfaces[ surfaceNum ];
			R_FreeStaticTriSurfVertexCaches( surf->geometry );
		} else {
			surf = &staticModel->surfaces.Alloc();
			surf->id = stageNum;
			surf->shader = stage->material;
			surf->geometry = R_AllocStaticTriSurf();
			R_AllocStaticTriSurfVerts( surf->geometry, 4 * count );
			R_AllocStaticTriSurfIndexes( surf->geometry, 6 * count );
			R_AllocStaticTriSurfPlanes( surf->geometry, 6 * count );
		}

		int numVerts = 0;
		idDrawVert *verts = surf->geometry->verts;
		for ( int index = 0; index < stage->totalParticles; ++index ) {
			g.index = index;
			steppingRandom.RandomInt();
			previousCycleRandom.RandomInt();

			const int bunchOffset = stage->particleLife * 1000 * stage->spawnBunching * index / stage->totalParticles;
			const int particleAge = stageAge - bunchOffset;
			const int particleCycle = particleAge / stage->cycleMsec;
			if ( particleCycle < 0 || ( stage->cycles != 0 && particleCycle >= stage->cycles ) ) {
				continue;
			}
			g.random = particleCycle == stageCycle ? steppingRandom : previousCycleRandom;
			const int inCycleTime = particleAge - particleCycle * stage->cycleMsec;
			if ( renderEntity->shaderParms[ SHADERPARM_PARTICLE_STOPTIME ] != 0.0f &&
					g.renderView->time - inCycleTime >= renderEntity->shaderParms[ SHADERPARM_PARTICLE_STOPTIME ] * 1000 ) {
				continue;
			}

			g.frac = static_cast<float>( inCycleTime ) / ( stage->particleLife * 1000 );
			if ( g.frac < 0.0f || g.frac > 1.0f ) {
				continue;
			}
			g.originalRandom = g.random;
			g.age = g.frac * stage->particleLife;
			numVerts += stage->CreateParticle( &g, verts + numVerts );
		}

		assert( ( numVerts & 3 ) == 0 && numVerts <= 4 * count );
		int numIndexes = 0;
		glIndex_t *indexes = surf->geometry->indexes;
		for ( int i = 0; i < numVerts; i += 4 ) {
			indexes[ numIndexes + 0 ] = i;
			indexes[ numIndexes + 1 ] = i + 2;
			indexes[ numIndexes + 2 ] = i + 3;
			indexes[ numIndexes + 3 ] = i;
			indexes[ numIndexes + 4 ] = i + 3;
			indexes[ numIndexes + 5 ] = i + 1;
			numIndexes += 6;
		}
		surf->geometry->tangentsCalculated = false;
		surf->geometry->facePlanesCalculated = false;
		surf->geometry->numVerts = numVerts;
		surf->geometry->numIndexes = numIndexes;
		surf->geometry->bounds = stage->bounds;
	}

	return staticModel;
}

/*
====================
idRenderModelPrt::IsDynamicModel
====================
*/
dynamicModel_t idRenderModelPrt::IsDynamicModel() const {
	return DM_CONTINUOUS;
}

/*
====================
idRenderModelPrt::Bounds
====================
*/
idBounds idRenderModelPrt::Bounds( const struct renderEntity_s *ent ) const {
	if ( particleSystem != NULL ) {
		return particleSystem->bounds;
	}
	return ent != NULL ? ent->bounds : bounds_zero;
}

/*
====================
idRenderModelPrt::DepthHack
====================
*/
float idRenderModelPrt::DepthHack() const {
	return particleSystem != NULL ? particleSystem->depthHack : 0.0f;
}

/*
====================
idRenderModelPrt::Memory
====================
*/
int idRenderModelPrt::Memory() const {
	int total = idRenderModelStatic::Memory();
	if ( particleSystem != NULL ) {
		total += sizeof( *particleSystem );
		for ( int i = 0; i < particleSystem->stages.Num(); ++i ) {
			total += sizeof( particleSystem->stages[i] );
		}
	}
	return total;
}
