uniform sampler2D Scene;
uniform sampler2D BloomTex0;
uniform sampler2D BloomTex1;
uniform sampler2D BloomTex2;
uniform sampler2D BloomTex3;
uniform sampler2D BloomTex4;
uniform float bloomIntensity;
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
uniform float hdrHighlightDesaturation;
uniform float hdrGamutCompression;
uniform float hdrDebugView;
uniform float bloomWeight0;
uniform float bloomWeight1;
uniform float bloomWeight2;
uniform float bloomWeight3;
uniform float bloomWeight4;

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
	return ( x * ( a * x + b ) ) / ( x * ( c * x + d ) + e );
}

vec3 SampleBloom( vec2 uv ) {
	vec3 bloom = vec3( 0.0 );
	bloom += max( texture2D( BloomTex0, uv ).rgb, vec3( 0.0 ) ) * bloomWeight0;
	bloom += max( texture2D( BloomTex1, uv ).rgb, vec3( 0.0 ) ) * bloomWeight1;
	bloom += max( texture2D( BloomTex2, uv ).rgb, vec3( 0.0 ) ) * bloomWeight2;
	bloom += max( texture2D( BloomTex3, uv ).rgb, vec3( 0.0 ) ) * bloomWeight3;
	bloom += max( texture2D( BloomTex4, uv ).rgb, vec3( 0.0 ) ) * bloomWeight4;
	return max( bloom, vec3( 0.0 ) );
}

vec3 SceneReferredHDRColor( vec3 color ) {
	return max( color, vec3( 0.0 ) );
}

vec3 HighlightCompress( vec3 color ) {
	float luma = dot( color, vec3( 0.2126, 0.7152, 0.0722 ) );
	float peak = max( max( color.r, color.g ), color.b );
	float highlight = smoothstep( 0.98, 1.0, peak );

	color = mix( color, vec3( luma ), clamp( highlight * hdrHighlightDesaturation, 0.0, 1.0 ) );

	peak = max( max( color.r, color.g ), color.b );
	if ( peak > 1.0 && hdrGamutCompression > 0.0 ) {
		float compressedPeak = 1.0 + ( peak - 1.0 ) / ( 1.0 + hdrGamutCompression * ( peak - 1.0 ) );
		color *= compressedPeak / peak;
	}

	return color;
}

vec3 ToneMapHDR( vec3 color ) {
	color = SceneReferredHDRColor( color );
	float safeExposure = max( hdrExposure, 0.001 );
	vec3 exposedColor = color * safeExposure;
	float safeWhitePoint = max( hdrWhitePoint, 1.0 );
	float shoulderStart = 0.98;
	float exposedWhitePoint = max( safeWhitePoint * safeExposure, shoulderStart + 0.001 );
	float shoulderRange = max( exposedWhitePoint - shoulderStart, 0.001 );
	float shoulderNorm = max( 1.0 - exp( -4.0 ), 0.0001 );
	vec3 shoulderT = max( exposedColor - vec3( shoulderStart ), vec3( 0.0 ) ) / shoulderRange;
	vec3 shoulderColor = vec3( shoulderStart ) + ( 1.0 - shoulderStart ) * ( vec3( 1.0 ) - exp( -shoulderT * 4.0 ) ) / shoulderNorm;
	vec3 mappedColor = mix( exposedColor, shoulderColor, step( vec3( shoulderStart ), exposedColor ) );
	return clamp( HighlightCompress( mappedColor ), 0.0, 1.0 );
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

vec3 ApplyColorAdjustments( vec3 color ) {
	color = ApplyLiftGammaGain( color );
	color = ApplyVibrance( color );

	float luma = dot( color, vec3( 0.2126, 0.7152, 0.0722 ) );
	color = mix( vec3( luma ), color, hdrSaturation );
	color = ( color - 0.5 ) * hdrContrast + 0.5;
	return clamp( color, 0.0, 1.0 );
}

vec3 DebugHeatmap( float scenePeak ) {
	float mapped = clamp( ( log2( max( scenePeak, 0.0001 ) ) + 8.0 ) / 8.0, 0.0, 1.0 );

	vec3 c0 = vec3( 0.02, 0.05, 0.16 );
	vec3 c1 = vec3( 0.00, 0.55, 0.95 );
	vec3 c2 = vec3( 0.18, 0.84, 0.18 );
	vec3 c3 = vec3( 0.98, 0.78, 0.08 );
	vec3 c4 = vec3( 0.95, 0.14, 0.05 );

	if ( mapped < 0.25 ) {
		return mix( c0, c1, mapped / 0.25 );
	}
	if ( mapped < 0.5 ) {
		return mix( c1, c2, ( mapped - 0.25 ) / 0.25 );
	}
	if ( mapped < 0.75 ) {
		return mix( c2, c3, ( mapped - 0.5 ) / 0.25 );
	}
	return mix( c3, c4, ( mapped - 0.75 ) / 0.25 );
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec4 sceneSample = texture2D( Scene, uv );
	vec3 sceneColor = SceneReferredHDRColor( sceneSample.rgb );
	vec3 color = sceneColor;

	if ( bloomEnabled > 0.5 && bloomIntensity > 0.0001 ) {
		color += SampleBloom( uv ) * bloomIntensity;
	}

	if ( hdrDebugView > 0.5 ) {
		float scenePeak = max( max( sceneColor.r, sceneColor.g ), sceneColor.b );
		if ( hdrDebugView > 1.5 ) {
			float grayscale = clamp( ( log2( max( scenePeak, 0.0001 ) ) + 10.0 ) / 10.0, 0.0, 1.0 );
			gl_FragColor = vec4( vec3( grayscale ), sceneSample.a );
		} else {
			gl_FragColor = vec4( DebugHeatmap( scenePeak ), sceneSample.a );
		}
		return;
	}

	if ( toneMapEnabled > 0.5 ) {
		color = ToneMapHDR( color );
		color = ApplyColorAdjustments( color );
	}

	gl_FragColor = vec4( color, sceneSample.a );
}
