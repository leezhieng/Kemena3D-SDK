/**
 * @file kobject.h
 * @brief Base class for all scene-graph nodes in the Kemena3D engine.
 */

#ifndef KOBJECT_H
#define KOBJECT_H

#include "kexport.h"
#include "kdriver.h"

#include <iostream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <nlohmann/json.hpp>

#include "kdatatype.h"
#include "kmaterial.h"
#include "kscriptmanager.h"
#include "kphysicsobject.h"
#include "kcharactercontroller.h"
#include "knavmesh.h"
#include "kparticle.h"
#include "kaudiosource.h"

using json = nlohmann::json;

namespace kemena
{
    /**
     * @brief Base scene-graph node.
     *
     * Every entity in the world hierarchy — meshes, lights, cameras, and plain
     * empty objects — derives from kObject. It manages parent/child
     * relationships, a local and world-space transform, an optional material,
     * and a list of attached AngelScript scripts.
     */
    class KEMENA3D_API kObject
    {
    public:
        /**
         * @brief Constructs a kObject and optionally attaches it to a parent.
         * @param parentNode Parent to attach to, or nullptr for a root node.
         */
        kObject(kObject *parentNode = nullptr);

        /**
         * @brief Destroys the object.
         *
         * Virtual to allow correct destruction through a kObject* pointer to a
         * derived node. Does not delete attached children or the physics body.
         */
        virtual ~kObject();

        /**
         * @brief Returns the parent node.
         * @return Pointer to the parent, or nullptr if this is a root node.
         */
        kObject *getParent();

        /**
         * @brief Reparents this object under @p newParent.
         * @param newParent New parent node.
         */
        void setParent(kObject *newParent);

        /**
         * @brief Removes this object from its parent's children list.
         *
         * The object is not deleted; its parent pointer is set to nullptr.
         * Has no effect if the object has no parent.
         */
        void detachFromParent();

        /**
         * @brief Reparents this object while keeping its world transform.
         *
         * Computes new local position/rotation/scale such that the object's
         * world-space pose is unchanged after switching parents — i.e. the
         * object does not visually jump. Use this for editor reparent
         * operations (drag in the hierarchy, etc.); use the plain
         * setParent() when constructing scenes where the local transform is
         * what you want preserved.
         *
         * @param newParent New parent node, or nullptr to detach to root space.
         */
        void setParentKeepTransform(kObject *newParent);

        /**
         * @brief Returns the list of direct children.
         * @return Copy of the children vector.
         */
        std::vector<kObject *> getChildren();

        /** @brief Returns a mutable reference to the list of attached scripts. */
        std::vector<kScript>& getScripts();

        /**
         * @brief Appends a script descriptor to this object.
         * @param script Descriptor to add (uuid and fileName should be set by the caller).
         */
        void addScript(const kScript& script);

        /**
         * @brief Removes the script with the given UUID from this object.
         * @param uuid UUID of the script to remove.
         */
        void removeScript(const kString& uuid);

        // --- Particles -------------------------------------------------------

        /** @brief Returns a mutable reference to the list of attached particle systems. */
        std::vector<kParticle>& getParticles();

        /**
         * @brief Appends a particle system descriptor to this object.
         * @param particle Descriptor to add.
         */
        void addParticle(const kParticle& particle);

        /**
         * @brief Removes the particle system with the given UUID from this object.
         * @param uuid UUID of the particle system to remove.
         */
        void removeParticle(const kString& uuid);

        // --- Audio sources ---------------------------------------------------

        /** @brief Returns a mutable reference to the list of attached audio sources. */
        std::vector<kAudioSource>& getAudioSources();

        /**
         * @brief Appends an audio source descriptor to this object.
         * @param source Descriptor to add.
         */
        void addAudioSource(const kAudioSource& source);

        /**
         * @brief Removes the audio source with the given UUID from this object.
         * @param uuid UUID of the audio source to remove.
         */
        void removeAudioSource(const kString& uuid);

        // --- Audio listener --------------------------------------------------

        /** @brief Returns a mutable reference to the list of audio listener components (0 or 1). */
        std::vector<kAudioListener>& getAudioListeners();

        /**
         * @brief Attaches an audio listener descriptor to this object.
         * @param listener Descriptor to add.
         */
        void addAudioListener(const kAudioListener& listener);

        /**
         * @brief Removes the audio listener with the given UUID from this object.
         * @param uuid UUID of the listener to remove.
         */
        void removeAudioListener(const kString& uuid);

