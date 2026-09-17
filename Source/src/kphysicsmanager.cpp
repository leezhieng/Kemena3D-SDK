#include "kphysicsmanager.h"

#ifdef _MSC_VER
#  pragma warning(push, 0)
#endif
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <iostream>
#include <memory>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// Object layer constants  (must match kphysicsobject.cpp / kcharactercontroller.cpp)
//
// Every user layer occupies two Jolt object layers: a static variant
// (index*2) and a moving variant (index*2+1). Bodies only collide with other
// bodies on the *same* user layer (see ObjLayerPairFilter).
// ---------------------------------------------------------------------------
namespace Layers
{
    static constexpr JPH::ObjectLayer NUM_LAYERS = kemena::kMaxPhysicsLayers * 2;
}

// ---------------------------------------------------------------------------
// Broad-phase layer constants
// ---------------------------------------------------------------------------
namespace BPLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS = 2;
}

// ---------------------------------------------------------------------------
// Broad-phase layer interface implementation
// ---------------------------------------------------------------------------
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        for (JPH::uint l = 0; l < Layers::NUM_LAYERS; ++l)
        {
            // Even (static variant) -> NON_MOVING, odd (moving variant) -> MOVING.
            m_objectToBroadPhase[l] = (l % 2 == 0) ? BPLayers::NON_MOVING
                                                   : BPLayers::MOVING;
        }
    }

    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return BPLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        JPH_ASSERT(layer < Layers::NUM_LAYERS);
        return m_objectToBroadPhase[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        switch ((JPH::BroadPhaseLayer::Type)layer)
        {
            case (JPH::BroadPhaseLayer::Type)BPLayers::NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BPLayers::MOVING:     return "MOVING";
            default: JPH_ASSERT(false); return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer m_objectToBroadPhase[Layers::NUM_LAYERS];
};

// ---------------------------------------------------------------------------
// Object vs broad-phase layer filter
// ---------------------------------------------------------------------------
class ObjVsBPLayerFilter : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer1,
                       JPH::BroadPhaseLayer layer2) const override
    {
        // Static variant only collides with moving bodies (broad-phase opt);
        // moving variant collides with everything.
        if (layer1 % 2 == 0)
            return layer2 == BPLayers::MOVING;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Object layer pair filter
// ---------------------------------------------------------------------------
class ObjLayerPairFilter : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer1,
                       JPH::ObjectLayer layer2) const override
    {
        // Bodies only interact when they belong to the same user layer.
        return kemena::physicsUserLayerFromObjectLayer(layer1) ==
               kemena::physicsUserLayerFromObjectLayer(layer2);
    }
};

// ---------------------------------------------------------------------------
// Contact collector
//
// Jolt calls ContactListener callbacks from its worker threads while all
// bodies are locked, so we only record the (sorted) body pair and the phase
// into a mutex-protected queue. kPhysicsManager::update() drains that queue on
// the main thread after the step and classifies each pair as a trigger
// (sensor) or collision event.
// ---------------------------------------------------------------------------
struct RawContact
{
    enum class Phase { Enter, Stay, Exit };

    Phase    phase = Phase::Enter;
    uint32_t bodyA = 0;
    uint32_t bodyB = 0;
};

class kContactCollector final : public JPH::ContactListener
{
public:
    std::mutex              *mutex = nullptr;
    std::vector<RawContact> *queue = nullptr;

    void queuePhase(RawContact::Phase phase, const JPH::BodyID &a, const JPH::BodyID &b)
    {
        std::lock_guard<std::mutex> lock(*mutex);
        RawContact c;
        c.phase = phase;
        c.bodyA = a.GetIndexAndSequenceNumber();
        c.bodyB = b.GetIndexAndSequenceNumber();
        queue->push_back(c);
    }

    void OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2,
                        const JPH::ContactManifold &,
                        JPH::ContactSettings &) override
    {
        queuePhase(RawContact::Phase::Enter, inBody1.GetID(), inBody2.GetID());
    }

    void OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2,
                            const JPH::ContactManifold &,
                            JPH::ContactSettings &) override
    {
        queuePhase(RawContact::Phase::Stay, inBody1.GetID(), inBody2.GetID());
    }

    void OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair) override
    {
        queuePhase(RawContact::Phase::Exit,
                   inSubShapePair.GetBody1ID(), inSubShapePair.GetBody2ID());
    }
};

