uniform sampler2D Scene;
uniform vec2 invTexSize;

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
	gl_FragColor = vec4( max( FilteredScene( gl_TexCoord[0].st ), vec3( 0.0 ) ), 1.0 );
}
