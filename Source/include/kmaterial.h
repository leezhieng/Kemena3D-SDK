/**
 * @file kmaterial.h
 * @brief Surface material combining a shader, textures, and PBR/Phong properties.
 */

#ifndef KMATERIAL_H
#define KMATERIAL_H

#include "kexport.h"

#include "kdatatype.h"
#include "kshader.h"
#include "ktexture2d.h"
#include "ktexturecube.h"

#include <map>
#include <string>

namespace kemena
{
    /**
     * @brief GLSL/HLSL type of a dynamic material parameter.
     *
     * Drives both how the value is stored and how the renderer pushes it to the
     * shader uniform of the same name.
     */
    enum class kMaterialParamType
    {
        FLOAT,        ///< Single float.
        INT,          ///< Single int (stored in value.x).
        BOOL,         ///< Boolean (stored in value.x as 0/1).
        VEC2,         ///< 2-component vector (value.xy).
        VEC3,         ///< 3-component vector / colour (value.xyz).
        VEC4,         ///< 4-component vector / colour (value.xyzw).
        SAMPLER2D,    ///< 2D texture sampler (uses the texture pointer).
        SAMPLERCUBE   ///< Cubemap sampler (uses the texture pointer).
    };

    /**
     * @brief One dynamic, shader-driven material parameter.
     *
     * Materials expose a set of these, named after shader uniforms discovered
     * from `// @var <type> <name>` annotations. Scalar/vector types use @ref
     * value; sampler types use @ref texture.
     */
    struct kMaterialParam
    {
        kMaterialParamType type    = kMaterialParamType::FLOAT; ///< Parameter type.
        kVec4              value   = kVec4(0.0f);               ///< Scalar/vector value (lower components used per type).
        kTexture          *texture = nullptr;                   ///< Texture for sampler types (not owned).
    };

    /**
     * @brief Groups a shader program with textures and surface parameters.
     *
     * A material is assigned to a kObject (and optionally its children) via
     * kObject::setMaterial().  The renderer queries the material for the shader
     * to bind, the texture list to upload, and the surface colour/PBR values
     * to pass as uniforms.
     */
    class kMaterial
    {
    public:
        /**
         * @brief Constructs a material with default surface parameters and no shader.
         */
        kMaterial();

        /**
         * @brief Destroys the material.
         *
         * Does not free the shader or textures; ownership of those resources
         * remains with the caller.
         */
        virtual ~kMaterial();

        /**
         * @brief Assigns a compiled shader to this material.
         * @param newShader Pointer to the shader program; must outlive the material.
         */
        void setShader(kShader *newShader);

        /**
         * @brief Returns the assigned shader.
         * @return Pointer to the shader, or nullptr if none is set.
         */
        kShader *getShader();

        /**
         * @brief Appends a texture to the material's texture list.
         * @param texture Texture to add; ownership is not transferred.
         */
        void addTexture(kTexture *texture);

        /**
         * @brief Returns all textures attached to this material.
         * @return Copy of the internal texture vector.
         */
        std::vector<kTexture *> getTextures();

        /**
         * @brief Returns a texture by index.
         * @param index Zero-based index into the texture list.
         * @return Pointer to the texture, or nullptr if index is out of range.
         */
        kTexture *getTexture(int index);

        /**
         * @brief Sets the transparency blending mode.
         * @param type Transparency type (none, alpha blend, etc.).
         */
        void setTransparent(kTransparentType type);

        /**
         * @brief Returns the transparency blending mode.
         * @return Current transparency type.
         */
        kTransparentType getTransparent();

        /**
         * @brief Sets the UV tiling factor applied to all texture coordinates.
         * @param newTiling Tiling multiplier (U, V).
         */
        void setUvTiling(kVec2 newTiling);

        /**
         * @brief Returns the UV tiling factor.
         * @return UV tiling (U, V).
         */
        kVec2 getUvTiling();

        /**
         * @brief Sets the ambient colour.
         * @param color RGB ambient colour (0..1 per channel).
         */
        void setAmbientColor(kVec3 color);

        /**
         * @brief Sets the diffuse colour.
         * @param color RGB diffuse colour.
         */
        void setDiffuseColor(kVec3 color);

        /**
         * @brief Sets the specular colour.
         * @param color RGB specular colour.
         */
        void setSpecularColor(kVec3 color);

        /**
         * @brief Sets the Phong shininess exponent.
         * @param value Shininess exponent (higher = tighter highlights).
         */
        void setShininess(float value);

        /**
         * @brief Sets the PBR metallic factor.
         * @param value Metallic value in the range [0, 1].
         */
        void setMetallic(float value);

        /**
         * @brief Sets the PBR roughness factor.
         * @param value Roughness value in the range [0, 1].
         */
        void setRoughness(float value);

        /**
         * @brief Returns the ambient colour.
         * @return RGB ambient colour.
         */
        kVec3 getAmbientColor();

        /**
         * @brief Returns the diffuse colour.
         * @return RGB diffuse colour.
         */
        kVec3 getDiffuseColor();

        /**
         * @brief Returns the specular colour.
         * @return RGB specular colour.
         */
        kVec3 getSpecularColor();

