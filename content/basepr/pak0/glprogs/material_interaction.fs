#version 110

uniform sampler2D uBumpMap;
uniform sampler2D uLightFalloffMap;
uniform sampler2D uLightProjectionMap;
uniform sampler2D uDiffuseMap;
uniform sampler2D uSpecularMap;

uniform vec4 uDiffuseColor;
uniform vec4 uSpecularColor;
uniform float uMaterialNormalScale;
uniform float uMaterialSpecularBoost;
uniform float uMaterialFresnel;
uniform float uStockInteraction;
uniform float uAmbientLight;
uniform samplerCube uAmbientNormalMap;
uniform vec4 uCelParams;
uniform vec4 uFlatDiffuseParams;

varying vec2 vBumpTexCoord;
varying vec2 vDiffuseTexCoord;
varying vec2 vSpecularTexCoord;
varying vec4 vLightFalloffTexCoord;
varying vec4 vLightProjectionTexCoord;
varying vec3 vLightVector;
varying vec3 vHalfAngleVector;
varying vec3 vViewVector;
varying vec3 vVertexColor;

vec3 SafeNormalize( vec3 value ) {
	return value * inversesqrt( max( dot( value, value ), 1.0e-8 ) );
}

// uFlatDiffuseParams = ( strength, local min Z, inverse height, upward phase ).
// A wrapped, feathered band avoids a visible reset when it reaches the top.
vec3 ApplyFlatDiffuseSweep( vec3 diffuse, float localZ ) {
	if ( uFlatDiffuseParams.x <= 0.0 ) {
		return diffuse;
	}
	float height = clamp( ( localZ - uFlatDiffuseParams.y ) * uFlatDiffuseParams.z, 0.0, 1.0 );
	float distanceToBand = abs( height - fract( uFlatDiffuseParams.w ) );
	distanceToBand = min( distanceToBand, 1.0 - distanceToBand );
	float band = 1.0 - smoothstep( 0.045, 0.16, distanceToBand );
	return mix( diffuse, vec3( 1.0 ), uFlatDiffuseParams.x * band );
}

// ---------------------------------------------------------------------------
// Cel banding. uCelParams is ( bandsEnabled, bandCount, hardSpecular, softness ).
// The same ladder is shared with R_CelQuantizeUnitValue on the CPU side.
// ---------------------------------------------------------------------------

float CelSteps() {
	return max( uCelParams.y - 1.0, 1.0 );
}

// Places a 0..1 value on the band ladder. uCelParams.w widens every boundary
// into a smoothstep centred exactly where the hard step would have landed, so
// the plateaus survive but the transition stops being one texel wide - which
// is what kept a terminator crawling across a curved surface as the camera
// moved. At 0 this is floor( value * steps + 0.5 ) / steps again, to the bit.
float CelLadder( float value ) {
	float steps = CelSteps();
	float scaled = value * steps;

	float softness = clamp( uCelParams.w, 0.0, 1.0 );
	if ( softness <= 0.0 ) {
		return floor( scaled + 0.5 ) / steps;
	}

	// floor( scaled ) names the band below this value and the boundary above it
	// sits half a band away, so the blend window is centred on 0.5.
	float lower = floor( scaled );
	float halfWidth = softness * 0.5;
	float blend = smoothstep( 0.5 - halfWidth, 0.5 + halfWidth, scaled - lower );

	return ( lower + blend ) / steps;
}

// Quantizes a light contribution without shifting its hue: the brightest
// channel picks the band and the others follow it. Black and overbright pass
// through untouched so unlit surfaces stay unlit and headroom is preserved.
vec3 CelQuantizeLight( vec3 light ) {
	if ( uCelParams.x <= 0.5 ) {
		return light;
	}

	float peak = max( max( light.r, light.g ), light.b );
	if ( peak <= 0.0 || peak >= 1.0 ) {
		return light;
	}

	return light * ( CelLadder( peak ) / peak );
}

// Collapses the specular falloff into flat plateaus on the same ladder, which
// is what gives cel highlights their hard edge.
float CelSpecularTerm( float term ) {
	if ( uCelParams.x <= 0.5 || uCelParams.z <= 0.5 ) {
		return term;
	}

	return CelLadder( clamp( term, 0.0, 1.0 ) );
}

