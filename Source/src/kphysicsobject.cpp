#include "kphysicsobject.h"

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <iostream>
#include <algorithm>

namespace kemena
{
    struct kPhysicsObject::Impl
    {
        JPH::PhysicsSystem *physicsSystem = nullptr;
        JPH::BodyID         bodyId;
        kPhysicsObjectType  type      = kPhysicsObjectType::Dynamic;
        kPhysicsShapeType   shapeType = kPhysicsShapeType::Box;
        bool                initialized = false;
    };

    kPhysicsObject::kPhysicsObject()
        : m_impl(new Impl())
    {
    }

    kPhysicsObject::~kPhysicsObject()
    {
        uninit();
        delete m_impl;
    }

    bool kPhysicsObject::init(void *physSys, const kPhysicsObjectDesc &desc, int userLayerIndex)
    {
        if (m_impl->initialized)
            uninit();

        // Clamp to the supported range (see kMaxPhysicsLayers).
        if (userLayerIndex < 0)            userLayerIndex = 0;
        if (userLayerIndex >= kMaxPhysicsLayers) userLayerIndex = kMaxPhysicsLayers - 1;

        auto *ps = static_cast<JPH::PhysicsSystem *>(physSys);

        // --- Build shape -------------------------------------------------
        JPH::ShapeRefC shape;
        const kPhysicsShapeDesc &sd = desc.shape;

        switch (sd.type)
        {
            case kPhysicsShapeType::Sphere:
                shape = new JPH::SphereShape(sd.radius);
                break;

            case kPhysicsShapeType::Box:
                shape = new JPH::BoxShape(
                    JPH::Vec3(sd.halfExtents.x, sd.halfExtents.y, sd.halfExtents.z));
                break;

            case kPhysicsShapeType::Capsule:
                // Jolt CapsuleShape(halfHeight, radius)
                shape = new JPH::CapsuleShape(sd.height * 0.5f, sd.radius);
                break;

            case kPhysicsShapeType::Cylinder:
                // Jolt CylinderShape(halfHeight, radius)
                shape = new JPH::CylinderShape(sd.height * 0.5f, sd.radius);
                break;

            case kPhysicsShapeType::ConvexHull:
            {
                if (sd.meshVertices.empty())
                {
                    std::cout << "[kPhysicsObject] ConvexHull shape has no vertices." << std::endl;
                    return false;
                }
                JPH::Array<JPH::Vec3> pts;
                pts.reserve(sd.meshVertices.size());
                for (const auto &v : sd.meshVertices)
                    pts.emplace_back(v.x, v.y, v.z);
                JPH::ConvexHullShapeSettings hs(pts);
                auto res = hs.Create();
                if (res.HasError())
                {
                    std::cout << "[kPhysicsObject] ConvexHull build failed: "
                              << res.GetError().c_str() << std::endl;
                    return false;
                }
                shape = res.Get();
                break;
            }

            case kPhysicsShapeType::Mesh:
            {
                if (sd.meshVertices.empty() || sd.meshIndices.empty() ||
                    (sd.meshIndices.size() % 3) != 0)
                {
                    std::cout << "[kPhysicsObject] Mesh shape needs vertices and a "
                                 "triangle-indexed index buffer." << std::endl;
                    return false;
                }
                JPH::VertexList verts;
                verts.reserve(sd.meshVertices.size());
                for (const auto &v : sd.meshVertices)
                    verts.emplace_back(v.x, v.y, v.z);

                JPH::IndexedTriangleList tris;
                tris.reserve(sd.meshIndices.size() / 3);
                for (size_t i = 0; i + 2 < sd.meshIndices.size(); i += 3)
                    tris.emplace_back(sd.meshIndices[i], sd.meshIndices[i + 1],
                                      sd.meshIndices[i + 2], 0);

                JPH::MeshShapeSettings ms(verts, tris);
                auto res = ms.Create();
                if (res.HasError())
                {
                    std::cout << "[kPhysicsObject] Mesh build failed: "
                              << res.GetError().c_str() << std::endl;
                    return false;
                }
                shape = res.Get();
                break;
            }

            case kPhysicsShapeType::Plane:
            {
                // Object's local +Y is the plane normal; the body's rotation
                // orients it in world space (so rotate the kObject to angle
                // the ground). halfExtents.x / halfExtents.z drive the
                // broadphase rectangle so a user-sized ground works; we use
                // the larger of the two as Jolt's symmetric half-extent.
                float halfX = std::max(0.1f, sd.halfExtents.x);
                float halfZ = std::max(0.1f, sd.halfExtents.z);
                float planeHalf = std::max(halfX, halfZ);
                JPH::PlaneShapeSettings ps(JPH::Plane(JPH::Vec3(0, 1, 0), 0.0f),
                                           nullptr, planeHalf);
                auto res = ps.Create();
                if (res.HasError())
                {
                    std::cout << "[kPhysicsObject] Plane build failed: "
                              << res.GetError().c_str() << std::endl;
                    return false;
                }
                shape = res.Get();
                break;
            }
        }

        // Wrap mesh-derived shapes in a ScaledShape so users can stretch them
        // on each axis through the Inspector. Primitives ignore customScale
        // (their own params already control their size).
        if ((sd.type == kPhysicsShapeType::ConvexHull ||
             sd.type == kPhysicsShapeType::Mesh) &&
            (sd.customScale.x != 1.0f || sd.customScale.y != 1.0f || sd.customScale.z != 1.0f))
        {
            shape = new JPH::ScaledShape(shape,
                                         JPH::Vec3(sd.customScale.x,
                                                   sd.customScale.y,
                                                   sd.customScale.z));
        }

        // Jolt's MeshShape and PlaneShape only allow Static / Kinematic motion.
        // Coerce anything else so we don't trip Jolt's body-creation assert.
        kPhysicsObjectType bodyType = desc.type;
        const bool requiresStatic = (sd.type == kPhysicsShapeType::Mesh ||
                                     sd.type == kPhysicsShapeType::Plane);
        if (requiresStatic &&
            bodyType != kPhysicsObjectType::Static &&
            bodyType != kPhysicsObjectType::Kinematic)
        {
            std::cout << "[kPhysicsObject] "
                      << (sd.type == kPhysicsShapeType::Plane ? "Plane" : "Mesh")
                      << " shape forced to Static "
                         "(Jolt does not allow Dynamic for this shape)." << std::endl;
            bodyType = kPhysicsObjectType::Static;
        }

        // --- Motion type and layer ---------------------------------------
        // The object layer encodes the user layer index (see kMaxPhysicsLayers)
        // plus a moving/static flag so the broad-phase optimisation is preserved
        // while bodies on different user layers never interact.
        JPH::EMotionType motionType;
        bool moving;

        switch (bodyType)
        {
            case kPhysicsObjectType::Dynamic:
                motionType = JPH::EMotionType::Dynamic;
                moving     = true;
                break;

            case kPhysicsObjectType::Static:
                motionType = JPH::EMotionType::Static;
                moving     = false;
                break;

            case kPhysicsObjectType::Kinematic:
                motionType = JPH::EMotionType::Kinematic;
                moving     = true;
                break;

            case kPhysicsObjectType::Trigger:
                // Trigger volumes are Kinematic sensors: they must not fall under
                // gravity or sink into static geometry, and a kinematic sensor only
                // reports overlaps with active dynamic/kinematic actors — it ignores
                // static floors and other triggers (see Jolt Body::SetIsSensor).
                motionType = JPH::EMotionType::Kinematic;
                moving     = true;
                break;

            default:
                motionType = JPH::EMotionType::Dynamic;
                moving     = true;
                break;
        }
        const JPH::ObjectLayer layer =
            static_cast<JPH::ObjectLayer>(physicsObjectLayer(userLayerIndex, moving));

        // --- Body creation settings --------------------------------------
        JPH::BodyCreationSettings settings(
            shape,
            JPH::RVec3(desc.position.x, desc.position.y, desc.position.z),
            JPH::Quat(desc.rotation.x, desc.rotation.y, desc.rotation.z, desc.rotation.w),
            motionType,
            layer);

        settings.mIsSensor        = (bodyType == kPhysicsObjectType::Trigger);
        settings.mFriction        = desc.friction;
        settings.mRestitution     = desc.restitution;
        settings.mLinearDamping   = desc.linearDamping;
        settings.mAngularDamping  = desc.angularDamping;
        settings.mGravityFactor   = desc.gravityFactor;

        if (motionType == JPH::EMotionType::Dynamic && desc.mass > 0.0f)
        {
            settings.mOverrideMassProperties         = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass   = desc.mass;
        }

        // --- Create and add body -----------------------------------------
        JPH::BodyInterface &bi = ps->GetBodyInterface();
        m_impl->bodyId = bi.CreateAndAddBody(settings, JPH::EActivation::Activate);

        if (m_impl->bodyId.IsInvalid())
        {
            std::cout << "[kPhysicsObject] Failed to create physics body." << std::endl;
            return false;
        }

        m_impl->physicsSystem = ps;
        m_impl->type          = bodyType;
        m_impl->shapeType     = desc.shape.type;
        m_impl->initialized   = true;
        return true;
    }

