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
uniform vec4 cascadeSplits; uniform int cascadeCount; uniform bool enableShadow; uniform bool receiveShadow;
in vec3 v_worldPos; in vec2 v_texCoord; in vec3 v_N;
out vec4 fragColor;
float csmSplit(int i) { if(i==0)return cascadeSplits.x; if(i==1)return cascadeSplits.y; if(i==2)return cascadeSplits.z; return cascadeSplits.w; }
float csmSample(int l, vec3 wp, float b) {
    vec4 ls=lightSpaceMatrices[l]*vec4(wp,1.0); vec3 p=ls.xyz/ls.w; p=p*0.5+0.5;
    if(p.z>1.0||p.x<0.0||p.x>1.0||p.y<0.0||p.y>1.0)return 0.0;
    vec2 ts=1.0/vec2(textureSize(shadowMapArray,0).xy); float s=0.0;
    for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)s+=(p.z-b>texture(shadowMapArray,vec3(p.xy+vec2(x,y)*ts,l)).r)?1.0:0.0;
    return s/9.0;
}
float csmShadow(vec3 wp, vec3 n) {
    if(!enableShadow||!receiveShadow)return 0.0;
    float fd=abs((viewMatrix*vec4(wp,1.0)).z); int l=cascadeCount-1;
    for(int i=0;i<cascadeCount;i++)if(fd<csmSplit(i)){l=i;break;}
    float b=max(0.0025*(1.0-dot(normalize(n),vec3(0.0,1.0,0.0))),0.0004);
    float sh=csmSample(l,wp,b); float sf=csmSplit(l); float band=sf*0.1;
    if(l+1<cascadeCount&&fd>sf-band)sh=mix(sh,csmSample(l+1,wp,b),clamp((fd-(sf-band))/band,0.0,1.0));
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
    float shadow = csmShadow(v_worldPos, N);
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
        j["tileSize"] = tileSize;
        j["minHeight"] = minHeight;
        j["maxHeight"] = maxHeight;
        j["slopeMin"] = slopeMin;
        j["slopeMax"] = slopeMax;
    }

    void kTerrainLayer::deserialize(const json &j)
    {
        if (j.contains("name"))
            name = j["name"].get<kString>();
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
        m_splatData.resize(static_cast<size_t>(m_heightRes) * m_heightRes * 4, 0);
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
        if (m_splatTexture != 0)
            return;

        kDriver *driver = kDriver::getCurrent();
        m_splatTexture = driver->createTexture2D(m_heightRes, m_heightRes,
                                                  kTextureFormat::TEX_FORMAT_RGBA,
                                                  m_splatData.data(),
                                                  kTextureWrap::CLAMP_TO_EDGE,
                                                  kTextureFilter::LINEAR,
                                                  kTextureFilter::LINEAR,
                                                  false);
    }

    void kTerrain::updateSplatTexture()
    {
        if (m_splatTexture == 0)
        {
            createSplatTexture();
            return;
        }

        kDriver *driver = kDriver::getCurrent();
        driver->uploadTexture2DSub(m_splatTexture, 0, 0, 0,
                                   m_heightRes, m_heightRes,
                                   kTextureFormat::TEX_FORMAT_RGBA,
                                   m_splatData.data());
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

        // For Smooth mode, compute a local average first
        std::vector<float> smoothed;
        if (mode == BrushMode::Smooth)
        {
            smoothed = m_heightData;
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
                                sum += m_heightData[nz * m_heightRes + nx];
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
                float falloff = std::cos(t * 3.14159265358979323846f * 0.5f);
                falloff *= strength;

                float oldVal = m_heightData[z * m_heightRes + x];
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

                m_heightData[z * m_heightRes + x] = newVal;
            }
        }
    }

    void kTerrain::fillHeight(float value)
    {
        std::fill(m_heightData.begin(), m_heightData.end(), value);
        if (m_loaded)
        {
            updateMesh();
        }
    }

    bool kTerrain::loadHeightData(const kString &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;

        file.read(reinterpret_cast<char *>(m_heightData.data()),
                  static_cast<std::streamsize>(m_heightData.size() * sizeof(float)));
        file.close();
        return true;
    }

    bool kTerrain::saveHeightData(const kString &path) const
    {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;

        file.write(reinterpret_cast<const char *>(m_heightData.data()),
                   static_cast<std::streamsize>(m_heightData.size() * sizeof(float)));
        file.close();
        return true;
    }

    bool kTerrain::loadSplatMap(const kString &path)
    {
        int w, h, channels;
        unsigned char *data = stbi_load(path.c_str(), &w, &h, &channels, 4);
        if (!data)
            return false;

        if (w == m_heightRes && h == m_heightRes)
        {
            m_splatData.assign(data, data + static_cast<size_t>(w) * h * 4);
        }
        else
        {
            int copyW = (std::min)(w, m_heightRes);
            int copyH = (std::min)(h, m_heightRes);
            m_splatData.assign(static_cast<size_t>(m_heightRes) * m_heightRes * 4, 0);
            for (int z = 0; z < copyH; ++z)
            {
                for (int x = 0; x < copyW; ++x)
                {
                    size_t srcIdx = static_cast<size_t>(z * w + x) * 4;
                    size_t dstIdx = static_cast<size_t>(z * m_heightRes + x) * 4;
                    m_splatData[dstIdx + 0] = data[srcIdx + 0];
                    m_splatData[dstIdx + 1] = data[srcIdx + 1];
                    m_splatData[dstIdx + 2] = data[srcIdx + 2];
                    m_splatData[dstIdx + 3] = data[srcIdx + 3];
                }
            }
        }

        stbi_image_free(data);
        return true;
    }

    bool kTerrain::saveSplatMap(const kString &path) const
    {
        return stbi_write_png(path.c_str(), m_heightRes, m_heightRes, 4,
                              m_splatData.data(), m_heightRes * 4) != 0;
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
            return 0.0f;
        }

        return m_heightData[z * m_heightRes + x];
    }

    kVec3 kTerrain::computeNormal(int x, int z) const
    {
        float s = m_sampleSpacing * 2.0f;

        float hL = getStitchedHeight((std::max)(0, x - 1), z);
        float hR = getStitchedHeight((std::min)(m_heightRes - 1, x + 1), z);
        float hD = getStitchedHeight(x, (std::max)(0, z - 1));
        float hU = getStitchedHeight(x, (std::min)(m_heightRes - 1, z + 1));

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

        // Create splat texture on GPU
        if (m_splatTexture != 0)
        {
            kDriver::getCurrent()->deleteTexture(m_splatTexture);
            m_splatTexture = 0;
        }
        createSplatTexture();

        int res = m_heightRes;
        float half = m_worldSize * 0.5f;

        // Build vertex positions with actual Y from height data.
        // No height map texture or vertex shader displacement is needed.
        m_mesh = new kMesh();
        m_mesh->reserveSpace(static_cast<size_t>(res) * res);

        for (int z = 0; z < res; ++z)
        {
            for (int x = 0; x < res; ++x)
            {
                float h = m_heightData[z * res + x];
                m_mesh->addVertex(kVec3(
                    -half + static_cast<float>(x) * m_sampleSpacing,
                    h,
                    -half + static_cast<float>(z) * m_sampleSpacing));
                m_mesh->addUV(kVec2(
                    static_cast<float>(x) / static_cast<float>(res - 1),
                    static_cast<float>(z) / static_cast<float>(res - 1)));
            }
        }

        // Compute normals from actual geometry using central differences
        for (int z = 0; z < res; ++z)
        {
            for (int x = 0; x < res; ++x)
            {
                kVec3 n = computeNormal(x, z);
                m_mesh->addNormal(n);
            }
        }

        // Add indices (two triangles per quad)
        for (int z = 0; z < res - 1; ++z)
        {
            for (int x = 0; x < res - 1; ++x)
            {
                uint32_t tl = static_cast<uint32_t>(z * res + x);
                uint32_t tr = tl + 1;
                uint32_t bl = static_cast<uint32_t>((z + 1) * res + x);
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
            // --- Create default terrain material with embedded TerrainPBR shader ---
            kShader *terrainShader = new kShader();
            terrainShader->loadGlslCode(kTerrainShaderSrc);

            m_terrainMaterial = new kMaterial();
            m_terrainMaterial->setShader(terrainShader);
            m_terrainMaterial->setDiffuseColor(kVec3(1.0f, 1.0f, 1.0f)); // white
            m_terrainMaterial->setAmbientColor(kVec3(1.0f, 1.0f, 1.0f));
            m_terrainMaterial->setMetallic(0.0f);
            m_terrainMaterial->setRoughness(0.5f);

            m_mesh->setMaterial(m_terrainMaterial, false);
        }

        // Bind splat texture to the material
        if (m_splatTexture != 0)
        {
            kTexture *splatTex = new kTexture();
            splatTex->setTextureID(m_splatTexture);
            splatTex->setTextureName("u_SplatMap");
            splatTex->setType(kTextureType::TEX_TYPE_2D);
            m_mesh->getMaterial()->setParamSampler2D("u_SplatMap", splatTex);
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

    void kTerrain::updateMesh()
    {
        if (!m_mesh || !m_loaded)
            return;

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
        if (m_splatTexture != 0)
        {
            kDriver::getCurrent()->deleteTexture(m_splatTexture);
            m_splatTexture = 0;
        }

        if (m_mesh)
        {
            if (m_scene)
                m_scene->removeMesh(m_mesh);
            delete m_mesh;
            m_mesh = nullptr;
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

        // Use mesh UUID for filenames so terrain data is linked to the scene object
        kString uuid = m_mesh ? m_mesh->getUuid() : "terrain_" + std::to_string(m_gridX) + "_" + std::to_string(m_gridZ);
        j["heightFile"] = uuid + ".height";
        j["splatFile"] = uuid + ".splat";
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
        m_heightData.resize(static_cast<size_t>(m_heightRes) * m_heightRes, 0.0f);
        m_splatData.resize(static_cast<size_t>(m_heightRes) * m_heightRes * 4, 0);

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
        updateNeighbors();
        return ptr;
    }

    bool kTerrainManager::removeTile(int gridX, int gridZ)
    {
        uint64_t key = gridKey(gridX, gridZ);

        auto it = m_tiles.find(key);
        if (it == m_tiles.end())
            return false;

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
