/**
 * @file kphysicsobject.h
 * @brief A single physics body — shape, type (dynamic / static / kinematic / trigger), and runtime controls.
 */

#ifndef KPHYSICSOBJECT_H
#define KPHYSICSOBJECT_H

#include "kexport.h"
#include "kdatatype.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kemena
{
    // -------------------------------------------------------------------------
    // Shared physics-layer constants
    // -------------------------------------------------------------------------

    /**
     * @brief Maximum number of user-defined physics layers.
     *
     * Each named layer (e.g. "Default", "Player", "Enemy") maps to a Jolt object
     * layer. To preserve the static/dynamic broad-phase optimisation every user
     * layer occupies two Jolt object layers: a static variant (index*2) and a
     * moving variant (index*2+1). Bodies only collide with other bodies that are
     * on the *same* user layer.
     */
    static constexpr int kMaxPhysicsLayers = 32;

    /// Returns the Jolt object-layer value for a user layer index + motion flag.
    /// @param userLayerIndex Index into the physics manager's layer name list.
    /// @param moving         true = moving (dynamic/kinematic/trigger), false = static.
    inline uint32_t physicsObjectLayer(int userLayerIndex, bool moving)
    {
        return static_cast<uint32_t>(userLayerIndex) * 2 + (moving ? 1u : 0u);
    }

    /// Returns the user-layer index from an encoded Jolt object-layer value.
    inline int physicsUserLayerFromObjectLayer(uint32_t objectLayer)
    {
        return static_cast<int>(objectLayer / 2);
    }
    // -------------------------------------------------------------------------
    // Shape descriptor
    // -------------------------------------------------------------------------

    /** @brief Collision shape type. */
    enum class kPhysicsShapeType
    {
        Sphere,     ///< Uniform sphere defined by radius.
        Box,        ///< Axis-aligned box defined by half-extents.
        Capsule,    ///< Capsule (cylinder + hemispherical end-caps) defined by radius + total height.
        Cylinder,   ///< Cylinder defined by radius + total height.
        ConvexHull, ///< Convex hull of the object's mesh vertices; any motion type.
        Mesh,       ///< Triangle mesh from the object's mesh; static-only (Jolt restriction).
        Plane,      ///< Infinite plane (object's local +Y); static-only (Jolt restriction).
    };

    /**
     * @brief Parameters that fully describe a collision shape.
     *
     * Only the fields relevant to the chosen @c type need to be set.
     *
     * | type       | relevant fields                                |
     * |------------|------------------------------------------------|
     * | Sphere     | radius                                         |
     * | Box        | halfExtents                                    |
     * | Capsule    | radius, height (total, tip-to-tip)             |
     * | Cylinder   | radius, height (total)                         |
     * | ConvexHull | meshVertices + customScale (per-axis stretch)  |
     * | Mesh       | meshVertices + meshIndices + customScale       |
     * | Plane      | halfExtents.x / halfExtents.z (broadphase rect)|
     *
     * @c meshVertices / @c meshIndices are populated by the caller right before
     * kPhysicsManager::createObject() (typically from the owning kMesh) — they
     * aren't serialised because they're derived from the mesh asset.
     *
     * @c customScale is applied to ConvexHull / Mesh shapes via Jolt's
     * @c ScaledShape wrapper so the user can stretch a mesh-derived collider
     * on each axis. Defaults to (1,1,1).
     */
    struct kPhysicsShapeDesc
    {
        kPhysicsShapeType     type        = kPhysicsShapeType::Box;
        kVec3                 halfExtents = kVec3(0.5f, 0.5f, 0.5f); ///< Box / Plane: per-axis half-extents (Plane uses x,z).
        float                 radius      = 0.5f;                   ///< Sphere / Capsule / Cylinder radius.
        float                 height      = 1.0f;                   ///< Capsule / Cylinder total height.
        kVec3                 customScale = kVec3(1.0f, 1.0f, 1.0f);///< ConvexHull / Mesh per-axis scale.
        std::vector<kVec3>    meshVertices;                         ///< Mesh / ConvexHull source vertices.
        std::vector<uint32_t> meshIndices;                          ///< Mesh source indices (triplets per triangle).
    };

    // -------------------------------------------------------------------------
    // Object type (motion mode)
    // -------------------------------------------------------------------------

    /**
     * @brief Determines how the physics engine moves (or does not move) a body.
     */
    enum class kPhysicsObjectType
    {
        Dynamic,   ///< Full rigid-body simulation (affected by forces and gravity).
        Static,    ///< Immovable; used for world geometry and terrain.
        Kinematic, ///< Moved via setPosition / setVelocity; not affected by forces.
        Trigger,   ///< Detects overlaps but exerts no physical response.
    };

    // -------------------------------------------------------------------------
    // Creation descriptor
    // -------------------------------------------------------------------------

    /**
     * @brief All parameters needed to create a kPhysicsObject.
     *
     * Pass this to kPhysicsManager::createObject().
     */
    struct kPhysicsObjectDesc
    {
        kPhysicsShapeDesc  shape;
        kPhysicsObjectType type          = kPhysicsObjectType::Dynamic;
        kVec3              position      = kVec3(0.0f, 0.0f, 0.0f);
        kQuat              rotation      = kQuat(1.0f, 0.0f, 0.0f, 0.0f); ///< Identity quaternion.
        /// Collider offset from the object's pivot, expressed in the object's
        /// local space. The collider shape is translated by this amount (rotated
        /// by the object's rotation) relative to the body origin.
        kVec3              offsetPosition = kVec3(0.0f, 0.0f, 0.0f);
        /// Collider rotation offset from the object's pivot. Applied on top of
        /// the object's rotation so the collider can be oriented independently.
        kQuat              offsetRotation = kQuat(1.0f, 0.0f, 0.0f, 0.0f);
        float              mass          = 1.0f;   ///< kg; ignored for Static / Kinematic.
        float              friction      = 0.5f;   ///< 0 = frictionless, 1 = high friction.
        float              restitution   = 0.0f;   ///< 0 = inelastic, 1 = perfectly elastic.
        float              linearDamping  = 0.05f; ///< Linear velocity drag per second.
        float              angularDamping = 0.05f; ///< Angular velocity drag per second.
        float              gravityFactor  = 1.0f;  ///< Multiplier on world gravity (0 = gravity-free).
        std::string        layer          = "Default"; ///< User physics layer name; bodies only collide with the same layer.
    };

    // -------------------------------------------------------------------------
    // kPhysicsObject
    // -------------------------------------------------------------------------

    /**
     * @brief Represents a single rigid body or trigger volume inside the physics simulation.
     *
     * Instances must be created via kPhysicsManager::createObject() and destroyed with
     * kPhysicsManager::destroyObject().
     *
     * @code
     *   kPhysicsObjectDesc desc;
     *   desc.shape.type        = kPhysicsShapeType::Box;
     *   desc.shape.halfExtents = kVec3(1.f, 0.5f, 1.f);
     *   desc.type              = kPhysicsObjectType::Dynamic;
     *   desc.position          = kVec3(0.f, 5.f, 0.f);
     *
     *   kPhysicsObject* box = physicsManager->createObject(desc);
     *   box->applyImpulse(kVec3(0.f, 10.f, 0.f));
     *
     *   // Each frame:
     *   physicsManager->update(deltaTime);
     *   object->setPosition(box->getPosition());
     * @endcode
     */
    class KEMENA3D_API kPhysicsObject
    {
    public:
        kPhysicsObject();
        ~kPhysicsObject();

        // --- Transform -------------------------------------------------------

        /** @brief Teleports the body to a new world-space position. */
        void setPosition(const kVec3 &position);

        /** @brief Sets the body's world-space orientation. */
        void setRotation(const kQuat &rotation);

        /** @brief Returns the body's current world-space position. */
        kVec3 getPosition() const;

        /** @brief Returns the body's current world-space orientation. */
        kQuat getRotation() const;

        /**
         * @brief Returns the collider's local offset from the object's pivot.
         *
         * The body transform is the owning object's transform composed with
         * this offset; kObject::syncFromPhysics() inverts it when copying the
         * simulated body transform back onto the object.
         */
        kVec3 getOffsetPosition() const;

        /** @brief Returns the collider's local rotation offset. */
        kQuat getOffsetRotation() const;

        // --- Velocity --------------------------------------------------------

        /** @brief Directly sets the linear velocity (m/s). */
        void setLinearVelocity(const kVec3 &velocity);

        /** @brief Directly sets the angular velocity (rad/s). */
        void setAngularVelocity(const kVec3 &velocity);

        /** @brief Returns the current linear velocity (m/s). */
        kVec3 getLinearVelocity() const;

        /** @brief Returns the current angular velocity (rad/s). */
        kVec3 getAngularVelocity() const;

        // --- Forces ----------------------------------------------------------

        /**
         * @brief Applies a continuous force (N) at the centre of mass this step.
         * @note Has no effect on Static or Kinematic bodies.
         */
        void applyForce(const kVec3 &force);

        /**
         * @brief Applies an instantaneous impulse (kg·m/s) at the centre of mass.
         * @note Has no effect on Static or Kinematic bodies.
         */
        void applyImpulse(const kVec3 &impulse);

        /**
         * @brief Applies a torque (N·m) for the current simulation step.
         * @note Has no effect on Static or Kinematic bodies.
         */
        void applyTorque(const kVec3 &torque);

        // --- Properties ------------------------------------------------------

        /**
         * @brief Changes the body mass (kg).  No-op for Static bodies.
         */
        void setMass(float mass);

        /** @brief Sets the coefficient of friction (0–1). */
        void setFriction(float friction);

        /** @brief Sets the coefficient of restitution / bounciness (0–1). */
        void setRestitution(float restitution);

        /** @brief Sets the linear velocity drag coefficient (per second). */
        void setLinearDamping(float damping);

        /** @brief Sets the angular velocity drag coefficient (per second). */
        void setAngularDamping(float damping);

        /**
         * @brief Scales the world gravity applied to this body.
         * @param factor 0 = no gravity, 1 = full gravity (default), negative = anti-gravity.
         */
        void setGravityFactor(float factor);

        // --- State queries ---------------------------------------------------

        /** @brief Returns true if the body is currently awake and being simulated. */
        bool isActive() const;

        /** @brief Returns the motion type (Dynamic / Static / Kinematic / Trigger). */
        kPhysicsObjectType getObjectType() const;

        /** @brief Returns the underlying collision shape type. */
        kPhysicsShapeType getShapeType() const;

        // --- Internal (kPhysicsManager only) ---------------------------------

        /**
         * @brief Initialises the body inside an existing Jolt PhysicsSystem.
         * @param physicsSystem Opaque pointer to a `JPH::PhysicsSystem` owned by kPhysicsManager.
         * @param desc          Full creation parameters.
         * @param userLayerIndex Index into the physics manager's layer-name list (see kMaxPhysicsLayers).
         * @return true on success.
         * @note Not part of the public API — call kPhysicsManager::createObject() instead.
         */
        bool init(void *physicsSystem, const kPhysicsObjectDesc &desc, int userLayerIndex = 0);

        /** @brief Removes the body from the simulation and releases its resources. */
        void uninit();

        /** @brief Returns the internal Jolt body ID (cast from JPH::BodyID). */
        uint32_t getBodyId() const;

    protected:
    private:
        struct Impl;
        Impl *m_impl;
    };

} // namespace kemena

#endif // KPHYSICSOBJECT_H
