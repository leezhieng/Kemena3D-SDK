// ===========================================================================
// TerrainPBR.hlsl — PBR Terrain Shader with 4-channel texture splatting (HLSL)
// ===========================================================================
//
// Semantically equivalent to TerrainPBR.glsl, using HLSL syntax for DX11/DX12.
// No height displacement — vertex positions carry the height directly.
//
// ===========================================================================

// --- VERTEX ---

cbuffer PerFrame : register(b0)
{
    float4x4 modelMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4x4 normalMatrix;
};

struct VS_INPUT
{
    float3 position   : POSITION;
    float3 color      : COLOR;
    float2 texCoord   : TEXCOORD;
    float3 normal     : NORMAL;
    float3 tangent    : TANGENT;
    float3 bitangent  : BITANGENT;
};

struct VS_OUTPUT
{
    float4 position   : SV_POSITION;
    float3 worldPos   : WORLDPOS;
    float3 color      : COLOR0;
    float2 texCoord   : TEXCOORD0;
    float3 T          : TEXCOORD1;
    float3 B          : TEXCOORD2;
    float3 N          : TEXCOORD3;
};

VS_OUTPUT mainVS(VS_INPUT input)
{
    VS_OUTPUT output;

    float4 worldPos = mul(modelMatrix, float4(input.position, 1.0));
    // No height displacement — vertex Y positions are set directly from terrain height data.
    output.worldPos = worldPos.xyz;
    output.color    = input.color;
    output.texCoord = input.texCoord;

    output.N = normalize(mul((float3x3)normalMatrix, input.normal));
    output.T = normalize(mul((float3x3)normalMatrix, input.tangent));
    output.B = normalize(mul((float3x3)normalMatrix, input.bitangent));

    output.position = mul(projectionMatrix, mul(viewMatrix, worldPos));
    return output;
}

// --- FRAGMENT ---

struct Material
{
    float3 diffuse;
    float3 ambient;
    float3 specular;
    float  shininess;
    float  metallic;
    float  roughness;
};

struct SunLight
{
    float  power;
    float3 direction;
    float3 diffuse;
    float3 specular;
};

struct PointLight
{
    float  power;
    float3 position;
    float  constant;
    float  linear;
    float  quadratic;
    float3 diffuse;
    float3 specular;
};

struct SpotLight
{
    float  power;
    float3 position;
    float3 direction;
    float  cutOff;
    float  outerCutOff;
    float  constant;
    float  linear;
    float  quadratic;
    float3 diffuse;
    float3 specular;
};

cbuffer MaterialUBO : register(b2)
{
    Material material;
};

cbuffer Lighting : register(b3)
{
    int      sunLightNum;
    SunLight sunLights[32];
    int      pointLightNum;
    PointLight pointLights[32];
    int      spotLightNum;
    SpotLight spotLights[32];
    float3   sceneAmbient;
    bool     skyboxAmbientEnabled;
    float    skyboxAmbientStrength;
};

cbuffer CSM : register(b4)
{
    float4x4 viewMatrix;
    float4x4 lightSpaceMatrices[4];
    float4   cascadeSplits;
    int      cascadeCount;
    float    shadowResolution;
    bool     enableShadow;
    bool     receiveShadow;
    float    shadowBias;
    float    shadowNormalBias;
    float    shadowNormalOffset;
    float    shadowSoftness;
};

// Textures
Texture2D    u_SplatMap          : register(t0);
Texture2D    u_AlbedoMap[4]      : register(t1);
Texture2D    u_NormalMap[4]      : register(t5);
Texture2D    u_RoughnessMap[4]   : register(t9);
Texture2D    u_MetalnessMap[4]   : register(t13);
Texture2D    u_AOMap[4]          : register(t17);
Texture2DArray shadowMapArray    : register(t21);
TextureCube  skyboxMap           : register(t22);

// Samplers
SamplerState s_LinearWrap        : register(s0);
SamplerComparisonState s_Shadow  : register(s1);

// Uniforms
float u_Tiling[4];
float u_BlendSharpness;

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLDPOS;
    float3 color    : COLOR0;
    float2 texCoord : TEXCOORD0;
    float3 T        : TEXCOORD1;
    float3 B        : TEXCOORD2;
    float3 N        : TEXCOORD3;
};

// CSM helpers
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
float csmBias(float3 n, float3 l)
{
    float ndl = max(dot(normalize(n), normalize(l)), 0.0);
    return shadowBias + shadowNormalBias * min(tan(acos(max(ndl, 1e-3))), 8.0);
}

