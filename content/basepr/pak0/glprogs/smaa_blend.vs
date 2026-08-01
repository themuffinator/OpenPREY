#version 130

in vec2 attr_Position;
in vec2 attr_TexCoord0;

out vec2 v_TexCoord;

void main() {
	v_TexCoord = attr_TexCoord0;
	gl_Position = vec4( attr_Position, 0.0, 1.0 );
}
