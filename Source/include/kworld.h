/**
 * @file kworld.h
 * @brief Top-level container holding scenes and cameras.
 */

#ifndef KWORLD_H
#define KWORLD_H

#include <string>
#include <iostream>
#include <vector>

#include "kdatatype.h"
#include "kassetmanager.h"
#include "kscene.h"
#include "kcamera.h"
#include "kscriptmanager.h"
#include "kparticle.h"

// Export macro
#ifdef _WIN32
#ifdef KEMENA3D_STATIC
#define KEMENA3D_API
#elif defined(KEMENA3D_EXPORTS)
#define KEMENA3D_API __declspec(dllexport)
#else
#define KEMENA3D_API __declspec(dllimport)
#endif
#else
#define KEMENA3D_API
#endif

namespace kemena
{
    class kScene;
    class kPhysicsManager;
    class kInputManager;

    /**
     * @brief Root container for the entire simulation environment.
     *
     * A kWorld owns one or more kScene instances and a camera list.
     * The renderer iterates over active scenes and draws them through the
     * main camera returned by getMainCamera().
     *
     * Typical setup:
     * @code
     *   kWorld world;
     *   world.setAssetManager(&assetMgr);
     *   kScene *scene = world.createScene("Main");
     *   kCamera *cam  = world.addCamera(kVec3(0,0,5));
     *   world.setMainCamera(cam);
     * @endcode
     */
    class KEMENA3D_API kWorld
    {
    public:
        /**
         * @brief Replaces the ordered list of named physics layers.
         *
         * Serialized with the world so the standalone runtime can restore the
         * layer configuration and enforce same-layer-only collision.
         *
         * @param names Layer names; "Default" is always kept at index 0.
         */
        void setPhysicsLayers(const std::vector<std::string> &names);

        /** @brief Returns the ordered list of named physics layers. */
        std::vector<std::string> getPhysicsLayers() const;

        /** @brief Constructs an empty world and creates its script manager. */
        kWorld();

        /** @brief Destroys the world, releasing owned scenes, cameras, and managers. */
        virtual ~kWorld();

        /**
         * @brief Returns the UUID of this world.
         * @return UUID v4 kString.
         */
        kString getUuid();

        /**
         * @brief Sets the UUID of this world.
         * @param newUuid UUID v4 kString.
         */
        void setUuid(kString newUuid);

        /**
         * @brief Creates a new scene and registers it in this world.
         * @param sceneName Human-readable name for the scene.
         * @param sceneUuid Optional UUID; auto-generated if empty.
         * @return Pointer to the newly created kScene.
         */
        kScene *createScene(kString sceneName, kString sceneUuid = "");

        /**
         * @brief Registers an existing scene in this world.
         * @param scene     Pre-constructed scene to add.
         * @param sceneUuid Optional UUID override.
         */
        void addScene(kScene *scene, kString sceneUuid = "");

        /**
         * @brief Creates and registers a camera in this world.
         * @param position   World-space initial position.
         * @param lookAt     Initial look-at target (for locked cameras).
         * @param type       Camera mode (free or locked).
         * @param objectUuid Optional UUID for the camera node.
         * @return Pointer to the newly created kCamera.
         */
        kCamera *addCamera(kVec3 position = kVec3(0.0f, 0.0f, 0.0f), kVec3 lookAt = kVec3(0.0f, 0.0f, 0.0f), kCameraType type = kCameraType::CAMERA_TYPE_FREE, kString objectUuid = "");

        /**
         * @brief Registers an existing camera in this world.
         * @param camera     Pre-constructed camera to add.
         * @param objectUuid Optional UUID override.
         */
        void addCamera(kCamera *camera, kString objectUuid = "");

        /**
         * @brief Returns the camera used by the renderer for the main view.
         * @return Pointer to the main camera, or nullptr if not set.
         */
        kCamera *getMainCamera();

        /**
         * @brief Sets the main camera used by the renderer.
         * @param camera Pointer to the desired camera.
         */
        void setMainCamera(kCamera *camera);

        /**
         * @brief Assigns the asset manager used by scenes in this world.
         * @param manager Asset manager instance; must outlive the world.
         */
        void setAssetManager(kAssetManager *manager);

        /**
         * @brief Returns the asset manager.
         * @return Pointer to the asset manager, or nullptr if not set.
         */
        kAssetManager *getAssetManager();

        /**
         * @brief Assigns the named input manager used by scripts (getAction/getAxis).
         *
         * The pointer is forwarded to this world's script manager so AngelScript
         * and logic-graph scripts can query named input. The input manager must
         * outlive the world (it is not owned here).
         *
         * @param manager Input manager instance, or nullptr to disable input queries.
         */
        void setInputManager(kInputManager *manager);

        /**
         * @brief Returns the named input manager assigned to this world.
         * @return Pointer to the input manager, or nullptr if not set.
         */
        kInputManager *getInputManager();

        /**
         * @brief Removes a camera from this world's camera list.
         * @param camera Camera to remove.
         */
        void removeCamera(kCamera *camera);

        /**
         * @brief Removes a scene from this world's scene list.
         * @param scene Scene to remove.
         */
        void removeScene(kScene *scene);

        /**
         * @brief Returns all scenes registered in this world.
         * @return Copy of the internal scene vector.
         */
        std::vector<kScene *> getScenes();

        /**
         * @brief Returns all cameras registered in this world.
         * @return Copy of the internal camera vector.
         */
        std::vector<kCamera *> getCameras();

        /**
         * @brief Returns every object across all active scenes as a flat list.
         *
         * Editor-side convenience (e.g. the Inspector's object-reference picker);
         * runtime code uses the private collectAllObjects() helper directly.
         * @return All objects in every active scene.
         */
        std::vector<kObject *> getAllObjects();