// ---------------------------------------------------------------------------
// kPhysicsManager::Impl
// ---------------------------------------------------------------------------
namespace kemena
{
    struct kPhysicsManager::Impl
    {
        std::unique_ptr<JPH::TempAllocatorImpl>    tempAllocator;
        std::unique_ptr<JPH::JobSystemThreadPool>  jobSystem;
        std::unique_ptr<BPLayerInterfaceImpl>      bpLayerInterface;
        std::unique_ptr<ObjVsBPLayerFilter>        ovbpFilter;
        std::unique_ptr<ObjLayerPairFilter>        olpFilter;
        std::unique_ptr<JPH::PhysicsSystem>        physicsSystem;

        std::vector<kPhysicsObject *>              objects;
        std::vector<kCharacterController *>        characters;
        std::vector<std::string>                   layerNames{ "Default" };
        bool                                       initialized = false;

        // Contact-event capture (Jolt worker threads -> main-thread drain).
        std::unique_ptr<kContactCollector>         contactListener;
        std::mutex                                 contactMutex;
        std::vector<RawContact>                    contactQueue;
        std::vector<kPhysicsContactEvent>          contactEvents;

        static constexpr JPH::uint cMaxBodies             = 65536;
        static constexpr JPH::uint cNumBodyMutexes        = 0;
        static constexpr JPH::uint cMaxBodyPairs          = 65536;
        static constexpr JPH::uint cMaxContactConstraints = 10240;
    };

    // -----------------------------------------------------------------------
    // Constructor / destructor
    // -----------------------------------------------------------------------

    kPhysicsManager::kPhysicsManager()
        : m_impl(new Impl())
    {
    }

    kPhysicsManager::~kPhysicsManager()
    {
        shutdown();
        delete m_impl;
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    bool kPhysicsManager::init()
    {
        if (m_impl->initialized)
            return true;

        // Global Jolt initialisation (safe to call multiple times)
        JPH::RegisterDefaultAllocator();

        if (!JPH::Factory::sInstance)
        {
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }

        // 10 MB scratch allocator for the physics engine
        m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

        // Thread-pool job system (leave 1 core for the game thread)
        const int workerCount = std::max(1,
            static_cast<int>(std::thread::hardware_concurrency()) - 1);
        m_impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workerCount);

        // Collision layer infrastructure
        m_impl->bpLayerInterface = std::make_unique<BPLayerInterfaceImpl>();
        m_impl->ovbpFilter       = std::make_unique<ObjVsBPLayerFilter>();
        m_impl->olpFilter        = std::make_unique<ObjLayerPairFilter>();

        // Physics world
        m_impl->physicsSystem = std::make_unique<JPH::PhysicsSystem>();
        m_impl->physicsSystem->Init(
            Impl::cMaxBodies,
            Impl::cNumBodyMutexes,
            Impl::cMaxBodyPairs,
            Impl::cMaxContactConstraints,
            *m_impl->bpLayerInterface,
            *m_impl->ovbpFilter,
            *m_impl->olpFilter);

        // Install the contact listener that captures collision / trigger
        // events while the simulation steps (see kContactCollector).
        m_impl->contactListener         = std::make_unique<kContactCollector>();
        m_impl->contactListener->mutex  = &m_impl->contactMutex;
        m_impl->contactListener->queue  = &m_impl->contactQueue;
        m_impl->physicsSystem->SetContactListener(m_impl->contactListener.get());

        // Default gravity: 9.81 m/s² downward
        m_impl->physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

        m_impl->initialized = true;
        return true;
    }

    void kPhysicsManager::shutdown()
    {
        if (!m_impl->initialized)
            return;

        // Destroy all tracked character controllers first (they hold bodies too)
        for (kCharacterController *cc : m_impl->characters)
        {
            cc->uninit();
            delete cc;
        }
        m_impl->characters.clear();

        // Destroy all tracked objects (removes bodies from the simulation)
        for (kPhysicsObject *obj : m_impl->objects)
        {
            obj->uninit();
            delete obj;
        }
        m_impl->objects.clear();

        // Detach the contact listener before the physics system is destroyed.
        if (m_impl->physicsSystem)
            m_impl->physicsSystem->SetContactListener(nullptr);
        m_impl->contactListener.reset();
        {
            std::lock_guard<std::mutex> lock(m_impl->contactMutex);
            m_impl->contactQueue.clear();
        }
        m_impl->contactEvents.clear();

        // Tear down Jolt systems in reverse order
        m_impl->physicsSystem.reset();
        m_impl->jobSystem.reset();
        m_impl->tempAllocator.reset();
        m_impl->olpFilter.reset();
        m_impl->ovbpFilter.reset();
        m_impl->bpLayerInterface.reset();

        // Release type registry (only if this is the last manager)
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        m_impl->initialized = false;
    }

