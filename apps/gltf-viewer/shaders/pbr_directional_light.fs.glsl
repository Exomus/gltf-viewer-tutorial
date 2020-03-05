#version 330
// INPUTS
in vec3 vViewSpacePosition;
in vec3 vViewSpaceNormal;
in vec2 vTexCoords;

//********** UNIFORMS ************
// LIGHT
uniform vec3 uLightDirection;
uniform vec3 uLightIntensity;

// BASE COLOR
uniform sampler2D uBaseColorTexture;
uniform vec4 uBaseColorFactor;

// METALLIC ROUGHNESS
uniform float uMetallicFactor;
uniform float uRoughnessFactor;
uniform sampler2D uMetallicRoughnessTexture;

// EMISSIVE
uniform sampler2D uEmissiveTexture;
uniform vec3 uEmissiveFactor;

// OCCLUSION
uniform sampler2D uOcclusionTexture;
uniform float uOcclusionStrength;

//********** OUTPUTS ***********
out vec3 fColor;

// Constants
const float GAMMA = 2.2;
const float INV_GAMMA = 1. / GAMMA;
const float M_PI = 3.141592653589793;
const float M_1_PI = 1.0 / M_PI;
const vec3 dielectricSpecular = vec3(0.04, 0.04, 0.04);
const vec3 black = vec3(0, 0, 0);

// We need some simple tone mapping functions
// Basic gamma = 2.2 implementation
// Stolen here: https://github.com/KhronosGroup/glTF-Sample-Viewer/blob/master/src/shaders/tonemapping.glsl

// linear to sRGB approximation
// see http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
vec3 LINEARtoSRGB(vec3 color)
{
    return pow(color, vec3(INV_GAMMA));
}

// sRGB to linear approximation
// see http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
vec4 SRGBtoLINEAR(vec4 srgbIn)
{
    return vec4(pow(srgbIn.xyz, vec3(GAMMA)), srgbIn.w);
}

void main()
{

    vec3 N = normalize(vViewSpaceNormal);
    vec3 L = uLightDirection;
    vec3 V = normalize(-vViewSpacePosition);
    vec3 H = normalize(L + V);

    vec4 baseColorVectorFromTexture = SRGBtoLINEAR(texture(uBaseColorTexture, vTexCoords));
    vec4 computedBaseColorVector = baseColorVectorFromTexture * uBaseColorFactor;

    vec4 metallicRoughnessVectorFromTexture = texture(uMetallicRoughnessTexture, vTexCoords);
    float computedMetallicValue = uMetallicFactor * metallicRoughnessVectorFromTexture.b;
    float computedRoughnessValue = uRoughnessFactor * metallicRoughnessVectorFromTexture.g;

    vec4 baseEmissiveVectorFromTexture = SRGBtoLINEAR(texture(uEmissiveTexture, vTexCoords));
    vec4 computedEmissiveVector = baseEmissiveVectorFromTexture * vec4(uEmissiveFactor, 0);

    vec4 baseOcclusionVectorFromTexture = texture(uOcclusionTexture, vTexCoords);

    vec3 c_diffuse = mix(computedBaseColorVector.rgb * (1 - dielectricSpecular.r), black, computedMetallicValue);
    vec3 F_O = mix(dielectricSpecular, computedBaseColorVector.rgb, computedMetallicValue);
    float alpha = computedRoughnessValue * computedRoughnessValue;

    float NdotL = clamp(dot(N, L), 0, 1);
    float NdotV = clamp(dot(N, V), 0, 1);
    float NdotH = clamp(dot(N, H), 0, 1);
    float VdotH = clamp(dot(V, H), 0, 1);


    float NdotLpow2 = NdotL * NdotL;
    float NdotVpow2 = NdotV * NdotV;
    float NdotHpow2 = NdotH * NdotH;

    float baseShlickFactor = (1 - VdotH);
    float shlickFactor = baseShlickFactor * baseShlickFactor;
    shlickFactor *= shlickFactor;
    shlickFactor *= baseShlickFactor;
    vec3 F = F_O + (1 - F_O) * shlickFactor;

    float alphaPow2 = alpha * alpha;
    float VisDenominator = NdotL * sqrt(NdotVpow2 * (1 - alphaPow2) + alphaPow2) + NdotV * sqrt(NdotLpow2 * (1 - alphaPow2) + alphaPow2);
    float Vis = 0;
    if (VisDenominator > 0) {
        Vis = 0.5 / VisDenominator;
    }

    float DDenominator = M_PI * (NdotHpow2 * (alphaPow2 - 1) + 1) * (NdotHpow2 * (alphaPow2 - 1) + 1);
    float D = 0;
    if (DDenominator > 0) {
        D = alphaPow2 / DDenominator;
    }

    vec3 diffuse = c_diffuse / M_PI;

    vec3 f_diffuse = (1 - F) * diffuse;
    vec3 f_specular = F * Vis * D;

    fColor = (f_diffuse + f_specular) * uLightIntensity * NdotL + vec3(computedEmissiveVector);
    fColor = mix(fColor, fColor * baseOcclusionVectorFromTexture.r, uOcclusionStrength);
    fColor = LINEARtoSRGB(fColor);
}