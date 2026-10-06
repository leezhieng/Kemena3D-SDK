/**
 * @file kscene.h
 * @brief Container for a self-contained scene (objects, lights, skybox).
 */

#ifndef KSCENE_H
#define KSCENE_H

#include "kexport.h"

#include <string>
#include <iostream>
#include <vector>
#include <typeinfo>

#include "kdatatype.h"
#include "kassetmanager.h"
#include "kworld.h"
#include "kobject.h"
#include "kmesh.h"
#include "kcamera.h"
#include "klight.h"

namespace kemena
{
    class kWorld;

    /**
     * @brief Holds all objects, lights, and rendering settings for one scene.
     *
     * A kScene owns a root scene-graph node under which all kObject, kMesh,
     * and kLight instances are organised.  Multiple scenes can coexist inside
     * a kWorld; only active scenes are rendered.
     */
    class KEMENA3D_API kScene
    {
    public:
        /** @brief Constructs an empty scene with a default root node. */
        kScene();

        /** @brief Destroys the scene. Does not free objects/meshes/lights held by pointer. */
        virtual ~kScene();

        /**
         * @brief Sets the asset manager used to load resources into this scene.
         * @param manager Pointer to the asset manager; must outlive the scene.
         */
        void setAssetManager(kAssetManager *manager);

        /**
         * @brief Returns the asset manager associated with this scene.
         * @return Pointer to the asset manager, or nullptr if not set.
         */
        kAssetManager *getAssetManager();

        /**
         * @brief Associates this scene with a parent world.
         * @param newWorld Owning world instance.
         */
        void setWorld(kWorld *newWorld);

        /**
         * @brief Returns the parent world.
         * @return Pointer to the owning kWorld.
         */
        kWorld *getWorld();

        /**
         * @brief Returns whether this scene is active (rendered and updated).
         * @return true if active.
         */
        bool getActive();

        /**
         * @brief Activates or deactivates this scene.
         * @param newActive false to skip this scene during rendering.
         */
        void setActive(bool newActive);

        /** @brief Enable or disable frustum culling for this scene (default: enabled). */
        void setFrustumCullingEnabled(bool enable) { frustumCullingEnabled = enable; }

        /** @brief Returns whether frustum culling is enabled for this scene. */
        bool getFrustumCullingEnabled() const { return frustumCullingEnabled; }

        /**
         * @brief Returns the UUID of this scene.
         * @return UUID v4 kString.
         */
        kString getUuid();

        /**
         * @brief Sets the UUID of this scene.
         * @param newUuid UUID v4 kString.
         */
        void setUuid(kString newUuid);

        /**
         * @brief Returns the human-readable scene name.
         * @return Scene name kString.
         */
        kString getName();

        /**
         * @brief Sets the human-readable scene name.
         * @param newName New name kString.
         */
        void setName(kString newName);

        /**
         * @brief Returns the auto-increment counter used to assign object IDs.
         * @return Current counter value.
         */
        unsigned int getIncrement();

        /**
         * @brief Sets the auto-increment counter (used when deserialising).
         * @param newIncrement New counter value.
         */
        void setIncrement(unsigned int newIncrement);

        /**
         * @brief Returns all generic scene-graph objects.
         * @return Copy of the internal object vector.
         */
        std::vector<kObject *> getObjects();

        /**
         * @brief Returns all mesh nodes in the scene.
         * @return Copy of the internal mesh vector.
         */
        std::vector<kMesh *> getMeshes();

        /**
         * @brief Returns a mutable reference to the internal mesh vector.
         *
         * Avoids the copy made by getMeshes(); intended for hot paths such as
         * decal projection rebuilds that iterate the mesh list every frame.
         * @return Mutable reference to the mesh list.
         */
        std::vector<kMesh *> &getMeshesRef();

        /**
         * @brief Returns all light nodes in the scene.
         * @return Copy of the internal light vector.
         */
        std::vector<kLight *> getLights();

        /**
         * @brief Returns the root node of the scene graph.
         * @return Pointer to the root kObject.
         */
        kObject *getRootNode();

        /**
         * @brief Adds a generic object to the scene graph.
         * @param object     Object to add; ownership is not transferred.
         * @param objectUuid Optional UUID to assign; auto-generated if empty.
         */
        void addObject(kObject *object, kString objectUuid = "");

