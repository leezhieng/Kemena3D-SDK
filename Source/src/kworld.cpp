#include "kworld.h"
#include "kscriptbindings.h"
#include "kphysicsmanager.h"
#include "kmesh.h"
#include "klight.h"
#include "kcamera.h"
#include "kfilesystem.h"
#include "kdecal.h"

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <cstdio>

#include <glm/gtc/quaternion.hpp>

namespace kemena
{
    kWorld::kWorld()
    {
        // Create a default UUID until it gets replaced (if any)
        uuid = generateUuid();

        // The world owns the AngelScript manager so scripts work with or
        // without the editor (e.g. in a standalone exported game).
        scriptManager = new kScriptManager();
        registerScriptBindings(scriptManager);
    }

    kWorld::~kWorld()
    {
        if (scriptManager)
        {
            scriptManager->destroy();
            delete scriptManager;
            scriptManager = nullptr;
        }
    }

    kString kWorld::getUuid()
    {
        return uuid;
    }

    void kWorld::setUuid(kString newUuid)
    {
        uuid = newUuid;
    }

    kScene *kWorld::createScene(kString sceneName, kString sceneUuid)
    {
        kScene *newScene = new kScene();
        newScene->setAssetManager(assetManager);
        newScene->setWorld(this);
        newScene->setName(sceneName);

        addScene(newScene, sceneUuid);

        return newScene;
    }

    void kWorld::addScene(kScene *scene, kString sceneUuid)
    {
        if (sceneUuid.empty())
            scene->setUuid(generateUuid());
        else
            scene->setUuid(sceneUuid);

        scene->setWorld(this);
        scenes.push_back(scene);
    }
	
	kCamera *kWorld::addCamera(kVec3 position, kVec3 lookAt, kCameraType type, kString objectUuid)
    {
        kCamera *camera = new kCamera();
        camera->setCameraType(type);
        camera->setPosition(position);
        camera->setLookAt(lookAt);
        //camera->setParent(rootNode);

        if (objectUuid.empty())
            camera->setUuid(generateUuid());
        else
            camera->setUuid(objectUuid);

        cameras.push_back(camera);

        // Set it as main camera if there is no main camera
        if (mainCamera == nullptr)
            mainCamera = camera;

        return camera;
    }

    void kWorld::addCamera(kCamera *camera, kString objectUuid)
    {
        //camera->setParent(rootNode);

        if (objectUuid.empty())
            camera->setUuid(generateUuid());
        else
            camera->setUuid(objectUuid);

        cameras.push_back(camera);

        // Set it as main camera if there is no main camera
        if (mainCamera == nullptr)
            mainCamera = camera;
    }

    kCamera *kWorld::getMainCamera()
    {
        return mainCamera;
    }

    void kWorld::setMainCamera(kCamera *camera)
    {
        mainCamera = camera;
    }

    void kWorld::setAssetManager(kAssetManager *manager)
    {
        assetManager = manager;
    }

    kAssetManager *kWorld::getAssetManager()
    {
        return assetManager;
    }

    void kWorld::setInputManager(kInputManager *manager)
    {
        inputManager = manager;
        if (scriptManager)
            scriptManager->setInputManager(manager);
    }

    kInputManager *kWorld::getInputManager()
    {
        return inputManager;
    }

    void kWorld::removeCamera(kCamera *camera)
    {
        cameras.erase(std::remove(cameras.begin(), cameras.end(), camera), cameras.end());
        if (mainCamera == camera)
            mainCamera = cameras.empty() ? nullptr : cameras[0];
    }

    void kWorld::removeScene(kScene *scene)
    {
        scenes.erase(std::remove(scenes.begin(), scenes.end(), scene), scenes.end());
    }

    std::vector<kScene *> kWorld::getScenes()
    {
        return scenes;
    }
	
	std::vector<kCamera *> kWorld::getCameras()
    {
        return cameras;
    }

    void kWorld::setPhysicsLayers(const std::vector<std::string> &names)
    {
        physicsLayers.clear();
        physicsLayers.push_back("Default");
        for (const std::string &n : names)
        {
            if (n.empty() || n == "Default")
                continue;
            if (static_cast<int>(physicsLayers.size()) >= kMaxPhysicsLayers)
                break;
            physicsLayers.push_back(n);
        }
    }

    std::vector<std::string> kWorld::getPhysicsLayers() const
    {
        return physicsLayers;
    }

    json kWorld::serialize(int startScene)
    {
        json scenesData = json::array();

        if (scenes.size() > 0)
        {
            // startScene is for skipping scene which you don't want to be serialized (eg. scene used internally by editor)
            for (size_t i = startScene; i < scenes.size(); ++i)
            {
                scenesData.push_back(scenes.at(i)->serialize());
            }
        }

        json layersData = json::array();
        for (const std::string &l : physicsLayers)
            layersData.push_back(l);

        json data =
            {
                {"uuid", getUuid()},
                {"scenes", scenesData},
                {"physics_layers", layersData},
            };

        return data;
    }