        // --- Physics descriptor (editor-side config) -------------------------

        /**
         * @brief Returns true if this object has a physics body descriptor configured.
         */
        bool getHasPhysicsDesc() const;

        /**
         * @brief Sets whether this object has a physics body descriptor.
         * @param val true = descriptor is active and should create a body at game-start.
         */
        void setHasPhysicsDesc(bool val);

        /**
         * @brief Returns a mutable reference to the physics body descriptor.
         *
         * Only meaningful when getHasPhysicsDesc() returns true.
         */
        kPhysicsObjectDesc& getPhysicsDesc();

        // --- Character controller descriptor (editor-side config) ------------

        /** @brief Returns true if this object has a character controller configured. */
        bool getHasCharacterDesc() const;

        /**
         * @brief Sets whether this object has a character controller descriptor.
         * @param val true = a character is created at game-start.
         */
        void setHasCharacterDesc(bool val);

        /** @brief Returns a mutable reference to the character controller descriptor. */
        kCharacterControllerDesc& getCharacterDesc();

        // --- Navigation surface descriptor (editor-side config) -------------

        /** @brief Returns true if this object carries a navigation surface. */
        bool getHasNavMeshDesc() const;

        /** @brief Sets whether this object carries a navigation surface. */
        void setHasNavMeshDesc(bool val);

        /** @brief Returns a mutable reference to the navigation surface descriptor. */
        kNavMeshDesc& getNavMeshDesc();

        /**
         * @brief Returns the scene-graph node type tag.
         * @return One of the kNodeType enum values.
         */
        kNodeType getType();

        /**
         * @brief Sets the node type tag.
         * @param newType Node type.
         */
        void setType(kNodeType newType);

        /**
         * @brief Returns whether this object is active (visible/processed).
         * @return true if the object and its children are rendered/updated.
         */
        bool getActive();

        /**
         * @brief Activates or deactivates the object.
         * @param newActive false to hide and skip the object during rendering.
         */
        void setActive(bool newActive);

        /**
         * @brief Returns whether this object was destroyed at runtime.
         *
         * A destroyed object is skipped by the script/scene-graph walks (so its
         * scripts stop dispatching) and is hidden via setActive(false). It stays
         * parented so the editor can restore it when Play stops.
         */
        bool isDestroyed() const { return destroyed; }

        /** @brief Marks/unmarks this object as destroyed at runtime. */
        void setDestroyed(bool value) { destroyed = value; }

        /**
         * @brief Returns whether this object is marked as static (immobile geometry).
         * @return true if the object will be indexed in the static octree.
         */
        bool getStatic();

        /**
         * @brief Marks the object as static or dynamic.
         * @param newStatic true for immobile objects; false for moving/animated ones.
         */
        void setStatic(bool newStatic);

        /**
         * @brief Returns whether editor debug visualization is enabled.
         * @return true if debug shapes (frustum, light range, etc.) are drawn.
         */
        bool getDebugMode();

        /**
         * @brief Enables or disables editor debug visualization for this object.
         * @param newMode true to draw debug shapes in the editor viewport.
         */
        void setDebugMode(bool newMode);

        /**
         * @brief Returns the numeric identifier of this object.
         * @return Unique unsigned integer ID.
         */
        unsigned int getId();

        /**
         * @brief Sets the numeric identifier.
         * @param newId New ID value.
         */
        void setId(unsigned int newId);

        /**
         * @brief Returns the UUID kString of this object.
         * @return UUID v4 kString.
         */
        kString getUuid();

        /**
         * @brief Sets the UUID kString.
         * @param newUuid UUID v4 kString.
         */
        void setUuid(kString newUuid);

        /**
         * @brief Returns the display name of this object.
         * @return Human-readable name.
         */
        kString getName();

        /**
         * @brief Sets the display name.
         * @param newName New name kString.
         */
        void setName(kString newName);

        /**
         * @brief Returns the user-defined tag of this object.
         *
         * Tags are free-form labels (like Unity's tags) that can be assigned to
         * any kObject and its derived classes. The list of available tags is
         * defined in the editor's project settings; the value is stored on the
         * object itself and serialized with the scene.
         *
         * @return The tag kString; empty means "no tag".
         */
        kString getTag();

        /**
         * @brief Sets the user-defined tag of this object.
         * @param newTag New tag kString (empty to clear).
         */
        void setTag(kString newTag);

