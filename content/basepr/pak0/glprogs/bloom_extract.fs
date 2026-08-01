uniform sampler2D Scene;
uniform vec2 invTexSize;
uniform float bloomThreshold;
uniform float bloomSoftKnee;

vec3 SceneReferredBloomColor( vec3 color ) {
	return max( color, vec3( 0.0 ) );
}

float BloomBrightness( vec3 color ) {
	float luminance = dot( color, vec3( 0.2126, 0.7152, 0.0722 ) );
	float peak = max( max( color.r, color.g ), color.b );
	// Pure luminance misses saturated HDR glows such as red weapon/plasma
	// highlights, while a pure max-channel threshold would make ordinary
	// saturated LDR textures glow. Blend a small amount of peak energy into
	// the threshold signal so only scene-referred saturated highlights survive.
	return mix( luminance, peak, 0.25 );
}

float BrightContribution( vec3 color ) {
	color = SceneReferredBloomColor( color );
	float brightness = BloomBrightness( color );
	float knee = max( bloomThreshold * bloomSoftKnee, 0.0 );

	if ( brightness <= 0.0001 ) {
		return 0.0;
	}

	if ( bloomThreshold <= 0.0001 ) {
		return 1.0;
	}

	float soft = 0.0;
	if ( knee > 0.0001 ) {
		soft = brightness - bloomThreshold + knee;
		soft = clamp( soft, 0.0, 2.0 * knee );
		soft = ( soft * soft ) / max( 4.0 * knee, 0.0001 );
	}

	float hard = max( brightness - bloomThreshold, 0.0 );
	float contribution = max( hard, soft );
	return contribution / brightness;
}

vec3 FilteredScene( vec2 uv ) {
	vec2 texel = invTexSize;
	vec3 color = texture2D( Scene, uv ).rgb * 0.25;

	color += texture2D( Scene, uv + vec2( texel.x, 0.0 ) ).rgb * 0.125;
	color += texture2D( Scene, uv - vec2( texel.x, 0.0 ) ).rgb * 0.125;
	color += texture2D( Scene, uv + vec2( 0.0, texel.y ) ).rgb * 0.125;
	color += texture2D( Scene, uv - vec2( 0.0, texel.y ) ).rgb * 0.125;

	color += texture2D( Scene, uv + vec2( texel.x, texel.y ) ).rgb * 0.0625;
	color += texture2D( Scene, uv + vec2( -texel.x, texel.y ) ).rgb * 0.0625;
	color += texture2D( Scene, uv + vec2( texel.x, -texel.y ) ).rgb * 0.0625;
	color += texture2D( Scene, uv - vec2( texel.x, texel.y ) ).rgb * 0.0625;

	return color;
}

void main() {
	vec2 uv = gl_TexCoord[0].st;
	vec3 color = SceneReferredBloomColor( FilteredScene( uv ) );
	gl_FragColor = vec4( color * BrightContribution( color ), 1.0 );
}
