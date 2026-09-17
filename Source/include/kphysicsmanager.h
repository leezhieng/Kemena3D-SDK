/**
 * @file kphysicsmanager.h
 * @brief Physics subsystem manager — owns the simulation world and acts as a factory for kPhysicsObject.
 */

#ifndef KPHYSICSMANAGER_H
#define KPHYSICSMANAGER_H

#include "kexport.h"
#include "kdatatype.h"
#include "kphysicsobject.h"
#include "kcharactercontroller.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kemena
{
    /**
     * @brief Result of a physics raycast query.
     *
     * Returned by kPhysicsManager::raycast().  Check @c hit before accessing
     * any other field.
     */
    struct kPhysicsRaycastHit
    {
        bool           hit      = false;   ///< true if the ray struck a body.
        float          distance = 0.0f;    ///< Distance from the ray origin to the hit point.
        kVec3          hitPoint;           ///< World-space position of the intersection.
        kVec3          hitNormal;          ///< World-space surface normal at the hit point.
        kPhysicsObject *object  = nullptr; ///< The physics body that was hit (manager-owned).
    };

    /**
     * @brief One collision/overlap transition captured during a physics step.
     *
     * Produced by the manager's internal Jolt contact listener and consumed by
     * the caller (kWorld / editor) after update(). Body ids are stable Jolt
     * body ids (@c BodyID::GetIndexAndSequenceNumber), sorted low-to-high.
     */
    struct KEMENA3D_API kPhysicsContactEvent
    {
        enum class Action
        {
            Enter, ///< Contact/overlap began this step.
            Stay,  ///< Contact/overlap persisted from the previous step.
            Exit,  ///< Contact/overlap ended this step.
        };

        Action   action    = Action::Enter; ///< Enter / Stay / Exit.
        bool     isTrigger = false;         ///< true when one body is a sensor (trigger volume).
        uint32_t bodyA     = 0;             ///< First participating body id (sorted low).
        uint32_t bodyB     = 0;             ///< Second participating body id (sorted high).
    };

    /**
     * @brief Owns the Jolt PhysicsSystem and manages the lifecycle of all physics bodies.
     *
     * Create one kPhysicsManager per scene.  Call init() once, then step the simulation
     * each frame with update().  After each update, sync moving objects back to their
     * scene nodes via kObject::syncFromPhysics().
     *
     * @code
     *   kPhysicsManager* physics = kemena::createPhysicsManager();
     *
     *   // Create a dynamic box
     *   kPhysicsObjectDesc desc;
     *   desc.shape.type        = kPhysicsShapeType::Box;
     *   desc.shape.halfExtents = kVec3(1.f, 1.f, 1.f);
     *   desc.position          = kVec3(0.f, 10.f, 0.f);
     *   kPhysicsObject* box = physics->createObject(desc);
     *
     *   // Attach to a scene node
     *   myObject->attachPhysics(box);
     *
     *   // Game loop:
     *   physics->update(deltaTime);
     *   myObject->syncFromPhysics();
     * @endcode
     */
    class KEMENA3D_API kPhysicsManager
    {
    public:
        /** @brief Constructs an uninitialised manager; call init() before use. */
        kPhysicsManager();

        /** @brief Destroys the manager, calling shutdown() to release all bodies. */
        ~kPhysicsManager();

        // --- Lifecycle -------------------------------------------------------

        /**
         * @brief Initialises the Jolt physics engine and internal systems.
         * @return true on success.
         */
        bool init();

        /**
         * @brief Destroys all physics bodies and tears down the engine.
         * Called automatically by the destructor.
         */
        void shutdown();

        // --- Simulation ------------------------------------------------------

        /**
         * @brief Advances the simulation by @p deltaTime seconds.
         * @param deltaTime Time since the last frame in seconds.
         * Call this once per game-loop iteration before reading body transforms.
         */
        void update(float deltaTime);

        // --- World settings --------------------------------------------------

        /**
         * @brief Sets the global gravity vector (m/s²).
         * @param gravity Acceleration vector — default is kVec3(0, -9.81, 0).
         */
        void setGravity(const kVec3 &gravity);

        /** @brief Returns the current global gravity vector (m/s²). */
        kVec3 getGravity() const;

        // --- User-defined layers ---------------------------------------------

        /**
         * @brief Replaces the list of named physics layers.
         *
         * The first entry is always the "Default" layer (index 0). Bodies are
         * only allowed to interact with other bodies on the same layer.
         *
         * @param names Ordered layer names; the list is capped at kMaxPhysicsLayers.
         */
        void setLayerNames(const std::vector<std::string> &names);

        /** @brief Returns the current ordered list of named physics layers. */
        std::vector<std::string> getLayerNames() const;

        /**
         * @brief Returns the index of a named layer, or -1 if it is not defined.
         * @param name Layer name to look up.
         */
        int getLayerIndex(const std::string &name) const;

        /**
         * @brief Returns the name of a layer index, clamped to the valid range.
         * @param index Layer index.
         */
        std::string getLayerName(int index) const;

        // --- Object factory --------------------------------------------------

        /**
         * @brief Creates a physics body and returns a new kPhysicsObject.
         *
         * The returned pointer is owned by this manager.  Release it with
         * destroyObject() rather than deleting it directly.
         *
         * @param desc Full shape and motion parameters.
         * @return Pointer to the new kPhysicsObject, or nullptr on failure.
         */
        kPhysicsObject *createObject(const kPhysicsObjectDesc &desc);

        /**
         * @brief Removes a physics body from the simulation and destroys the object.
         * @param object Pointer returned by createObject().
         */
        void destroyObject(kPhysicsObject *object);

        /**
         * @brief Creates a character controller in this physics world.
         *
         * The returned pointer is owned by the manager; release it with
         * destroyCharacter(). Each character's ground state is refreshed inside
         * update() after the world steps.
         *
         * @param desc Capsule + motion parameters.
         * @return Pointer to the new kCharacterController, or nullptr on failure.
         */
        kCharacterController *createCharacter(const kCharacterControllerDesc &desc);

        /**
         * @brief Removes a character controller from the simulation and destroys it.
         * @param character Pointer returned by createCharacter().
         */
        void destroyCharacter(kCharacterController *character);

        // --- Queries ---------------------------------------------------------

        /**
         * @brief Casts a ray into the physics world and returns the closest hit.
         *
         * Intended for game-play use — requires objects to have physics bodies
         * attached (created via createObject()).  For editor picking without
         * physics bodies, use kRenderer::pickObject() instead.
         *
         * @code
         *   kVec3 origin, dir;
         *   camera->screenToRay(mouseX, mouseY, vpW, vpH, origin, dir);
         *
         *   auto hit = physicsManager->raycast(origin, dir, 1000.0f);
         *   if (hit.hit)
         *       myObject->setPosition(hit.hitPoint);
         * @endcode
         *
         * @param origin      Ray origin in world space.
         * @param direction   Normalised ray direction in world space.
         * @param maxDistance Maximum distance along the ray to test.
         * @return kPhysicsRaycastHit with hit == false if no body was struck.
         */
        kPhysicsRaycastHit raycast(const kVec3 &origin,
                                   const kVec3 &direction,
                                   float maxDistance = 1000.0f);

        // --- Contact events -------------------------------------------------

        /**
         * @brief Returns and clears the contact events captured during the last update().
         *
         * Call after update() to receive every collision / sensor-overlap
         * transition that occurred while the world stepped. Body ids refer to
         * the owning kPhysicsObject / kCharacterController (see getBodyId()).
         *
         * @return Events from the most recent step (Enter/Stay/Exit).
         */
        std::vector<kPhysicsContactEvent> takeContactEvents();

    protected:
    private:
        struct Impl;
        Impl *m_impl;
    };

} // namespace kemena

#endif // KPHYSICSMANAGER_H
