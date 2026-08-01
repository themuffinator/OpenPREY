uniform sampler2D Depth;
uniform sampler2D Blur1;
uniform float effectRange;
uniform float focus;
uniform float scroll;
uniform vec4 approachColor;
uniform float approachPercent;

void main() {
	vec2 uv = gl_TexCoord[0].st;
	float depth = texture2D( Depth, uv ).r;
	vec3 blurredScene = texture2D( Blur1, uv ).rgb;

	float safeRange = max( effectRange, 0.01 );
	float clearBand = max( 0.015, 1.0 / ( safeRange * 6.0 ) );
	float blurFactor = smoothstep( clearBand, clearBand * ( 1.5 + safeRange * 0.5 ), abs( depth - focus ) );
	float tintAmount = clamp( approachPercent, 0.0, 1.0 );

	float variance = sin( ( uv.x + uv.y + scroll ) * 6.28318531 ) * 0.5 + 0.5;
	vec3 overlayColor = mix( blurredScene, approachColor.rgb, tintAmount * variance );
	float alpha = blurFactor * clamp( 0.6 + tintAmount * 0.4, 0.0, 1.0 );

	gl_FragColor = vec4( overlayColor, alpha );
}
