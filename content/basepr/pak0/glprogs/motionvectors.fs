uniform sampler2D DepthBuffer;
uniform vec2 viewportSize;

varying vec4 currentClipPosition;
varying vec4 previousClipPosition;

void main() {
	if ( currentClipPosition.w <= 0.00001 || previousClipPosition.w <= 0.00001 ) {
		gl_FragColor = vec4( 0.0 );
		return;
	}

	vec2 currentUV = currentClipPosition.xy / currentClipPosition.w * 0.5 + 0.5;
	if ( currentUV.x < 0.0 || currentUV.y < 0.0 || currentUV.x > 1.0 || currentUV.y > 1.0 ) {
		discard;
	}

	float sceneDepth = texture2D( DepthBuffer, currentUV ).x;
	if ( sceneDepth >= 0.99999 ) {
		discard;
	}

	float depthTolerance = max( 0.00008, sceneDepth * 0.00005 );
	if ( abs( sceneDepth - gl_FragCoord.z ) > depthTolerance ) {
		discard;
	}

	vec2 previousUV = previousClipPosition.xy / previousClipPosition.w * 0.5 + 0.5;
	vec2 velocityPixels = ( currentUV - previousUV ) * viewportSize;
	gl_FragColor = vec4( velocityPixels, 0.0, 1.0 );
}
