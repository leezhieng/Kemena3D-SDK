// ===========================================================================
// TerrainPBR.glsl — PBR Terrain Shader with 4-channel texture splatting
// ===========================================================================
//
// Uses a RGBA8 control (splat) map to blend up to four terrain materials,
// each with its own albedo, normal, roughness, metallic, and AO texture.
// Lighting: PBR (GGX + Smith + Schlick Fresnel) with CSM shadows.
//
// No height displacement — vertex positions carry the height directly.
// ===========================================================================

// --- VERTEX ---
#version 330 core

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexColor;
layout(location = 2) in vec2 texCoord;
layout(location = 3) in vec3 vertexNormal;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 normalMatrix;

out vec3 v_worldPos;
out vec3 v_color;
out vec2 v_texCoord;

void main()
{
    vec4 worldPos = modelMatrix * vec4(vertexPosition, 1.0);
    // No height displacement — vertex Y positions are set directly from terrain height data.
    v_worldPos = worldPos.xyz;
    v_color    = vertexColor;
    v_texCoord = texCoord;

    gl_Position = projectionMatrix * viewMatrix * worldPos;
}

// --- FRAGMENT ---
#version 330 core

// ===========================================================================
// Material
// ===========================================================================
struct Material
{
    vec3  diffuse;
    vec3  ambient;
    vec3  specular;
    float shininess;
    float metallic;
    float roughness;
};
uniform Material material;

// ===========================================================================
// Light structures
// ===========================================================================
struct SunLight
{
    float power;
    vec3  direction;
    vec3  diffuse;
    vec3  specular;
};

struct PointLight
{
    float power;
    vec3  position;
    float constant;
    float linear;
    float quadratic;
    vec3  diffuse;
    vec3  specular;
};

struct SpotLight
{
    float power;
    vec3  position;
    vec3  direction;
    float cutOff;
    float outerCutOff;
    float constant;
    float linear;
    float quadratic;
    vec3  diffuse;
    vec3  specular;
};

uniform int        sunLightNum;
uniform SunLight   sunLights[32];
uniform int        pointLightNum;
uniform PointLight pointLights[32];
uniform int        spotLightNum;
uniform SpotLight  spotLights[32];

uniform vec3        sceneAmbient;
uniform samplerCube skyboxMap;
uniform bool        skyboxAmbientEnabled;
uniform float       skyboxAmbientStrength;

// ===========================================================================
// CSM (Cascaded Shadow Maps)
// ===========================================================================
uniform mat4           viewMatrix;
uniform sampler2DArray shadowMapArray;
uniform mat4           lightSpaceMatrices[4];
uniform vec4           cascadeSplits;
uniform int            cascadeCount;
uniform float          shadowResolution;
uniform bool           enableShadow;
uniform bool           receiveShadow;
uniform float          shadowBias;
uniform float          shadowNormalBias;
uniform float          shadowNormalOffset;
uniform float          shadowSoftness;

// ===========================================================================
// Terrain textures
// ===========================================================================
uniform sampler2D u_SplatMap;          // RGBA8 control map
uniform sampler2D u_AlbedoMap[4];      // 4 albedo/diffuse textures
uniform sampler2D u_NormalMap[4];      // 4 normal maps
uniform sampler2D u_RoughnessMap[4];   // 4 roughness maps
uniform sampler2D u_MetalnessMap[4];   // 4 metallic maps
uniform sampler2D u_AOMap[4];          // 4 AO maps
uniform float     u_Tiling[4];         // Per-layer UV tiling scale
uniform float     u_BlendSharpness;    // Blend edge sharpness (0.5 - 4.0)

// ===========================================================================
// Varying inputs
// ===========================================================================
in vec3 v_worldPos;
in vec3 v_color;
in vec2 v_texCoord;

out vec4 fragColor;

// ===========================================================================
// CSM shadow map helpers
//
// Follows the Microsoft DX tech articles: normal-offset sampling, constant +
// slope-scaled receiver bias, softness-driven PCF, and a smooth blend across
// the cascade boundary.
// ===========================================================================
float csmSplit(int i)
{
    if (i == 0) return cascadeSplits.x;
    if (i == 1) return cascadeSplits.y;
    if (i == 2) return cascadeSplits.z;
    return cascadeSplits.w;
}

// World-space size of one shadow-map texel for a cascade (light ortho [0][0]==1/radius).
float csmTexelWorld(int layer)
{
    float m = abs(lightSpaceMatrices[layer][0][0]);
    return (m > 0.0) ? 2.0 / (m * max(shadowResolution, 1.0)) : 0.0;
}

// Constant + slope-scaled receiver bias (clamped tan(acos(N·L))).
float csmBias(vec3 n, vec3 l)
{
    float ndl = max(dot(normalize(n), normalize(l)), 0.0);
    return shadowBias + shadowNormalBias * min(tan(acos(max(ndl, 1e-3))), 8.0);
}

