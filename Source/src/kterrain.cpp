/**
 * @file kterrain.cpp
 * @brief Implementation of the grid-based terrain system.
 *
 * Terrain mesh uses polygon-based height data — vertex Y positions are set
 * directly from m_heightData.  No GPU height map texture or vertex shader
 * displacement is used.  The shader only performs material splatting.
 */

#include "kterrain.h"
#include "kgl_internal.h"
#include "kscene.h"
#include "kmesh.h"
#include "kmaterial.h"
#include "kshader.h"
#include "ktexture2d.h"
#include "ktexture.h"
#include "kassetmanager.h"
#include "koctree.h"

#include <fstream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <filesystem>

// stb_image for splat map loading/saving
#include <stb_image.h>
#include <stb_image_write.h>

// ---------------------------------------------------------------------------
// Minimal TerrainPBR shader — embedded to guarantee compilation even when
// the resource file hasn't been rebuilt.  White albedo + PBR lighting.
// No height displacement — vertex positions carry the height directly.
// ---------------------------------------------------------------------------
static const char *kTerrainShaderSrc = R"(
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
out vec2 v_texCoord;
out vec3 v_N;
void main() {
    vec4 worldPos = modelMatrix * vec4(vertexPosition, 1.0);
    v_worldPos = worldPos.xyz;
    v_texCoord = texCoord;
    v_N = normalize(mat3(normalMatrix) * vertexNormal);
    gl_Position = projectionMatrix * viewMatrix * worldPos;
}
// --- FRAGMENT ---
#version 330 core
struct Material { vec3 diffuse; vec3 ambient; vec3 specular; float shininess; float metallic; float roughness; };
uniform Material material;
struct SunLight { float power; vec3 direction; vec3 diffuse; vec3 specular; };
struct PointLight { float power; vec3 position; float constant; float linear; float quadratic; vec3 diffuse; vec3 specular; };
struct SpotLight { float power; vec3 position; vec3 direction; float cutOff; float outerCutOff; float constant; float linear; float quadratic; vec3 diffuse; vec3 specular; };
uniform int sunLightNum; uniform SunLight sunLights[32];
uniform int pointLightNum; uniform PointLight pointLights[32];
uniform int spotLightNum; uniform SpotLight spotLights[32];
uniform vec3 sceneAmbient;
uniform samplerCube skyboxMap;
uniform bool skyboxAmbientEnabled; uniform float skyboxAmbientStrength;
uniform mat4 viewMatrix;
uniform sampler2DArray shadowMapArray;
uniform mat4 lightSpaceMatrices[4];
uniform vec4 cascadeSplits; uniform int cascadeCount;
uniform float shadowResolution; uniform bool enableShadow; uniform bool receiveShadow;
uniform float shadowBias; uniform float shadowNormalBias; uniform float shadowNormalOffset; uniform float shadowSoftness;
in vec3 v_worldPos; in vec2 v_texCoord; in vec3 v_N;
out vec4 fragColor;
float csmSplit(int i) { if(i==0)return cascadeSplits.x; if(i==1)return cascadeSplits.y; if(i==2)return cascadeSplits.z; return cascadeSplits.w; }
float csmTexelWorld(int l){ float m=abs(lightSpaceMatrices[l][0][0]); return (m>0.0)?2.0/(m*max(shadowResolution,1.0)):0.0; }
float csmBias(vec3 n, vec3 l){ float ndl=max(dot(normalize(n),normalize(l)),0.0); return shadowBias+shadowNormalBias*min(tan(acos(max(ndl,1e-3))),8.0); }
bool csmProject(int l, vec3 wp, out vec3 p){ vec4 ls=lightSpaceMatrices[l]*vec4(wp,1.0); p=ls.xyz/ls.w; p=p*0.5+0.5; return p.z<=1.0&&p.x>=0.0&&p.x<=1.0&&p.y>=0.0&&p.y<=1.0; }
float csmPCF(int l, vec3 p, float b) {
    vec2 ts=(1.0/vec2(textureSize(shadowMapArray,0).xy))*max(shadowSoftness,0.5); float s=0.0;
    for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)s+=(p.z-b>texture(shadowMapArray,vec3(p.xy+vec2(x,y)*ts,l)).r)?1.0:0.0;
    return s/9.0;
}
float csmShadow(vec3 wp, vec3 n, vec3 lightDir) {
    if(!enableShadow||!receiveShadow||cascadeCount<=0)return 0.0;
    vec3 l=normalize(-lightDir);
    float fd=abs((viewMatrix*vec4(wp,1.0)).z); int layer=cascadeCount-1;
    for(int i=0;i<cascadeCount;i++)if(fd<csmSplit(i)){layer=i;break;}
    vec3 samplePos=wp+normalize(n)*(csmTexelWorld(layer)*shadowNormalOffset);
    float b=csmBias(n,l); vec3 p; if(!csmProject(layer,samplePos,p))return 0.0;
    float sh=csmPCF(layer,p,b); float sf=csmSplit(layer); float band=sf*0.1;
    if(layer+1<cascadeCount&&fd>sf-band){ vec3 p2; if(csmProject(layer+1,samplePos,p2)){ float t=clamp((fd-(sf-band))/max(band,1e-4),0.0,1.0); sh=mix(sh,csmPCF(layer+1,p2,b),t); } }
    return sh;
}
const float PI=3.14159265359;
float distGGX(float NdotH,float r){float a=r*r,a2=a*a;return a2/(PI*(NdotH*NdotH*(a2-1.0)+1.0)*(NdotH*NdotH*(a2-1.0)+1.0));}
float geoSchlick(float ndotv,float r){float k=(r+1.0)*(r+1.0)/8.0;return ndotv/(ndotv*(1.0-k)+k);}
float geoSmith(float NdotV,float NdotL,float r){return geoSchlick(NdotV,r)*geoSchlick(NdotL,r);}
vec3 fresnelSchlick(float ct,vec3 F0){return F0+(1.0-F0)*pow(clamp(1.0-ct,0.0,1.0),5.0);}
vec3 fresnelSchlickRoughness(float ct,vec3 F0,float r){return F0+(max(vec3(1.0-r),F0)-F0)*pow(clamp(1.0-ct,0.0,1.0),5.0);}
vec3 envBRDFApprox(vec3 sc,float r,float NoV){
    const vec4 c0=vec4(-1.0,-0.0275,-0.572,0.022),c1=vec4(1.0,0.0425,1.04,-0.04);
    vec4 ir=r*c0+c1; float a=min(ir.x*ir.x,exp2(-9.28*NoV))*ir.x+ir.y;
    vec2 AB=vec2(-1.04,1.04)*a+ir.zw; return sc*AB.x+AB.y;
}
vec3 calcPBR(vec3 al,float met,float r,vec3 F0,vec3 n,vec3 v,vec3 l,vec3 rd){
    vec3 h=normalize(v+l); float NdotH=max(dot(n,h),0.0),NdotV=max(dot(n,v),0.0),NdotL=max(dot(n,l),0.0);
    float NDF=distGGX(NdotH,r),G=geoSmith(NdotV,NdotL,r);
    vec3 F=fresnelSchlick(max(dot(h,v),0.0),F0); vec3 kD=(1.0-F)*(1.0-met);
    vec3 sp=NDF*G*F/(4.0*NdotV*NdotL+0.0001); return (kD*al/PI+sp)*rd*NdotL;
}
void main() {
    vec3 albedo = material.diffuse; // default white
    vec3 N = normalize(v_N);
    vec3 v = normalize(vec3(0.0)-v_worldPos);
    vec3 F0 = mix(vec3(0.04), albedo, material.metallic);
    vec3 Lo = vec3(0.0);
    float shadow = csmShadow(v_worldPos, N, sunLightNum > 0 ? sunLights[0].direction : vec3(0.0,-1.0,0.0));
    for(int i=0;i<sunLightNum;i++){vec3 l=normalize(-sunLights[i].direction);Lo+=calcPBR(albedo,material.metallic,material.roughness,F0,N,v,l,sunLights[i].diffuse*sunLights[i].power)*(1.0-shadow);}
    for(int i=0;i<pointLightNum;i++){vec3 l=normalize(pointLights[i].position-v_worldPos);float d=length(pointLights[i].position-v_worldPos);Lo+=calcPBR(albedo,material.metallic,material.roughness,F0,N,v,l,pointLights[i].diffuse/(pointLights[i].constant+pointLights[i].linear*d+pointLights[i].quadratic*d*d));}
    for(int i=0;i<spotLightNum;i++){vec3 l=normalize(spotLights[i].position-v_worldPos);float theta=dot(l,normalize(-spotLights[i].direction));float eps=spotLights[i].cutOff-spotLights[i].outerCutOff;float intens=clamp((theta-spotLights[i].outerCutOff)/eps,0.0,1.0);float d=length(spotLights[i].position-v_worldPos);Lo+=calcPBR(albedo,material.metallic,material.roughness,F0,N,v,l,spotLights[i].diffuse*intens/(spotLights[i].constant+spotLights[i].linear*d+spotLights[i].quadratic*d*d));}
    vec3 ambient = sceneAmbient * albedo;
    if(skyboxAmbientEnabled){
        // Split-sum IBL so metallic/roughness/normal shape the sky ambient.
        float NdotV=max(dot(N,v),0.0); vec3 R=reflect(-v,N);
        vec3 irr=textureLod(skyboxMap,N,8.0).rgb;
        vec3 pre=textureLod(skyboxMap,R,material.roughness*6.0).rgb;
        vec3 Fa=fresnelSchlickRoughness(NdotV,F0,material.roughness);
        vec3 kD=(1.0-Fa)*(1.0-material.metallic);
        ambient+=(kD*albedo*irr+pre*envBRDFApprox(F0,material.roughness,NdotV))*skyboxAmbientStrength;
    }
    fragColor = vec4(ambient+Lo, 1.0);
}
)";

namespace kemena
{

    // ============================================================================
    // kTerrainLayer
    // ============================================================================

    void kTerrainLayer::serialize(json &j) const
    {
        j["name"] = name;
        j["tiling"] = tiling;
        j["tileSize"] = tileSize;
        j["minHeight"] = minHeight;
        j["maxHeight"] = maxHeight;
        j["slopeMin"] = slopeMin;
        j["slopeMax"] = slopeMax;
        j["albedoPath"] = albedoPath;
        j["normalPath"] = normalPath;
        j["ormPath"] = ormPath;
        j["maskPath"] = maskPath;
        j["maskChannel"] = maskChannel;
    }

    void kTerrainLayer::deserialize(const json &j)
    {
        if (j.contains("name"))
            name = j["name"].get<kString>();
        if (j.contains("tiling"))
            tiling = j["tiling"].get<float>();
        if (j.contains("tileSize"))
            tileSize = j["tileSize"].get<float>();
        if (j.contains("minHeight"))
            minHeight = j["minHeight"].get<float>();
        if (j.contains("maxHeight"))
            maxHeight = j["maxHeight"].get<float>();
        if (j.contains("slopeMin"))
            slopeMin = j["slopeMin"].get<float>();
        if (j.contains("slopeMax"))
            slopeMax = j["slopeMax"].get<float>();
        if (j.contains("albedoPath"))
            albedoPath = j["albedoPath"].get<kString>();
        if (j.contains("normalPath"))
            normalPath = j["normalPath"].get<kString>();
        if (j.contains("ormPath"))
            ormPath = j["ormPath"].get<kString>();
        if (j.contains("maskPath"))
            maskPath = j["maskPath"].get<kString>();
        if (j.contains("maskChannel"))
            maskChannel = j["maskChannel"].get<int>();
    }

