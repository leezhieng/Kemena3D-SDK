#include "kcharactercontroller.h"
#include "kphysicsobject.h"

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Character/Character.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <algorithm>
#include <iostream>
#include <algorithm>

// Degrees → radians (avoids pulling in extra math headers).
static constexpr float K_DEG2RAD = 0.01745329252f;

namespace kemena
{
    struct kCharacterController::Impl
    {
        JPH::PhysicsSystem    *physicsSystem = nullptr;
        JPH::Ref<JPH::Character> character;
        bool                   initialized = false;
        kVec3                  pendingMove = kVec3(0.0f); ///< Displacement queued by move() for the next step.
        bool                   moveDriven  = false;       ///< True while move() is the active driver (enables the anti-slide brake).
    };

    kCharacterController::kCharacterController()
        : m_impl(new Impl())
    {
    }

    kCharacterController::~kCharacterController()
    {
        uninit();
        delete m_impl;
    }

    bool kCharacterController::init(void *physSys, const kCharacterControllerDesc &desc, int userLayerIndex)
    {
        if (m_impl->initialized)
            uninit();

        // Clamp to the supported range (see kMaxPhysicsLayers).
        if (userLayerIndex < 0)            userLayerIndex = 0;
        if (userLayerIndex >= kMaxPhysicsLayers) userLayerIndex = kMaxPhysicsLayers - 1;

        auto *ps = static_cast<JPH::PhysicsSystem *>(physSys);
        if (!ps)
            return false;

        // Build a capsule whose origin sits at the character's feet: the capsule
        // shape is offset upward by half the total height.
        float cylinderHalf = std::max(desc.height * 0.5f - desc.radius, 0.01f);
        JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(cylinderHalf, desc.radius);

        auto offsetResult =
            JPH::RotatedTranslatedShapeSettings(
                JPH::Vec3(0.0f, desc.height * 0.5f, 0.0f),
                JPH::Quat::sIdentity(),
                capsule)
            .Create();
        if (offsetResult.HasError())
        {
            std::cout << "[kCharacterController] shape error: "
                      << offsetResult.GetError().c_str() << std::endl;
            return false;
        }
        JPH::RefConst<JPH::Shape> shape = offsetResult.Get();

        JPH::Ref<JPH::CharacterSettings> settings = new JPH::CharacterSettings();
        settings->mShape         = shape;
        settings->mLayer         = static_cast<JPH::ObjectLayer>(
            kemena::physicsObjectLayer(userLayerIndex, /*moving=*/true));
        settings->mMass          = desc.mass;
        settings->mFriction      = desc.friction;
        settings->mGravityFactor = desc.gravityFactor;
        settings->mMaxSlopeAngle = desc.slopeLimit * K_DEG2RAD;
        settings->mUp            = JPH::Vec3::sAxisY();
        // Anything below the feet plane is treated as supporting ground.
        settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -desc.radius);

        m_impl->character = new JPH::Character(
            settings.GetPtr(),
            JPH::RVec3(desc.position.x, desc.position.y, desc.position.z),
            JPH::Quat(desc.rotation.x, desc.rotation.y, desc.rotation.z, desc.rotation.w),
            0,
            ps);

        if (m_impl->character == nullptr)
        {
            std::cout << "[kCharacterController] failed to create character." << std::endl;
            return false;
        }