bool csmProject(int layer, vec3 wp, out vec3 p)
{
    vec4 ls = lightSpaceMatrices[layer] * vec4(wp, 1.0);
    p = ls.xyz / ls.w;
    p = p * 0.5 + 0.5;
    return p.z <= 1.0 && p.x >= 0.0 && p.x <= 1.0 && p.y >= 0.0 && p.y <= 1.0;
}

float csmPCF(int layer, vec3 p, float bias)
{
    vec2 ts = (1.0 / vec2(textureSize(shadowMapArray, 0).xy)) * max(shadowSoftness, 0.5);
    float s = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            s += (p.z - bias > texture(shadowMapArray, vec3(p.xy + vec2(x, y) * ts, float(layer))).r) ? 1.0 : 0.0;
    return s / 9.0;
}

float csmShadow(vec3 wp, vec3 n, vec3 lightDir)
{
    if (!enableShadow || !receiveShadow || cascadeCount <= 0) return 0.0;
    vec3 l = normalize(-lightDir);
    float fd = abs((viewMatrix * vec4(wp, 1.0)).z);
    int layer = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; i++)
        if (fd < csmSplit(i)) { layer = i; break; }
    // Normal-offset, then constant + slope-scaled depth bias.
    vec3 samplePos = wp + normalize(n) * (csmTexelWorld(layer) * shadowNormalOffset);
    float bias = csmBias(n, l);
    vec3 p;
    if (!csmProject(layer, samplePos, p)) return 0.0;
    float sh = csmPCF(layer, p, bias);
    float sf = csmSplit(layer);
    float band = sf * 0.1;
    if (layer + 1 < cascadeCount && fd > sf - band)
    {
        vec3 p2;
        if (csmProject(layer + 1, samplePos, p2))
        {
            float t = clamp((fd - (sf - band)) / max(band, 1e-4), 0.0, 1.0);
            sh = mix(sh, csmPCF(layer + 1, p2, bias), t);
        }
    }
    return sh;
}

// ===========================================================================
// PBR lighting (GGX + Smith + Schlick Fresnel)
// ===========================================================================
const float PI = 3.14159265359;

float distGGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geoSchlick(float ndotv, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / (ndotv * (1.0 - k) + k);
}