    // ============================================================================
    // kTerrainHeightLayer
    // ============================================================================

    void kTerrainHeightLayer::serialize(json &j) const
    {
        j["name"] = name;
        j["type"] = static_cast<int>(type);
        j["enabled"] = enabled;
        j["weight"] = weight;

        j["noiseScale"] = noiseScale;
        j["noiseAmplitude"] = noiseAmplitude;
        j["noiseOctaves"] = noiseOctaves;
        j["noiseSeed"] = noiseSeed;
        j["noiseType"] = noiseType;
        j["noiseAdditive"] = noiseAdditive;

        j["droplets"] = droplets;
        j["erodeRadius"] = erodeRadius;
        j["erodeInertia"] = erodeInertia;
        j["erodeCapacity"] = erodeCapacity;
        j["erodeDeposit"] = erodeDeposit;
        j["erodeErode"] = erodeErode;
        j["erodeEvaporate"] = erodeEvaporate;
        j["erodeGravity"] = erodeGravity;

        j["iterations"] = iterations;
        j["talus"] = talus;
        j["strength"] = strength;
        j["windDirection"] = windDirection;

        j["hydroK"] = hydroK;
        j["hydroM"] = hydroM;
        j["hydroN"] = hydroN;
        j["hydroDeposit"] = hydroDeposit;
    }

    void kTerrainHeightLayer::deserialize(const json &j)
    {
        name = j.value("name", name);
        type = static_cast<kTerrainHeightLayerType>(j.value("type", static_cast<int>(type)));
        enabled = j.value("enabled", enabled);
        weight = j.value("weight", weight);

        noiseScale = j.value("noiseScale", noiseScale);
        noiseAmplitude = j.value("noiseAmplitude", noiseAmplitude);
        noiseOctaves = j.value("noiseOctaves", noiseOctaves);
        noiseSeed = j.value("noiseSeed", noiseSeed);
        noiseType = j.value("noiseType", noiseType);
        noiseAdditive = j.value("noiseAdditive", noiseAdditive);

        droplets = j.value("droplets", droplets);
        erodeRadius = j.value("erodeRadius", erodeRadius);
        erodeInertia = j.value("erodeInertia", erodeInertia);
        erodeCapacity = j.value("erodeCapacity", erodeCapacity);
        erodeDeposit = j.value("erodeDeposit", erodeDeposit);
        erodeErode = j.value("erodeErode", erodeErode);
        erodeEvaporate = j.value("erodeEvaporate", erodeEvaporate);
        erodeGravity = j.value("erodeGravity", erodeGravity);

        iterations = j.value("iterations", iterations);
        talus = j.value("talus", talus);
        strength = j.value("strength", strength);
        windDirection = j.value("windDirection", windDirection);

        hydroK = j.value("hydroK", hydroK);
        hydroM = j.value("hydroM", hydroM);
        hydroN = j.value("hydroN", hydroN);
        hydroDeposit = j.value("hydroDeposit", hydroDeposit);
    }

    // ============================================================================
    // kTerrain
    // ============================================================================

    kTerrain::kTerrain(kScene *scene, kAssetManager *assetManager,
                       int gridX, int gridZ, float worldSize, int heightRes)
        : kObject(), m_scene(scene), m_assetManager(assetManager), m_gridX(gridX), m_gridZ(gridZ), m_worldSize(worldSize), m_heightRes(heightRes)
    {
        m_sampleSpacing = m_worldSize / static_cast<float>(m_heightRes - 1);
        m_heightData.resize(static_cast<size_t>(m_heightRes) * m_heightRes, 0.0f);
        m_baseHeightData.resize(static_cast<size_t>(m_heightRes) * m_heightRes, 0.0f);
        m_weightData.resize(static_cast<size_t>(m_heightRes) * m_heightRes * kMaxTerrainLayers, 0);

        // Default: layer 0 fully covers the tile.
        const size_t pixels = static_cast<size_t>(m_heightRes) * m_heightRes;
        for (size_t i = 0; i < pixels; ++i)
            m_weightData[i * kMaxTerrainLayers + 0] = 255;

        setType(NODE_TYPE_TERRAIN);
    }

    kTerrain::~kTerrain()
    {
        unload();
    }

    kVec3 kTerrain::getWorldPosition() const
    {
        float wx = (static_cast<float>(m_gridX) + 0.5f) * m_worldSize;
        float wz = (static_cast<float>(m_gridZ) + 0.5f) * m_worldSize;
        return kVec3(wx, 0.0f, wz);
    }

    // ============================================================================
    // Splat texture (RGBA8 GPU texture)
    // ============================================================================

    void kTerrain::createSplatTexture()
    {
        kDriver *driver = kDriver::getCurrent();
        if (!driver)
            return;

        const unsigned char *wd = m_weightData.data();
        const size_t pixels = static_cast<size_t>(m_heightRes) * m_heightRes;

        // Pack the 16 per-sample layer weights into four RGBA weight-map textures.
        for (int w = 0; w < 4; ++w)
        {
            std::vector<unsigned char> ch(pixels * 4);
            for (size_t i = 0; i < pixels; ++i)
                for (int c = 0; c < 4; ++c)
                    ch[i * 4 + c] = wd[i * kMaxTerrainLayers + w * 4 + c];

            m_weightTextures[w] = driver->createTexture2D(
                m_heightRes, m_heightRes, kTextureFormat::TEX_FORMAT_RGBA,
                ch.data(), kTextureWrap::CLAMP_TO_EDGE,
                kTextureFilter::LINEAR, kTextureFilter::LINEAR, false);
        }
    }

    void kTerrain::updateSplatTexture()
    {
        kDriver *driver = kDriver::getCurrent();
        if (!driver)
            return;
        if (m_weightTextures[0] == 0)
        {
            createSplatTexture();
            return;
        }

        const unsigned char *wd = m_weightData.data();
        const size_t pixels = static_cast<size_t>(m_heightRes) * m_heightRes;
        for (int w = 0; w < 4; ++w)
        {
            std::vector<unsigned char> ch(pixels * 4);
            for (size_t i = 0; i < pixels; ++i)
                for (int c = 0; c < 4; ++c)
                    ch[i * 4 + c] = wd[i * kMaxTerrainLayers + w * 4 + c];

            driver->uploadTexture2DSub(m_weightTextures[w], 0, 0, 0,
                                       m_heightRes, m_heightRes,
                                       kTextureFormat::TEX_FORMAT_RGBA, ch.data());
        }
    }

    // ============================================================================
    // Weight-map helpers + layer texture arrays
    // ============================================================================

    unsigned char kTerrain::getWeight(int x, int z, int layer) const
    {
        if (x < 0 || x >= m_heightRes || z < 0 || z >= m_heightRes ||
            layer < 0 || layer >= kMaxTerrainLayers)
            return 0;
        return m_weightData[(static_cast<size_t>(z) * m_heightRes + x) * kMaxTerrainLayers + layer];
    }

    void kTerrain::setWeight(int x, int z, int layer, unsigned char v)
    {
        if (x < 0 || x >= m_heightRes || z < 0 || z >= m_heightRes ||
            layer < 0 || layer >= kMaxTerrainLayers)
            return;
        m_weightData[(static_cast<size_t>(z) * m_heightRes + x) * kMaxTerrainLayers + layer] = v;
    }

    void kTerrain::createLayerArrays()
    {
        kDriver *driver = kDriver::getCurrent();
        if (!driver)
            return;

        // Nothing to build unless at least one layer references an image.
        bool anyPath = false;
        for (const auto &ly : m_layers)
            if (!ly.albedoPath.empty() || !ly.normalPath.empty() ||
                !ly.ormPath.empty() || !ly.maskPath.empty())
            {
                anyPath = true;
                break;
            }
        if (!anyPath)
            return;

        const int S = kTerrainArraySize;
        const int L = kMaxTerrainLayers;
        const size_t layerBytes = static_cast<size_t>(S) * S * 4;

        // kind: 0 albedo, 1 normal, 2 orm, 3 mask
        auto build = [&](uint32_t &outId, int kind,
                         const unsigned char def[4], bool srgb) -> void
        {
            std::vector<unsigned char> buf(layerBytes * L);
            for (int layer = 0; layer < L; ++layer)
            {
                unsigned char *dst = buf.data() + static_cast<size_t>(layer) * layerBytes;
                for (int i = 0; i < S * S; ++i)
                {
                    dst[i * 4 + 0] = def[0];
                    dst[i * 4 + 1] = def[1];
                    dst[i * 4 + 2] = def[2];
                    dst[i * 4 + 3] = def[3];
                }

                if (layer >= static_cast<int>(m_layers.size()))
                    continue;

                const kTerrainLayer &ly = m_layers[layer];
                const kString &path = (kind == 0) ? ly.albedoPath
                                    : (kind == 1) ? ly.normalPath
                                    : (kind == 2) ? ly.ormPath
                                                  : ly.maskPath;
                if (path.empty())
                    continue;

                int iw = 0, ih = 0, ic = 0;
                unsigned char *img = stbi_load(path.c_str(), &iw, &ih, &ic, 4);
                if (!img)
                    continue;

                int cw = (std::min)(iw, S);
                int ch = (std::min)(ih, S);
                for (int y = 0; y < ch; ++y)
                    for (int x = 0; x < cw; ++x)
                    {
                        size_t si = (static_cast<size_t>(y) * iw + x) * 4;
                        size_t di = (static_cast<size_t>(y) * S + x) * 4;
                        dst[di + 0] = img[si + 0];
                        dst[di + 1] = img[si + 1];
                        dst[di + 2] = img[si + 2];
                        dst[di + 3] = img[si + 3];
                    }
                stbi_image_free(img);
            }

            outId = driver->createTexture2DArray(
                S, S, L,
                srgb ? kTextureFormat::TEX_FORMAT_SRGBA : kTextureFormat::TEX_FORMAT_RGBA,
                buf.data(), kTextureWrap::REPEAT,
                kTextureFilter::LINEAR_MIPMAP_LINEAR, kTextureFilter::LINEAR, true);
        };

        const unsigned char white[4] = {255, 255, 255, 255};
        const unsigned char nrm[4]   = {128, 128, 255, 255};
        const unsigned char orm[4]   = {255, 255, 0, 255};
        const unsigned char mask[4]  = {255, 255, 255, 255};

        build(m_albedoArray, 0, white, true);
        build(m_normalArray, 1, nrm, false);
        build(m_ormArray,    2, orm, false);
        build(m_maskArray,   3, mask, false);
    }

    void kTerrain::syncLayerMaskChannels()
    {
        if (!m_mesh)
            return;
        kMaterial *mat = m_mesh->getMaterial();
        if (!mat)
            return;

        for (int l = 0; l < kMaxTerrainLayers; ++l)
        {
            // The value is a LAYER index into the packed weight maps (0..15); -1 is
            // "auto" = the layer's own weight, which TerrainPBR resolves with
            // getWeightLayer(l). The legacy 4-layer path clamps it to the RGBA
            // channels of weight map 0, so painting layers 0..3 also works before
            // any layer texture is assigned.
            float ch = -1.0f;
            if (l < static_cast<int>(m_layers.size()) && m_layers[l].maskChannel >= 0)
                ch = static_cast<float>(m_layers[l].maskChannel);
            mat->setParamFloat("u_MaskChannel[" + std::to_string(l) + "]", ch);
        }
    }

