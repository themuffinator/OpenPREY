#version 110

// Screen-space cel outline over world geometry.
//
// Two things make an edge worth inking: a depth step, where one surface passes
// in front of another, and a normal step, where two faces of the same object
// meet at a corner. Silhouettes need the first, interior creases need the
// second, and a cel look wants both. The depth buffer sampled here holds world
// geometry only, so model entities keep the outline shell drawn around their
// own silhouette instead of picking up a second line here.
//
// Edges are found in that world-only snapshot but drawn only where the world is
// still the visible surface, which is what SceneDepthBuffer is for. The two jobs
// are deliberately separate: a wall corner hidden behind the first-person weapon
// must not be inked across the weapon, yet the corner's shape must still be
// measured from world geometry alone so the weapon cannot bend the line it casts
// on the wall beside it.

uniform sampler2D Scene;
uniform sampler2D WorldDepthBuffer;
uniform sampler2D SceneDepthBuffer;

uniform vec2 invTexSize;
uniform vec4 projectionInfo;
uniform vec2 depthProjection;

// x: edge radius in pixels
// y: silhouette sensitivity as a fraction of view distance
// z: crease sensitivity, 0 disables interior lines
// w: debug view
uniform vec4 celEdgeParams;
uniform vec4 celOutlineColor;

const float kSkyDepth = 0.99999;

float SampleDepth( vec2 uv ) {
	return texture2D( WorldDepthBuffer, uv ).x;
}

float SampleSceneDepth( vec2 uv ) {
	return texture2D( SceneDepthBuffer, uv ).x;
}

// Identical to the SSAO reconstruction, so both passes agree on what a world
// unit of distance means.
float ViewSpaceZFromDepth( float depth ) {
	float ndcDepth = depth * 2.0 - 1.0;
	float denom = ndcDepth + depthProjection.x;
	if ( abs( denom ) < 0.00001 ) {
		denom = ( denom < 0.0 ) ? -0.00001 : 0.00001;
	}
	return ( -depthProjection.y ) / denom;
}

vec3 ReconstructViewPosition( vec2 uv, float depth ) {
	float viewZ = ViewSpaceZFromDepth( depth );
	vec2 ndc = uv * 2.0 - 1.0;

	return vec3(
		-viewZ * ( ndc.x + projectionInfo.z ) * projectionInfo.x,
		-viewZ * ( ndc.y + projectionInfo.w ) * projectionInfo.y,
		viewZ );
}

