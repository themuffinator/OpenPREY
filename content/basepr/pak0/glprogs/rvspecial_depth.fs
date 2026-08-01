uniform sampler2D Image;
uniform float distanceScale;

float LinearizeDepth( float depth ) {
	const float zNear = 0.25;
	float ndcDepth = depth * 2.0 - 1.0;
	float denom = 0.999 - ndcDepth;
	if ( denom < 0.001 ) {
		denom = 0.001;
	}

	return ( 2.0 * zNear ) / denom;
}

void main() {
	float alpha = texture2D( Image, gl_TexCoord[0].st ).a;
	float linearDepth = LinearizeDepth( gl_FragCoord.z );
	float normalizedDepth = clamp( linearDepth / max( distanceScale, 1.0 ), 0.0, 1.0 );
	gl_FragColor = vec4( normalizedDepth, normalizedDepth, normalizedDepth, alpha );
}