    void kTerrain::rebuildLayerArrays()
    {
        kDriver *driver = kDriver::getCurrent();
        if (!driver)
            return;
        if (m_albedoArray) { driver->deleteTexture(m_albedoArray); m_albedoArray = 0; }
        if (m_normalArray) { driver->deleteTexture(m_normalArray); m_normalArray = 0; }
        if (m_ormArray)    { driver->deleteTexture(m_ormArray);    m_ormArray = 0; }
        if (m_maskArray)   { driver->deleteTexture(m_maskArray);   m_maskArray = 0; }
        createLayerArrays();
    }

    void kTerrain::syncLayerTiling()
    {
        if (!m_mesh)
            return;
        kMaterial *mat = m_mesh->getMaterial();
        if (!mat)
            return;

        for (int l = 0; l < kMaxTerrainLayers; ++l)
        {
            // Tiling is authored per material layer, so each slot uploads its own
            // repeat count; layers that do not exist keep 16 (the historical
            // default) so the shader's texture lookups stay valid.
            float tiling = 16.0f;
            if (l < static_cast<int>(m_layers.size()) && m_layers[l].tiling > 0.0f)
                tiling = m_layers[l].tiling;
            mat->setParamFloat("u_Tiling[" + std::to_string(l) + "]", tiling);
        }
    }

    // ============================================================================
    // Coordinate helpers
    // ============================================================================

    float kTerrain::sampleToWorldX(int x) const
    {
        return static_cast<float>(m_gridX) * m_worldSize + static_cast<float>(x) * m_sampleSpacing;
    }

    float kTerrain::sampleToWorldZ(int z) const
    {
        return static_cast<float>(m_gridZ) * m_worldSize + static_cast<float>(z) * m_sampleSpacing;
    }

    float kTerrain::sampleHeight(const kVec3 &worldPos) const
    {
        float localX = worldPos.x - static_cast<float>(m_gridX) * m_worldSize;
        float localZ = worldPos.z - static_cast<float>(m_gridZ) * m_worldSize;

        if (localX < 0.0f || localX > m_worldSize || localZ < 0.0f || localZ > m_worldSize)
            return 0.0f;

        float fx = localX / m_sampleSpacing;
        float fz = localZ / m_sampleSpacing;

        int x0 = static_cast<int>(fx);
        int z0 = static_cast<int>(fz);
        int x1 = (std::min)(x0 + 1, m_heightRes - 1);
        int z1 = (std::min)(z0 + 1, m_heightRes - 1);

        float tx = fx - static_cast<float>(x0);
        float tz = fz - static_cast<float>(z0);

        int res = m_heightRes;
        float h00 = m_heightData[z0 * res + x0];
        float h10 = m_heightData[z0 * res + x1];
        float h01 = m_heightData[z1 * res + x0];
        float h11 = m_heightData[z1 * res + x1];

        float h0 = h00 + (h10 - h00) * tx;
        float h1 = h01 + (h11 - h01) * tx;
        return h0 + (h1 - h0) * tz;
    }

    void kTerrain::setHeight(int x, int z, float value)
    {
        if (x < 0 || x >= m_heightRes || z < 0 || z >= m_heightRes)
            return;
        m_heightData[z * m_heightRes + x] = value;
    }

    bool kTerrain::worldToHeightmap(const kVec3 &worldPos, int &outX, int &outZ) const
    {
        float localX = worldPos.x - static_cast<float>(m_gridX) * m_worldSize;
        float localZ = worldPos.z - static_cast<float>(m_gridZ) * m_worldSize;

        if (localX < 0.0f || localX > m_worldSize || localZ < 0.0f || localZ > m_worldSize)
        {
            outX = -1;
            outZ = -1;
            return false;
        }

        outX = static_cast<int>(std::round(localX / m_sampleSpacing));
        outZ = static_cast<int>(std::round(localZ / m_sampleSpacing));

        outX = (std::max)(0, (std::min)(outX, m_heightRes - 1));
        outZ = (std::max)(0, (std::min)(outZ, m_heightRes - 1));
        return true;
    }

    kVec3 kTerrain::heightmapToWorld(int x, int z) const
    {
        float worldX = static_cast<float>(m_gridX) * m_worldSize + static_cast<float>(x) * m_sampleSpacing;
        float worldZ = static_cast<float>(m_gridZ) * m_worldSize + static_cast<float>(z) * m_sampleSpacing;
        return kVec3(worldX, 0.0f, worldZ);
    }

    void kTerrain::applyBrush(const kVec3 &worldPos, float radius, float strength,
                              BrushMode mode, float flattenTargetHeight)
    {
        int cx, cz;
        if (!worldToHeightmap(worldPos, cx, cz))
            return;

        int radSamples = static_cast<int>(std::ceil(radius / m_sampleSpacing));
        int xMin = (std::max)(0, cx - radSamples);
        int xMax = (std::min)(m_heightRes - 1, cx + radSamples);
        int zMin = (std::max)(0, cz - radSamples);
        int zMax = (std::min)(m_heightRes - 1, cz + radSamples);

        float worldRadiusSq = radius * radius;

        // Sculpting and height painting only ever touch the *base* field; the
        // generated modifier layers are composited back on top afterwards.
        // For Smooth mode, compute a local average first
        std::vector<float> smoothed;
        if (mode == BrushMode::Smooth)
        {
            smoothed = m_baseHeightData;
            for (int z = zMin; z <= zMax; ++z)
            {
                for (int x = xMin; x <= xMax; ++x)
                {
                    kVec3 sampleWorld = heightmapToWorld(x, z);
                    float dx = sampleWorld.x - worldPos.x;
                    float dz = sampleWorld.z - worldPos.z;
                    if (dx * dx + dz * dz <= worldRadiusSq)
                    {
                        int count = 0;
                        float sum = 0.0f;
                        for (int nz = (std::max)(0, z - 1); nz <= (std::min)(m_heightRes - 1, z + 1); ++nz)
                        {
                            for (int nx = (std::max)(0, x - 1); nx <= (std::min)(m_heightRes - 1, x + 1); ++nx)
                            {
                                sum += m_baseHeightData[nz * m_heightRes + nx];
                                ++count;
                            }
                        }
                        smoothed[z * m_heightRes + x] = sum / static_cast<float>(count);
                    }
                }
            }
        }

        for (int z = zMin; z <= zMax; ++z)
        {
            for (int x = xMin; x <= xMax; ++x)
            {
                kVec3 sampleWorld = heightmapToWorld(x, z);
                float dx = sampleWorld.x - worldPos.x;
                float dz = sampleWorld.z - worldPos.z;
                float distSq = dx * dx + dz * dz;

                if (distSq > worldRadiusSq)
                    continue;

                float dist = std::sqrt(distSq);
                float t = dist / radius;

                // One stamp has to be clearly visible. `strength` alone is a 0..1
                // factor, and a single frame of it moved the surface by well under
                // one sample spacing on a 256-unit tile — which is why a click
                // looked like nothing happened. Scale the delta by the brush's own
                // world radius so a click always digs a visible amount.
                float falloff = std::cos(t * 3.14159265358979323846f * 0.5f);
                falloff *= strength * (std::max)(radius * 0.5f, m_sampleSpacing);

                float oldVal = m_baseHeightData[z * m_heightRes + x];
                float newVal = oldVal;

                switch (mode)
                {
                case BrushMode::Raise:
                    newVal = oldVal + falloff;
                    break;
                case BrushMode::Lower:
                    newVal = oldVal - falloff;
                    break;
                case BrushMode::Flatten:
                    newVal = oldVal + (flattenTargetHeight - oldVal) * falloff;
                    break;
                case BrushMode::Smooth:
                    newVal = oldVal + (smoothed[z * m_heightRes + x] - oldVal) * falloff;
                    break;
                }

                m_baseHeightData[z * m_heightRes + x] = newVal;
            }
        }

        // Fold the generated modifier layers back over the edited base.
        recomposeHeight();
    }

    void kTerrain::fillHeight(float value)
    {
        std::fill(m_baseHeightData.begin(), m_baseHeightData.end(), value);
        recomposeHeight();
        if (m_loaded)
        {
            updateMesh();
        }
    }

    namespace
    {
        // Versioned header for the .height / .splat payloads.  Files written by
        // older builds have no header (raw float32 / RGBA8 at the tile's current
        // resolution) and are detected by the missing magic so they still load.
        constexpr char kHeightMagic[4] = {'K', '3', 'D', 'H'};
        constexpr uint32_t kHeightVersion = 1;

        struct TerrainFileHeader
        {
            char magic[4];
            uint32_t version;
            int32_t res;
            float worldSize;
        };

        // Creates a 1x1 solid-colour texture. Used to give the legacy 4-layer
        // splat path neutral defaults so an untextured terrain never samples an
        // unbound sampler (which shows whatever texture was bound last).
        uint32_t makeSolidTexture(unsigned char r, unsigned char g,
                                  unsigned char b, unsigned char a)
        {
            kDriver *driver = kDriver::getCurrent();
            if (!driver)
                return 0;

            const unsigned char px[4] = {r, g, b, a};
            return driver->createTexture2D(1, 1, kTextureFormat::TEX_FORMAT_RGBA,
                                           px, kTextureWrap::REPEAT,
                                           kTextureFilter::NEAREST,
                                           kTextureFilter::NEAREST, false);
        }
    }