bool csmProject(int layer, float3 wp, out float3 p)
{
    float4 ls = mul(lightSpaceMatrices[layer], float4(wp, 1.0));
    p = ls.xyz / ls.w;
    p = p * 0.5 + 0.5;
    return p.z <= 1.0 && p.x >= 0.0 && p.x <= 1.0 && p.y >= 0.0 && p.y <= 1.0;
}

float csmPCF(int layer, float3 p, float bias)
{
    uint w, h, elems;
    shadowMapArray.GetDimensions(0, w, h, elems);
    float2 texel = (1.0 / float2(w, h)) * max(shadowSoftness, 0.5);
    float s = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
            s += shadowMapArray.SampleCmpLevelZero(s_Shadow, float3(p.xy + float2(x, y) * texel, float(layer)), p.z - bias) ? 1.0 : 0.0;
    return s / 9.0;
}

float csmShadow(float3 wp, float3 n, float3 lightDir)
{
    if (!enableShadow || !receiveShadow || cascadeCount <= 0) return 0.0;
    float3 l = normalize(-lightDir);
    float fd = abs(mul(viewMatrix, float4(wp, 1.0)).z);
    int layer = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; i++)
        if (fd < csmSplit(i)) { layer = i; break; }
    // Normal-offset, then constant + slope-scaled depth bias.
    float3 samplePos = wp + normalize(n) * (csmTexelWorld(layer) * shadowNormalOffset);
    float bias = csmBias(n, l);
    float3 p;
    if (!csmProject(layer, samplePos, p)) return 0.0;
    float sh = csmPCF(layer, p, bias);
    float sf = csmSplit(layer);
    float band = sf * 0.1;
    if (layer + 1 < cascadeCount && fd > sf - band)
    {
        float3 p2;
        if (csmProject(layer + 1, samplePos, p2))
        {
            float t = clamp((fd - (sf - band)) / max(band, 1e-4), 0.0, 1.0);
            sh = lerp(sh, csmPCF(layer + 1, p2, bias), t);
        }
    }
    return sh;
}

// PBR lighting
static const float PI = 3.14159265359;

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

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 calcPBR(float3 albedo, float metallic, float roughness, float3 F0,
               float3 n, float3 v, float3 l, float3 radiance)
{
    float3 h     = normalize(v + l);
    float  NdotH = max(dot(n, h),   0.0);
    float  NdotV = max(dot(n, v),   0.0);
    float  NdotL = max(dot(n, l),   0.0);
    float  NDF   = distGGX(NdotH, roughness);
    float  G     = geoSmith(NdotV, NdotL, roughness);
    float3 F     = fresnelSchlick(max(dot(h, v), 0.0), F0);
    float3 kD    = (1.0 - F) * (1.0 - metallic);
    float3 spec  = NDF * G * F / (4.0 * NdotV * NdotL + 0.0001);
    return (kD * albedo / PI + spec) * radiance * NdotL;
}

// Fresnel with a roughness-aware ceiling (see mesh_pbr.hlsl).
float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max((float3)(1.0 - roughness), F0) - F0) *
           pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Analytic split-sum specular BRDF approximation (Karis, UE4).
float3 envBRDFApprox(float3 specularColor, float roughness, float NoV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572,  0.022);
    const float4 c1 = float4( 1.0,  0.0425,  1.04,  -0.04);
    float4 r    = roughness * c0 + c1;
    float  a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB   = float2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * AB.x + AB.y;
}

float heightBlend(float weight, float height, float sharpness)
{
    return clamp(weight * sharpness - (sharpness - 1.0) * 0.5, 0.0, 1.0);
}

// Compute normal from world position via screen-space derivatives.
float3 computeDisplacedNormal(float3 worldPos)
{
    float3 dx = ddx(worldPos);
    float3 dy = ddy(worldPos);
    return normalize(cross(dx, dy));
}

