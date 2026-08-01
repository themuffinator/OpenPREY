#version 110

uniform sampler2D uAlphaMap;
uniform float uAlphaTestEnabled;
uniform float uAlphaRef;
uniform float uAlphaTestMode;
uniform float uAlphaScale;
uniform float uAlphaHashEnabled;
uniform float uAlphaHashStable;
uniform vec2 uShadowCasterDepthOffset;

varying vec2 vAlphaTexCoord;
varying vec3 vAlphaHashCoord;
varying float vShadowDepth;

float AlphaHashThreshold( vec2 fragmentCoord ) {
	vec2 texel = floor( fragmentCoord );
	return fract( 52.9829189 * fract( texel.x * 0.06711056 + texel.y * 0.00583715 ) );
}

float StableAlphaHashThreshold( vec3 hashCoord ) {
	vec3 texel = floor( hashCoord * 0.5 );
	return fract( 52.9829189 * fract( texel.x * 0.06711056 + texel.y * 0.00583715 + texel.z * 0.01327111 ) );
}

float AlphaCoverage( float alpha ) {
	float alphaRef = clamp( uAlphaRef, 0.0, 0.9999 );
	float scaledAlpha = clamp( alpha, 0.0, 1.0 );
	return clamp( ( scaledAlpha - alphaRef ) / max( 1.0 - alphaRef, 1.0e-4 ), 0.0, 1.0 );
}

bool AlphaTestPass( float alpha ) {
	if ( uAlphaTestMode < -0.5 ) {
		return alpha < uAlphaRef;
	}
	if ( abs( uAlphaTestMode ) <= 0.5 ) {
		return abs( alpha - uAlphaRef ) <= ( 0.5 / 255.0 );
	}
	return alpha > uAlphaRef;
}

void main() {
	if ( uAlphaTestEnabled > 0.5 ) {
		float alpha = texture2D( uAlphaMap, vAlphaTexCoord ).a * uAlphaScale;
		if ( uAlphaHashEnabled > 0.5 && uAlphaTestMode > 0.5 ) {
			float coverage = AlphaCoverage( alpha );
			float threshold = ( uAlphaHashStable > 0.5 ) ? StableAlphaHashThreshold( vAlphaHashCoord ) : AlphaHashThreshold( gl_FragCoord.xy );
			if ( coverage <= 0.0 || coverage <= threshold ) {
				discard;
			}
		} else if ( !AlphaTestPass( alpha ) ) {
			discard;
		}
	}

	// Casters past the falloff plane clamp to the far depth instead of
	// vanishing (better behaved under PCF near the falloff boundary); only
	// apex-side geometry (depth <= 0), which cannot occlude anything inside
	// the light volume, is discarded. The discard runs after the alpha fetch
	// so the texture fetch's implicit derivatives stay defined.
	if ( vShadowDepth <= 0.0 ) {
		discard;
	}

	// Shader-written fragment depth bypasses glPolygonOffset, so the classic
	// slope-scale caster offset must be applied here: x scales with the depth
	// slope, y is pre-scaled by the CPU to one resolvable depth-buffer step.
	float depthSlope = max( abs( dFdx( vShadowDepth ) ), abs( dFdy( vShadowDepth ) ) );
	gl_FragDepth = clamp( vShadowDepth + uShadowCasterDepthOffset.x * depthSlope + uShadowCasterDepthOffset.y, 0.0, 1.0 );
	gl_FragColor = vec4( 0.0 );
}