float geoSmith(float NdotV, float NdotL, float roughness)
{
    return geoSchlick(NdotV, roughness) * geoSchlick(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 calcPBR(vec3 albedo, float metallic, float roughness, vec3 F0,
             vec3 n, vec3 v, vec3 l, vec3 radiance)
{
    vec3  h     = normalize(v + l);
    float NdotH = max(dot(n, h),   0.0);
    float NdotV = max(dot(n, v),   0.0);
    float NdotL = max(dot(n, l),   0.0);
    float NDF   = distGGX(NdotH, roughness);
    float G     = geoSmith(NdotV, NdotL, roughness);
    vec3  F     = fresnelSchlick(max(dot(h, v), 0.0), F0);
    vec3  kD    = (1.0 - F) * (1.0 - metallic);
    vec3  spec  = NDF * G * F / (4.0 * NdotV * NdotL + 0.0001);
    return (kD * albedo / PI + spec) * radiance * NdotL;
}

// Fresnel with a roughness-aware ceiling (see mesh_pbr.glsl).
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
           pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Analytic split-sum specular BRDF approximation (Karis, UE4).
vec3 envBRDFApprox(vec3 specularColor, float roughness, float NoV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04,  -0.04);
    vec4  r    = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2  AB   = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * AB.x + AB.y;
}

// ===========================================================================
// Height blend helper: sharpens transitions between layers
// ===========================================================================
float heightBlend(float weight, float height, float sharpness)
{
    return clamp(weight * sharpness - (sharpness - 1.0) * 0.5, 0.0, 1.0);
}

// ===========================================================================
// Compute normal from world position via screen-space derivatives.
// This is robust for terrain — works regardless of vertex normal/tangent quality.
// ===========================================================================
vec3 computeDisplacedNormal(vec3 worldPos)
{
    vec3 dx = dFdx(worldPos);
    vec3 dy = dFdy(worldPos);
    return normalize(cross(dx, dy));
}

// ===========================================================================
// main()
// ===========================================================================
void main()
{
    // ----- Sample splat map -------------------------------------------------
    vec4 splat = texture(u_SplatMap, v_texCoord);

    // ----- Accumulate blended PBR inputs ------------------------------------
    vec3 albedo    = vec3(0.0);
    vec3 normal    = vec3(0.0, 0.0, 1.0); // tangent-space normal accumulation
    float roughness = 0.0;
    float metallic  = 0.0;
    float ao        = 0.0;
    float totalWeight = 0.0;

    // ----- Per-layer sampling -----------------------------------------------
    for (int i = 0; i < 4; i++)
    {
        float weight = splat[i];
        if (weight <= 0.001)
            continue;

        vec2 uv = v_texCoord * u_Tiling[i];

        float aWeight = weight; // base albedo weight

        // Height blending using the albedo luminance as a proxy height
        {
            vec4 a = texture(u_AlbedoMap[i], uv);
            float h = dot(a.rgb, vec3(0.299, 0.587, 0.114)); // luminance as height
            aWeight = heightBlend(weight, h, u_BlendSharpness);
        }

        // Sample albedo
        vec4 albedoSample = texture(u_AlbedoMap[i], uv);
        albedo += albedoSample.rgb * aWeight;

        // Sample normal map
        vec3 normSample = texture(u_NormalMap[i], uv).rgb;
        normal += normSample * aWeight;

        // Sample roughness
        roughness += texture(u_RoughnessMap[i], uv).r * aWeight;

        // Sample metallic
        metallic += texture(u_MetalnessMap[i], uv).r * aWeight;

        // Sample AO
        ao += texture(u_AOMap[i], uv).r * aWeight;

        totalWeight += aWeight;
    }

    // ----- Normalize blended results ----------------------------------------
    if (totalWeight > 0.001)
    {
        albedo    /= totalWeight;
        normal    /= totalWeight;
        roughness /= totalWeight;
        metallic  /= totalWeight;
        ao        /= totalWeight;
    }
    else
    {
        // Fallback: white albedo, flat normal, default roughness/metallic
        albedo    = vec3(0.5);
        normal    = vec3(0.5, 0.5, 1.0);
        roughness = 0.5;
        metallic  = 0.0;
        ao        = 1.0;
    }

    // ----- Compute geometric normal from displaced world position -----------
    // Using screen-space derivatives (dFdx/dFdy) — always valid, no TBN needed.
    vec3 N = computeDisplacedNormal(v_worldPos);

    // ----- PBR lighting -----------------------------------------------------
    vec3 v     = normalize(vec3(0.0) - v_worldPos); // view direction (toward camera)
    vec3 F0    = mix(vec3(0.04), albedo, metallic);
    vec3 Lo    = vec3(0.0);

    // Sun lights (directional)
    float shadow = csmShadow(v_worldPos, N,
                             sunLightNum > 0 ? sunLights[0].direction : vec3(0.0, -1.0, 0.0));
    for (int i = 0; i < sunLightNum; i++)
    {
        vec3 l = normalize(-sunLights[i].direction);
        vec3 radiance = sunLights[i].diffuse * sunLights[i].power;
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l, radiance) * (1.0 - shadow);
    }

    // Point lights
    for (int i = 0; i < pointLightNum; i++)
    {
        vec3 l = normalize(pointLights[i].position - v_worldPos);
        float dist = length(pointLights[i].position - v_worldPos);
        float att  = 1.0 / (pointLights[i].constant + pointLights[i].linear * dist +
                            pointLights[i].quadratic * dist * dist);
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l, pointLights[i].diffuse * att);
    }

    // Spot lights
    for (int i = 0; i < spotLightNum; i++)
    {
        vec3 l    = normalize(spotLights[i].position - v_worldPos);
        float theta = dot(l, normalize(-spotLights[i].direction));
        float eps   = spotLights[i].cutOff - spotLights[i].outerCutOff;
        float intens = clamp((theta - spotLights[i].outerCutOff) / eps, 0.0, 1.0);
        float dist   = length(spotLights[i].position - v_worldPos);
        float att    = 1.0 / (spotLights[i].constant + spotLights[i].linear * dist +
                              spotLights[i].quadratic * dist * dist);
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l,
                      spotLights[i].diffuse * att * intens);
    }

    // ----- Ambient ----------------------------------------------------------
    vec3 ambient = sceneAmbient * albedo * ao;

    // Skybox ambient — physically-based (split-sum IBL) so the terrain's
    // metallic / roughness / normal (and per-layer AO) shape the result
    // instead of the sky simply being overlaid.
    if (skyboxAmbientEnabled)
    {
        float NdotV = max(dot(N, v), 0.0);
        vec3  R     = reflect(-v, N);
        vec3  irradiance  = textureLod(skyboxMap, N, 8.0).rgb;
        vec3  prefiltered = textureLod(skyboxMap, R, roughness * 6.0).rgb;

        vec3 F_amb = fresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kD    = (1.0 - F_amb) * (1.0 - metallic);

        ambient += (kD * albedo * irradiance +
                    prefiltered * envBRDFApprox(F0, roughness, NdotV)) *
                   skyboxAmbientStrength * ao;
    }

    // ----- Final color ------------------------------------------------------
    fragColor = vec4(ambient + Lo, 1.0);
}
