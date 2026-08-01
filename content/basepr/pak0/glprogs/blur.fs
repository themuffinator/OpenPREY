uniform sampler2D Scene;
uniform sampler2D DepthTex;
uniform vec2 invTexSize;
uniform float range;
uniform float focus;
uniform vec4 approachColor;
uniform float approachPercent;
uniform float distanceScale;

float LinearizeDepth( float depth, float zNear ) {
	float ndcDepth = depth * 2.0 - 1.0;
	float denom = 0.999 - ndcDepth;
	if ( denom < 0.001 ) {
		denom = 0.001;
	}

	return ( 2.0 * zNear ) / denom;
}

float ViewDistanceFromDepth( float depth, float zNear ) {
	return ( depth < 0.99999 ) ? LinearizeDepth( depth, zNear ) : 4096.0;
}

float CircleOfConfusion( float viewDistance, float focusDistance, float blurRange, float blurStrength ) {
	float blurFactor = clamp( abs( viewDistance - focusDistance ) / blurRange, 0.0, 1.0 );
	return smoothstep( 0.0, 1.0, blurFactor ) * blurStrength;
}

void GatherDepthAwareSample(
	vec2 uv,
	vec2 minUv,
	vec2 maxUv,
	vec2 kernelScale,
	vec2 kernelPoint,
	float radialWeight,
	float centerDistance,
	float zNear,
	inout vec4 colorSum,
	inout float weightSum ) {
	vec2 sampleUv = clamp( uv + kernelPoint * kernelScale, minUv, maxUv );
	float sampleDepth = texture2D( DepthTex, sampleUv ).x;
	float sampleDistance = ViewDistanceFromDepth( sampleDepth, zNear );

	// Preserve silhouettes by strongly rejecting taps that cross a depth edge.
	// The relative allowance keeps sloped and distant surfaces smooth without
	// letting a sharp foreground weapon bleed into the defocused world.
	float depthDelta = abs( sampleDistance - centerDistance );
	float depthTolerance = max( 3.0, min( centerDistance, sampleDistance ) * 0.04 );
	float sameSurface = 1.0 - smoothstep( depthTolerance, depthTolerance * 4.0, depthDelta );
	float sampleWeight = radialWeight * mix( 0.02, 1.0, sameSurface );

	colorSum += texture2D( Scene, sampleUv ) * sampleWeight;
	weightSum += sampleWeight;
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec4 scene = texture2D( Scene, uv );
	float blurStrength = clamp( approachPercent, 0.0, 1.0 );
	float zNear = max( distanceScale, 0.25 );
	float focusDistance = max( focus, 0.0 );
	if ( focusDistance <= 0.0 ) {
		float focusDepth = texture2D( DepthTex, vec2( 0.5, 0.5 ) ).x;
		focusDistance = ( focusDepth < 0.99999 ) ? LinearizeDepth( focusDepth, zNear ) : 16.0;
	}

	float blurRange = max( range, 0.0 );
	if ( blurRange <= 0.0 ) {
		blurRange = max( 64.0, focusDistance * 0.25 );
	}

	float depth = texture2D( DepthTex, uv ).x;
	float viewDistance = ViewDistanceFromDepth( depth, zNear );
	float blurAmount = CircleOfConfusion( viewDistance, focusDistance, blurRange, blurStrength );

	if ( blurAmount <= 0.001 ) {
		gl_FragColor = scene;
		return;
	}

	// Scale the physical footprint with the local circle of confusion. The old
	// fixed-radius cross kernel mixed widely separated pixels even at a focus
	// transition; this grows smoothly and distributes energy over three rings.
	vec2 kernelScale = invTexSize * ( 24.0 * blurAmount );
	vec2 minUv = invTexSize * 0.5;
	vec2 maxUv = vec2( 1.0 ) - minUv;
	vec4 blur = scene;
	float blurWeight = 1.0;

	// Inner ring: radius 0.28, four diagonal samples.
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2(  0.197990,  0.197990 ), 0.86, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2( -0.197990,  0.197990 ), 0.86, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2( -0.197990, -0.197990 ), 0.86, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2(  0.197990, -0.197990 ), 0.86, viewDistance, zNear, blur, blurWeight );

	// Middle ring: radius 0.62, six evenly spaced samples.
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2(  0.620000,  0.000000 ), 0.46, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2(  0.310000,  0.536936 ), 0.46, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2( -0.310000,  0.536936 ), 0.46, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2( -0.620000,  0.000000 ), 0.46, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2( -0.310000, -0.536936 ), 0.46, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2(  0.310000, -0.536936 ), 0.46, viewDistance, zNear, blur, blurWeight );

	// Outer ring: eight rotated samples avoid the axial star pattern.
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2(  0.923880,  0.382683 ), 0.14, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2(  0.382683,  0.923880 ), 0.14, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2( -0.382683,  0.923880 ), 0.14, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2( -0.923880,  0.382683 ), 0.14, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2( -0.923880, -0.382683 ), 0.14, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2( -0.382683, -0.923880 ), 0.14, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2(  0.382683, -0.923880 ), 0.14, viewDistance, zNear, blur, blurWeight );
	GatherDepthAwareSample( uv, minUv, maxUv, kernelScale, vec2(  0.923880, -0.382683 ), 0.14, viewDistance, zNear, blur, blurWeight );

	blur /= max( blurWeight, 0.0001 );

	vec4 mixed = mix( scene, blur, blurAmount );
	float tintAmount = blurAmount * clamp( approachColor.a, 0.0, 1.0 ) * 0.04;
	mixed.rgb = mix( mixed.rgb, approachColor.rgb, tintAmount );
	mixed.a = scene.a;

	gl_FragColor = mixed;
}