    void kWorld::deserialize(json data)
    {
    }

    // -----------------------------------------------------------------------
    // Scripting
    // -----------------------------------------------------------------------

    kScriptManager *kWorld::getScriptManager()
    {
        return scriptManager;
    }

    // Recursively flattens a scene-graph subtree into out.
    static void collectSubtree(kObject *node, std::vector<kObject *> &out)
    {
        if (!node)
            return;
        out.push_back(node);
        for (kObject *child : node->getChildren())
            collectSubtree(child, out);
    }

    // Finds the first animator in an object's subtree. The editor attaches an
    // animator to the skinned (bone) meshes inside the animated object, so the
    // referenced object itself may not be the mesh that holds the kAnimator.
    static kAnimator *findAnimatorInSubtree(kObject *node)
    {
        if (!node)
            return nullptr;
        if (node->getType() == NODE_TYPE_MESH)
        {
            if (kAnimator *a = static_cast<kMesh *>(node)->getAnimator())
                return a;
        }
        for (kObject *child : node->getChildren())
        {
            if (kAnimator *a = findAnimatorInSubtree(child))
                return a;
        }
        return nullptr;
    }

    std::vector<kObject *> kWorld::collectAllObjects()
    {
        std::vector<kObject *> out;
        for (kScene *scene : scenes)
        {
            if (scene && scene->getActive())
                collectSubtree(scene->getRootNode(), out);
        }
        return out;
    }

    std::vector<kObject *> kWorld::getAllObjects()
    {
        return collectAllObjects();
    }

    kString kWorld::resolveScriptAsset(kScript &component)
    {
        // The editor populates scriptUuid; in its absence (engine-only use) the
        // component's own UUID doubles as an ad-hoc script-asset identifier.
        kString sid = component.scriptUuid.empty() ? component.uuid : component.scriptUuid;
        if (sid.empty())
            return "";

        if (!scriptManager->getScriptAsset(sid))
        {
            if (component.fileName.empty())
                return "";
            // No bytecode path known here — the asset compiles from source text.
            scriptManager->registerScriptAsset(sid, component.fileName);
        }
        return sid;
    }

    void kWorld::startScripts()
    {
        if (scriptsRunning || !scriptManager)
            return;

        // Reclaim the global host-API context: a secondary world (e.g. an editor
        // material/mesh preview) may have repointed getSelf() at its own manager.
        setActiveScriptContext(scriptManager);

        std::vector<kObject *> objects = collectAllObjects();

        // UUID -> object map used to resolve handle-type variable bindings.
        std::map<kString, kObject *> byUuid;
        for (kObject *o : objects)
            if (o && !o->getUuid().empty())
                byUuid[o->getUuid()] = o;

        // Pass 1 — build every instance, then dispatch Awake().
        for (kObject *obj : objects)
        {
            for (kScript &comp : obj->getScripts())
            {
                if (!comp.isActive)
                    continue;

                kString sid = resolveScriptAsset(comp);
                if (sid.empty())
                    continue;

                kScriptInstance *inst = scriptManager->createInstance(comp.uuid, sid);
                if (!inst)
                    continue;

                inst->owner = obj;

                // Apply user-assigned script-global values before Awake() so
                // the first frame of gameplay already sees them.
                for (const auto &binding : comp.variableBindings)
                {
                    if (!binding.assigned)
                        continue;

                    kScriptManager::kScriptGlobalValue gv;
                    gv.name     = binding.name;
                    gv.typeName = binding.typeName;
                    gv.i        = (int)binding.valueFloat[0];
                    gv.f        = binding.valueFloat[0];
                    gv.b        = binding.valueBool;
                    gv.vec[0]   = binding.valueFloat[0];
                    gv.vec[1]   = binding.valueFloat[1];
                    gv.vec[2]   = binding.valueFloat[2];

                    if (binding.typeName == "kObject@")
                    {
                        auto it = byUuid.find(binding.valueStr);
                        gv.handle = (it != byUuid.end()) ? (void *)it->second : nullptr;
                    }
                    else if (binding.typeName == "kAnimator@")
                    {
                        auto it = byUuid.find(binding.valueStr);
                        if (it != byUuid.end())
                            gv.handle = (void *)findAnimatorInSubtree(it->second);
                    }
                    else if (binding.typeName == "kAudioSource@")
                    {
                        auto it = byUuid.find(binding.valueStr);
                        if (it != byUuid.end() && !it->second->getAudioSources().empty())
                            gv.handle = (void *)&it->second->getAudioSources()[0];
                    }
                    // kMaterial@ is asset-side; left null unless resolved elsewhere.

                    scriptManager->applyGlobalVariable(inst, gv);
                }

                scriptManager->setActiveObject(obj);
                scriptManager->callEvent(inst, K_SCRIPT_AWAKE);
                inst->awakeCalled = true;
            }
        }

        // Pass 2 — dispatch Start() once every Awake() has run (Unity order).
        for (kObject *obj : objects)
        {
            for (kScript &comp : obj->getScripts())
            {
                kScriptInstance *inst = scriptManager->getInstance(comp.uuid);
                if (!inst || !inst->valid)
                    continue;

                scriptManager->setActiveObject(obj);
                scriptManager->callEvent(inst, K_SCRIPT_START);
                inst->startCalled = true;
            }
        }

        scriptsRunning = true;
    }