        /**
         * @brief Returns the Phong shininess exponent.
         * @return Shininess value.
         */
        float getShininess();

        /**
         * @brief Returns the PBR metallic factor.
         * @return Metallic value.
         */
        float getMetallic();

        /**
         * @brief Returns the PBR roughness factor.
         * @return Roughness value.
         */
        float getRoughness();

        /**
         * @brief Configures face culling for this material.
         * @param newSingleSided true to enable face culling.
         * @param newCullBack    true to cull back faces; false to cull front faces.
         */
        void setSingleSided(bool newSingleSided, bool newCullBack = true);

        /**
         * @brief Returns whether face culling is enabled.
         * @return true if single-sided rendering is active.
         */
        bool getSingleSided();

        /**
         * @brief Returns which face is culled when single-sided rendering is on.
         * @return true if back faces are culled; false if front faces are culled.
         */
        bool getCullBack();

        // --- Dynamic, shader-driven parameters -------------------------------
        // A material may carry arbitrary named parameters discovered from the
        // shader's `// @var <type> <name>` annotations. The renderer pushes
        // each one to the uniform of the same name. This is how custom (and,
        // eventually, all) materials drive their shaders without hardcoded
        // fields. Sampler params also have their texture bound by name.

        /**
         * @brief Sets (or replaces) a named dynamic parameter.
         * @param name  Shader uniform name (matches a `@var` annotation).
         * @param param Parameter type + value/texture.
         */
        void setParam(const kString &name, const kMaterialParam &param) { params[name] = param; }

        /** @brief Sets a float parameter. */
        void setParamFloat(const kString &name, float v)
        { kMaterialParam p; p.type = kMaterialParamType::FLOAT; p.value.x = v; params[name] = p; }
        /** @brief Sets an int parameter. */
        void setParamInt(const kString &name, int v)
        { kMaterialParam p; p.type = kMaterialParamType::INT; p.value.x = (float)v; params[name] = p; }
        /** @brief Sets a bool parameter. */
        void setParamBool(const kString &name, bool v)
        { kMaterialParam p; p.type = kMaterialParamType::BOOL; p.value.x = v ? 1.0f : 0.0f; params[name] = p; }
        /** @brief Sets a vec2 parameter. */
        void setParamVec2(const kString &name, kVec2 v)
        { kMaterialParam p; p.type = kMaterialParamType::VEC2; p.value = kVec4(v.x, v.y, 0, 0); params[name] = p; }
        /** @brief Sets a vec3 parameter. */
        void setParamVec3(const kString &name, kVec3 v)
        { kMaterialParam p; p.type = kMaterialParamType::VEC3; p.value = kVec4(v.x, v.y, v.z, 0); params[name] = p; }
        /** @brief Sets a vec4 parameter. */
        void setParamVec4(const kString &name, kVec4 v)
        { kMaterialParam p; p.type = kMaterialParamType::VEC4; p.value = v; params[name] = p; }
        /** @brief Sets a sampler2D parameter (texture bound by uniform name). */
        void setParamSampler2D(const kString &name, kTexture *tex)
        { kMaterialParam p; p.type = kMaterialParamType::SAMPLER2D; p.texture = tex; params[name] = p; }
        /** @brief Sets a samplerCube parameter. */
        void setParamSamplerCube(const kString &name, kTexture *tex)
        { kMaterialParam p; p.type = kMaterialParamType::SAMPLERCUBE; p.texture = tex; params[name] = p; }

        /** @brief Whether a named parameter exists. */
        bool hasParam(const kString &name) const { return params.find(name) != params.end(); }

        /** @brief Removes a named parameter if present. */
        void removeParam(const kString &name) { params.erase(name); }

        /** @brief Removes all dynamic parameters. */
        void clearParams() { params.clear(); }

        /**
         * @brief Returns the full set of dynamic parameters (name → value).
         * @return Const reference to the parameter map.
         */
        const std::map<kString, kMaterialParam> &getParams() const { return params; }

    protected:
    private:
        kShader          *shader      = nullptr;         ///< Bound shader program.
        kTransparentType  transparent = TRANSP_TYPE_NONE; ///< Blend mode.
        std::vector<kTexture *> textures;                ///< Ordered texture list.

        kVec2 uvTiling = kVec2(1, 1); ///< UV tiling multiplier.

        kVec3  ambientColor  = kVec3(1.0f, 1.0f, 1.0f); ///< Ambient surface colour.
        kVec3  diffuseColor  = kVec3(1.0f, 1.0f, 1.0f); ///< Diffuse surface colour.
        kVec3  specularColor = kVec3(1.0f, 1.0f, 1.0f); ///< Specular surface colour.
        float shininess  = 32.0f; ///< Phong shininess exponent.
        float metallic   = 0.0f;  ///< PBR metallic factor.
        float roughness  = 0.5f;  ///< PBR roughness factor.

        bool isSingleSided = true; ///< Face culling enabled.
        bool isCullBack    = true; ///< Cull back (true) or front (false) faces.

        std::map<kString, kMaterialParam> params; ///< Dynamic shader-driven parameters (name → value).
    };
}

#endif // KMATERIAL_H
