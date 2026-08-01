uniform sampler2D RT;
uniform sampler2D LightImage;
uniform float distanceScale;
uniform vec3 LightLoc;
uniform vec4 LightColor;
uniform float LightSize;
uniform float LightMinDistance;

void main() {
	vec2 screenUV = gl_TexCoord[0].st;
	vec2 lightUV = gl_TexCoord[1].st;

	float sceneDepth = texture2D( RT, screenUV ).r;
	if ( sceneDepth <= 0.001 ) {
		sceneDepth = 1.0;
	}

	float sprite = texture2D( LightImage, lightUV ).a;
	float normalizedLightDepth = clamp( LightMinDistance / max( distanceScale, 1.0 ), 0.0, 1.0 );
	float normalizedLightRadius = max( LightSize / max( distanceScale, 1.0 ), 0.0005 );
	float occlusion = smoothstep(
		normalizedLightDepth - normalizedLightRadius * 0.25,
		normalizedLightDepth + normalizedLightRadius,
		sceneDepth );
	float intensity = sprite * occlusion * 0.35;

	gl_FragColor = vec4( LightColor.rgb * intensity, LightColor.a * intensity );
}
