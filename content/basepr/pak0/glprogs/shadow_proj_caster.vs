#version 110

uniform vec4 uAlphaTexCoordS;
uniform vec4 uAlphaTexCoordT;
uniform vec4 uShadowDepthRow;
uniform vec4 uModelMatrixRow0;
uniform vec4 uModelMatrixRow1;
uniform vec4 uModelMatrixRow2;

varying vec2 vAlphaTexCoord;
varying vec3 vAlphaHashCoord;
varying float vShadowDepth;

void main() {
	vec4 alphaTexCoord = vec4( gl_MultiTexCoord0.xy, 0.0, 1.0 );
	vec4 position = gl_Vertex;
	vAlphaTexCoord = vec2( dot( alphaTexCoord, uAlphaTexCoordS ), dot( alphaTexCoord, uAlphaTexCoordT ) );
	vAlphaHashCoord = vec3(
		dot( position, uModelMatrixRow0 ),
		dot( position, uModelMatrixRow1 ),
		dot( position, uModelMatrixRow2 ) );
	vShadowDepth = dot( position, uShadowDepthRow );
	gl_Position = ftransform();
}
