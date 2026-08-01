#version 110

uniform vec4 uModelMatrixRow0;
uniform vec4 uModelMatrixRow1;
uniform vec4 uModelMatrixRow2;

varying vec3 vWorldNormal;
varying vec3 vWorldPosition;

vec3 TransformVectorToWorld( vec3 localVector ) {
	return vec3(
		dot( localVector, uModelMatrixRow0.xyz ),
		dot( localVector, uModelMatrixRow1.xyz ),
		dot( localVector, uModelMatrixRow2.xyz ) );
}

void main() {
	vWorldNormal = normalize( TransformVectorToWorld( gl_Normal.xyz ) );
	vWorldPosition = vec3(
		dot( gl_Vertex, uModelMatrixRow0 ),
		dot( gl_Vertex, uModelMatrixRow1 ),
		dot( gl_Vertex, uModelMatrixRow2 ) );

	gl_Position = ftransform();
}