    void kWorld::stopScripts()
    {
        if (!scriptsRunning || !scriptManager)
            return;

        // Reclaim the host-API context so OnDestroy()'s getSelf() resolves here.
        setActiveScriptContext(scriptManager);

        for (kObject *obj : collectAllObjects())
        {
            for (kScript &comp : obj->getScripts())
            {
                kScriptInstance *inst = scriptManager->getInstance(comp.uuid);
                if (!inst)
                    continue;
                scriptManager->setActiveObject(obj);
                scriptManager->callEvent(inst, K_SCRIPT_ON_DESTROY);
            }
        }

        scriptManager->destroyAllInstances();
        scriptManager->setActiveObject(nullptr);
        scriptsRunning = false;
    }

    void kWorld::updateScripts(float deltaTime)
    {
        if (!scriptsRunning || !scriptManager)
            return;

        // Reclaim the host-API context in case another world (preview/thumbnail)
        // hijacked it since the last frame — otherwise getSelf() returns null.
        setActiveScriptContext(scriptManager);

        scriptManager->setDeltaTime(deltaTime);
        std::vector<kObject *> objects = collectAllObjects();

        // Update()
        for (kObject *obj : objects)
        {
            if (!obj->getActive())
                continue;
            for (kScript &comp : obj->getScripts())
            {
                if (!comp.isActive)
                    continue;
                kScriptInstance *inst = scriptManager->getInstance(comp.uuid);
                if (!inst || !inst->valid)
                    continue;
                scriptManager->setActiveObject(obj);
                scriptManager->callEvent(inst, K_SCRIPT_UPDATE);
            }
        }

        // LateUpdate() — separate pass so it runs after every Update().
        for (kObject *obj : objects)
        {
            if (!obj->getActive())
                continue;
            for (kScript &comp : obj->getScripts())
            {
                if (!comp.isActive)
                    continue;
                kScriptInstance *inst = scriptManager->getInstance(comp.uuid);
                if (!inst || !inst->valid)
                    continue;
                scriptManager->setActiveObject(obj);
                scriptManager->callEvent(inst, K_SCRIPT_LATE_UPDATE);
            }
        }
    }