    bool kTerrain::loadHeightData(const kString &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;

        TerrainFileHeader hdr{};
        file.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));

        const bool versioned = file.good() &&
                               std::memcmp(hdr.magic, kHeightMagic, 4) == 0 &&
                               hdr.version == kHeightVersion &&
                               hdr.res > 1;

        if (versioned && hdr.res != m_heightRes)
        {
            // Adopt the stored resolution rather than misreading the payload.
            m_heightRes = hdr.res;
            m_sampleSpacing = m_worldSize / static_cast<float>(m_heightRes - 1);
            const size_t pixels = static_cast<size_t>(m_heightRes) * m_heightRes;
            m_heightData.assign(pixels, 0.0f);
            m_baseHeightData.assign(pixels, 0.0f);
            m_weightData.assign(pixels * kMaxTerrainLayers, 0);
        }
        else if (!versioned)
        {
            // Legacy headerless blob: raw float32 at the tile's current resolution.
            file.clear();
            file.seekg(0, std::ios::beg);
        }

        file.read(reinterpret_cast<char *>(m_baseHeightData.data()),
                  static_cast<std::streamsize>(m_baseHeightData.size() * sizeof(float)));
        const bool ok = file.gcount() > 0;
        file.close();

        recomposeHeight();
        return ok;
    }

    bool kTerrain::saveHeightData(const kString &path) const
    {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;

        TerrainFileHeader hdr{};
        std::memcpy(hdr.magic, kHeightMagic, 4);
        hdr.version = kHeightVersion;
        hdr.res = m_heightRes;
        hdr.worldSize = m_worldSize;

        file.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));
        file.write(reinterpret_cast<const char *>(m_baseHeightData.data()),
                   static_cast<std::streamsize>(m_baseHeightData.size() * sizeof(float)));
        file.close();
        return true;
    }

    bool kTerrain::loadSplatMap(const kString &path)
    {
        // Preferred format: versioned binary carrying all 16 layer weights per
        // sample, so nothing is lost on a save/load round trip.
        std::ifstream file(path, std::ios::binary);
        if (file.is_open())
        {
            TerrainFileHeader hdr{};
            file.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
            const bool versioned = file.good() &&
                                   std::memcmp(hdr.magic, kHeightMagic, 4) == 0 &&
                                   hdr.version == kHeightVersion &&
                                   hdr.res > 1;
            if (versioned)
            {
                if (hdr.res != m_heightRes)
                    resizeWeightData(hdr.res);
                file.read(reinterpret_cast<char *>(m_weightData.data()),
                          static_cast<std::streamsize>(m_weightData.size()));
                const bool ok = file.gcount() > 0;
                file.close();
                return ok;
            }
            file.close();
        }

        // Legacy PNG: 4 channels carrying layers 0..3, bilinearly resampled so a
        // stored map of a different size still maps across the whole tile.
        int w = 0, h = 0, channels = 0;
        unsigned char *data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!data || w <= 0 || h <= 0)
        {
            if (data)
                stbi_image_free(data);
            return false;
        }

        const int res = m_heightRes;
        for (int z = 0; z < res; ++z)
        {
            float fz = (res > 1) ? (static_cast<float>(z) / static_cast<float>(res - 1)) * (h - 1) : 0.0f;
            int z0 = (std::min)(static_cast<int>(fz), h - 1);
            int z1 = (std::min)(z0 + 1, h - 1);
            float tz = fz - static_cast<float>(z0);
            for (int x = 0; x < res; ++x)
            {
                float fx = (res > 1) ? (static_cast<float>(x) / static_cast<float>(res - 1)) * (w - 1) : 0.0f;
                int x0 = (std::min)(static_cast<int>(fx), w - 1);
                int x1 = (std::min)(x0 + 1, w - 1);
                float tx = fx - static_cast<float>(x0);

                size_t dst = (static_cast<size_t>(z) * res + x) * kMaxTerrainLayers;
                for (int c = 0; c < 4; ++c)
                {
                    float v00 = data[(static_cast<size_t>(z0) * w + x0) * 4 + c];
                    float v10 = data[(static_cast<size_t>(z0) * w + x1) * 4 + c];
                    float v01 = data[(static_cast<size_t>(z1) * w + x0) * 4 + c];
                    float v11 = data[(static_cast<size_t>(z1) * w + x1) * 4 + c];
                    float v0 = v00 + (v10 - v00) * tx;
                    float v1 = v01 + (v11 - v01) * tx;
                    float v = v0 + (v1 - v0) * tz;
                    m_weightData[dst + c] =
                        static_cast<unsigned char>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
                }
            }
        }

        stbi_image_free(data);
        return true;
    }

    bool kTerrain::saveSplatMap(const kString &path) const
    {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;

        TerrainFileHeader hdr{};
        std::memcpy(hdr.magic, kHeightMagic, 4);
        hdr.version = kHeightVersion;
        hdr.res = m_heightRes;
        hdr.worldSize = m_worldSize;

        file.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));
        file.write(reinterpret_cast<const char *>(m_weightData.data()),
                   static_cast<std::streamsize>(m_weightData.size()));
        file.close();
        return true;
    }

    bool kTerrain::saveHeightMapPng(const kString &path) const
    {
        // Normalize height to 0-255 grayscale
        std::vector<uint8_t> pixels(static_cast<size_t>(m_heightRes) * m_heightRes);
        float hMin = +FLT_MAX, hMax = -FLT_MAX;
        size_t count = static_cast<size_t>(m_heightRes) * m_heightRes;
        for (size_t i = 0; i < count; ++i)
        {
            hMin = (std::min)(hMin, m_heightData[i]);
            hMax = (std::max)(hMax, m_heightData[i]);
        }
        float hRange = (hMax > hMin) ? (hMax - hMin) : 1.0f;
        for (size_t i = 0; i < count; ++i)
        {
            float t = (m_heightData[i] - hMin) / hRange;
            pixels[i] = static_cast<uint8_t>(t * 255.0f);
        }
        return stbi_write_png(path.c_str(), m_heightRes, m_heightRes, 1,
                              pixels.data(), m_heightRes) != 0;
    }

    void kTerrain::setLayerMaterial(int index, kMaterial *material)
    {
        if (index < 0 || index > 3)
            return;

        while (static_cast<int>(m_layers.size()) <= index)
        {
            kTerrainLayer layer;
            layer.name = "Layer " + std::to_string(m_layers.size());
            m_layers.push_back(layer);
        }

        m_layers[index].material = material;
    }

    void kTerrain::setLayer(const kTerrainLayer &layer)
    {
        for (auto &existing : m_layers)
        {
            if (existing.name == layer.name)
            {
                existing = layer;
                return;
            }
        }
        m_layers.push_back(layer);
    }

    // ---------------------------------------------------------------------------
    // Height field: sculpted base + non-destructive modifier layers
    // ---------------------------------------------------------------------------

    void kTerrain::recomposeHeight()
    {
        m_heightData = m_baseHeightData;
        const size_t n = m_heightData.size();
        for (const auto &layer : m_heightLayers)
        {
            if (!layer.enabled || layer.weight == 0.0f || layer.offsets.size() != n)
                continue;
            const float w = layer.weight;
            for (size_t i = 0; i < n; ++i)
                m_heightData[i] += layer.offsets[i] * w;
        }
    }

    int kTerrain::addHeightLayer(const kTerrainHeightLayer &layer)
    {
        kTerrainHeightLayer copy = layer;
        copy.offsets.assign(static_cast<size_t>(m_heightRes) * m_heightRes, 0.0f);
        m_heightLayers.push_back(std::move(copy));
        return static_cast<int>(m_heightLayers.size()) - 1;
    }

    bool kTerrain::removeHeightLayer(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_heightLayers.size()))
            return false;
        m_heightLayers.erase(m_heightLayers.begin() + index);
        recomposeHeight();
        return true;
    }

    void kTerrain::buildLayerInput(int index, std::vector<float> &out) const
    {
        out = m_baseHeightData;
        const size_t n = out.size();
        for (int i = 0; i < index && i < static_cast<int>(m_heightLayers.size()); ++i)
        {
            const auto &layer = m_heightLayers[static_cast<size_t>(i)];
            if (!layer.enabled || layer.weight == 0.0f || layer.offsets.size() != n)
                continue;
            const float w = layer.weight;
            for (size_t k = 0; k < n; ++k)
                out[k] += layer.offsets[k] * w;
        }
    }

    void kTerrain::generateHeightLayer(int index,
                                       const std::function<void(const std::vector<float> &input,
                                                                std::vector<float> &output)> &generator)
    {
        if (index < 0 || index >= static_cast<int>(m_heightLayers.size()) || !generator)
            return;

        std::vector<float> input;
        buildLayerInput(index, input);

        std::vector<float> output = input;
        generator(input, output);

        const size_t n = input.size();
        if (output.size() != n)
            output.resize(n, 0.0f);

        // Store the delta over the layer's input, so weight/enabled can be
        // re-tuned later without re-running the (potentially costly) generator.
        auto &layer = m_heightLayers[static_cast<size_t>(index)];
        layer.offsets.resize(n);
        for (size_t i = 0; i < n; ++i)
            layer.offsets[i] = output[i] - input[i];

        recomposeHeight();
    }

    // ---------------------------------------------------------------------------
    // Resizing
    // ---------------------------------------------------------------------------

    // Bilinear resample of a square float field, preserving the tile's world extent.
    static void resampleHeightField(const std::vector<float> &src, int srcRes, float worldSize,
                                    std::vector<float> &dst, int dstRes)
    {
        dst.assign(static_cast<size_t>(dstRes) * dstRes, 0.0f);
        if (src.empty() || srcRes < 2 || dstRes < 2)
            return;

        for (int z = 0; z < dstRes; ++z)
        {
            float fz = (static_cast<float>(z) / static_cast<float>(dstRes - 1)) *
                       static_cast<float>(srcRes - 1);
            int z0 = (std::min)(static_cast<int>(fz), srcRes - 1);
            int z1 = (std::min)(z0 + 1, srcRes - 1);
            float tz = fz - static_cast<float>(z0);
            for (int x = 0; x < dstRes; ++x)
            {
                float fx = (static_cast<float>(x) / static_cast<float>(dstRes - 1)) *
                           static_cast<float>(srcRes - 1);
                int x0 = (std::min)(static_cast<int>(fx), srcRes - 1);
                int x1 = (std::min)(x0 + 1, srcRes - 1);
                float tx = fx - static_cast<float>(x0);

                float v00 = src[static_cast<size_t>(z0) * srcRes + x0];
                float v10 = src[static_cast<size_t>(z0) * srcRes + x1];
                float v01 = src[static_cast<size_t>(z1) * srcRes + x0];
                float v11 = src[static_cast<size_t>(z1) * srcRes + x1];
                float v0 = v00 + (v10 - v00) * tx;
                float v1 = v01 + (v11 - v01) * tx;
                dst[static_cast<size_t>(z) * dstRes + x] = v0 + (v1 - v0) * tz;
            }
        }
    }

    void kTerrain::resizeWeightData(int newHeightRes)
    {
        if (newHeightRes < 2 || newHeightRes == m_heightRes)
            return;

        const int oldRes = m_heightRes;
        const int L = kMaxTerrainLayers;
        const std::vector<unsigned char> src = m_weightData;
        std::vector<unsigned char> dst(static_cast<size_t>(newHeightRes) * newHeightRes * L, 0);

        for (int z = 0; z < newHeightRes; ++z)
        {
            float fz = (static_cast<float>(z) / static_cast<float>(newHeightRes - 1)) *
                       static_cast<float>(oldRes - 1);
            int z0 = (std::min)(static_cast<int>(fz), oldRes - 1);
            int z1 = (std::min)(z0 + 1, oldRes - 1);
            float tz = fz - static_cast<float>(z0);
            for (int x = 0; x < newHeightRes; ++x)
            {
                float fx = (static_cast<float>(x) / static_cast<float>(newHeightRes - 1)) *
                           static_cast<float>(oldRes - 1);
                int x0 = (std::min)(static_cast<int>(fx), oldRes - 1);
                int x1 = (std::min)(x0 + 1, oldRes - 1);
                float tx = fx - static_cast<float>(x0);

                size_t d = (static_cast<size_t>(z) * newHeightRes + x) * L;
                for (int c = 0; c < L; ++c)
                {
                    float v00 = src[(static_cast<size_t>(z0) * oldRes + x0) * L + c];
                    float v10 = src[(static_cast<size_t>(z0) * oldRes + x1) * L + c];
                    float v01 = src[(static_cast<size_t>(z1) * oldRes + x0) * L + c];
                    float v11 = src[(static_cast<size_t>(z1) * oldRes + x1) * L + c];
                    float v0 = v00 + (v10 - v00) * tx;
                    float v1 = v01 + (v11 - v01) * tx;
                    float v = v0 + (v1 - v0) * tz;
                    dst[d + c] = static_cast<unsigned char>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
                }
            }
        }

        m_weightData.swap(dst);
    }

    void kTerrain::resizeHeightData(int newHeightRes)
    {
        if (newHeightRes < 2 || newHeightRes == m_heightRes)
            return;

        const int oldRes = m_heightRes;
        const float worldSize = m_sampleSpacing * static_cast<float>(oldRes - 1);

        // Must run while m_heightRes still holds the old resolution.
        resizeWeightData(newHeightRes);

        const std::vector<float> oldBase = m_baseHeightData;
        m_heightRes = newHeightRes;
        m_sampleSpacing = worldSize / static_cast<float>(newHeightRes - 1);

        std::vector<float> newBase;
        resampleHeightField(oldBase, oldRes, worldSize, newBase, newHeightRes);
        m_baseHeightData.swap(newBase);

        for (auto &layer : m_heightLayers)
        {
            if (layer.offsets.empty())
            {
                layer.offsets.assign(static_cast<size_t>(newHeightRes) * newHeightRes, 0.0f);
                continue;
            }
            std::vector<float> newOffsets;
            resampleHeightField(layer.offsets, oldRes, worldSize, newOffsets, newHeightRes);
            layer.offsets.swap(newOffsets);
        }

        m_heightData.assign(static_cast<size_t>(newHeightRes) * newHeightRes, 0.0f);
        recomposeHeight();
    }

    void kTerrain::setWorldSize(float worldSize)
    {
        if (worldSize <= 0.0f)
            return;

        m_worldSize = worldSize;
        m_sampleSpacing = m_worldSize / static_cast<float>(m_heightRes - 1);

        if (m_terrainMaterial)
            m_terrainMaterial->setParamFloat("u_TileWorldSize", m_worldSize);

        if (m_mesh)
            m_mesh->setPosition(getWorldPosition());

        if (m_loaded)
            updateMesh();
    }

    // ---------------------------------------------------------------------------
    // Internally owned assets
    // ---------------------------------------------------------------------------

    void kTerrain::createOwnedAssets(const kString &dataDir)
    {
        if (dataDir.empty())
            return;

        std::error_code ec;
        std::filesystem::path dir(dataDir);
        std::filesystem::create_directories(dir, ec);

        kString uuid = m_mesh ? m_mesh->getUuid() : kString();
        if (uuid.empty())
            uuid = "terrain_" + std::to_string(m_gridX) + "_" + std::to_string(m_gridZ);

        m_heightFilePath = (dir / (uuid + ".height")).string();
        m_splatFilePath = (dir / (uuid + ".splat")).string();
        m_heightLayersFilePath = (dir / (uuid + ".hlayers")).string();
        m_materialFilePath = (dir / (uuid + ".mat")).string();

        // First creation only, and only once the mesh (and therefore the UUID the
        // filenames derive from) exists. An existing payload is left untouched so
        // that a project load — which assigns paths before reading them back —
        // can never clobber the saved terrain with a freshly zeroed one.
        if (m_mesh && !std::filesystem::exists(m_heightFilePath))
            saveOwnedAssets();
    }

    void kTerrain::adoptOwnedAssetPaths(const kString &heightFile, const kString &splatFile,
                                        const kString &heightLayersFile,
                                        const kString &materialFile)
    {
        if (!heightFile.empty())
            m_heightFilePath = heightFile;
        if (!splatFile.empty())
            m_splatFilePath = splatFile;
        if (!heightLayersFile.empty())
            m_heightLayersFilePath = heightLayersFile;
        if (!materialFile.empty())
            m_materialFilePath = materialFile;
    }

    void kTerrain::destroyOwnedAssets()
    {
        std::error_code ec;
        const kString *paths[4] = {&m_heightFilePath, &m_splatFilePath,
                                   &m_heightLayersFilePath, &m_materialFilePath};
        for (const kString *p : paths)
            if (!p->empty())
                std::filesystem::remove(*p, ec);

        m_heightFilePath.clear();
        m_splatFilePath.clear();
        m_heightLayersFilePath.clear();
        m_materialFilePath.clear();
    }

    void kTerrain::saveOwnedAssets()
    {
        if (m_heightFilePath.empty())
            return;

        saveHeightData(m_heightFilePath);
        saveSplatMap(m_splatFilePath);

        // Height modifier offsets: too large for JSON, so a compact binary blob
        // (count-prefixed per layer) alongside a matching entry in the tile JSON.
        if (!m_heightLayersFilePath.empty())
        {
            std::ofstream file(m_heightLayersFilePath, std::ios::binary);
            if (file.is_open())
            {
                const int32_t count = static_cast<int32_t>(m_heightLayers.size());
                file.write(reinterpret_cast<const char *>(&count), sizeof(count));
                for (const auto &layer : m_heightLayers)
                {
                    const int32_t n = static_cast<int32_t>(layer.offsets.size());
                    file.write(reinterpret_cast<const char *>(&n), sizeof(n));
                    if (n > 0)
                        file.write(reinterpret_cast<const char *>(layer.offsets.data()),
                                   static_cast<std::streamsize>(n) * sizeof(float));
                }
                file.close();
            }
        }

        // The tile's material: a self-describing .mat holding the TerrainPBR
        // parameters.  It is created and deleted with the tile, never assigned
        // by hand.
        if (!m_materialFilePath.empty())
        {
            json matJson;
            matJson["name"] = m_mesh ? m_mesh->getName() : kString("Terrain");
            matJson["shader"] = "TerrainPBR";
            matJson["owned_by_terrain"] = true;
            matJson["world_size"] = m_worldSize;

            if (m_terrainMaterial)
            {
                const kVec3 diffuse = m_terrainMaterial->getDiffuseColor();
                matJson["diffuse"] = {diffuse.x, diffuse.y, diffuse.z};
                matJson["metallic"] = m_terrainMaterial->getMetallic();
                matJson["roughness"] = m_terrainMaterial->getRoughness();

                json paramsJson = json::object();
                for (const auto &kv : m_terrainMaterial->getParams())
                    if (kv.second.type == kMaterialParamType::FLOAT)
                        paramsJson[kv.first] = kv.second.value.x;
                matJson["params"] = paramsJson;
            }

            // The layer stack (names, per-layer texture paths, mask channels, tile
            // sizes) belongs to the tile's own payload — no other file records it,
            // so without this every layer texture assignment was lost on reload.
            json layersJson = json::array();
            for (const auto &layer : m_layers)
            {
                json lj;
                layer.serialize(lj);
                layersJson.push_back(lj);
            }
            matJson["layers"] = layersJson;

            std::ofstream file(m_materialFilePath);
            if (file.is_open())
            {
                file << matJson.dump(2);
                file.close();
            }
        }
    }

    void kTerrain::loadOwnedAssets()
    {
        if (!m_heightFilePath.empty() && std::filesystem::exists(m_heightFilePath))
            loadHeightData(m_heightFilePath);

        if (!m_splatFilePath.empty() && std::filesystem::exists(m_splatFilePath))
            loadSplatMap(m_splatFilePath);

        if (!m_heightLayersFilePath.empty() && std::filesystem::exists(m_heightLayersFilePath))
        {
            std::ifstream file(m_heightLayersFilePath, std::ios::binary);
            if (file.is_open())
            {
                int32_t count = 0;
                file.read(reinterpret_cast<char *>(&count), sizeof(count));
                if (count >= 0)
                {
                    m_heightLayers.resize(static_cast<size_t>(count));
                    for (auto &layer : m_heightLayers)
                    {
                        int32_t n = 0;
                        file.read(reinterpret_cast<char *>(&n), sizeof(n));
                        layer.offsets.assign(n > 0 ? static_cast<size_t>(n) : 0, 0.0f);
                        if (n > 0)
                            file.read(reinterpret_cast<char *>(layer.offsets.data()),
                                      static_cast<std::streamsize>(n) * sizeof(float));
                    }
                }
                file.close();
            }
        }

        if (!m_materialFilePath.empty() && std::filesystem::exists(m_materialFilePath))
        {
            std::ifstream file(m_materialFilePath);
            if (file.is_open())
            {
                json matJson;
                try
                {
                    file >> matJson;
                }
                catch (...)
                {
                    matJson = json::object();
                }
                file.close();

                // Restore the layer stack saved by saveOwnedAssets(). This is the
                // only place the layer texture paths survive a project reload.
                if (matJson.contains("layers"))
                {
                    m_layers.clear();
                    for (const auto &lj : matJson["layers"])
                    {
                        kTerrainLayer layer;
                        layer.deserialize(lj);
                        m_layers.push_back(layer);
                    }
                }

                if (matJson.contains("params"))
                {
                    for (auto it = matJson["params"].begin(); it != matJson["params"].end(); ++it)
                    {
                        if (!it.value().is_number())
                            continue;
                        const float v = it.value().get<float>();
                        // The tile's material is created by the first rebuildMesh(),
                        // which runs *after* this call on a project load — park the
                        // values until it exists.
                        if (m_terrainMaterial)
                            m_terrainMaterial->setParamFloat(it.key(), v);
                        else
                            m_pendingMaterialParams[it.key()] = v;
                    }
                }
            }
        }

        recomposeHeight();
    }

    // ---------------------------------------------------------------------------
    // Neighbor stitching
    // ---------------------------------------------------------------------------

    void kTerrain::setNeighbor(const kString &direction, kTerrain *neighbor)
    {
        if (direction == "north")
            m_neighborNorth = neighbor;
        else if (direction == "south")
            m_neighborSouth = neighbor;
        else if (direction == "east")
            m_neighborEast = neighbor;
        else if (direction == "west")
            m_neighborWest = neighbor;
    }

    kTerrain *kTerrain::getNeighbor(const kString &direction) const
    {
        if (direction == "north")
            return m_neighborNorth;
        if (direction == "south")
            return m_neighborSouth;
        if (direction == "east")
            return m_neighborEast;
        if (direction == "west")
            return m_neighborWest;
        return nullptr;
    }

    float kTerrain::getStitchedHeight(int x, int z) const
    {
        if (x < 0 || x >= m_heightRes || z < 0 || z >= m_heightRes)
        {
            int res = m_heightRes;
            if (x < 0 && m_neighborWest && m_neighborWest->getHeightData())
            {
                int nx = res - 1 + x;
                if (nx >= 0 && nx < res && z >= 0 && z < res)
                    return m_neighborWest->getHeightData()[z * res + nx];
            }
            if (x >= res && m_neighborEast && m_neighborEast->getHeightData())
            {
                int nx = x - res;
                if (nx >= 0 && nx < res && z >= 0 && z < res)
                    return m_neighborEast->getHeightData()[z * res + nx];
            }
            if (z < 0 && m_neighborSouth && m_neighborSouth->getHeightData())
            {
                int nz = res - 1 + z;
                if (nz >= 0 && nz < res && x >= 0 && x < res)
                    return m_neighborSouth->getHeightData()[nz * res + x];
            }
            if (z >= res && m_neighborNorth && m_neighborNorth->getHeightData())
            {
                int nz = z - res;
                if (nz >= 0 && nz < res && x >= 0 && x < res)
                    return m_neighborNorth->getHeightData()[nz * res + x];
            }

            // No neighbour in that direction (open world border): clamp to this
            // tile's own edge instead of returning 0, which used to bend normals
            // downwards at the outermost samples.
            const int cx = (std::max)(0, (std::min)(x, m_heightRes - 1));
            const int cz = (std::max)(0, (std::min)(z, m_heightRes - 1));
            return m_heightData[cz * m_heightRes + cx];
        }

        return m_heightData[z * m_heightRes + x];
    }

    float kTerrain::rawCompositeHeight(int x, int z) const
    {
        const int res = m_heightRes;
        const size_t i = static_cast<size_t>(z) * res + x;
        if (i >= m_baseHeightData.size())
            return 0.0f;

        float h = m_baseHeightData[i];
        const size_t n = m_baseHeightData.size();
        for (const auto &layer : m_heightLayers)
        {
            if (!layer.enabled || layer.weight == 0.0f || layer.offsets.size() != n)
                continue;
            h += layer.offsets[i] * layer.weight;
        }
        return h;
    }

    void kTerrain::stitchSeams()
    {
        const int res = m_heightRes;
        if (res < 2 || m_stitching)
            return;
        m_stitching = true;

        // Rebuild this tile's composite from base + layers first, so stitching is
        // idempotent (running it twice cannot accumulate).
        for (int z = 0; z < res; ++z)
            for (int x = 0; x < res; ++x)
                m_heightData[static_cast<size_t>(z) * res + x] = rawCompositeHeight(x, z);

        // Stitch the tiles we copy from first: a shared corner sample can only be
        // resolved to one value if the neighbour's own west/south rules have been
        // applied. The recursion walks strictly west/south, so it terminates at the
        // north-east-most tile and is independent of rebuild order.
        if (m_neighborWest && m_neighborWest->getHeightRes() == res)
            m_neighborWest->stitchSeams();
        if (m_neighborSouth && m_neighborSouth->getHeightRes() == res)
            m_neighborSouth->stitchSeams();

        // West column <- west neighbour's east column.
        if (m_neighborWest && m_neighborWest->getHeightRes() == res)
        {
            const float *west = m_neighborWest->getHeightData();
            for (int z = 0; z < res; ++z)
                m_heightData[static_cast<size_t>(z) * res] =
                    west[static_cast<size_t>(z) * res + (res - 1)];
        }

        // South row <- south neighbour's north row.
        if (m_neighborSouth && m_neighborSouth->getHeightRes() == res)
        {
            const float *south = m_neighborSouth->getHeightData();
            const size_t row = static_cast<size_t>(res - 1) * res;
            for (int x = 0; x < res; ++x)
                m_heightData[row + x] = south[row + x];
        }

        m_stitching = false;
    }

    kVec3 kTerrain::computeNormal(int x, int z) const
    {
        float s = m_sampleSpacing * 2.0f;

        // Deliberately *unclamped*: getStitchedHeight() maps -1 / res into the
        // neighbouring tile, so normals smooth across a tile border instead of
        // breaking at it (the previous version clamped first, which made the
        // neighbour lookups unreachable).
        float hL = getStitchedHeight(x - 1, z);
        float hR = getStitchedHeight(x + 1, z);
        float hD = getStitchedHeight(x, z - 1);
        float hU = getStitchedHeight(x, z + 1);

        kVec3 normal;
        normal.x = -(hR - hL) / s;
        normal.y = 1.0f;
        normal.z = -(hU - hD) / s;

        return glm::normalize(normal);
    }

    // ---------------------------------------------------------------------------
    // Mesh generation -- polygon-based height (no shader displacement)
    // ---------------------------------------------------------------------------

    void kTerrain::rebuildMesh(bool stitchNeighbors)
    {
        if (!m_scene)
            return;

        // Preserve material and UUID across rebuilds so the editor
        // inspector / selection can still reference the mesh.
        // IMPORTANT: Detach the material before deleting the mesh so the
        // material is NOT deleted along with the mesh (kMesh owns materials
        // by default). If we don't detach, savedMaterial becomes dangling.
        kMaterial *savedMaterial = nullptr;
        kString savedMaterialUuid;
        kString savedUuid;
        if (m_mesh)
        {
            savedMaterial = m_mesh->getMaterial();
            savedMaterialUuid = m_mesh->getMaterialUuid();
            savedUuid = m_mesh->getUuid();
            // Detach material so it survives mesh deletion
            m_mesh->setMaterial(nullptr, false);
            m_scene->removeMesh(m_mesh);
            delete m_mesh;
            m_mesh = nullptr;
        }

        // (Re)create the four GPU weight-map textures packing the 16 layers, and
        // build the per-layer texture arrays (once, until the layer set changes).
        kDriver *driver = kDriver::getCurrent();
        for (int w = 0; w < 4; ++w)
        {
            if (m_weightTextures[w] != 0)
            {
                driver->deleteTexture(m_weightTextures[w]);
                m_weightTextures[w] = 0;
            }
        }
        createSplatTexture();
        // Layer texture arrays are built lazily (each costs arraySize^2 * 16 * 4
        // bytes), by rebuildLayerArrays() from the editor once the layer image
        // paths are set — so tiles that never use the array path pay nothing and
        // the default 4-layer splat path keeps rendering. A tile restored from a
        // project has its layer paths but no arrays yet, so build them here or the
        // material would silently drop back to the legacy path.
        if (m_albedoArray == 0 && m_normalArray == 0 && m_ormArray == 0 && m_maskArray == 0)
        {
            for (const auto &ly : m_layers)
                if (!ly.albedoPath.empty() || !ly.normalPath.empty() ||
                    !ly.ormPath.empty() || !ly.maskPath.empty())
                {
                    createLayerArrays();
                    break;
                }
        }

        // Share the boundary samples with the adjacent tiles before generating the
        // grid: the west/south edges take the neighbour's values, so both meshes
        // produce identical positions along a shared border (watertight seam).
        if (stitchNeighbors)
            stitchSeams();

        int res = m_heightRes;
        float half = m_worldSize * 0.5f;

        // Distance LOD: build the mesh from every m_lodStep-th height sample.
        // Steps are powers of two that divide (res-1) so the reduced grid stays
        // aligned to the tile edges (513 -> 512 samples -> step 1, 2, 4, 8, ...).
        // This is a practical stand-in for a GPU clipmap: near tiles render at
        // full resolution, distant tiles at a fraction of the vertex count.
        int step = (std::max)(1, m_lodStep);
        if (step >= res)
            step = 1;
        int gridN = (res - 1) / step + 1;

        m_mesh = new kMesh();
        m_mesh->reserveSpace(static_cast<size_t>(gridN) * gridN);

        for (int gz = 0; gz < gridN; ++gz)
        {
            int z = (std::min)(gz * step, res - 1);
            for (int gx = 0; gx < gridN; ++gx)
            {
                int x = (std::min)(gx * step, res - 1);
                float h = m_heightData[z * res + x];
                m_mesh->addVertex(kVec3(
                    -half + static_cast<float>(x) * m_sampleSpacing,
                    h,
                    -half + static_cast<float>(z) * m_sampleSpacing));
                m_mesh->addUV(kVec2(
                    static_cast<float>(gx) / static_cast<float>(gridN - 1),
                    static_cast<float>(gz) / static_cast<float>(gridN - 1)));
            }
        }

        // Compute normals from actual geometry using central differences
        for (int gz = 0; gz < gridN; ++gz)
        {
            int z = (std::min)(gz * step, res - 1);
            for (int gx = 0; gx < gridN; ++gx)
            {
                int x = (std::min)(gx * step, res - 1);
                m_mesh->addNormal(computeNormal(x, z));
            }
        }

        // Add indices (two triangles per quad)
        for (int gz = 0; gz < gridN - 1; ++gz)
        {
            for (int gx = 0; gx < gridN - 1; ++gx)
            {
                uint32_t tl = static_cast<uint32_t>(gz * gridN + gx);
                uint32_t tr = tl + 1;
                uint32_t bl = static_cast<uint32_t>((gz + 1) * gridN + gx);
                uint32_t br = bl + 1;

                m_mesh->addIndex(tl);
                m_mesh->addIndex(bl);
                m_mesh->addIndex(tr);

                m_mesh->addIndex(tr);
                m_mesh->addIndex(bl);
                m_mesh->addIndex(br);
            }
        }

        // Generate tangents and upload to GPU
        m_mesh->generateTangents();
        m_mesh->generateVbo();

        // Register with scene — re-use the saved UUID so the editor
        // inspector / object selection stays valid across rebuilds.
        m_scene->addMesh(m_mesh, savedUuid.empty() ? generateUuid() : savedUuid);
        m_mesh->setPosition(getWorldPosition());
        m_mesh->setName("Terrain_" + std::to_string(m_gridX) + "_" + std::to_string(m_gridZ));
        m_mesh->setLoaded(true);
        // Serialize as type "terrain" so the .world file preserves this as
        // a distinct object type (like light, camera) instead of a generic mesh.
        m_mesh->setSerializeType("terrain");
        // Store terrain metadata on the mesh for serialization
        {
            json tj;
            serialize(tj);
            kString meshUuid = m_mesh->getUuid();
            if (meshUuid.empty())
                meshUuid = "terrain_" + std::to_string(m_gridX) + "_" + std::to_string(m_gridZ);
            m_mesh->setTerrainData(
                m_gridX, m_gridZ, m_worldSize, m_heightRes,
                tj.value("heightFile", meshUuid + ".height"),
                tj.value("splatFile", meshUuid + ".splat"));
        }

        if (savedMaterial)
        {
            m_mesh->setMaterial(savedMaterial, false);
            if (!savedMaterialUuid.empty())
                m_mesh->setMaterialUuid(savedMaterialUuid);
        }
        else
        {
            // --- Create default terrain material with the TerrainPBR shader ---
            // Prefer the resource shader (full 4-layer splatting plus the
            // stochastic anti-tiling / triplanar / noise-variegation controls,
            // with a matching HLSL variant on D3D11).  It is compiled once and
            // shared by every tile; the embedded minimal source stays as a
            // fallback for builds whose resources are unavailable.
            static kShader *s_terrainShader = nullptr;
            if (s_terrainShader == nullptr)
            {
                if (m_assetManager)
                    s_terrainShader = m_assetManager->loadGlslFromResource("SHADER_TERRAIN_PBR");

                if (s_terrainShader == nullptr)
                {
                    s_terrainShader = new kShader();
                    s_terrainShader->loadGlslCode(kTerrainShaderSrc);
                }
            }

            m_terrainMaterial = new kMaterial();
            m_terrainMaterial->setShader(s_terrainShader);
            m_terrainMaterial->setDiffuseColor(kVec3(1.0f, 1.0f, 1.0f)); // white
            m_terrainMaterial->setAmbientColor(kVec3(1.0f, 1.0f, 1.0f));
            m_terrainMaterial->setMetallic(0.0f);
            m_terrainMaterial->setRoughness(0.5f);

            // Defaults for the TerrainPBR controls so a freshly created tile gets
            // the improved look (world-space tiling + stochastic anti-tiling +
            // triplanar cliffs) without any editor input. u_TileWorldSize must
            // match this tile's world size for the seam-free world UVs.
            for (int li = 0; li < 4; ++li)
                m_terrainMaterial->setParamFloat("u_Tiling[" + std::to_string(li) + "]", 16.0f);
            m_terrainMaterial->setParamFloat("u_BlendSharpness", 1.0f);
            m_terrainMaterial->setParamFloat("u_TileWorldSize", m_worldSize);
            m_terrainMaterial->setParamFloat("u_Stochastic", 1.0f);
            // Triplanar cliffs are on by default (they only kick in on slopes);
            // the global UV rotation is opt-in and starts at 0 degrees.
            m_terrainMaterial->setParamFloat("u_Triplanar", 1.0f);
            m_terrainMaterial->setParamFloat("u_TriplanarStrength", 0.75f);
            m_terrainMaterial->setParamFloat("u_TriplanarSharpness", 3.0f);
            m_terrainMaterial->setParamFloat("u_RotateUV", 0.0f);
            m_terrainMaterial->setParamFloat("u_UVRotation", 0.0f);
            // No noise variegation out of the box: until the operator assigns
            // layer textures the terrain should read as plain neutral white
            // rather than a tinted procedural surface.
            m_terrainMaterial->setParamFloat("u_NoiseVariegation", 0.0f);
            m_terrainMaterial->setParamFloat("u_NoiseScale", 0.02f);
            // Masked blending is off until a layer albedo carries an alpha mask.
            m_terrainMaterial->setParamFloat("u_MaskedBlend", 0.0f);

            m_mesh->setMaterial(m_terrainMaterial, false);
        }

        // Bind the terrain's samplers. The engine gives a material eight texture
        // units (0..7; 8 is the shadow array and 9 the skybox), and every sampler
        // past that budget is silently skipped and falls back to unit 0 — which is
        // why the terrain used to render flat white no matter what the weights
        // said, and why assigning a layer texture changed nothing.
        //
        //  * Array path (a layer texture is assigned): four packed weight maps plus
        //    the albedo / normal / ORM / mask arrays = exactly 8 samplers, and
        //    u_SplatMap (a duplicate of weight map 0) is unbound to stay inside it.
        //  * Legacy path (no layer texture yet): u_SplatMap plus the four albedo
        //    maps = 5 samplers. TerrainPBR feeds the other PBR inputs from
        //    constants there, so nothing else needs a unit.
        if (kMaterial *mat = m_mesh->getMaterial())
        {
            bool arrayPath = (m_albedoArray != 0 && m_normalArray != 0 && m_ormArray != 0);
#ifdef KEMENA_D3D11
            // The D3D11 variant of TerrainPBR (TerrainPBR.hlsl) implements only the
            // legacy 4-layer sampler2D splat path — it has no packed weight maps or
            // texture arrays — so that backend has to stay on the legacy bindings
            // rather than being fed samplers it cannot sample.
            if (kDriver *activeDriver = kDriver::getCurrent())
            {
                if (activeDriver->getRendererType() == kRendererType::RENDERER_D3D11)
                    arrayPath = false;
            }
#endif

            auto bindSampler = [&](const kString &name, uint32_t id, kTextureType type)
            {
                if (id == 0)
                {
                    // Neutralise a binding left over from an earlier rebuild: the
                    // renderer skips a null texture, so it consumes no unit.
                    mat->setParamSampler2D(name, nullptr);
                    return;
                }
                kTexture *t = new kTexture();
                t->setTextureID(id);
                t->setTextureName(name);
                t->setType(type);
                mat->setParamSampler2D(name, t);
            };

            // Slots that are not part of the active path must be neutralised, or a
            // stale binding from a previous rebuild would keep eating a unit.
            auto clearLegacy = [&]()
            {
                for (int i = 0; i < 4; ++i)
                {
                    const kString si = "[" + std::to_string(i) + "]";
                    mat->setParamSampler2D("u_AlbedoMap" + si, nullptr);
                    mat->setParamSampler2D("u_NormalMap" + si, nullptr);
                    mat->setParamSampler2D("u_RoughnessMap" + si, nullptr);
                    mat->setParamSampler2D("u_MetalnessMap" + si, nullptr);
                    mat->setParamSampler2D("u_AOMap" + si, nullptr);
                }
            };

            if (arrayPath)
            {
                for (int w = 0; w < 4; ++w)
                    bindSampler("u_WeightMap" + std::to_string(w), m_weightTextures[w],
                                kTextureType::TEX_TYPE_2D);

                bindSampler("u_AlbedoArray", m_albedoArray, kTextureType::TEX_TYPE_2D_ARRAY);
                bindSampler("u_NormalArray", m_normalArray, kTextureType::TEX_TYPE_2D_ARRAY);
                bindSampler("u_ORMArray",    m_ormArray,    kTextureType::TEX_TYPE_2D_ARRAY);
                bindSampler("u_MaskArray",   m_maskArray,   kTextureType::TEX_TYPE_2D_ARRAY);

                mat->setParamSampler2D("u_SplatMap", nullptr);
                clearLegacy();
            }
            else
            {
                // Legacy alias: weight map 0 is the old RGBA splat control map.
                bindSampler("u_SplatMap", m_weightTextures[0], kTextureType::TEX_TYPE_2D);

                // Neutral 1x1 albedo so an untextured terrain reads plain white
                // instead of whatever texture happened to be bound last.
                static uint32_t s_whiteTex = 0;
                if (s_whiteTex == 0)
                    s_whiteTex = makeSolidTexture(255, 255, 255, 255);
                for (int i = 0; i < 4; ++i)
                    bindSampler("u_AlbedoMap[" + std::to_string(i) + "]", s_whiteTex,
                                kTextureType::TEX_TYPE_2D);

                for (int i = 0; i < 4; ++i)
                {
                    const kString si = "[" + std::to_string(i) + "]";
                    mat->setParamSampler2D("u_NormalMap" + si, nullptr);
                    mat->setParamSampler2D("u_RoughnessMap" + si, nullptr);
                    mat->setParamSampler2D("u_MetalnessMap" + si, nullptr);
                    mat->setParamSampler2D("u_AOMap" + si, nullptr);
                }

                mat->setParamSampler2D("u_AlbedoArray", nullptr);
                mat->setParamSampler2D("u_NormalArray", nullptr);
                mat->setParamSampler2D("u_ORMArray",    nullptr);
                mat->setParamSampler2D("u_MaskArray",   nullptr);
            }

            mat->setParamFloat("u_UseArrays", arrayPath ? 1.0f : 0.0f);

            // Values restored from the owned .mat (see loadOwnedAssets) — applied
            // here because this is where the tile's material first exists. The
            // derived uniforms re-asserted below deliberately win over the copies.
            if (!m_pendingMaterialParams.empty())
            {
                for (const auto &kv : m_pendingMaterialParams)
                    mat->setParamFloat(kv.first, kv.second);
                m_pendingMaterialParams.clear();
                mat->setParamFloat("u_UseArrays", arrayPath ? 1.0f : 0.0f);
                mat->setParamFloat("u_TileWorldSize", m_worldSize);
            }

            // Per-layer texture tiling (u_Tiling[0..15]).
            syncLayerTiling();

            // Per-layer splat-mask channel (R/G/B/A) for this tile's layers.
            syncLayerMaskChannels();
        }

        // Compute AABB from height data
        m_mesh->computeLocalAABB();
        float minH = 0.0f, maxH = 0.0f;
        for (float h : m_heightData)
        {
            if (h < minH)
                minH = h;
            if (h > maxH)
                maxH = h;
        }
        kAABB aabb = m_mesh->getLocalAABB();
        aabb.expandBy(kVec3(-half, minH, -half));
        aabb.expandBy(kVec3(half, maxH, half));
        m_mesh->computeLocalAABB();

        m_mesh->calculateModelMatrix();
        m_mesh->calculateNormalMatrix();

        m_loaded = true;
    }

    void kTerrain::setLodStep(int step)
    {
        step = (std::max)(1, step);
        if (step == m_lodStep)
            return;
        m_lodStep = step;
        if (m_loaded)
            rebuildMesh(true);
    }

    void kTerrain::updateMesh()
    {
        if (!m_mesh || !m_loaded)
            return;

        // The fast sub-update assumes a full-resolution vertex grid; any LOD
        // decimation requires a full rebuild instead.
        if (m_lodStep != 1)
        {
            rebuildMesh(true);
            return;
        }

        // Safety check: GPU VBO must exist before calling glBufferSubData
        // If position VBO is 0, do a full rebuild instead of fast sub-update
        if (m_mesh->getVertexBuffer() == 0)
        {
            rebuildMesh(true);
            return;
        }

        int res = m_heightRes;

        // Verify CPU vectors have correct size before writing
        std::vector<kVec3> &verts = m_mesh->getVerticesRef();
        std::vector<kVec3> &norms = m_mesh->getNormalsRef();
        if (verts.size() < static_cast<size_t>(res) * res ||
            norms.size() < static_cast<size_t>(res) * res)
        {
            rebuildMesh(true);
            return;
        }

        for (int z = 0; z < res; ++z)
        {
            for (int x = 0; x < res; ++x)
            {
                int idx = z * res + x;
                verts[idx].y = m_heightData[idx];
                norms[idx] = computeNormal(x, z);
            }
        }

        // Sub-update GPU buffers using kMesh's fast paths (glBufferSubData)
        m_mesh->updatePositions();
        m_mesh->updateNormals();

        // Update bounding box
        m_mesh->computeLocalAABB();
        m_mesh->calculateModelMatrix();
        m_mesh->calculateNormalMatrix();
    }

    // ---------------------------------------------------------------------------
    // Load / Unload
    // ---------------------------------------------------------------------------

    void kTerrain::unload()
    {
        kDriver *driver = kDriver::getCurrent();
        for (int w = 0; w < 4; ++w)
        {
            if (m_weightTextures[w] != 0)
            {
                driver->deleteTexture(m_weightTextures[w]);
                m_weightTextures[w] = 0;
            }
        }
        if (m_albedoArray) { driver->deleteTexture(m_albedoArray); m_albedoArray = 0; }
        if (m_normalArray) { driver->deleteTexture(m_normalArray); m_normalArray = 0; }
        if (m_ormArray)    { driver->deleteTexture(m_ormArray);    m_ormArray = 0; }
        if (m_maskArray)   { driver->deleteTexture(m_maskArray);   m_maskArray = 0; }

        if (m_mesh)
        {
            if (m_scene)
                m_scene->removeMesh(m_mesh);
            delete m_mesh;
            m_mesh = nullptr;
        }

        // The terrain material is owned by the tile (nobody else may replace it),
        // so it dies with the mesh.
        if (m_terrainMaterial)
        {
            delete m_terrainMaterial;
            m_terrainMaterial = nullptr;
        }

        m_loaded = false;
    }

    void kTerrain::reload(bool stitchNeighbors)
    {
        rebuildMesh(stitchNeighbors);
    }

    // ---------------------------------------------------------------------------
    // Serialization
    // ---------------------------------------------------------------------------

    void kTerrain::serialize(json &j) const
    {
        j["type"] = "terrain";
        j["gridX"] = m_gridX;
        j["gridZ"] = m_gridZ;
        j["worldSize"] = m_worldSize;
        j["heightRes"] = m_heightRes;

        json layersJson = json::array();
        for (const auto &layer : m_layers)
        {
            json lj;
            layer.serialize(lj);
            layersJson.push_back(lj);
        }
        j["layers"] = layersJson;

        // Non-destructive height modifiers: parameters here, offsets in .hlayers.
        json hlJson = json::array();
        for (const auto &layer : m_heightLayers)
        {
            json lj;
            layer.serialize(lj);
            hlJson.push_back(lj);
        }
        j["heightLayers"] = hlJson;

        // Use mesh UUID for filenames so terrain data is linked to the scene object
        kString uuid = m_mesh ? m_mesh->getUuid() : "terrain_" + std::to_string(m_gridX) + "_" + std::to_string(m_gridZ);
        j["heightFile"] = uuid + ".height";
        j["splatFile"] = uuid + ".splat";
        j["heightLayersFile"] = uuid + ".hlayers";
        j["materialFile"] = uuid + ".mat";
        // Terrain assets are owned by the tile: never user-assignable.
        j["ownedAssets"] = true;
    }

    void kTerrain::deserialize(const json &j)
    {
        if (j.contains("gridX"))
            m_gridX = j["gridX"].get<int>();
        if (j.contains("gridZ"))
            m_gridZ = j["gridZ"].get<int>();
        if (j.contains("worldSize"))
            m_worldSize = j["worldSize"].get<float>();
        if (j.contains("heightRes"))
            m_heightRes = j["heightRes"].get<int>();

        m_sampleSpacing = m_worldSize / static_cast<float>(m_heightRes - 1);
        const size_t pixels = static_cast<size_t>(m_heightRes) * m_heightRes;
        m_heightData.assign(pixels, 0.0f);
        m_baseHeightData.assign(pixels, 0.0f);
        m_weightData.assign(pixels * kMaxTerrainLayers, 0);

        // Default: layer 0 fully covers the tile until a weight map is loaded.
        for (size_t i = 0; i < pixels; ++i)
            m_weightData[i * kMaxTerrainLayers + 0] = 255;

        if (j.contains("layers"))
        {
            m_layers.clear();
            for (const auto &lj : j["layers"])
            {
                kTerrainLayer layer;
                layer.deserialize(lj);
                m_layers.push_back(layer);
            }
        }

        // Height modifier parameters.  The offsets themselves live in the
        // .hlayers payload and are restored by loadOwnedAssets().
        if (j.contains("heightLayers"))
        {
            m_heightLayers.resize(j["heightLayers"].size());
            size_t index = 0;
            for (const auto &lj : j["heightLayers"])
            {
                m_heightLayers[index].deserialize(lj);
                m_heightLayers[index].offsets.clear();
                ++index;
            }
        }
    }

    // ============================================================================
    // kTerrainManager
    // ============================================================================

    kTerrainManager::kTerrainManager() = default;
    kTerrainManager::~kTerrainManager()
    {
        clear();
    }

    void kTerrainManager::init(kScene *scene, kAssetManager *assetManager,
                               float worldSize, int heightRes)
    {
        m_scene = scene;
        m_assetManager = assetManager;
        m_worldSize = worldSize;
        m_heightRes = heightRes;

        // Tiles may already exist (init() is also called when a world reloads).
        adoptAssetDirectory();
    }

    void kTerrainManager::update(const kVec3 &playerPos, float loadRadius, float unloadRadius)
    {
        if (!m_scene)
            return;

        int playerGridX = 0;
        int playerGridZ = 0;
        worldToGrid(playerPos, m_worldSize, playerGridX, playerGridZ);

        int tileRadius = static_cast<int>(std::ceil(loadRadius / m_worldSize)) + 1;

        std::unordered_set<uint64_t> desiredLoaded;

        for (int dx = -tileRadius; dx <= tileRadius; ++dx)
        {
            for (int dz = -tileRadius; dz <= tileRadius; ++dz)
            {
                int gx = playerGridX + dx;
                int gz = playerGridZ + dz;

                kVec3 tileWorldPos = gridToWorld(gx, gz, m_worldSize);
                kVec3 tileCenter = tileWorldPos + kVec3(m_worldSize * 0.5f, 0.0f, m_worldSize * 0.5f);
                float distXZ = glm::length(kVec2(playerPos.x - tileCenter.x, playerPos.z - tileCenter.z));

                if (distXZ <= loadRadius)
                {
                    uint64_t key = gridKey(gx, gz);
                    desiredLoaded.insert(key);

                    kTerrain *tile = getTile(gx, gz);
                    if (!tile)
                        tile = createTile(gx, gz);

                    if (tile && !tile->isLoaded())
                        loadTile(tile);

                    // Distance LOD: decimate distant tiles (near = full res).
                    if (tile && tile->isLoaded())
                    {
                        int lod = 1;
                        if (distXZ > m_worldSize * 3.0f)
                            lod = 8;
                        else if (distXZ > m_worldSize * 2.0f)
                            lod = 4;
                        else if (distXZ > m_worldSize * 1.25f)
                            lod = 2;
                        tile->setLodStep(lod);
                    }
                }
            }
        }

        // Unload tiles outside unload radius
        for (auto it = m_loadedTiles.begin(); it != m_loadedTiles.end();)
        {
            uint64_t key = *it;
            if (desiredLoaded.find(key) == desiredLoaded.end())
            {
                auto tileIt = m_tiles.find(key);
                if (tileIt != m_tiles.end())
                {
                    int gx = tileIt->second->getGridX();
                    int gz = tileIt->second->getGridZ();
                    kVec3 tileWorldPos = gridToWorld(gx, gz, m_worldSize);
                    kVec3 tileCenter = tileWorldPos + kVec3(m_worldSize * 0.5f, 0.0f, m_worldSize * 0.5f);
                    float distXZ = glm::length(kVec2(playerPos.x - tileCenter.x, playerPos.z - tileCenter.z));

                    if (distXZ > unloadRadius)
                    {
                        unloadTile(tileIt->second.get());
                        it = m_loadedTiles.erase(it);
                        continue;
                    }
                }
            }
            ++it;
        }
    }

    kTerrain *kTerrainManager::createTile(int gridX, int gridZ)
    {
        uint64_t key = gridKey(gridX, gridZ);

        auto it = m_tiles.find(key);
        if (it != m_tiles.end())
            return it->second.get();

        auto terrain = std::make_unique<kTerrain>(
            m_scene, m_assetManager,
            gridX, gridZ, m_worldSize, m_heightRes);

        kTerrain *ptr = terrain.get();
        m_tiles[key] = std::move(terrain);

        // Every tile owns its height / splat / height-layer / material files.
        if (!m_assetDirectory.empty())
            ptr->createOwnedAssets(m_assetDirectory);

        updateNeighbors();
        return ptr;
    }

    void kTerrainManager::setAssetDirectory(const kString &directory)
    {
        m_assetDirectory = directory;
        adoptAssetDirectory();
    }

    void kTerrainManager::adoptAssetDirectory()
    {
        if (m_assetDirectory.empty())
            return;

        for (auto &pair : m_tiles)
            if (pair.second)
                pair.second->createOwnedAssets(m_assetDirectory);
    }

    void kTerrainManager::resizeAllTiles(float worldSize, int heightRes)
    {
        if (worldSize <= 0.0f || heightRes < 2)
            return;

        m_worldSize = worldSize;
        m_heightRes = heightRes;

        for (auto &pair : m_tiles)
        {
            kTerrain *tile = pair.second.get();
            if (!tile)
                continue;

            tile->resizeHeightData(heightRes);
            tile->setWorldSize(worldSize);

            if (tile->isLoaded())
                tile->rebuildMesh(true);

            // Persist the resampled payload straight away so a crash cannot leave
            // the on-disk terrain at the old resolution.
            tile->saveOwnedAssets();
        }

        updateNeighbors();
    }

    bool kTerrainManager::removeTile(int gridX, int gridZ)
    {
        uint64_t key = gridKey(gridX, gridZ);

        auto it = m_tiles.find(key);
        if (it == m_tiles.end())
            return false;

        // Deleting a tile deletes the assets it owns.
        it->second->destroyOwnedAssets();
        it->second->unload();
        m_loadedTiles.erase(key);
        m_tiles.erase(it);
        updateNeighbors();
        return true;
    }

    kTerrain *kTerrainManager::getTile(int gridX, int gridZ) const
    {
        uint64_t key = gridKey(gridX, gridZ);
        auto it = m_tiles.find(key);
        return (it != m_tiles.end()) ? it->second.get() : nullptr;
    }

    void kTerrainManager::worldToGrid(const kVec3 &worldPos, float worldSize,
                                      int &outGridX, int &outGridZ)
    {
        outGridX = static_cast<int>(std::floor(worldPos.x / worldSize));
        outGridZ = static_cast<int>(std::floor(worldPos.z / worldSize));
    }

    kVec3 kTerrainManager::gridToWorld(int gridX, int gridZ, float worldSize)
    {
        return kVec3(
            static_cast<float>(gridX) * worldSize,
            0.0f,
            static_cast<float>(gridZ) * worldSize);
    }

    uint64_t kTerrainManager::gridKey(int gridX, int gridZ)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(gridX)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(gridZ));
    }

    void kTerrainManager::updateNeighbors()
    {
        for (auto &pair : m_tiles)
        {
            kTerrain *tile = pair.second.get();
            int gx = tile->getGridX();
            int gz = tile->getGridZ();

            tile->setNeighbor("north", getTile(gx, gz + 1));
            tile->setNeighbor("south", getTile(gx, gz - 1));
            tile->setNeighbor("east", getTile(gx + 1, gz));
            tile->setNeighbor("west", getTile(gx - 1, gz));
        }
    }

    void kTerrainManager::loadTile(kTerrain *terrain)
    {
        if (!terrain || terrain->isLoaded())
            return;

        terrain->reload(true);
        m_loadedTiles.insert(gridKey(terrain->getGridX(), terrain->getGridZ()));
    }

    void kTerrainManager::unloadTile(kTerrain *terrain)
    {
        if (!terrain || !terrain->isLoaded())
            return;

        terrain->unload();
    }

    int kTerrainManager::getLoadedCount() const
    {
        int count = 0;
        for (const auto &key : m_loadedTiles)
        {
            auto it = m_tiles.find(key);
            if (it != m_tiles.end() && it->second->isLoaded())
                ++count;
        }
        return count;
    }

    void kTerrainManager::loadFromDirectory(const kString &directory)
    {
        std::filesystem::path dir(directory);
        if (!std::filesystem::exists(dir))
            return;

        for (const auto &entry : std::filesystem::directory_iterator(dir))
        {
            if (entry.path().extension() == ".terrain")
            {
                std::ifstream file(entry.path());
                if (!file.is_open())
                    continue;

                json j;
                try
                {
                    file >> j;
                }
                catch (...)
                {
                    continue;
                }

                int gridX = j.value("gridX", 0);
                int gridZ = j.value("gridZ", 0);

                kTerrain *tile = createTile(gridX, gridZ);
                if (!tile)
                    continue;

                tile->deserialize(j);

                kString stem = entry.path().stem().string();
                kString dataDir = directory + "/" + stem + "_data/";
                if (std::filesystem::exists(dataDir))
                {
                    kString hFile = dataDir + j.value("heightFile", "");
                    if (!hFile.empty() && std::filesystem::exists(hFile))
                        tile->loadHeightData(hFile);

                    kString sFile = dataDir + j.value("splatFile", "");
                    if (!sFile.empty() && std::filesystem::exists(sFile))
                        tile->loadSplatMap(sFile);
                }

                updateNeighbors();
            }
        }
    }

    void kTerrainManager::saveToDirectory(const kString &directory) const
    {
        std::filesystem::path dir(directory);
        std::filesystem::create_directories(dir);

        for (const auto &pair : m_tiles)
        {
            const kTerrain *tile = pair.second.get();

            kString terrainName = "Terrain_" + std::to_string(tile->getGridX()) +
                                  "_" + std::to_string(tile->getGridZ());
            kString terrainFile = directory + "/" + terrainName + ".terrain";
            kString dataDir = directory + "/" + terrainName + "_data/";

            json j;
            tile->serialize(j);

            std::ofstream file(terrainFile);
            if (file.is_open())
            {
                file << j.dump(2);
                file.close();
            }

            std::filesystem::create_directories(dataDir);

            kString hFile = dataDir + j.value("heightFile", "height.raw");
            tile->saveHeightData(hFile);

            kString sFile = dataDir + j.value("splatFile", "splat.png");
            tile->saveSplatMap(sFile);
        }
    }

    void kTerrainManager::clear()
    {
        for (auto &pair : m_tiles)
            pair.second->unload();
        m_tiles.clear();
        m_loadedTiles.clear();
    }

} // namespace kemena