    // -----------------------------------------------------------------------
    // Simulation
    // -----------------------------------------------------------------------

    void kPhysicsManager::update(float deltaTime)
    {
        if (!m_impl->initialized || deltaTime <= 0.0f)
            return;

        // Convert each character's pending per-step move() (issued by scripts
        // since the last step) into the velocity that travels exactly that delta.
        for (kCharacterController *cc : m_impl->characters)
            cc->applyPendingMove(deltaTime);

        // 1 collision step is fine for games running ≥ 30 fps
        const int cCollisionSteps = 1;
        m_impl->physicsSystem->Update(
            deltaTime,
            cCollisionSteps,
            m_impl->tempAllocator.get(),
            m_impl->jobSystem.get());

        // Drain contact events captured on worker threads during the step and
        // classify each pair as a collision or a trigger (sensor) overlap. This
        // runs on the main thread, after bodies are no longer locked.
        {
            std::vector<RawContact> raw;
            {
                std::lock_guard<std::mutex> lock(m_impl->contactMutex);
                raw.swap(m_impl->contactQueue);
            }
            m_impl->contactEvents.clear();
            m_impl->contactEvents.reserve(raw.size());
            const JPH::BodyLockInterface &bli = m_impl->physicsSystem->GetBodyLockInterface();
            for (const RawContact &rc : raw)
            {
                // Bodies may be gone by the time we drain (teardown/Exit), skip them.
                JPH::BodyLockRead lockA(bli, JPH::BodyID(rc.bodyA));
                JPH::BodyLockRead lockB(bli, JPH::BodyID(rc.bodyB));
                if (!lockA.Succeeded() || !lockB.Succeeded())
                    continue;
                const JPH::Body &ba = lockA.GetBody();
                const JPH::Body &bb = lockB.GetBody();
                const bool sensorA = ba.IsSensor();
                const bool sensorB = bb.IsSensor();
                if (sensorA && sensorB)
                    continue; // Jolt never pairs two sensors; ignore defensively.

                const bool isTrigger = sensorA || sensorB;
                if (isTrigger)
                {
                    // A trigger only reports overlaps with movable actors. Ignore
                    // static geometry (e.g. a floor the volume rests on or sinks
                    // into) so OnTriggerEnter doesn't fire spuriously.
                    const JPH::Body &other = sensorA ? bb : ba;
                    if (!other.IsDynamic() && !other.IsKinematic())
                        continue;
                }

                kPhysicsContactEvent ev;
                ev.isTrigger = isTrigger;
                ev.bodyA     = rc.bodyA;
                ev.bodyB     = rc.bodyB;
                ev.action    = rc.phase == RawContact::Phase::Enter
                                   ? kPhysicsContactEvent::Action::Enter
                               : rc.phase == RawContact::Phase::Stay
                                   ? kPhysicsContactEvent::Action::Stay
                                   : kPhysicsContactEvent::Action::Exit;
                m_impl->contactEvents.push_back(ev);
            }
        }

        // Refresh each character's ground/contact state after the world step.
        for (kCharacterController *cc : m_impl->characters)
            cc->update(deltaTime);
    }

    std::vector<kPhysicsContactEvent> kPhysicsManager::takeContactEvents()
    {
        std::vector<kPhysicsContactEvent> out;
        if (m_impl)
            out.swap(m_impl->contactEvents);
        return out;
    }

    // -----------------------------------------------------------------------
    // World settings
    // -----------------------------------------------------------------------