        /**
         * @brief Loads a mesh asset and adds it to the scene.
         * @param fileName   Path to the mesh asset file.
         * @param objectUuid Optional UUID for the new mesh node.
         * @return Pointer to the created kMesh.
         */
        kMesh *addMesh(kString fileName, kString objectUuid = "");

        /**
         * @brief Adds an existing mesh node to the scene graph.
         * @param mesh       Pre-constructed mesh to add.
         * @param objectUuid Optional UUID override.
         */
        void addMesh(kMesh *mesh, kString objectUuid = "");

        /**
         * @brief Returns the scene-level ambient light colour.
         * @return RGB ambient colour.
         */
        kVec3 getAmbientLightColor();

        /**
         * @brief Sets the scene-level ambient light colour.
         * @param newColor RGB ambient colour.
         */
        void setAmbientLightColor(kVec3 newColor);

        /**
         * @brief Creates and adds a directional (sun) light.
         * @param position      World-space position of the light node.
         * @param direction     Normalised light direction.
         * @param diffuseColor  Diffuse colour component.
         * @param specularColor Specular colour component.
         * @param objectUuid    Optional UUID for the new node.
         * @return Pointer to the created kLight.
         */
        kLight *addSunLight(kVec3 position = kVec3(0.0f, 0.0f, 0.0f), kVec3 direction = kVec3(0.0f, -1.0f, 0.0f), kVec3 diffuseColor = kVec3(1.0f, 1.0f, 1.0f), kVec3 specularColor = kVec3(1.0f, 1.0f, 1.0f), kString objectUuid = "");

        /**
         * @brief Creates and adds an omnidirectional point light.
         * @param position      World-space position.
         * @param diffuseColor  Diffuse colour component.
         * @param specularColor Specular colour component.
         * @param objectUuid    Optional UUID for the new node.
         * @return Pointer to the created kLight.
         */
        kLight *addPointLight(kVec3 position = kVec3(0.0f, 0.0f, 0.0f), kVec3 diffuseColor = kVec3(1.0f, 1.0f, 1.0f), kVec3 specularColor = kVec3(1.0f, 1.0f, 1.0f), kString objectUuid = "");

        /**
         * @brief Creates and adds a cone spotlight.
         * @param position      World-space position.
         * @param diffuseColor  Diffuse colour component.
         * @param specularColor Specular colour component.
         * @param objectUuid    Optional UUID for the new node.
         * @return Pointer to the created kLight.
         */
        kLight *addSpotLight(kVec3 position = kVec3(0.0f, 0.0f, 0.0f), kVec3 diffuseColor = kVec3(1.0f, 1.0f, 1.0f), kVec3 specularColor = kVec3(1.0f, 1.0f, 1.0f), kString objectUuid = "");

        /**
         * @brief Removes a generic object from the scene graph.
         * @param object Object to detach; memory is NOT freed.
         */
        void removeObject(kObject *object);

        /**
         * @brief Removes a mesh node from the scene graph.
         * @param mesh Mesh to detach; memory is NOT freed.
         */
        void removeMesh(kMesh *mesh);

        /**
         * @brief Removes a light node from the scene graph and the light list.
         * @param light Light to detach; memory is NOT freed.
         */
        void removeLight(kLight *light);

        /**
         * @brief Re-attaches an existing light to the scene (used for undo).
         * @param light Light to re-add; must already have its UUID set.
         */
        void addLight(kLight *light);

        /**
         * @brief Returns whether the scene wants shadow rendering.
         *
         * The renderer reads this each frame; turning it off skips the shadow
         * map pass entirely and tells lit shaders to drop the shadow term.
         * Default: true.
         */
        bool getShadowsEnabled() const;

        /**
         * @brief Toggles shadow rendering for this scene.
         */
        void setShadowsEnabled(bool enabled);

        /**
         * @brief Returns the constant shadow bias used by lit shaders.
         */
        float getShadowBias() const;

        /**
         * @brief Sets the constant shadow bias used by lit shaders.
         *        Larger values reduce acne but can detach the shadow from its caster (peter-panning).
         */
        void setShadowBias(float bias);

        /**
         * @brief Returns the slope-scaled component of the shadow bias.
         */
        float getShadowNormalBias() const;

        /**
         * @brief Sets the slope-scaled component of the shadow bias.
         *        Multiplied by tan(acos(N·L)); only affects grazing-angle surfaces.
         */
        void setShadowNormalBias(float bias);