        /**
         * @brief Compares this object's tag to a given tag (like Unity's CompareTag).
         *
         * Two objects share a tag when their tag kStrings are equal and
         * non-empty. An empty tag never matches anything, so untagged objects
         * compare false against any value.
         *
         * @param otherTag Tag to compare against.
         * @return true if this object's tag equals @p otherTag (and both are non-empty).
         */
        bool compareTag(const kString &otherTag);

        // --- Multiple tags ---------------------------------------------------

        /** @brief Returns every tag assigned to this object. */
        std::vector<kString> getTags() const;

        /** @brief Replaces the object's tag list (duplicates/empties ignored). */
        void setTags(const std::vector<kString> &newTags);

        /** @brief Adds a tag to the object if not already present. */
        void addTag(const kString &newTag);

        /** @brief Removes a tag from the object (no-op if absent). */
        void removeTag(const kString &tagToRemove);

        /** @brief Returns true if the object carries the given tag. */
        bool hasTag(const kString &tag) const;

        // --- Layers ----------------------------------------------------------

        /**
         * @brief Returns the object's layer bitmask.
         *
         * Bit 0 is the built-in "Default" layer. An object can belong to any
         * combination of the project's named layers; systems such as physics,
         * decal projection and (future) navigation use this mask to filter
         * which objects they consider.
         */
        uint32_t getLayerMask() const;

        /** @brief Sets the object's layer bitmask. */
        void setLayerMask(uint32_t mask);

        /** @brief Returns true if any bit in @p mask is set on this object. */
        bool isOnAnyLayer(uint32_t mask) const;

        /**
         * @brief Returns the prefab asset UUID this object is an instance of, or empty.
         *
         * Non-empty only on the root node of a prefab instance. Set by the editor
         * when instantiating a prefab into a scene.
         */
        kString getPrefabRef() const;

        /** @brief Sets the prefab asset UUID this object is an instance of. */
        void setPrefabRef(const kString &ref);

        /**
         * @brief Returns the UUID of the corresponding node in the prefab template.
         *
         * Set on every node of a prefab instance subtree so the editor can match
         * instance nodes back to their template counterparts.
         */
        kString getTemplateUuid() const;

        /** @brief Sets the corresponding template UUID for this prefab instance node. */
        void setTemplateUuid(const kString &uuid);

        /**
         * @brief Returns the local position.
         * @return Position in parent space (or world space if root).
         */
        kVec3 getPosition();

        /**
         * @brief Sets the local position.
         *
         * No-op when the object is marked as static. Use setPositionForced() to
         * bypass the static guard (e.g. during deserialization or editor authoring).
         * @param newPosition Position in parent space.
         */
        virtual void setPosition(kVec3 newPosition);

        /**
         * @brief Sets the local position, bypassing the static guard.
         *
         * Intended for deserialization and editor authoring where the initial
         * transform must be applied regardless of the static flag.
         * @param newPosition Position in parent space.
         */
        virtual void setPositionForced(kVec3 newPosition);

        /**
         * @brief Returns the local rotation as a quaternion.
         * @return Unit quaternion representing the local orientation.
         */
        kQuat getRotation();

        /**
         * @brief Returns the local rotation as Euler angles in degrees.
         * @return XYZ Euler angles in degrees.
         */
        kVec3 getRotationEuler();

        /**
         * @brief Sets the local rotation from a quaternion.
         *
         * No-op when the object is marked as static. Use setRotationForced() to
         * bypass the static guard (e.g. during deserialization or editor authoring).
         * @param newRotation Unit quaternion.
         */
        virtual void setRotation(kQuat newRotation);

        /**
         * @brief Sets the local rotation from a quaternion, bypassing the static guard.
         *
         * Intended for deserialization and editor authoring where the initial
         * transform must be applied regardless of the static flag.
         * @param newRotation Unit quaternion.
         */
        virtual void setRotationForced(kQuat newRotation);

        /**
         * @brief Returns the local scale.
         * @return Per-axis scale factors.
         */
        kVec3 getScale();

        /**
         * @brief Sets the local scale.
         *
         * No-op when the object is marked as static. Use setScaleForced() to
         * bypass the static guard (e.g. during deserialization or editor authoring).
         * @param newScale Per-axis scale factors.
         */
        virtual void setScale(kVec3 newScale);

        /**
         * @brief Sets the local scale, bypassing the static guard.
         *
         * Intended for deserialization and editor authoring where the initial
         * transform must be applied regardless of the static flag.
         * @param newScale Per-axis scale factors.
         */
        virtual void setScaleForced(kVec3 newScale);

