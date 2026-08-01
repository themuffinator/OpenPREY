void main() {
	gl_Position = ftransform();
	gl_TexCoord[1] = gl_MultiTexCoord1;
}