        /**
         * @brief Returns the normal-offset distance, in shadow-map texels.
         */
        float getShadowNormalOffset() const;

        /**
         * @brief Sets the normal-offset distance, in shadow-map texels.
         *
         * Before the depth comparison each receiver is pushed along its own
         * normal by this many shadow-map texels (the texel's world size is
         * derived from the light matrix). This removes contact-shadow acne the
         * way a depth bias does, but without detaching the shadow from its
         * caster (peter-panning). 0 disables the technique.
         */
        void setShadowNormalOffset(float offset);

        /**
         * @brief Returns the per-cascade shadow map resolution in pixels.
         */
        int  getShadowMapResolution() const;

        /**
         * @brief Sets the per-cascade shadow map resolution (e.g. 512, 1024, 2048, 4096).
         *        Reallocates the shadow texture on the renderer.
         */
        void setShadowMapResolution(int resolution);

        /**
         * @brief Returns the PCF tap spacing (in shadow-map texels).
         */
        float getShadowSoftness() const;

        /**
         * @brief Sets the PCF tap spacing in shadow-map texels.
         *        Larger = softer edges but more bleeding of small occluders.
         */
        void setShadowSoftness(float softness);

        /**
         * @brief Returns whether skybox image-based ambient is enabled.
         */
        bool getSkyboxAmbientEnabled();

        /**
         * @brief Enables or disables skybox image-based ambient lighting.
         */
        void setSkyboxAmbientEnabled(bool enabled);

        /**
         * @brief Returns the skybox ambient strength multiplier.
         */
        float getSkyboxAmbientStrength();

        /**
         * @brief Sets the skybox ambient strength multiplier.
         */
        void setSkyboxAmbientStrength(float strength);

        /**
         * @brief Sets the skybox material and mesh.
         * @param newMaterial Material with a cube-map texture.
         * @param newMesh     Unit-cube mesh used to render the skybox.
         */
        void setSkybox(kMaterial *newMaterial, kMesh *newMesh);

        /**
         * @brief Returns the skybox material.
         * @return Pointer to the skybox material, or nullptr if not set.
         */
        kMaterial *getSkyboxMaterial();

        /**
         * @brief Returns the skybox mesh.
         * @return Pointer to the skybox mesh, or nullptr if not set.
         */
        kMesh *getSkyboxMesh();

        /**
         * @brief Serialises the scene to JSON.
         * @return JSON object with all scene data.
         */
        virtual json serialize();

        /**
         * @brief Restores the scene from a JSON object.
         * @param data JSON produced by serialize().
         */
        virtual void deserialize(json data);

    protected:
    private:
        kAssetManager *assetManager = nullptr; ///< Asset loader reference.
        kWorld        *world        = nullptr; ///< Owning world reference.

        bool isActive = true;              ///< Scene active flag.
        bool frustumCullingEnabled = true; ///< Whether frustum culling applies to this scene.

        kString uuid; ///< Scene UUID.
        kString name; ///< Human-readable scene name.

        std::vector<kObject *> objects; ///< Generic scene-graph nodes.
        std::vector<kMesh *>   meshes;  ///< Mesh nodes.
        std::vector<kLight *>  lights;  ///< Light nodes.

        kObject *rootNode = nullptr; ///< Scene-graph root.

        kVec3 ambientLightColor = kVec3(0.1f, 0.1f, 0.1f); ///< Scene ambient colour.

        bool  shadowsEnabled        = true;   ///< Render shadows for this scene.
        float shadowBias            = 0.0006f;///< Constant shadow depth bias.
        float shadowNormalBias      = 0.0015f;///< Slope-scaled component of shadow bias.
        float shadowNormalOffset    = 1.5f;   ///< Normal-offset distance in shadow-map texels.
        int   shadowMapResolution   = 2048;   ///< Per-cascade shadow map size in pixels.
        float shadowSoftness        = 1.5f;   ///< PCF tap spacing in shadow-map texels.
        bool  skyboxAmbientEnabled  = false;  ///< Enable skybox IBL ambient.
        float skyboxAmbientStrength = 1.0f;   ///< Skybox ambient multiplier.

        kMaterial *skyMaterial = nullptr; ///< Skybox material.
        kMesh     *skyMesh     = nullptr; ///< Skybox geometry.
    };
}

#endif // KSCENE_H