        // --- Scripting -------------------------------------------------------

        /**
         * @brief Returns the world's AngelScript manager.
         *
         * Created automatically with the world; used to register script assets,
         * compile bytecode, and dispatch lifecycle events.
         */
        kScriptManager *getScriptManager();

        /**
         * @brief Starts script execution across every active scene.
         *
         * Builds a private module instance for each active script component
         * (preferring compiled bytecode), then dispatches Awake() to all
         * instances followed by Start(). Call once when gameplay begins.
         */
        void startScripts();

        /**
         * @brief Stops script execution: dispatches OnDestroy() and releases
         *        every script instance. Call when gameplay ends.
         */
        void stopScripts();

        /**
         * @brief Dispatches Update() then LateUpdate() to all running scripts.
         * @param deltaTime Seconds since the last frame.
         */
        void updateScripts(float deltaTime);

        /**
         * @brief Dispatches FixedUpdate() to all running scripts.
         * @param fixedDeltaTime Seconds of the fixed (physics) step.
         */
        void fixedUpdateScripts(float fixedDeltaTime);

        /**
         * @brief Dispatches physics collision/trigger events to running scripts.
         *
         * Reads the contact events captured by @p pm during its most recent
         * update() and fires OnCollision* / OnTrigger* on the script components
         * of every object that owns one of the participating bodies/characters.
         *
         * @param pm        Physics manager whose events are consumed.
         * @param bodyNodes Scene nodes with live rigid bodies owned by @p pm.
         * @param charNodes Scene nodes with live characters owned by @p pm.
         */
        void dispatchPhysicsContactEvents(kPhysicsManager *pm,
                                          const std::vector<kObject *> &bodyNodes,
                                          const std::vector<kObject *> &charNodes);

        /** @brief Returns true between startScripts() and stopScripts(). */
        bool getScriptsRunning() const { return scriptsRunning; }

        // --- Physics lifecycle (standalone runtime) -------------------------

        /**
         * @brief Spawns physics bodies and character controllers from every
         *        object's editor-authored descriptor, seeded at its current
         *        world transform. Call once when gameplay begins.
         *
         * The editor drives physics itself; this is for a built game running
         * the world directly.
         */
        void startPhysics();

        /** @brief Steps the simulation and syncs transforms back into nodes. */
        void updatePhysics(float deltaTime);

        /** @brief Destroys all bodies/characters and shuts the simulation down. */
        void stopPhysics();

        /** @brief Returns true between startPhysics() and stopPhysics(). */
        bool getPhysicsRunning() const { return physicsRunning; }

        // --- Particle system lifecycle (standalone runtime) ------------------

        /**
         * @brief Returns the particle manager owned by this world.
         *
         * Created automatically; call init() on it after a driver is current
         * before calling startParticles().
         */
        kParticleManager *getParticleManager();

        /**
         * @brief Registers every particle descriptor from scene objects with
         *        the particle manager and begins simulation. Call once when
         *        gameplay begins, after the particle manager is initialised.
         */
        void startParticles();

        /** @brief Steps the particle simulation by @p deltaTime seconds. */
        void updateParticles(float deltaTime);

        /** @brief Stops simulation and clears all live particles. */
        void stopParticles();

        /** @brief Returns true between startParticles() and stopParticles(). */
        bool getParticlesRunning() const { return particlesRunning; }

        /**
         * @brief Serialises the world to JSON.
         * @param startScene Index of the first scene to include (default 0).
         * @return JSON object with UUID, scenes, and cameras.
         */
        virtual json serialize(int startScene = 0);

        /**
         * @brief Restores the world from a JSON object.
         * @param data JSON produced by serialize().
         */
        virtual void deserialize(json data);

        /**
         * @brief Loads a world from a serialized @c scene.world file, standalone.
         *
         * Reconstructs every scene and object (meshes, lights, cameras, empties)
         * with their transforms and components (physics, character, navigation,
         * scripts), resolving mesh references and script bytecode relative to the
         * file's folder. Designed for a built game running without the editor.
         *
         * Requires setAssetManager() to have been called first. Picks the first
         * camera found as the main camera.
         *
         * @param path Path to @c scene.world; asset data is resolved relative to
         *             its parent folder (Library/ImportedAssets, Library/Scripts).
         * @return true on success.
         */
        bool loadFromFile(const kString &path);

    protected:
    private:
        /// Collects every object across all active scenes into a flat list.
        std::vector<kObject *> collectAllObjects();
        /// Ensures the component's script asset is registered; returns its UUID.
        kString resolveScriptAsset(kScript &component);

        kAssetManager *assetManager = nullptr; ///< Asset loader reference.
        kInputManager *inputManager = nullptr; ///< Named input manager (borrowed, not owned).

        std::vector<kScene *>  scenes;  ///< Registered scenes.
        std::vector<kCamera *> cameras; ///< Registered cameras.

        kCamera *mainCamera = nullptr; ///< Active render camera.

        kScriptManager *scriptManager  = nullptr; ///< AngelScript manager (world-owned).
        bool            scriptsRunning = false;   ///< True while scripts are executing.

        kPhysicsManager       *physicsManager  = nullptr; ///< Physics world (runtime-owned).
        bool                   physicsRunning  = false;   ///< True while physics is stepping.
        std::vector<kObject *> physicsBodies;             ///< Nodes with a live rigid body.
        std::vector<kObject *> characterBodies;           ///< Nodes with a live character.
        std::vector<std::string> physicsLayers{ "Default" }; ///< Named physics layers (serialized with the world).

        kParticleManager *particleManager  = nullptr; ///< Particle system manager (world-owned).
        bool              particlesRunning = false;   ///< True while particles are simulating.

        kString uuid; ///< World UUID.
    };
}

#endif // KWORLD_H
