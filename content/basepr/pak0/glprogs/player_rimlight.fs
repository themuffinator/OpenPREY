#version 110

uniform vec4 uViewOrigin;
uniform vec4 uColor;

// x: falloff exponent, from r_playerRimlightPower. Higher tightens the band to
//    the silhouette, lower spreads it across the body.
// y: intensity scale applied on top of the entity's rimlight alpha
// z: floor added to the rim term, so a thin sliver of colour survives even where
//    the surface faces the camera head on
// w: unused
uniform vec4 uRimParams;

varying vec3 vWorldNormal;
varying vec3 vWorldPosition;

void main() {
	vec3 normal = normalize( vWorldNormal );
	vec3 viewDir = normalize( uViewOrigin.xyz - vWorldPosition );
	float rim = 1.0 - max( dot( normal, viewDir ), 0.0 );
	rim = pow( max( rim, 0.0 ), max( uRimParams.x, 0.001 ) );

	// The floor lifts the rim term itself rather than the final colour, so it
	// still scales with the entity's requested strength instead of punching a
	// fixed wash through a rimlight the player asked to be faint.
	rim = clamp( rim + uRimParams.z, 0.0, 1.0 );

	float contribution = clamp( uColor.a * rim * uRimParams.y, 0.0, 1.0 );
	gl_FragColor = vec4( uColor.rgb * contribution, contribution );
}