float3 perturbNormal(float3 normalMapSample, float3 T, float3 B, float3 N)
{
    float3 n = normalize(normalMapSample * 2.0 - 1.0);
    float3x3 TBN = float3x3(normalize(T), normalize(B), normalize(N));
    return normalize(mul(n, TBN));
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    // Sample splat map
    float4 splat = u_SplatMap.Sample(s_LinearWrap, input.texCoord);

    // Accumulate blended PBR inputs
    float3 albedo    = float3(0, 0, 0);
    float3 normal    = float3(0, 0, 1);
    float  roughness = 0.0;
    float  metallic  = 0.0;
    float  ao        = 0.0;
    float  totalWeight = 0.0;

    for (int i = 0; i < 4; i++)
    {
        float weight = splat[i];
        if (weight <= 0.001)
            continue;

        float2 uv = input.texCoord * u_Tiling[i];

        float aWeight = weight;

        // Height blending using albedo luminance as proxy height
        {
            float4 a = u_AlbedoMap[i].Sample(s_LinearWrap, uv);
            float h = dot(a.rgb, float3(0.299, 0.587, 0.114));
            aWeight = heightBlend(weight, h, u_BlendSharpness);
        }

        // Sample albedo
        float4 albedoSample = u_AlbedoMap[i].Sample(s_LinearWrap, uv);
        albedo += albedoSample.rgb * aWeight;

        // Sample normal map
        float3 normSample = u_NormalMap[i].Sample(s_LinearWrap, uv).rgb;
        normal += normSample * aWeight;

        // Sample roughness
        roughness += u_RoughnessMap[i].Sample(s_LinearWrap, uv).r * aWeight;

        // Sample metallic
        metallic += u_MetalnessMap[i].Sample(s_LinearWrap, uv).r * aWeight;

        // Sample AO
        ao += u_AOMap[i].Sample(s_LinearWrap, uv).r * aWeight;

        totalWeight += aWeight;
    }

    // Normalize
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
        albedo    = float3(0.5, 0.5, 0.5);
        normal    = float3(0.5, 0.5, 1.0);
        roughness = 0.5;
        metallic  = 0.0;
        ao        = 1.0;
    }

    // Compute geometric normal from displaced world position.
    // Using screen-space derivatives (ddx/ddy) — always valid, no TBN needed.
    float3 N = computeDisplacedNormal(input.worldPos);

    // PBR lighting
    float3 v  = normalize(float3(0, 0, 0) - input.worldPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 Lo = float3(0, 0, 0);

    // Sun lights
    float shadow = csmShadow(input.worldPos, N,
                             sunLightNum > 0 ? sunLights[0].direction : float3(0.0, -1.0, 0.0));
    for (int i = 0; i < sunLightNum; i++)
    {
        float3 l = normalize(-sunLights[i].direction);
        float3 radiance = sunLights[i].diffuse * sunLights[i].power;
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l, radiance) * (1.0 - shadow);
    }

    // Point lights
    for (int i = 0; i < pointLightNum; i++)
    {
        float3 l = normalize(pointLights[i].position - input.worldPos);
        float dist = length(pointLights[i].position - input.worldPos);
        float att  = 1.0 / (pointLights[i].constant + pointLights[i].linear * dist +
                            pointLights[i].quadratic * dist * dist);
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l, pointLights[i].diffuse * att);
    }

    // Spot lights
    for (int i = 0; i < spotLightNum; i++)
    {
        float3 l    = normalize(spotLights[i].position - input.worldPos);
        float  theta = dot(l, normalize(-spotLights[i].direction));
        float  eps   = spotLights[i].cutOff - spotLights[i].outerCutOff;
        float  intens = clamp((theta - spotLights[i].outerCutOff) / eps, 0.0, 1.0);
        float  dist   = length(spotLights[i].position - input.worldPos);
        float  att    = 1.0 / (spotLights[i].constant + spotLights[i].linear * dist +
                               spotLights[i].quadratic * dist * dist);
        Lo += calcPBR(albedo, metallic, roughness, F0, N, v, l,
                      spotLights[i].diffuse * att * intens);
    }

    // Ambient
    float3 ambient = sceneAmbient * albedo * ao;

    // Skybox ambient — physically-based (split-sum IBL) so the terrain's
    // metallic / roughness / normal (and per-layer AO) shape the result
    // instead of the sky simply being overlaid.
    if (skyboxAmbientEnabled)
    {
        float  NdotV       = max(dot(N, v), 0.0);
        float3 R           = reflect(-v, N);
        float3 irradiance  = skyboxMap.SampleLevel(s_LinearWrap, N, 8.0).rgb;
        float3 prefiltered = skyboxMap.SampleLevel(s_LinearWrap, R, roughness * 6.0).rgb;

        float3 F_amb = fresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kD    = (1.0 - F_amb) * (1.0 - metallic);

        ambient += (kD * albedo * irradiance +
                    prefiltered * envBRDFApprox(F0, roughness, NdotV)) *
                   skyboxAmbientStrength * ao;
    }

    return float4(ambient + Lo, 1.0);
}
