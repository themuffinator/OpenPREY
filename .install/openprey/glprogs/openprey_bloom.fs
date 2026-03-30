uniform sampler2D Scene;
uniform vec2 invTexSize;
uniform float bloomThreshold;
uniform float bloomSoftKnee;
uniform float bloomIntensity;
uniform float bloomRadius;
uniform float bloomEnabled;
uniform float toneMapEnabled;
uniform float hdrExposure;
uniform float hdrWhitePoint;
uniform float hdrLift;
uniform float hdrPostGamma;
uniform float hdrGain;
uniform float hdrVibrance;
uniform float hdrSaturation;
uniform float hdrContrast;

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

float ACESFilmScalar( float x ) {
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return ( x * ( a * x + b ) ) / ( x * ( c * x + d ) + e );
}

vec3 ACESFilm( vec3 x ) {
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return clamp( ( x * ( a * x + b ) ) / ( x * ( c * x + d ) + e ), 0.0, 1.0 );
}

vec3 ToneMapHDR( vec3 color ) {
	vec3 exposedColor = color * max( hdrExposure, 0.001 );
	float safeWhitePoint = max( hdrWhitePoint, 1.0 );
	float whiteScale = 1.0 / max( ACESFilmScalar( safeWhitePoint ), 0.0001 );
	return clamp( ACESFilm( exposedColor ) * whiteScale, 0.0, 1.0 );
}

vec3 ApplyLiftGammaGain( vec3 color ) {
	color = max( color + vec3( hdrLift ), vec3( 0.0 ) );
	color = pow( color, vec3( 1.0 / max( hdrPostGamma, 0.001 ) ) );
	color *= hdrGain;
	return color;
}

vec3 ApplyVibrance( vec3 color ) {
	float luma = dot( color, vec3( 0.2126, 0.7152, 0.0722 ) );
	float maxChannel = max( max( color.r, color.g ), color.b );
	float minChannel = min( min( color.r, color.g ), color.b );
	float saturation = maxChannel - minChannel;
	float vibranceMix = clamp( 1.0 + hdrVibrance * ( 1.0 - saturation ), 0.0, 2.0 );
	return mix( vec3( luma ), color, vibranceMix );
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec3 baseColor = texture2D( Scene, uv ).rgb;
	vec3 color = baseColor;

	if ( bloomEnabled > 0.5 && bloomIntensity > 0.0001 ) {
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

		color += bloom * bloomIntensity;
	}

	if ( toneMapEnabled > 0.5 ) {
		color = ToneMapHDR( color );
	}

	color = ApplyLiftGammaGain( color );
	color = ApplyVibrance( color );

	float luma = dot( color, vec3( 0.2126, 0.7152, 0.0722 ) );
	color = mix( vec3( luma ), color, hdrSaturation );
	color = ( color - 0.5 ) * hdrContrast + 0.5;
	color = clamp( color, 0.0, 1.0 );

	gl_FragColor = vec4( color, texture2D( Scene, uv ).a );
}
