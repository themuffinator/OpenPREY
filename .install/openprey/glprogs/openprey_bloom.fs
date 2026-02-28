uniform sampler2D Scene;
uniform vec2 invTexSize;
uniform float bloomThreshold;
uniform float bloomSoftKnee;
uniform float bloomIntensity;
uniform float bloomRadius;

float BrightContribution( vec3 color ) {
	float brightness = max( max( color.r, color.g ), color.b );
	float knee = max( bloomSoftKnee, 0.0001 );
	float soft = clamp( ( brightness - bloomThreshold + knee ) / ( 2.0 * knee ), 0.0, 1.0 );
	float contribution = max( brightness - bloomThreshold, 0.0 ) + soft * soft * knee;
	return contribution / max( brightness, 0.0001 );
}

vec3 SampleBloom( vec2 uv, vec2 offset ) {
	vec3 sampleColor = texture2D( Scene, uv + offset ).rgb;
	return sampleColor * BrightContribution( sampleColor );
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec3 baseColor = texture2D( Scene, uv ).rgb;

	vec2 stepSize = invTexSize * bloomRadius;
	vec3 bloom = vec3( 0.0 );

	bloom += SampleBloom( uv, vec2( 0.0, 0.0 ) ) * 0.20;

	bloom += SampleBloom( uv, vec2( stepSize.x, 0.0 ) ) * 0.12;
	bloom += SampleBloom( uv, vec2( -stepSize.x, 0.0 ) ) * 0.12;
	bloom += SampleBloom( uv, vec2( 0.0, stepSize.y ) ) * 0.12;
	bloom += SampleBloom( uv, vec2( 0.0, -stepSize.y ) ) * 0.12;

	bloom += SampleBloom( uv, vec2( stepSize.x, stepSize.y ) ) * 0.07;
	bloom += SampleBloom( uv, vec2( -stepSize.x, stepSize.y ) ) * 0.07;
	bloom += SampleBloom( uv, vec2( stepSize.x, -stepSize.y ) ) * 0.07;
	bloom += SampleBloom( uv, vec2( -stepSize.x, -stepSize.y ) ) * 0.07;

	vec2 farStep = stepSize * 2.0;
	bloom += SampleBloom( uv, vec2( farStep.x, 0.0 ) ) * 0.02;
	bloom += SampleBloom( uv, vec2( -farStep.x, 0.0 ) ) * 0.02;
	bloom += SampleBloom( uv, vec2( 0.0, farStep.y ) ) * 0.02;
	bloom += SampleBloom( uv, vec2( 0.0, -farStep.y ) ) * 0.02;

	vec3 result = baseColor + bloom * bloomIntensity;
	gl_FragColor = vec4( result, texture2D( Scene, uv ).a );
}