    void kWorld::fixedUpdateScripts(float fixedDeltaTime)
    {
        if (!scriptsRunning || !scriptManager)
            return;

        // Reclaim the host-API context (see updateScripts).
        setActiveScriptContext(scriptManager);

        scriptManager->setFixedDeltaTime(fixedDeltaTime);

        for (kObject *obj : collectAllObjects())
        {
            if (!obj->getActive())
                continue;
            for (kScript &comp : obj->getScripts())
            {
                if (!comp.isActive)
                    continue;
                kScriptInstance *inst = scriptManager->getInstance(comp.uuid);
                if (!inst || !inst->valid)
                    continue;
                scriptManager->setActiveObject(obj);
                scriptManager->callEvent(inst, K_SCRIPT_FIXED_UPDATE);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Physics lifecycle (standalone runtime)
    // -----------------------------------------------------------------------

    void kWorld::startPhysics()
    {
        if (physicsRunning)
            return;

        if (!physicsManager)
        {
            physicsManager = new kPhysicsManager();
            if (!physicsManager->init())
            {
                printf("kWorld::startPhysics: physics init failed.\n");
                delete physicsManager;
                physicsManager = nullptr;
                return;
            }
            // Restore the world's named physics layers before creating bodies.
            physicsManager->setLayerNames(physicsLayers);
        }

        if (scriptManager)
            scriptManager->setPhysicsManager(physicsManager);

        physicsBodies.clear();
        characterBodies.clear();

        for (kObject *node : collectAllObjects())
        {
            if (node->getHasPhysicsDesc())
            {
                kPhysicsObjectDesc desc = node->getPhysicsDesc();
                desc.position = node->getGlobalPosition();
                desc.rotation = node->getGlobalRotation();

                // Mesh / ConvexHull shapes pull their geometry from the
                // owning kMesh. Not serialised — re-fetched on every Play.
                if (desc.shape.type == kPhysicsShapeType::Mesh ||
                    desc.shape.type == kPhysicsShapeType::ConvexHull)
                {
                    if (node->getType() == NODE_TYPE_MESH)
                    {
                        kMesh *m = static_cast<kMesh *>(node);
                        desc.shape.meshVertices = m->getVertices();
                        if (desc.shape.type == kPhysicsShapeType::Mesh)
                            desc.shape.meshIndices = m->getIndices();
                    }
                    else
                    {
                        printf("kWorld::startPhysics: %s shape on non-mesh node '%s' — skipped.\n",
                               desc.shape.type == kPhysicsShapeType::Mesh ? "Mesh" : "ConvexHull",
                               node->getName().c_str());
                        continue;
                    }
                }

                if (kPhysicsObject *body = physicsManager->createObject(desc))
                {
                    node->attachPhysics(body);
                    physicsBodies.push_back(node);
                }
            }
            if (node->getHasCharacterDesc())
            {
                kCharacterControllerDesc cd = node->getCharacterDesc();
                cd.position = node->getGlobalPosition();
                cd.rotation = node->getGlobalRotation();
                if (kCharacterController *cc = physicsManager->createCharacter(cd))
                {
                    node->attachCharacter(cc);
                    characterBodies.push_back(node);
                }
                else
                {
                    printf("kWorld::startPhysics: createCharacter failed for '%s'\n", node->getName().c_str());
                }
            }
        }

        physicsRunning = true;
    }

    void kWorld::updatePhysics(float deltaTime)
    {
        if (!physicsRunning || !physicsManager || deltaTime <= 0.0f)
            return;

        physicsManager->update(deltaTime);

        for (kObject *obj : physicsBodies)
            if (obj->getPhysicsObject())
                obj->syncFromPhysics();
        for (kObject *obj : characterBodies)
            if (obj->getCharacterController())
                obj->syncFromCharacter();

        // Fire OnCollision*/OnTrigger* events captured during the step.
        dispatchPhysicsContactEvents(physicsManager, physicsBodies, characterBodies);
    }

    // -----------------------------------------------------------------------
    // Physics contact -> script event dispatch
    // -----------------------------------------------------------------------

    void kWorld::dispatchPhysicsContactEvents(kPhysicsManager *pm,
                                              const std::vector<kObject *> &bodyNodes,
                                              const std::vector<kObject *> &charNodes)
    {
        if (!pm || !scriptManager || !scriptsRunning)
            return;

        std::vector<kPhysicsContactEvent> events = pm->takeContactEvents();
        if (events.empty())
            return;

        // Map live Jolt body ids back to the scene nodes that own them.
        std::map<uint32_t, kObject *> owner;
        for (kObject *n : bodyNodes)
            if (n && n->getPhysicsObject())
                owner[n->getPhysicsObject()->getBodyId()] = n;
        for (kObject *n : charNodes)
            if (n && n->getCharacterController())
                owner[n->getCharacterController()->getBodyId()] = n;
        if (owner.empty())
            return;

        // Fires a collision/trigger event on @p obj, passing @p other (the
        // counterpart in the contact/overlap) as the function's kObject@ other.
        auto dispatchTo = [&](kObject *obj, kScriptEvent evt, kObject *other)
        {
            if (!obj || !obj->getActive())
                return;
            for (kScript &comp : obj->getScripts())
            {
                if (!comp.isActive)
                    continue;
                kScriptInstance *inst = scriptManager->getInstance(comp.uuid);
                if (!inst || !inst->valid)
                    continue;
                scriptManager->setActiveObject(obj);
                scriptManager->callEventWithObject(inst, evt, other);
            }
        };

        for (const kPhysicsContactEvent &ev : events)
        {
            auto itA = owner.find(ev.bodyA);
            auto itB = owner.find(ev.bodyB);
            kObject *objA = (itA != owner.end()) ? itA->second : nullptr;
            kObject *objB = (itB != owner.end()) ? itB->second : nullptr;
            if (!objA && !objB)
                continue;

            kScriptEvent evt;
            if (ev.isTrigger)
            {
                evt = ev.action == kPhysicsContactEvent::Action::Enter ? K_SCRIPT_ON_TRIGGER_ENTER
                    : ev.action == kPhysicsContactEvent::Action::Stay  ? K_SCRIPT_ON_TRIGGER_STAY
                    : K_SCRIPT_ON_TRIGGER_EXIT;
            }
            else
            {
                evt = ev.action == kPhysicsContactEvent::Action::Enter ? K_SCRIPT_ON_COLLISION_ENTER
                    : ev.action == kPhysicsContactEvent::Action::Stay  ? K_SCRIPT_ON_COLLISION_STAY
                    : K_SCRIPT_ON_COLLISION_EXIT;
            }

            // Each participant sees the other participant as its "other" object.
            dispatchTo(objA, evt, objB);
            if (objB != objA)
                dispatchTo(objB, evt, objA);
        }
    }

    void kWorld::stopPhysics()
    {
        if (!physicsManager)
        {
            physicsRunning = false;
            return;
        }

        for (kObject *obj : physicsBodies)
            obj->detachPhysics();
        for (kObject *obj : characterBodies)
            obj->detachCharacter();
        physicsBodies.clear();
        characterBodies.clear();

        // shutdown() destroys all bodies/characters the manager created.
        physicsManager->shutdown();
        delete physicsManager;
        physicsManager = nullptr;
        physicsRunning = false;

        if (scriptManager)
            scriptManager->setPhysicsManager(nullptr);
    }

    // -----------------------------------------------------------------------
    // Particle system lifecycle (standalone runtime)
    // -----------------------------------------------------------------------

    kParticleManager *kWorld::getParticleManager()
    {
        if (!particleManager)
            particleManager = new kParticleManager();
        return particleManager;
    }

    void kWorld::startParticles()
    {
        if (particlesRunning)
            return;

        kParticleManager *pm = getParticleManager();
        if (!pm->isInitialized())
        {
            if (!pm->init())
            {
                printf("kWorld::startParticles: particle manager init failed.\n");
                return;
            }
        }

        // Register every particle descriptor from every object in every scene.
        for (kObject *obj : collectAllObjects())
        {
            for (kParticle &pdesc : obj->getParticles())
            {
                if (!pdesc.isActive)
                    continue;
                pm->addEmitter(pdesc, obj->getGlobalPosition());
            }
        }

        particlesRunning = true;
    }

    void kWorld::updateParticles(float deltaTime)
    {
        if (!particlesRunning || !particleManager)
            return;

        // Update emitter positions from their owning objects before stepping.
        for (kObject *obj : collectAllObjects())
        {
            for (kParticle &pdesc : obj->getParticles())
            {
                particleManager->setEmitterPosition(pdesc.uuid, obj->getGlobalPosition());
            }
        }

        particleManager->update(deltaTime);
    }

    void kWorld::stopParticles()
    {
        if (!particleManager)
        {
            particlesRunning = false;
            return;
        }

        particleManager->clear();
        particlesRunning = false;
    }

    // -----------------------------------------------------------------------
    // Standalone project loading (built game / runtime)
    // -----------------------------------------------------------------------

    namespace
    {
        kVec3 jvec3xyz(const json &j, const char *key, kVec3 def)
        {
            if (!j.contains(key)) return def;
            const auto &v = j[key];
            return kVec3(v.value("x", def.x), v.value("y", def.y), v.value("z", def.z));
        }
        kVec3 jvec3rgb(const json &j, const char *key, kVec3 def)
        {
            if (!j.contains(key)) return def;
            const auto &v = j[key];
            return kVec3(v.value("r", def.x), v.value("g", def.y), v.value("b", def.z));
        }
    }

    static kObject *loadRuntimeObject(const json &obj, kScene *scene, kWorld *world,
                                      kAssetManager *am, const std::filesystem::path &dataRoot,
                                      kObject *parent)
    {
        namespace fs = std::filesystem;
        std::string type = obj.value("type", "object");
        std::string uuid = obj.value("uuid", "");
        std::string name = obj.value("name", "");
        bool active      = obj.value("active", true);
        bool topLevel    = (parent == nullptr);

        kVec3 pos   = jvec3xyz(obj, "position", kVec3(0.0f));
        kVec3 rotEu = jvec3xyz(obj, "rotation", kVec3(0.0f));
        kVec3 scl   = jvec3xyz(obj, "scale",    kVec3(1.0f));

        kObject *result = nullptr;

        if (type == "mesh")
        {
            std::string refName  = obj.value("reference", "");
            std::string fileName = obj.value("file_name", "");
            fs::path meshPath;
            if (!refName.empty())
                meshPath = dataRoot / "Library" / "ImportedAssets" / (refName + ".glb");
            else if (!fileName.empty())
                meshPath = fs::path(fileName);

            kMesh *mesh = nullptr;
            std::error_code ec;
            if (!meshPath.empty() && fs::exists(meshPath, ec))
                mesh = am->loadMesh(meshPath.string());
            if (!mesh)
                mesh = new kMesh();

            mesh->setName(name);
            mesh->setActive(active);
            mesh->setStatic(obj.value("static", false));
            mesh->setCastShadow(obj.value("cast_shadow", true));
            mesh->setReceiveShadow(obj.value("receive_shadow", true));
            if (topLevel) scene->addMesh(mesh, uuid);
            else { mesh->setUuid(uuid.empty() ? generateUuid() : uuid); mesh->setParent(parent); }
            result = mesh;
        }
        else if (type == "light")
        {
            std::string lt = obj.value("light_type", "sun");
            kVec3 diff = jvec3rgb(obj, "diffuse",  kVec3(1.0f));
            kVec3 spec = jvec3rgb(obj, "specular", kVec3(1.0f));
            kVec3 dir  = jvec3xyz(obj, "direction", kVec3(0.0f, -1.0f, 0.0f));

            kLight *light = nullptr;
            if (topLevel)
            {
                if (lt == "point")     light = scene->addPointLight(pos, diff, spec, uuid);
                else if (lt == "spot") light = scene->addSpotLight(pos, diff, spec, uuid);
                else                   light = scene->addSunLight(pos, dir, diff, spec, uuid);
            }
            else
            {
                light = new kLight();
                if (lt == "point")     light->setLightType(kLightType::LIGHT_TYPE_POINT);
                else if (lt == "spot") light->setLightType(kLightType::LIGHT_TYPE_SPOT);
                else                   light->setLightType(kLightType::LIGHT_TYPE_SUN);
                light->setDiffuseColor(diff);
                light->setSpecularColor(spec);
                light->setUuid(uuid.empty() ? generateUuid() : uuid);
                light->setParent(parent);
            }
            light->setName(name);
            light->setActive(active);
            light->setPower(obj.value("power", 1.0f));
            light->setConstant(obj.value("constant", 1.0f));
            light->setLinear(obj.value("linear", 0.7f));
            light->setQuadratic(obj.value("quadratic", 1.8f));
            light->setDirection(dir);
            light->setCutOff(obj.value("cut_off", glm::cos(glm::radians(15.0f))));
            light->setOuterCutOff(obj.value("outer_cut_off", glm::cos(glm::radians(20.0f))));
            result = light;
        }
        else if (type == "camera")
        {
            std::string camTypeStr = obj.value("camera_type", "free");
            kCameraType camType = (camTypeStr == "locked") ? kCameraType::CAMERA_TYPE_LOCKED
                                                           : kCameraType::CAMERA_TYPE_FREE;
            kCamera *cam = new kCamera();
            cam->setName(name);
            cam->setActive(active);
            cam->setCameraType(camType);
            cam->setLookAt(jvec3xyz(obj, "look_at", kVec3(0.0f)));
            cam->setFOV(obj.value("fov", 60.0f));
            cam->setNearClip(obj.value("near_clip", 0.1f));
            cam->setFarClip(obj.value("far_clip", 1000.0f));
            if (topLevel) scene->addObject(cam, uuid);
            else { cam->setUuid(uuid.empty() ? generateUuid() : uuid); cam->setParent(parent); }
            world->addCamera(cam, cam->getUuid());
            result = cam;
        }
        else if (type == "audio")
        {
            kObject *audioObj = new kObject();
            audioObj->setType(NODE_TYPE_AUDIO);
            audioObj->setName(name);
            audioObj->setActive(active);
            if (topLevel) scene->addObject(audioObj, uuid);
            else { audioObj->setUuid(uuid.empty() ? generateUuid() : uuid); audioObj->setParent(parent); }
            result = audioObj;
        }
        else if (type == "decal")
        {
            kDecal *decal = new kDecal();
            decal->setName(name);
            decal->setActive(active);
            decal->setStatic(obj.value("static", false));
            decal->setShaderType(obj.value("decal_shader", std::string("flat")));
            decal->setSurfaceOffset(obj.value("decal_offset", 0.01f));
            if (topLevel) scene->addObject(decal, uuid);
            else { decal->setUuid(uuid.empty() ? generateUuid() : uuid); decal->setParent(parent); }
            result = decal;
        }
        else
        {
            kObject *empty = new kObject();
            empty->setName(name);
            empty->setActive(active);
            if (topLevel) scene->addObject(empty, uuid);
            else { empty->setUuid(uuid.empty() ? generateUuid() : uuid); empty->setParent(parent); }
            result = empty;
        }

        if (!result) return nullptr;

        result->setPosition(pos);
        result->setRotation(kQuat(glm::radians(rotEu)));
        result->setScale(scl);
        result->setTag(obj.value("tag", std::string("")));

        // Physics descriptor
        if (obj.contains("physics") && obj["physics"].is_object())
        {
            const json &phys = obj["physics"];
            kPhysicsObjectDesc &d = result->getPhysicsDesc();
            d.shape.type = (kPhysicsShapeType)phys.value("shape_type", 0);
            if (phys.contains("half_extents"))
            {
                const auto &he = phys["half_extents"];
                d.shape.halfExtents = kVec3(he.value("x", 0.5f), he.value("y", 0.5f), he.value("z", 0.5f));
            }
            d.shape.radius   = phys.value("radius", 0.5f);
            d.shape.height   = phys.value("height", 1.0f);
            d.type           = (kPhysicsObjectType)phys.value("body_type", 0);
            d.mass           = phys.value("mass", 1.0f);
            d.friction       = phys.value("friction", 0.5f);
            d.restitution    = phys.value("restitution", 0.0f);
            d.linearDamping  = phys.value("linear_damping", 0.05f);
            d.angularDamping = phys.value("angular_damping", 0.05f);
            d.gravityFactor  = phys.value("gravity_factor", 1.0f);
            d.layer          = phys.value("layer", "Default");
            result->setHasPhysicsDesc(true);
        }

        // Character controller descriptor
        if (obj.contains("character") && obj["character"].is_object())
        {
            const json &ch = obj["character"];
            kCharacterControllerDesc &cd = result->getCharacterDesc();
            cd.radius        = ch.value("radius", 0.3f);
            cd.height        = ch.value("height", 1.8f);
            cd.mass          = ch.value("mass", 80.0f);
            cd.friction      = ch.value("friction", 0.5f);
            cd.gravityFactor = ch.value("gravity_factor", 1.0f);
            cd.slopeLimit    = ch.value("slope_limit", 45.0f);
            cd.stepHeight    = ch.value("step_height", 0.3f);
            cd.layer         = ch.value("layer", "Default");
            result->setHasCharacterDesc(true);
        }

        // Navigation surface descriptor
        if (obj.contains("navmesh_surface") && obj["navmesh_surface"].is_object())
        {
            const json &nm = obj["navmesh_surface"];
            kNavMeshDesc &nd = result->getNavMeshDesc();
            nd.useArea = nm.value("use_area", false);
            if (nm.contains("area_size"))
            {
                const auto &as = nm["area_size"];
                nd.areaSize = kVec3(as.value("x", 20.0f), as.value("y", 10.0f), as.value("z", 20.0f));
            }
            nd.config.cellSize      = nm.value("cell_size", 0.3f);
            nd.config.cellHeight    = nm.value("cell_height", 0.2f);
            nd.config.agentHeight   = nm.value("agent_height", 2.0f);
            nd.config.agentRadius   = nm.value("agent_radius", 0.6f);
            nd.config.agentMaxClimb = nm.value("agent_max_climb", 0.9f);
            nd.config.agentMaxSlope = nm.value("agent_max_slope", 45.0f);
            nd.config.tileSize      = nm.value("tile_size", 48.0f);
            result->setHasNavMeshDesc(true);
        }

        // Scripts — register the compiled bytecode so startScripts() runs them.
        if (obj.contains("script") && obj["script"].is_array())
        {
            for (const auto &sj : obj["script"])
            {
                kScript s;
                s.uuid       = sj.value("uuid", generateUuid());
                s.scriptUuid = sj.value("script_uuid", std::string(""));
                s.fileName   = sj.value("file_name", std::string(""));
                s.checksum   = sj.value("checksum", std::string(""));
                s.isActive   = sj.value("active", true);

                if (sj.contains("variables") && sj["variables"].is_array())
                {
                    for (const auto &vj : sj["variables"])
                    {
                        kScriptVarBinding vb;
                        vb.name      = vj.value("name", std::string(""));
                        vb.typeName  = vj.value("type", std::string(""));
                        vb.valueStr  = vj.value("value_str", std::string(""));
                        vb.valueBool = vj.value("value_bool", false);
                        vb.assigned  = vj.value("assigned", false);
                        if (vj.contains("value") && vj["value"].is_array() &&
                            vj["value"].size() == 3)
                            for (int i = 0; i < 3; ++i)
                                vb.valueFloat[i] = vj["value"][i].get<float>();
                        s.variableBindings.push_back(vb);
                    }
                }

                kString sid = s.scriptUuid.empty() ? s.uuid : s.scriptUuid;
                if (!sid.empty() && world->getScriptManager())
                {
                    fs::path kbc = dataRoot / "Library" / "Scripts" / (sid + ".kbc");
                    world->getScriptManager()->registerScriptAsset(
                        sid, "", K_SCRIPT_TEXT, kbc.string());
                }
                result->addScript(s);
            }
        }

        // Audio sources
        if (obj.contains("audio_sources") && obj["audio_sources"].is_array())
        {
            for (const auto &aj : obj["audio_sources"])
            {
                kAudioSource as;
                as.uuid         = aj.value("uuid", generateUuid());
                as.name         = aj.value("name", std::string("Audio Source"));
                as.audioFile    = aj.value("audio_file", std::string(""));
                as.isActive     = aj.value("active", true);
                as.playOnAwake  = aj.value("play_on_awake", false);
                as.loop         = aj.value("loop", false);
                as.volume       = aj.value("volume", 1.0f);
                as.pitch        = aj.value("pitch", 1.0f);
                as.spatialize   = aj.value("spatialize", true);
                as.attenuationModel = aj.value("attenuation_model", 2);
                as.minDistance  = aj.value("min_distance", 1.0f);
                as.maxDistance  = aj.value("max_distance", 100.0f);
                result->addAudioSource(as);
            }
        }

        // Audio listeners
        if (obj.contains("audio_listeners") && obj["audio_listeners"].is_array())
        {
            for (const auto &lj : obj["audio_listeners"])
            {
                kAudioListener al;
                al.uuid    = lj.value("uuid", generateUuid());
                al.isActive = lj.value("active", true);
                result->addAudioListener(al);
            }
        }

        // Particle systems
        if (obj.contains("particle") && obj["particle"].is_array())
        {
            for (const auto &pj : obj["particle"])
            {
                kParticle p;
                p.uuid           = pj.value("uuid", generateUuid());
                p.name           = pj.value("name", std::string("Particle System"));
                p.isActive       = pj.value("active", true);
                p.looping        = pj.value("looping", true);
                p.maxParticles   = pj.value("max_particles", 100);
                p.emissionRate   = pj.value("emission_rate", 10.0f);
                p.lifetime       = pj.value("lifetime", 2.0f);
                p.gravityScale   = pj.value("gravity_scale", 1.0f);
                p.startSpeed     = pj.value("start_speed", 1.0f);
                p.sizeStart      = pj.value("size_start", 0.1f);
                p.sizeEnd        = pj.value("size_end", 0.0f);
                p.emissionShape  = (kParticle::EmissionShape)pj.value("emission_shape", 0);
                p.texturePath    = pj.value("texture_path", std::string(""));

                if (pj.contains("start_velocity") && pj["start_velocity"].is_object())
                {
                    const auto &sv = pj["start_velocity"];
                    p.startVelocity = kVec3(sv.value("x", 0.0f), sv.value("y", 1.0f), sv.value("z", 0.0f));
                }
                if (pj.contains("velocity_variance") && pj["velocity_variance"].is_object())
                {
                    const auto &vv = pj["velocity_variance"];
                    p.velocityVariance = kVec3(vv.value("x", 0.1f), vv.value("y", 0.1f), vv.value("z", 0.1f));
                }
                if (pj.contains("color_start") && pj["color_start"].is_object())
                {
                    const auto &cs = pj["color_start"];
                    p.colorStart = kVec4(cs.value("r", 1.0f), cs.value("g", 1.0f), cs.value("b", 1.0f), cs.value("a", 1.0f));
                }
                if (pj.contains("color_end") && pj["color_end"].is_object())
                {
                    const auto &ce = pj["color_end"];
                    p.colorEnd = kVec4(ce.value("r", 1.0f), ce.value("g", 1.0f), ce.value("b", 1.0f), ce.value("a", 0.0f));
                }
                if (pj.contains("shape_size") && pj["shape_size"].is_object())
                {
                    const auto &ss = pj["shape_size"];
                    p.shapeSize = kVec3(ss.value("x", 0.5f), ss.value("y", 0.5f), ss.value("z", 0.5f));
                }

                result->addParticle(p);
            }
        }

        // Children
        if (obj.contains("children") && obj["children"].is_array())
            for (const auto &child : obj["children"])
                loadRuntimeObject(child, scene, world, am, dataRoot, result);

        return result;
    }

    bool kWorld::loadFromFile(const kString &path)
    {
        namespace fs = std::filesystem;

        if (!assetManager)
        {
            printf("kWorld::loadFromFile: no asset manager set.\n");
            return false;
        }

        json data;

        // When running from a package, read the world JSON from the VFS.
        if (kFileSystem::isPackaged())
        {
            kString worldJson = kFileSystem::readFileString("scene.world");
            if (worldJson.empty())
            {
                // Try the path as-is (may be a relative path)
                worldJson = kFileSystem::readFileString(path);
            }
            if (worldJson.empty())
            {
                printf("kWorld::loadFromFile: cannot read '%s' from package.\n", path.c_str());
                return false;
            }
            try { data = json::parse(worldJson); }
            catch (const std::exception &e)
            {
                printf("kWorld::loadFromFile: parse error: %s\n", e.what());
                return false;
            }
        }
        else
        {
            std::ifstream f(path);
            if (!f.is_open())
            {
                printf("kWorld::loadFromFile: cannot open '%s'.\n", path.c_str());
                return false;
            }

            try { data = json::parse(f); }
            catch (const std::exception &e)
            {
                printf("kWorld::loadFromFile: parse error: %s\n", e.what());
                return false;
            }
        }

        fs::path dataRoot = kFileSystem::isPackaged()
            ? fs::path()
            : fs::path(path).parent_path();

        if (!data.contains("scenes") || !data["scenes"].is_array())
            return false;

        // Restore named physics layers (default is just "Default").
        if (data.contains("physics_layers") && data["physics_layers"].is_array())
        {
            std::vector<std::string> layers;
            for (const auto &l : data["physics_layers"])
                if (l.is_string())
                    layers.push_back(l.get<std::string>());
            if (!layers.empty())
                setPhysicsLayers(layers);
        }

        for (const auto &sceneJson : data["scenes"])
        {
            std::string sceneUuid = sceneJson.value("uuid", "");
            std::string sceneName = sceneJson.value("name", "Scene");
            kScene *scene = createScene(sceneName, sceneUuid);
            scene->setActive(sceneJson.value("active", true));

            if (sceneJson.contains("ambient_light"))
            {
                const auto &al = sceneJson["ambient_light"];
                scene->setAmbientLightColor(kVec3(al.value("r", 0.1f), al.value("g", 0.1f), al.value("b", 0.1f)));
            }
            scene->setShadowsEnabled(sceneJson.value("shadows_enabled", true));
            scene->setShadowBias(sceneJson.value("shadow_bias", 0.0008f));
            scene->setShadowNormalBias(sceneJson.value("shadow_normal_bias", 0.003f));
            scene->setShadowMapResolution(sceneJson.value("shadow_map_resolution", 2048));
            scene->setShadowSoftness(sceneJson.value("shadow_softness", 1.5f));
            scene->setSkyboxAmbientEnabled(sceneJson.value("skybox_ambient_enabled", false));
            scene->setSkyboxAmbientStrength(sceneJson.value("skybox_ambient_strength", 1.0f));

            if (sceneJson.contains("objects") && sceneJson["objects"].is_array())
                for (const auto &objJson : sceneJson["objects"])
                    loadRuntimeObject(objJson, scene, this, assetManager, dataRoot, nullptr);
        }

        // Use the first registered camera as the main render camera.
        if (!cameras.empty())
            mainCamera = cameras.front();

        return true;
    }
}