        m_impl->character->AddToPhysicsSystem(JPH::EActivation::Activate);
        m_impl->physicsSystem = ps;
        m_impl->initialized   = true;
        return true;
    }

    void kCharacterController::uninit()
    {
        if (!m_impl->initialized)
            return;

        m_impl->pendingMove = kVec3(0.0f);
        m_impl->moveDriven  = false;

        if (m_impl->character != nullptr)
        {
            m_impl->character->RemoveFromPhysicsSystem();
            m_impl->character = nullptr;
        }
        m_impl->physicsSystem = nullptr;
        m_impl->initialized   = false;
    }

    void kCharacterController::update(float /*deltaTime*/)
    {
        if (!m_impl->initialized || m_impl->character == nullptr)
            return;
        // Refresh the character's ground/contact state against the world after
        // the simulation step. 0.05 m is Jolt's recommended separation margin.
        m_impl->character->PostSimulation(0.05f);
    }

    kVec3 kCharacterController::getPosition() const
    {
        if (!m_impl->initialized) return kVec3(0.0f);
        JPH::RVec3 p = m_impl->character->GetPosition();
        return kVec3(p.GetX(), p.GetY(), p.GetZ());
    }

    void kCharacterController::setPosition(const kVec3 &position)
    {
        if (!m_impl->initialized) return;
        m_impl->character->SetPosition(JPH::RVec3(position.x, position.y, position.z));
    }

    kQuat kCharacterController::getRotation() const
    {
        if (!m_impl->initialized) return kQuat(1.0f, 0.0f, 0.0f, 0.0f);
        JPH::Quat q = m_impl->character->GetRotation();
        return kQuat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }

    void kCharacterController::setRotation(const kQuat &rotation)
    {
        if (!m_impl->initialized) return;
        m_impl->character->SetRotation(
            JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w));
    }

    void kCharacterController::setLinearVelocity(const kVec3 &velocity)
    {
        if (!m_impl->initialized) return;
        // Switching to explicit velocity mode: drop any queued move and the
        // move-driven brake flag so neither can override this velocity next step.
        m_impl->pendingMove = kVec3(0.0f);
        m_impl->moveDriven  = false;
        m_impl->character->SetLinearVelocity(JPH::Vec3(velocity.x, velocity.y, velocity.z));
    }

    void kCharacterController::move(const kVec3 &delta)
    {
        if (!m_impl->initialized || m_impl->character == nullptr)
            return;
        // Accumulate: a script may issue several move() calls per frame.
        m_impl->pendingMove.x += delta.x;
        m_impl->pendingMove.y += delta.y;
        m_impl->pendingMove.z += delta.z;
    }

    void kCharacterController::applyPendingMove(float deltaTime)
    {
        if (!m_impl->initialized || m_impl->character == nullptr || deltaTime <= 0.0f)
            return;

        if (m_impl->pendingMove.x != 0.0f ||
            m_impl->pendingMove.y != 0.0f ||
            m_impl->pendingMove.z != 0.0f)
        {
            // Convert the per-step displacement into the velocity that travels
            // exactly that delta over this step. Preserve the current Y velocity
            // so gravity/floor handling keeps working, unless the caller explicitly
            // requested a vertical move via the Y component.
            const JPH::Vec3 cur = m_impl->character->GetLinearVelocity();
            float vy = cur.GetY();
            if (m_impl->pendingMove.y != 0.0f)
                vy = m_impl->pendingMove.y / deltaTime;

            const float invDt = 1.0f / deltaTime;
            m_impl->character->SetLinearVelocity(JPH::Vec3(
                m_impl->pendingMove.x * invDt,
                vy,
                m_impl->pendingMove.z * invDt));
            m_impl->pendingMove = kVec3(0.0f);
            m_impl->moveDriven  = true;
            return;
        }

        // No displacement queued this step: if move() was driving the character
        // on the previous step, brake now so the residual body velocity cannot
        // make it slide after the motion ends. Pure setLinearVelocity characters
        // never set moveDriven, so their velocity is left untouched.
        if (m_impl->moveDriven)
        {
            const JPH::Vec3 cur = m_impl->character->GetLinearVelocity();
            m_impl->character->SetLinearVelocity(JPH::Vec3(0.0f, cur.GetY(), 0.0f));
            m_impl->moveDriven = false;
        }
    }

    kVec3 kCharacterController::getLinearVelocity() const
    {
        if (!m_impl->initialized) return kVec3(0.0f);
        JPH::Vec3 v = m_impl->character->GetLinearVelocity();
        return kVec3(v.GetX(), v.GetY(), v.GetZ());
    }

    bool kCharacterController::isOnGround() const
    {
        if (!m_impl->initialized) return false;
        return m_impl->character->GetGroundState() ==
               JPH::CharacterBase::EGroundState::OnGround;
    }

    uint32_t kCharacterController::getBodyId() const
    {
        if (!m_impl->initialized || m_impl->character == nullptr)
            return 0;
        return m_impl->character->GetBodyID().GetIndexAndSequenceNumber();
    }
}
