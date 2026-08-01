uniform mat4 previousModelViewProjection;
uniform vec2 viewportSize;

varying vec4 currentClipPosition;
varying vec4 previousClipPosition;

void main() {
	vec4 position = gl_Vertex;
	currentClipPosition = ftransform();
	previousClipPosition = previousModelViewProjection * position;
	gl_Position = currentClipPosition;
}