    void kPhysicsManager::setGravity(const kVec3 &gravity)
    {
        if (!m_impl->initialized) return;
        m_impl->physicsSystem->SetGravity(
            JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }

    kVec3 kPhysicsManager::getGravity() const
    {
        if (!m_impl->initialized) return kVec3(0.0f, -9.81f, 0.0f);
        JPH::Vec3 g = m_impl->physicsSystem->GetGravity();
        return kVec3(g.GetX(), g.GetY(), g.GetZ());
    }

    // -----------------------------------------------------------------------
    // User-defined layers
    // -----------------------------------------------------------------------

    void kPhysicsManager::setLayerNames(const std::vector<std::string> &names)
    {
        m_impl->layerNames.clear();
        // Always keep "Default" as the first layer (index 0).
        m_impl->layerNames.push_back("Default");
        for (const std::string &n : names)
        {
            if (n.empty() || n == "Default")
                continue;
            if (static_cast<int>(m_impl->layerNames.size()) >= kMaxPhysicsLayers)
                break;
            m_impl->layerNames.push_back(n);
        }
    }

    std::vector<std::string> kPhysicsManager::getLayerNames() const
    {
        return m_impl->layerNames;
    }

    int kPhysicsManager::getLayerIndex(const std::string &name) const
    {
        for (size_t i = 0; i < m_impl->layerNames.size(); ++i)
            if (m_impl->layerNames[i] == name)
                return static_cast<int>(i);
        return -1;
    }

    std::string kPhysicsManager::getLayerName(int index) const
    {
        if (index < 0) index = 0;
        if (index >= static_cast<int>(m_impl->layerNames.size()))
            index = static_cast<int>(m_impl->layerNames.size()) - 1;
        return m_impl->layerNames[index];
    }

    // -----------------------------------------------------------------------
    // Object factory
    // -----------------------------------------------------------------------

    kPhysicsObject *kPhysicsManager::createObject(const kPhysicsObjectDesc &desc)
    {
        if (!m_impl->initialized)
        {
            std::cout << "[kPhysicsManager] createObject called before init()." << std::endl;
            return nullptr;
        }

        // Resolve the named layer to its index (fall back to Default = 0).
        int userLayer = getLayerIndex(desc.layer);
        if (userLayer < 0) userLayer = 0;

        kPhysicsObject *obj = new kPhysicsObject();
        if (!obj->init(m_impl->physicsSystem.get(), desc, userLayer))
        {
            delete obj;
            return nullptr;
        }

        m_impl->objects.push_back(obj);
        return obj;
    }

    void kPhysicsManager::destroyObject(kPhysicsObject *object)
    {
        if (!object) return;

        auto it = std::find(m_impl->objects.begin(), m_impl->objects.end(), object);
        if (it != m_impl->objects.end())
            m_impl->objects.erase(it);

        object->uninit();
        delete object;
    }

    kCharacterController *kPhysicsManager::createCharacter(const kCharacterControllerDesc &desc)
    {
        if (!m_impl->initialized)
        {
            std::cout << "[kPhysicsManager] createCharacter called before init()." << std::endl;
            return nullptr;
        }

        // Resolve the named layer to its index (fall back to Default = 0).
        int userLayer = getLayerIndex(desc.layer);
        if (userLayer < 0) userLayer = 0;

        kCharacterController *cc = new kCharacterController();
        if (!cc->init(m_impl->physicsSystem.get(), desc, userLayer))
        {
            std::cout << "[kPhysicsManager] createCharacter: cc->init failed." << std::endl;
            delete cc;
            return nullptr;
        }

        m_impl->characters.push_back(cc);
        return cc;
    }

    void kPhysicsManager::destroyCharacter(kCharacterController *character)
    {
        if (!character) return;

        auto it = std::find(m_impl->characters.begin(), m_impl->characters.end(), character);
        if (it != m_impl->characters.end())
            m_impl->characters.erase(it);

        character->uninit();
        delete character;
    }

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    kPhysicsRaycastHit kPhysicsManager::raycast(const kVec3 &origin,
                                                 const kVec3 &direction,
                                                 float maxDistance)
    {
        kPhysicsRaycastHit result;
        if (!m_impl->initialized) return result;

        JPH::RRayCast ray{
            JPH::RVec3(origin.x, origin.y, origin.z),
            JPH::Vec3(direction.x, direction.y, direction.z) * maxDistance
        };

        JPH::RayCastResult hit;
        if (!m_impl->physicsSystem->GetNarrowPhaseQuery().CastRay(ray, hit))
            return result;

        result.hit      = true;
        result.distance = hit.mFraction * maxDistance;
        result.hitPoint = origin + direction * result.distance;

        // Retrieve surface normal via a body read lock.
        {
            JPH::BodyLockRead lock(m_impl->physicsSystem->GetBodyLockInterface(),
                                   hit.mBodyID);
            if (lock.Succeeded())
            {
                const JPH::Body &body = lock.GetBody();
                JPH::Vec3 normal = body.GetWorldSpaceSurfaceNormal(
                    hit.mSubShapeID2,
                    JPH::RVec3(result.hitPoint.x, result.hitPoint.y, result.hitPoint.z));
                result.hitNormal = kVec3(normal.GetX(), normal.GetY(), normal.GetZ());
            }
        }

        // Map the Jolt BodyID back to the kPhysicsObject owned by this manager.
        uint32_t bodyIdVal = hit.mBodyID.GetIndexAndSequenceNumber();
        for (kPhysicsObject *obj : m_impl->objects)
        {
            if (obj->getBodyId() == bodyIdVal)
            {
                result.object = obj;
                break;
            }
        }

        return result;
    }

} // namespace kemena