vec3 DecodeLocalNormal( vec4 bumpSample ) {
	vec2 localNormalXY = vec2( bumpSample.a, bumpSample.g ) * 2.0 - 1.0;
	localNormalXY *= max( uMaterialNormalScale, 0.0 );

	float xyLengthSq = dot( localNormalXY, localNormalXY );
	if ( xyLengthSq > 1.0 ) {
		localNormalXY *= inversesqrt( xyLengthSq );
		xyLengthSq = 1.0;
	}

	float encodedZ = max( bumpSample.b * 2.0 - 1.0, 0.0 );
	float reconstructedZ = sqrt( max( 1.0 - xyLengthSq, 0.0 ) );
	return SafeNormalize( vec3( localNormalXY, mix( encodedZ, reconstructedZ, 0.75 ) ) );
}

vec3 DecodeStockLocalNormal( vec4 bumpSample ) {
	return vec3( bumpSample.a, bumpSample.g, bumpSample.b ) * 2.0 - 1.0;
}

float EnhancedSpecularTerm( vec3 halfAngle, vec3 viewDir, vec3 localNormal, vec3 specularSample ) {
	float ndoth = max( dot( halfAngle, localNormal ), 0.0 );
	float ndotv = max( dot( viewDir, localNormal ), 0.0 );
	float gloss = clamp( max( max( specularSample.r, specularSample.g ), specularSample.b ), 0.0, 1.0 );
	float specularPower = mix( 10.0, 40.0, gloss );
	float fresnel = 1.0 + ( pow( 1.0 - ndotv, 5.0 ) * 2.0 * clamp( uMaterialFresnel, 0.0, 1.0 ) );
	return pow( ndoth, specularPower ) * max( uMaterialSpecularBoost, 0.0 ) * fresnel;
}

float StockSpecularTerm( vec3 halfAngle, vec3 localNormal ) {
	float specular = clamp( dot( halfAngle, localNormal ) * 4.0 - 3.0, 0.0, 1.0 );
	return specular * specular * 2.0;
}

void main() {
	vec4 bumpSample = texture2D( uBumpMap, vBumpTexCoord );
	vec3 localNormal = ( uStockInteraction > 0.5 )
		? DecodeStockLocalNormal( bumpSample )
		: DecodeLocalNormal( bumpSample );

	// Stock ambient lights replace the normalization cube with the generated
	// ambient cube. Sampling the actual image preserves its historical channel
	// packing and quantization instead of reconstructing a subtly different
	// direction from the renderer-side float.
	vec3 ambientLightDir = textureCube( uAmbientNormalMap, vec3( 0.0, 0.0, 1.0 ) ).rgb * 2.0 - 1.0;
	vec3 lightDir = ( uAmbientLight > 0.5 ) ? ambientLightDir : SafeNormalize( vLightVector );
	float ndotl = max( dot( lightDir, localNormal ), 0.0 );

	vec3 light = vec3( ndotl );
	light *= texture2DProj( uLightFalloffMap, vLightFalloffTexCoord ).rgb;
	light *= texture2DProj( uLightProjectionMap, vLightProjectionTexCoord ).rgb;

	vec3 diffuse = texture2D( uDiffuseMap, vDiffuseTexCoord ).rgb * uDiffuseColor.rgb;
	diffuse = ApplyFlatDiffuseSweep( diffuse, vLightFalloffTexCoord.z );

	vec3 specularSample = texture2D( uSpecularMap, vSpecularTexCoord ).rgb;
	// The stock ARB2 interaction reads the half-angle back through the
	// normalization cube map before the specular term. The interpolated
	// varying is the sum of two unit vectors pushed through the tangent
	// frame, so its magnitude reaches 2; feeding that straight into
	// clamp( dot * 4 - 3 ) pins the term at 1 across most of the lit
	// hemisphere and blows specular out on every stock surface. Normalize
	// unconditionally, which is what the cube-map lookup did.
	vec3 halfAngle = SafeNormalize( vHalfAngleVector );
	vec3 viewDir = SafeNormalize( vViewVector );
	float specularTerm = ( uStockInteraction > 0.5 )
		? StockSpecularTerm( halfAngle, localNormal )
		: EnhancedSpecularTerm( halfAngle, viewDir, localNormal, specularSample );
	vec3 specular = specularSample * uSpecularColor.rgb * CelSpecularTerm( specularTerm );

	vec3 color = ( diffuse + specular ) * CelQuantizeLight( light ) * vVertexColor;
	gl_FragColor = vec4( color, 0.0 );
}