    void kPhysicsObject::uninit()
    {
        if (!m_impl->initialized)
            return;

        JPH::BodyInterface &bi = m_impl->physicsSystem->GetBodyInterface();
        bi.RemoveBody(m_impl->bodyId);
        bi.DestroyBody(m_impl->bodyId);

        m_impl->physicsSystem = nullptr;
        m_impl->bodyId        = JPH::BodyID();
        m_impl->initialized   = false;
    }

    uint32_t kPhysicsObject::getBodyId() const
    {
        return m_impl->bodyId.GetIndexAndSequenceNumber();
    }

    // --- Transform -------------------------------------------------------

    void kPhysicsObject::setPosition(const kVec3 &position)
    {
        if (!m_impl->initialized) return;
        m_impl->physicsSystem->GetBodyInterface().SetPosition(
            m_impl->bodyId,
            JPH::RVec3(position.x, position.y, position.z),
            JPH::EActivation::Activate);
    }

    void kPhysicsObject::setRotation(const kQuat &rotation)
    {
        if (!m_impl->initialized) return;
        m_impl->physicsSystem->GetBodyInterface().SetRotation(
            m_impl->bodyId,
            JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w),
            JPH::EActivation::Activate);
    }

    kVec3 kPhysicsObject::getPosition() const
    {
        if (!m_impl->initialized) return kVec3(0.0f);
        JPH::RVec3 p = m_impl->physicsSystem->GetBodyInterface().GetPosition(m_impl->bodyId);
        return kVec3(p.GetX(), p.GetY(), p.GetZ());
    }

    kQuat kPhysicsObject::getRotation() const
    {
        if (!m_impl->initialized) return kQuat(1.0f, 0.0f, 0.0f, 0.0f);
        JPH::Quat q = m_impl->physicsSystem->GetBodyInterface().GetRotation(m_impl->bodyId);
        return kQuat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }

    // --- Velocity --------------------------------------------------------

    void kPhysicsObject::setLinearVelocity(const kVec3 &velocity)
    {
        if (!m_impl->initialized) return;
        m_impl->physicsSystem->GetBodyInterface().SetLinearVelocity(
            m_impl->bodyId, JPH::Vec3(velocity.x, velocity.y, velocity.z));
    }

    void kPhysicsObject::setAngularVelocity(const kVec3 &velocity)
    {
        if (!m_impl->initialized) return;
        m_impl->physicsSystem->GetBodyInterface().SetAngularVelocity(
            m_impl->bodyId, JPH::Vec3(velocity.x, velocity.y, velocity.z));
    }

    kVec3 kPhysicsObject::getLinearVelocity() const
    {
        if (!m_impl->initialized) return kVec3(0.0f);
        JPH::Vec3 v = m_impl->physicsSystem->GetBodyInterface().GetLinearVelocity(m_impl->bodyId);
        return kVec3(v.GetX(), v.GetY(), v.GetZ());
    }

    kVec3 kPhysicsObject::getAngularVelocity() const
    {
        if (!m_impl->initialized) return kVec3(0.0f);
        JPH::Vec3 v = m_impl->physicsSystem->GetBodyInterface().GetAngularVelocity(m_impl->bodyId);
        return kVec3(v.GetX(), v.GetY(), v.GetZ());
    }

    // --- Forces ----------------------------------------------------------

    void kPhysicsObject::applyForce(const kVec3 &force)
    {
        if (!m_impl->initialized) return;
        m_impl->physicsSystem->GetBodyInterface().AddForce(
            m_impl->bodyId, JPH::Vec3(force.x, force.y, force.z));
    }

    void kPhysicsObject::applyImpulse(const kVec3 &impulse)
    {
        if (!m_impl->initialized) return;
        m_impl->physicsSystem->GetBodyInterface().AddImpulse(
            m_impl->bodyId, JPH::Vec3(impulse.x, impulse.y, impulse.z));
    }

    void kPhysicsObject::applyTorque(const kVec3 &torque)
    {
        if (!m_impl->initialized) return;
        m_impl->physicsSystem->GetBodyInterface().AddTorque(
            m_impl->bodyId, JPH::Vec3(torque.x, torque.y, torque.z));
    }

    // --- Properties ------------------------------------------------------

    void kPhysicsObject::setMass(float mass)
    {
        if (!m_impl->initialized || mass <= 0.0f) return;

        JPH::BodyLockWrite lock(m_impl->physicsSystem->GetBodyLockInterface(),
                                m_impl->bodyId);
        if (lock.Succeeded())
        {
            JPH::Body *body = &lock.GetBody();
            if (body->GetMotionType() == JPH::EMotionType::Dynamic)
                body->GetMotionProperties()->SetInverseMass(1.0f / mass);
        }
    }

    void kPhysicsObject::setFriction(float friction)
    {
        if (!m_impl->initialized) return;
        m_impl->physicsSystem->GetBodyInterface().SetFriction(m_impl->bodyId, friction);
    }

    void kPhysicsObject::setRestitution(float restitution)
    {
        if (!m_impl->initialized) return;
        m_impl->physicsSystem->GetBodyInterface().SetRestitution(m_impl->bodyId, restitution);
    }

    void kPhysicsObject::setLinearDamping(float damping)
    {
        if (!m_impl->initialized) return;

        JPH::BodyLockWrite lock(m_impl->physicsSystem->GetBodyLockInterface(),
                                m_impl->bodyId);
        if (lock.Succeeded())
        {
            JPH::Body *body = &lock.GetBody();
            if (body->GetMotionProperties())
                body->GetMotionProperties()->SetLinearDamping(damping);
        }
    }

    void kPhysicsObject::setAngularDamping(float damping)
    {
        if (!m_impl->initialized) return;

        JPH::BodyLockWrite lock(m_impl->physicsSystem->GetBodyLockInterface(),
                                m_impl->bodyId);
        if (lock.Succeeded())
        {
            JPH::Body *body = &lock.GetBody();
            if (body->GetMotionProperties())
                body->GetMotionProperties()->SetAngularDamping(damping);
        }
    }

    void kPhysicsObject::setGravityFactor(float factor)
    {
        if (!m_impl->initialized) return;

        JPH::BodyLockWrite lock(m_impl->physicsSystem->GetBodyLockInterface(),
                                m_impl->bodyId);
        if (lock.Succeeded())
        {
            JPH::Body *body = &lock.GetBody();
            if (body->GetMotionProperties())
                body->GetMotionProperties()->SetGravityFactor(factor);
        }
    }

    // --- State queries ---------------------------------------------------

    bool kPhysicsObject::isActive() const
    {
        if (!m_impl->initialized) return false;
        return m_impl->physicsSystem->GetBodyInterface().IsActive(m_impl->bodyId);
    }

    kPhysicsObjectType kPhysicsObject::getObjectType() const
    {
        return m_impl->type;
    }

    kPhysicsShapeType kPhysicsObject::getShapeType() const
    {
        return m_impl->shapeType;
    }

} // namespace kemena