        /**
         * @brief Computes the local right (+X) direction in world space.
         * @return Normalised right vector.
         */
        kVec3 calculateRight();

        /**
         * @brief Computes the local forward (-Z) direction in world space.
         * @return Normalised forward vector.
         */
        kVec3 calculateForward();

        /**
         * @brief Computes the local up (+Y) direction in world space.
         * @return Normalised up vector.
         */
        kVec3 calculateUp();

        /**
         * @brief Rotates the object around an axis by a given angular speed.
         * @param rotationAxis  World-space rotation axis (normalised).
         * @param angularSpeed  Rotation magnitude in radians.
         */
        void rotate(kVec3 rotationAxis, float angularSpeed);

        /**
         * @brief Returns the world-space position derived from the world transform.
         * @return Translation column of the world transform matrix.
         */
        kVec3 getGlobalPosition();

        /**
         * @brief Returns the world-space rotation derived from the world transform.
         * @return Normalised world-space quaternion.
         */
        kQuat getGlobalRotation();

        /**
         * @brief Returns the world-space scale derived from the world transform.
         * @return Per-axis world scale.
         */
        kVec3 getGlobalScale();

        /**
         * @brief Assigns a material to this object and optionally its children.
         * @param newMaterial  Material to assign.
         * @param setChildren  If true, the same material is assigned to all descendants.
         */
        void setMaterial(kMaterial *newMaterial, bool setChildren = true);

        /**
         * @brief Returns the assigned material.
         * @return Pointer to the material, or nullptr if none is set.
         */
        kMaterial *getMaterial();

        /**
         * @brief Sets the source asset UUID of the assigned material.
         *
         * The runtime kMaterial carries no link back to the .mat asset it was
         * built from, so the editor records the material asset's UUID here.
         * It is serialized with the object so the assignment survives save/load;
         * an empty string means "no material asset assigned".
         *
         * @param uuid Material asset UUID, or empty string for none.
         */
        void    setMaterialUuid(const kString &uuid) { materialUuid = uuid; }

        /**
         * @brief Returns the source asset UUID of the assigned material.
         * @return Material asset UUID, or empty string if none is assigned.
         */
        kString getMaterialUuid() const              { return materialUuid; }

        /**
         * @brief Sets the source .animator asset UUID assigned to this object.
         *
         * Stored alongside the object in the world file. The Game panel runtime
         * resolves this UUID to load and instantiate the animator controller.
         *
         * @param uuid Animator asset UUID, or empty string for none.
         */
        void    setAnimatorRef(const kString &uuid) { animatorRef = uuid; }

        /**
         * @brief Returns the assigned .animator asset UUID.
         * @return Animator asset UUID, or empty string if none is assigned.
         */
        kString getAnimatorRef() const              { return animatorRef; }

        /**
         * @brief Marks this node as an import-derived sub-mesh.
         *
         * Sub-meshes produced when loading a model file are reconstructed from
         * that file on every load, so they must not be serialized as children
         * of their parent (doing so would duplicate them). The editor still
         * assigns them a UUID for selection/hierarchy purposes; this flag lets
         * serialize() skip them while keeping authored children intact.
         *
         * @param v true if this node was created by the model importer.
         */
        void setImportChild(bool v) { importChild = v; }

        /**
         * @brief Whether this node is an import-derived sub-mesh (see setImportChild).
         * @return true if created by the model importer and excluded from serialization.
         */
        bool getImportChild() const { return importChild; }

        /**
         * @brief Recomputes the local and world-space model matrices.
         *
         * Combines position, rotation, and scale into localTransform, then
         * multiplies by the parent's world transform to produce worldTransform.
         */
        void calculateModelMatrix();

        /**
         * @brief Returns the world-space model matrix.
         * @return 4x4 model matrix in world space.
         */
        kMat4 getModelMatrixWorld();

        /**
         * @brief Returns the local model matrix (relative to the parent).
         * @return 4x4 model matrix in parent/local space.
         */
        kMat4 getModelMatrixLocal();

        /**
         * @brief Draws the object.
         *
         * For base kObject (non-mesh, non-light) nodes this renders a billboard
         * icon quad. Derived classes override this for their own geometry.
         */
        virtual void draw();

        /**
         * @brief Draws the reusable billboard icon quad.
         *
         * The caller is responsible for binding a billboard shader and setting
         * its uniforms before calling this. Exposed so nodes that override
         * draw() with their own geometry (e.g. decals) can still render an
         * editor gizmo icon.
         */
        void drawIcon();

