/**
 * @file kcharactercontroller.h
 * @brief Capsule character controller built on Jolt's JPH::Character.
 *
 * A character controller is configured in the editor (radius/height/slope/etc.)
 * but, like rigid bodies, is only instantiated and simulated once the game is
 * playing. Movement is driven by gameplay code via setLinearVelocity().
 */

#ifndef KCHARACTERCONTROLLER_H
#define KCHARACTERCONTROLLER_H

#include "kexport.h"
#include "kdatatype.h"

#include <cstdint>

namespace kemena
{
    /**
     * @brief Editor-authored description of a character controller.
     *
     * The capsule's origin is at the object's feet (the collision capsule is
     * offset upward by height/2), matching how character controllers are placed
     * in most engines.
     */
    struct KEMENA3D_API kCharacterControllerDesc
    {
        float radius        = 0.3f;  ///< Capsule radius.
        float height        = 1.8f;  ///< Total capsule height (feet to head).
        float mass          = 80.0f; ///< Body mass in kg.
        float friction      = 0.5f;  ///< Surface friction.
        float gravityFactor = 1.0f;  ///< Multiplier on world gravity.
        float slopeLimit    = 45.0f; ///< Maximum walkable slope, in degrees.
        float stepHeight     = 0.3f; ///< Reserved for step-up handling.

        kVec3 position = kVec3(0.0f, 0.0f, 0.0f);       ///< Initial world-space position (capsule feet origin).
        kQuat rotation = kQuat(1.0f, 0.0f, 0.0f, 0.0f); ///< Initial world-space orientation.

        std::string layer = "Default"; ///< User physics layer name (characters collide only with the same layer).
    };

    /**
     * @brief Runtime wrapper around a Jolt character.
     *
     * Created by kPhysicsManager::createCharacter(); never instantiated directly
     * by editor code. Call update() once per physics step (kPhysicsManager does
     * this) and read getPosition() to drive the scene node.
     */
    class KEMENA3D_API kCharacterController
    {
    public:
        /** @brief Constructs an uninitialised character controller; call init() before use. */
        kCharacterController();

        /** @brief Destroys the controller, releasing any underlying Jolt character. */
        ~kCharacterController();

        /**
         * @brief Initialises the Jolt character.
         * @param physicsSystem Opaque JPH::PhysicsSystem* from kPhysicsManager.
         * @param desc          Capsule + motion parameters.
         * @param userLayerIndex Index into the physics manager's layer-name list.
         * @return true on success.
         */
        bool init(void *physicsSystem, const kCharacterControllerDesc &desc, int userLayerIndex = 0);

        /** @brief Removes the character from the physics system. */
        void uninit();

        /**
         * @brief Per-step maintenance (Jolt PostSimulation). Called by
         *        kPhysicsManager::update() after the world steps.
         */
        void update(float deltaTime);

        /** @brief Returns the character's current world-space position (capsule feet origin). */
        kVec3 getPosition() const;

        /**
         * @brief Teleports the character to a world-space position.
         * @param position New world-space position (capsule feet origin).
         */
        void  setPosition(const kVec3 &position);

        /** @brief Returns the character's current world-space orientation. */
        kQuat getRotation() const;

        /**
         * @brief Sets the character's world-space orientation.
         * @param rotation New world-space orientation.
         */
        void  setRotation(const kQuat &rotation);

        /**
         * @brief Sets the character's world-space velocity (m/s).
         * @param velocity Desired linear velocity in metres per second.
         */
        void  setLinearVelocity(const kVec3 &velocity);

        /**
         * @brief Moves the character by a world-space per-step delta.
         *
         * Always treats the argument as a displacement for the next physics
         * step, never as a velocity. kPhysicsManager::update() converts it to
         * the body velocity that travels exactly that delta, so an animator
         * root-motion delta can be fed straight in and the character moves 1:1
         * with the animation. Use setLinearVelocity() only for true m/s drives.
         * @param delta World-space displacement to travel over one step.
         */
        void  move(const kVec3 &delta);

        /** @brief Returns the character's current world-space velocity (m/s). */
        kVec3 getLinearVelocity() const;

        /** @brief True when the character is standing on walkable ground. */
        bool  isOnGround() const;

        /** @brief Returns the underlying Jolt body id (GetIndexAndSequenceNumber). */
        uint32_t getBodyId() const;

        /**
         * @brief Internal — called by kPhysicsManager::update() right before the
         *        world steps to apply the pending move() displacement as a
         *        velocity. Not intended for direct scripting use.
         */
        void  applyPendingMove(float deltaTime);

    private:
        struct Impl;
        Impl *m_impl;
    };
}

#endif // KCHARACTERCONTROLLER_H
