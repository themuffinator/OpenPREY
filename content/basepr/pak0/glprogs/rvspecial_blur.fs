uniform sampler2D Image;
uniform vec2 textureScale;
uniform float sampleDist;

void main() {
	vec2 uv = gl_TexCoord[0].st * textureScale;
	vec2 offset = vec2( sampleDist, sampleDist );

	vec4 color = texture2D( Image, uv ) * 0.227027;
	color += texture2D( Image, uv + vec2( offset.x, 0.0 ) ) * 0.1945946;
	color += texture2D( Image, uv - vec2( offset.x, 0.0 ) ) * 0.1945946;
	color += texture2D( Image, uv + vec2( 0.0, offset.y ) ) * 0.1945946;
	color += texture2D( Image, uv - vec2( 0.0, offset.y ) ) * 0.1945946;
	color += texture2D( Image, uv + offset ) * 0.0945946;
	color += texture2D( Image, uv - offset ) * 0.0945946;

	gl_FragColor = vec4( color.rgb, 1.0 );
}