        /**
         * @brief Serialises the object to a JSON value.
         * @return JSON object containing all serialisable fields.
         */
        virtual json serialize();

        /**
         * @brief Restores the object state from a JSON value.
         * @param data JSON object produced by serialize().
         */
        virtual void deserialize(json data);

        // --- Physics ---------------------------------------------------------

        /**
         * @brief Attaches a physics body to this object.
         *
         * The kObject does NOT take ownership of @p physicsObj — it must be
         * managed (and destroyed) via kPhysicsManager::destroyObject().
         *
         * @param physicsObj Physics body created by kPhysicsManager::createObject().
         */
        void attachPhysics(kPhysicsObject *physicsObj);

        /**
         * @brief Detaches the physics body without destroying it.
         */
        void detachPhysics();

        /**
         * @brief Returns the attached physics body, or nullptr if none is attached.
         */
        kPhysicsObject *getPhysicsObject();

        /**
         * @brief Copies the physics body's current position and rotation into
         *        this object's local transform.
         *
         * Call this once per frame after kPhysicsManager::update() to keep the
         * scene node in sync with the simulation.
         *
         * Has no effect if no physics body is attached.
         */
        void syncFromPhysics();

        // --- Character controller -------------------------------------------

        /**
         * @brief Attaches a runtime character controller to this object.
         *
         * Ownership stays with kPhysicsManager (destroy via destroyCharacter()).
         */
        void attachCharacter(kCharacterController *character);

        /** @brief Detaches the character controller without destroying it. */
        void detachCharacter();

        /** @brief Returns the attached character controller, or nullptr. */
        kCharacterController *getCharacterController();

        /**
         * @brief Copies the character controller's position into this object's
         *        local transform. Call once per frame after the physics update.
         */
        void syncFromCharacter();

    protected:
    private:
        kObject *parent = nullptr;
        std::vector<kObject *> children;

        bool isActive   = true;
        bool isStatic   = false;
        bool debugMode  = false;
        bool destroyed  = false; ///< Runtime destroy flag (see setDestroyed()).

        kNodeType type = NODE_TYPE_OBJECT;
        unsigned int id = 0;
        kString uuid;
        kString name;
        std::vector<kString> tags;  ///< User-defined tags (may be empty).
        uint32_t layerMask = 1u;    ///< Layer bitmask; bit 0 = built-in "Default".

        kString prefabRef;     ///< If non-empty, this is the root of a prefab instance referencing the prefab asset with this UUID.
        kString templateUuid;  ///< For nodes inside a prefab instance, the UUID of the corresponding node in the prefab template.

        kVec3 position = kVec3(0.0f, 0.0f, 0.0f);
        kQuat rotation = kQuat(kVec3(0.0f, 0.0f, 0.0f));
        kVec3 scale    = kVec3(1.0f, 1.0f, 1.0f);

        kMat4 localTransform = kMat4(1.0f); ///< Model matrix (local space).
        kMat4 worldTransform = kMat4(1.0f); ///< Model matrix (world space).

        kMaterial       *material      = nullptr;
        kString          materialUuid;  ///< Source .mat asset UUID (for save/load).
        kString          animatorRef;   ///< Source .animator asset UUID (for save/load).
        bool             importChild   = false; ///< true → import-derived sub-mesh, excluded from serialization.
        kPhysicsObject  *physicsObject = nullptr;

        uint32_t iconVAO = 0;          ///< VAO for the billboard icon quad (lazy-init).
        uint32_t iconVertexBuffer = 0; ///< VBO for the billboard icon vertices (lazy-init).
        float iconVertices[12] =
        {
            -0.5f, -0.5f, 0.0f,
             0.5f, -0.5f, 0.0f,
            -0.5f,  0.5f, 0.0f,
             0.5f,  0.5f, 0.0f,
        };

        std::vector<kScript>       scripts;
        std::vector<kParticle>     particles;
        std::vector<kAudioSource>  audioSources;
        std::vector<kAudioListener>audioListeners;

        bool               hasPhysicsDesc = false;
        kPhysicsObjectDesc physicsDesc;

        bool                     hasCharacterDesc = false;
        kCharacterControllerDesc characterDesc;
        kCharacterController     *characterController = nullptr; ///< Runtime; not owned.

        bool         hasNavMeshDesc = false;
        kNavMeshDesc navMeshDesc;
    };
}

#endif // KOBJECT_H
