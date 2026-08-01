uniform sampler2D DepthBuffer;
uniform vec2 invDepthTexSize;
uniform vec2 viewportTexScale;
uniform vec2 lightCenterUV;
uniform vec4 lightColor;
uniform float lightDepth;
uniform float depthBias;
uniform float occlusionRadiusPixels;
uniform vec2 flareAxis;
uniform float elementKind;
uniform vec4 elementParams;

float SampleDepth( vec2 uv ) {
	// Inset by half a texel so nearest filtering never fetches outside the
	// viewport region when the depth texture is larger than the viewport.
	vec2 minUv = 0.5 * invDepthTexSize;
	vec2 maxUv = max( minUv, viewportTexScale - 0.5 * invDepthTexSize );
	vec2 clampedUv = clamp( uv, minUv, maxUv );
	return texture2D( DepthBuffer, clampedUv ).r;
}

float CompareDepth( float sceneDepth ) {
	// depthBias is computed per light on the CPU from a world-space
	// tolerance, so the comparison stays meaningful across the non-linear
	// depth range.
	return smoothstep( lightDepth - depthBias, lightDepth + depthBias * 2.0, sceneDepth );
}

vec2 SafeNormalizedAxis( vec2 axis ) {
	if ( dot( axis, axis ) < 0.0001 ) {
		axis = vec2( 1.0, 0.0 );
	}
	return normalize( axis );
}

float ComputeVisibility() {
	vec2 axis = SafeNormalizedAxis( flareAxis );
	vec2 ortho = vec2( -axis.y, axis.x );
	float radius = max( occlusionRadiusPixels, 1.0 );

	float visibility = 0.0;
	visibility += CompareDepth( SampleDepth( lightCenterUV ) ) * 0.40;
	visibility += CompareDepth( SampleDepth( lightCenterUV + axis * invDepthTexSize * radius ) ) * 0.20;
	visibility += CompareDepth( SampleDepth( lightCenterUV - axis * invDepthTexSize * radius ) ) * 0.20;
	visibility += CompareDepth( SampleDepth( lightCenterUV + ortho * invDepthTexSize * radius ) ) * 0.10;
	visibility += CompareDepth( SampleDepth( lightCenterUV - ortho * invDepthTexSize * radius ) ) * 0.10;

	float borderDistance = min(
		min( lightCenterUV.x, lightCenterUV.y ),
		min( viewportTexScale.x - lightCenterUV.x, viewportTexScale.y - lightCenterUV.y ) );
	float borderThreshold = max( min( viewportTexScale.x, viewportTexScale.y ) * 0.06, 0.002 );
	float borderFade = smoothstep( 0.0, borderThreshold, borderDistance );

	return clamp( visibility * borderFade, 0.0, 1.0 );
}

float RingProfile( float radius, float ringRadius, float ringWidth ) {
	float ringDistance = abs( radius - ringRadius );
	return smoothstep( ringWidth, 0.0, ringDistance );
}

void main() {
	vec2 p = gl_TexCoord[1].st * 2.0 - 1.0;
	vec2 axis = SafeNormalizedAxis( flareAxis );
	vec2 ortho = vec2( -axis.y, axis.x );
	vec2 oriented = vec2( dot( p, axis ), dot( p, ortho ) );
	float radius = length( p );

	float visibility = ComputeVisibility();
	if ( visibility <= 0.001 ) {
		discard;
	}

	float intensity = 0.0;
	if ( elementKind < 0.5 ) {
		float core = exp( -radius * radius * elementParams.x );
		float ring = RingProfile( radius, elementParams.y, elementParams.z );
		float streak = pow( max( 0.0, 1.0 - abs( oriented.y ) * 5.0 ), 4.0 ) * exp( -abs( oriented.x ) * 1.8 );
		intensity = ( core + ring * 0.25 + streak * 0.18 ) * elementParams.w;
	} else if ( elementKind < 1.5 ) {
		float core = exp( -radius * radius * elementParams.x );
		float ring = RingProfile( radius, elementParams.y, elementParams.z );
		float smear = exp( -abs( oriented.y ) * 4.0 ) * exp( -abs( oriented.x ) * 1.2 );
		intensity = ( core * 0.85 + ring * 0.30 + smear * 0.15 ) * elementParams.w;
	} else {
		float line = pow( max( 0.0, 1.0 - abs( oriented.y ) * elementParams.y ), elementParams.z );
		float taper = exp( -abs( oriented.x ) * elementParams.x );
		float core = exp( -radius * radius * 2.5 );
		intensity = ( line * taper + core * 0.25 ) * elementParams.w;
	}

	intensity *= visibility;
	if ( intensity <= 0.0005 ) {
		discard;
	}

	// Additive pass: keep destination alpha untouched.
	gl_FragColor = vec4( lightColor.rgb * intensity, 0.0 );
}