// The world snapshot is taken before any model entity is drawn, so an edge found
// in it can land on a pixel the finished frame gave to something else. Comparing
// the two depths is the same test the depth buffer already made: when the scene
// settled on a surface a world unit or more in front, the world here is hidden
// and its ink belongs behind whatever won. Matched to the identically named test
// in ssao.fs, which rejects foreground pixels for the same reason.
bool PixelIsForeground( vec2 uv, float worldDepth ) {
	float sceneDepth = SampleSceneDepth( uv );
	if ( sceneDepth >= kSkyDepth || worldDepth >= kSkyDepth ) {
		return false;
	}

	float worldViewDepth = -ViewSpaceZFromDepth( worldDepth );
	float sceneViewDepth = -ViewSpaceZFromDepth( sceneDepth );

	return sceneViewDepth + 1.0 < worldViewDepth;
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec4 scene = texture2D( Scene, uv );
	float centerDepth = SampleDepth( uv );

	// Nothing behind this pixel belongs to the world, so there is no world edge
	// to draw on it. Neighbouring sky still registers as a silhouette below.
	if ( centerDepth >= kSkyDepth ) {
		gl_FragColor = ( celEdgeParams.w > 0.5 ) ? vec4( 0.0, 0.0, 0.0, scene.a ) : scene;
		return;
	}

	// A model entity, or the first-person weapon, owns this pixel. Whatever the
	// world does behind it is inked where the world is actually visible; here the
	// occluder's own silhouette is already carried by its outline shell.
	if ( PixelIsForeground( uv, centerDepth ) ) {
		gl_FragColor = ( celEdgeParams.w > 0.5 ) ? vec4( 0.0, 0.0, 0.0, scene.a ) : scene;
		return;
	}

	vec2 texel = invTexSize * max( celEdgeParams.x, 1.0 );
	vec2 offsetX = vec2( texel.x, 0.0 );
	vec2 offsetY = vec2( 0.0, texel.y );

	// Axial neighbours drive the depth step and, later, the plane fit.
	float leftDepth = SampleDepth( uv - offsetX );
	float rightDepth = SampleDepth( uv + offsetX );
	float downDepth = SampleDepth( uv - offsetY );
	float upDepth = SampleDepth( uv + offsetY );

	vec3 centerPos = ReconstructViewPosition( uv, centerDepth );
	vec3 leftPos = ReconstructViewPosition( uv - offsetX, leftDepth );
	vec3 rightPos = ReconstructViewPosition( uv + offsetX, rightDepth );
	vec3 downPos = ReconstructViewPosition( uv - offsetY, downDepth );
	vec3 upPos = ReconstructViewPosition( uv + offsetY, upDepth );

	float viewDistance = max( -centerPos.z, 1.0 );

	// A neighbour that is sky is the far side of a silhouette. Comparing against
	// its reconstructed distance would swamp the threshold, so it scores as a
	// full edge directly.
	float skyNeighbour = 0.0;
	skyNeighbour = max( skyNeighbour, step( kSkyDepth, leftDepth ) );
	skyNeighbour = max( skyNeighbour, step( kSkyDepth, rightDepth ) );
	skyNeighbour = max( skyNeighbour, step( kSkyDepth, downDepth ) );
	skyNeighbour = max( skyNeighbour, step( kSkyDepth, upDepth ) );

	// Second derivative of view-space depth: flat surfaces and even steep ramps
	// cancel out, only genuine steps survive. Scaling the threshold by view
	// distance keeps a line the same weight near and far.
	float depthEdge = max(
		abs( ( -leftPos.z ) + ( -rightPos.z ) - 2.0 * viewDistance ),
		abs( ( -downPos.z ) + ( -upPos.z ) - 2.0 * viewDistance ) );
	float depthTolerance = max( celEdgeParams.y, 0.00001 ) * viewDistance;
	float silhouette = smoothstep( depthTolerance, depthTolerance * 2.0, depthEdge );
	silhouette = max( silhouette, skyNeighbour );

	float crease = 0.0;
	if ( celEdgeParams.z > 0.0 ) {
		// Fit the surface plane from the diagonal samples, then measure how far
		// the axial neighbours sit off it. Deriving the normal from a disjoint
		// set of taps is what makes this a real test: on a flat surface, and on
		// any smooth ramp, the axial neighbours lie in the fitted plane and the
		// deviation is zero. At a corner they lift off it.
		vec2 diagonal = offsetX + offsetY;
		vec2 antiDiagonal = offsetX - offsetY;
		vec3 upperRight = ReconstructViewPosition( uv + diagonal, SampleDepth( uv + diagonal ) );
		vec3 lowerLeft = ReconstructViewPosition( uv - diagonal, SampleDepth( uv - diagonal ) );
		vec3 lowerRight = ReconstructViewPosition( uv + antiDiagonal, SampleDepth( uv + antiDiagonal ) );
		vec3 upperLeft = ReconstructViewPosition( uv - antiDiagonal, SampleDepth( uv - antiDiagonal ) );

		vec3 planeNormal = cross( upperRight - lowerLeft, upperLeft - lowerRight );
		float planeLength = length( planeNormal );
		if ( planeLength > 0.000001 ) {
			planeNormal /= planeLength;

			float deviation = max(
				max( abs( dot( planeNormal, leftPos - centerPos ) ), abs( dot( planeNormal, rightPos - centerPos ) ) ),
				max( abs( dot( planeNormal, downPos - centerPos ) ), abs( dot( planeNormal, upPos - centerPos ) ) ) );

			// Express the lift as a slope so the test is independent of how far
			// apart the taps landed in world space.
			float span = max( length( rightPos - leftPos ), 0.0001 );
			float creaseTolerance = max( 1.0 - celEdgeParams.z, 0.02 ) * 0.5;
			crease = smoothstep( creaseTolerance * 0.5, creaseTolerance, deviation / span );
		}

		// A crease measured across a depth step is not a corner, it is the two
		// sides of a silhouette being compared, and that is already inked.
		crease *= 1.0 - silhouette;
	}

	float edge = clamp( max( silhouette, crease ), 0.0, 1.0 ) * celOutlineColor.a;

	if ( celEdgeParams.w > 0.5 ) {
		gl_FragColor = vec4( vec3( edge ), scene.a );
		return;
	}

	gl_FragColor = vec4( mix( scene.rgb, celOutlineColor.rgb, edge ), scene.a );
}
