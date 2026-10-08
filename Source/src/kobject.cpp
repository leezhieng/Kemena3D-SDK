#include "kmesh.h"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace kemena
{
    static unsigned int s_nextObjectId = 1;

    kObject::kObject(kObject *parentNode)
    {
        id = s_nextObjectId++;
        if (parentNode != nullptr)
            setParent(parentNode);
        setType(kNodeType::NODE_TYPE_OBJECT);
    }

    kObject::~kObject()
    {
        kDriver *driver = kDriver::getCurrent();
        if (driver == nullptr) return;

        if (iconVAO)          driver->deleteVertexArray(iconVAO);
        if (iconVertexBuffer) driver->deleteBuffer(iconVertexBuffer);
    }

    kObject *kObject::getParent()
    {
        return parent;
    }

    void kObject::setParent(kObject *newParent)
    {
        parent = newParent;
        parent->children.push_back(this);
    }

    void kObject::detachFromParent()
    {
        if (parent == nullptr) return;
        auto &siblings = parent->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        parent = nullptr;
    }

    void kObject::setParentKeepTransform(kObject *newParent)
    {
        // Same parent — nothing to do, and we'd otherwise double-append to its
        // children list.
        if (newParent == parent) return;

        // Snapshot our current world TRS. calculateModelMatrix() refreshes our
        // worldTransform from local TRS using the (about-to-be-old) parent's
        // current worldTransform, which the renderer keeps up to date.
        calculateModelMatrix();
        kVec3 wPos = getGlobalPosition();
        kQuat wRot = getGlobalRotation();
        kVec3 wScl = getGlobalScale();

        // Swap the parent edge in the scene graph.
        detachFromParent();
        parent = newParent;
        if (newParent != nullptr)
            newParent->children.push_back(this);

        // Resolve the new parent's world TRS (identity when reparenting to
        // root space).
        kVec3 pPos(0.0f);
        kQuat pRot(1.0f, 0.0f, 0.0f, 0.0f);
        kVec3 pScl(1.0f);
        if (newParent != nullptr)
        {
            newParent->calculateModelMatrix();
            pPos = newParent->getGlobalPosition();
            pRot = newParent->getGlobalRotation();
            pScl = newParent->getGlobalScale();
        }

        // Solve for the local TRS that keeps world TRS constant under
        // world = parent_world * T*R*S.  Safe-divide guards against a parent
        // axis with zero scale (which would otherwise propagate NaNs).
        auto safeDiv = [](const kVec3 &a, const kVec3 &b) {
            return kVec3(
                b.x != 0.0f ? a.x / b.x : a.x,
                b.y != 0.0f ? a.y / b.y : a.y,
                b.z != 0.0f ? a.z / b.z : a.z);
        };

        kQuat invPRot = glm::inverse(pRot);
        kVec3 newLocalPos = invPRot * safeDiv(wPos - pPos, pScl);
        kQuat newLocalRot = glm::normalize(invPRot * wRot);
        kVec3 newLocalScl = safeDiv(wScl, pScl);

        setPosition(newLocalPos);
        setRotation(newLocalRot);
        setScale(newLocalScl);

        calculateModelMatrix();
    }

    std::vector<kObject *> kObject::getChildren()
    {
        return children;
    }

    std::vector<kScript>& kObject::getScripts()
    {
        return scripts;
    }

    void kObject::addScript(const kScript& script)
    {
        scripts.push_back(script);
    }

    void kObject::removeScript(const kString& uuid)
    {
        scripts.erase(
            std::remove_if(scripts.begin(), scripts.end(),
                [&uuid](const kScript& s) { return s.uuid == uuid; }),
            scripts.end());
    }

    // --- Particles -----------------------------------------------------------

    std::vector<kParticle>& kObject::getParticles()
    {
        return particles;
    }

    void kObject::addParticle(const kParticle& particle)
    {
        particles.push_back(particle);
    }

    void kObject::removeParticle(const kString& uuid)
    {
        particles.erase(
            std::remove_if(particles.begin(), particles.end(),
                [&uuid](const kParticle& p) { return p.uuid == uuid; }),
            particles.end());
    }

    // --- Audio sources -------------------------------------------------------

    std::vector<kAudioSource>& kObject::getAudioSources()
    {
        return audioSources;
    }

    void kObject::addAudioSource(const kAudioSource& source)
    {
        audioSources.push_back(source);
    }

    void kObject::removeAudioSource(const kString& uuid)
    {
        audioSources.erase(
            std::remove_if(audioSources.begin(), audioSources.end(),
                [&uuid](const kAudioSource& s) { return s.uuid == uuid; }),
            audioSources.end());
    }

    // --- Audio listener ------------------------------------------------------

    std::vector<kAudioListener>& kObject::getAudioListeners()
    {
        return audioListeners;
    }

    void kObject::addAudioListener(const kAudioListener& listener)
    {
        audioListeners.push_back(listener);
    }

    void kObject::removeAudioListener(const kString& uuid)
    {
        audioListeners.erase(
            std::remove_if(audioListeners.begin(), audioListeners.end(),
                [&uuid](const kAudioListener& l) { return l.uuid == uuid; }),
            audioListeners.end());
    }

    // --- Physics descriptor --------------------------------------------------

    bool kObject::getHasPhysicsDesc() const
    {
        return hasPhysicsDesc;
    }

    void kObject::setHasPhysicsDesc(bool val)
    {
        hasPhysicsDesc = val;
    }

    kPhysicsObjectDesc& kObject::getPhysicsDesc()
    {
        return physicsDesc;
    }

    bool kObject::getHasCharacterDesc() const
    {
        return hasCharacterDesc;
    }

    void kObject::setHasCharacterDesc(bool val)
    {
        hasCharacterDesc = val;
    }

    kCharacterControllerDesc& kObject::getCharacterDesc()
    {
        return characterDesc;
    }

    bool kObject::getHasNavMeshDesc() const
    {
        return hasNavMeshDesc;
    }

    void kObject::setHasNavMeshDesc(bool val)
    {
        hasNavMeshDesc = val;
    }

    kNavMeshDesc& kObject::getNavMeshDesc()
    {
        return navMeshDesc;
    }

    kNodeType kObject::getType()
    {
        return type;
    }

    void kObject::setType(kNodeType newType)
    {
        type = newType;
    }

    bool kObject::getActive()
    {
        return isActive;
    }

    void kObject::setActive(bool newActive)
    {
        isActive = newActive;
    }

    bool kObject::getStatic()
    {
        return isStatic;
    }

    void kObject::setStatic(bool newStatic)
    {
        isStatic = newStatic;
    }

    bool kObject::getDebugMode()
    {
        return debugMode;
    }

    void kObject::setDebugMode(bool newMode)
    {
        debugMode = newMode;
    }

    unsigned int kObject::getId()
    {
        return id;
    }

    void kObject::setId(unsigned int newId)
    {
        id = newId;
    }

    kString kObject::getUuid()
    {
        return uuid;
    }

    void kObject::setUuid(kString newUuid)
    {
        uuid = newUuid;
    }

    kString kObject::getName()
    {
        return name;
    }

    void kObject::setName(kString newName)
    {
        name = newName;
    }

    kString kObject::getTag()
    {
        // Legacy single-tag accessor: returns the first assigned tag.
        return tags.empty() ? kString() : tags.front();
    }

    void kObject::setTag(kString newTag)
    {
        // Legacy single-tag setter: replaces the whole list.
        tags.clear();
        if (!newTag.empty())
            tags.push_back(newTag);
    }

    bool kObject::compareTag(const kString &otherTag)
    {
        return hasTag(otherTag);
    }

    std::vector<kString> kObject::getTags() const
    {
        return tags;
    }

    void kObject::setTags(const std::vector<kString> &newTags)
    {
        tags.clear();
        for (const kString &t : newTags)
            if (!t.empty() && !hasTag(t))
                tags.push_back(t);
    }

    void kObject::addTag(const kString &newTag)
    {
        if (newTag.empty() || hasTag(newTag))
            return;
        tags.push_back(newTag);
    }

    void kObject::removeTag(const kString &tagToRemove)
    {
        tags.erase(std::remove(tags.begin(), tags.end(), tagToRemove), tags.end());
    }

    bool kObject::hasTag(const kString &tag) const
    {
        if (tag.empty())
            return false;
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    }

    uint32_t kObject::getLayerMask() const
    {
        return layerMask;
    }

    void kObject::setLayerMask(uint32_t mask)
    {
        layerMask = mask;
    }

    bool kObject::isOnAnyLayer(uint32_t mask) const
    {
        return (layerMask & mask) != 0u;
    }

    kString kObject::getPrefabRef() const
    {
        return prefabRef;
    }

    void kObject::setPrefabRef(const kString &ref)
    {
        prefabRef = ref;
    }

    kString kObject::getTemplateUuid() const
    {
        return templateUuid;
    }

    void kObject::setTemplateUuid(const kString &uuid)
    {
        templateUuid = uuid;
    }

    kVec3 kObject::getPosition()
    {
        return position;
    }

    void kObject::setPosition(kVec3 newPosition)
    {
        if (isStatic)
            return;
        position = newPosition;
    }

    void kObject::setPositionForced(kVec3 newPosition)
    {
        position = newPosition;
    }

    kQuat kObject::getRotation()
    {
        return rotation;
    }

    kVec3 kObject::getRotationEuler()
    {
        kVec3 eulerAngles = glm::eulerAngles(rotation);
        kVec3 eulerAnglesDegrees = glm::degrees(eulerAngles);

        return eulerAnglesDegrees;
    }

    void kObject::setRotation(kQuat newRotation)
    {
        if (isStatic)
            return;
        rotation = glm::normalize(newRotation);
    }

    void kObject::setRotationForced(kQuat newRotation)
    {
        rotation = glm::normalize(newRotation);
    }

    kVec3 kObject::getScale()
    {
        return scale;
    }

    void kObject::setScale(kVec3 newScale)
    {
        if (isStatic)
            return;
        scale = newScale;
    }

    void kObject::setScaleForced(kVec3 newScale)
    {
        scale = newScale;
    }

    kVec3 kObject::calculateRight()
    {
        return glm::normalize(glm::cross(calculateUp(), calculateForward()));
    }

    kVec3 kObject::calculateForward()
    {
        kVec3 worldForwardAxis = kVec3(0.0f, 0.0f, -1.0f); // Default forward direction in OpenGL (negative Z axis)

        // Rotate forward vector by quaternion
        kVec3 front = getRotation() * worldForwardAxis;

        return glm::normalize(front);
    }

    kVec3 kObject::calculateUp()
    {
        kVec3 worldUpAxis = kVec3(0.0f, 1.0f, 0.0f);

        return getRotation() * worldUpAxis;
    }

    void kObject::rotate(kVec3 rotationAxis, float angularSpeed)
    {
        if (isStatic)
            return;

        // Compute the amount of rotation in radians
        float angle = angularSpeed;

        // Create a quaternion representing the small rotation
        kQuat deltaRotation = glm::angleAxis(angle, glm::normalize(rotationAxis));

        // Apply the incremental rotation
        setRotation(deltaRotation * getRotation());
    }

    kVec3 kObject::getGlobalPosition()
    {
        // return globalPosition;

        kVec3 globalPos = kVec3(worldTransform[3]);

        return globalPos;
    }

    kQuat kObject::getGlobalRotation()
    {
        // return glm::normalize(globalRotation);

        // Extract global scale
        kVec3 globalSc = kVec3(
            glm::length(kVec3(worldTransform[0])), // X axis scale
            glm::length(kVec3(worldTransform[1])), // Y axis scale
            glm::length(kVec3(worldTransform[2]))  // Z axis scale
        );

        // Normalize rotation matrix by removing scaling
        kMat3 rotationMat = kMat3(
            kVec3(worldTransform[0]) / globalSc.x,
            kVec3(worldTransform[1]) / globalSc.y,
            kVec3(worldTransform[2]) / globalSc.z);

        // Convert to quaternion
        kQuat globalRot = glm::normalize(glm::quat_cast(rotationMat));

        return globalRot;
    }

    kVec3 kObject::getGlobalScale()
    {
        // return globalScale;

        // Extract global scale
        kVec3 globalSc = kVec3(
            glm::length(kVec3(worldTransform[0])), // X axis scale
            glm::length(kVec3(worldTransform[1])), // Y axis scale
            glm::length(kVec3(worldTransform[2]))  // Z axis scale
        );

        return globalSc;
    }

    void kObject::setMaterial(kMaterial *newMaterial, bool setChildren)
    {
        material = newMaterial;

        if (setChildren)
        {
            if (getChildren().size() > 0)
            {
                for (size_t i = 0; i < getChildren().size(); ++i)
                {
                    getChildren().at(i)->setMaterial(newMaterial);
                }
            }
        }
    }

    kMaterial *kObject::getMaterial()
    {
        return material;
    }

    void kObject::calculateModelMatrix()
    {
        // Local transformations
        kMat4 trans = glm::translate(kMat4(1.0), getPosition());
        kQuat quat = kQuat(getRotation());
        kMat4 rot = glm::toMat4(quat);
        kMat4 scale = glm::scale(kMat4(1.0), getScale());

        localTransform = trans * rot * scale;

        // Parent global transform
        if (parent != nullptr)
        {
            // Combine parent transformations
            worldTransform = parent->getModelMatrixWorld() * localTransform;
        }
        else
        {
            worldTransform = localTransform;
        }
    }

    kMat4 kObject::getModelMatrixWorld()
    {
        return worldTransform;
    }

    kMat4 kObject::getModelMatrixLocal()
    {
        return localTransform;
    }

    void kObject::draw()
    {
        if (material == nullptr) return;
        drawIcon();
    }

    void kObject::drawIcon()
    {
        kDriver *driver = kDriver::getCurrent();
        if (driver == nullptr) return;

        // Lazy-init icon VAO/VBO
        if (iconVAO == 0)
        {
            iconVAO = driver->createVertexArray();
            driver->bindVertexArray(iconVAO);
            iconVertexBuffer = driver->createBuffer();
            driver->uploadVertexBuffer(iconVertexBuffer, iconVertices, sizeof(iconVertices));
            driver->setVertexAttribFloat(0, 3, 0, 0);
            driver->unbindVertexArray();
        }

        driver->drawArrays(iconVAO, kPrimitiveType::TRIANGLE_STRIP, 4);
    }

    // Collect per-sub-mesh overrides on import-child nodes. Those nodes are
    // excluded from the normal children serialization (they're rebuilt from the
    // model file on load), so any property the user changed on a sub-mesh would
    // otherwise be lost. Key each by an index-path among import-child siblings —
    // the model importer recreates sub-meshes in a deterministic order, and their
    // UUIDs are regenerated every load, so the index-path is the only stable
    // handle. Only non-default (overridden) fields are written, so an untouched
    // sub-mesh contributes nothing.
    static void collectSubmeshMaterials(kObject *node, const std::string &path, json &out)
    {
        int idx = 0;
        for (kObject *c : node->getChildren())
        {
            if (!c->getImportChild()) continue;
            std::string p = path.empty() ? std::to_string(idx)
                                          : path + "." + std::to_string(idx);

            json e;
            if (!c->getMaterialUuid().empty()) e["material_uuid"]  = c->getMaterialUuid();
            if (!c->getActive())               e["active"]         = false; // default true
            if (c->getStatic())                e["static"]         = true;  // default false
            if (kMesh *m = dynamic_cast<kMesh *>(c))
            {
                if (!m->getVisible())        e["visible"]        = false; // default true
                if (!m->getCastShadow())     e["cast_shadow"]    = false; // default true
                if (!m->getReceiveShadow())  e["receive_shadow"] = false; // default true
            }
            if (!e.empty())
            {
                e["path"] = p;
                out.push_back(e);
            }

            collectSubmeshMaterials(c, p, out);
            ++idx;
        }
    }

    json kObject::serialize()
    {
        json childrenData = json::array();
        if (getChildren().size() > 0)
        {
            for (size_t i = 0; i < getChildren().size(); ++i)
            {
                kObject *child = getChildren().at(i);
                // Skip engine/import-added children: those with no UUID, and
                // import-derived sub-meshes (which are rebuilt from the model
                // file on load and must not be duplicated in the scene JSON).
                if (!child->getUuid().empty() && !child->getImportChild())
                    childrenData.push_back(child->serialize());
            }
        }

        json scriptsData = json::array();
        if (getScripts().size() > 0)
        {
            for (size_t j = 0; j < getScripts().size(); ++j)
            {
                const kScript &sc = getScripts().at(j);

                json varBindings = json::array();
                for (const auto &vb : sc.variableBindings)
                {
                    varBindings.push_back({
                        {"name",       vb.name},
                        {"type",       vb.typeName},
                        {"value_str",  vb.valueStr},
                        {"value",      {vb.valueFloat[0], vb.valueFloat[1], vb.valueFloat[2]}},
                        {"value_bool", vb.valueBool},
                        {"assigned",   vb.assigned},
                    });
                }

                scriptsData.push_back({
                    {"uuid",        sc.uuid},       // component (attachment) UUID
                    {"script_uuid", sc.scriptUuid}, // referenced script asset UUID
                    {"file_name",   sc.fileName},   // source path (fallback / editor)
                    {"checksum",    sc.checksum},   // source checksum at last compile
                    {"active",      sc.isActive},
                    {"variables",   varBindings},   // user-assigned globals
                });
            }
        }

        // Audio sources
        json audioSourcesData = json::array();
        if (getAudioSources().size() > 0)
        {
            for (size_t j = 0; j < getAudioSources().size(); ++j)
            {
                const kAudioSource &as = getAudioSources().at(j);
                audioSourcesData.push_back({
                    {"uuid",          as.uuid},
                    {"name",          as.name},
                    {"audio_file",    as.audioFile},
                    {"active",        as.isActive},
                    {"play_on_awake", as.playOnAwake},
                    {"loop",          as.loop},
                    {"volume",        as.volume},
                    {"pitch",         as.pitch},
                    {"spatialize",    as.spatialize},
                    {"attenuation_model", as.attenuationModel},
                    {"min_distance",  as.minDistance},
                    {"max_distance",  as.maxDistance},
                });
            }
        }

        // Audio listeners
        json audioListenersData = json::array();
        if (getAudioListeners().size() > 0)
        {
            for (size_t j = 0; j < getAudioListeners().size(); ++j)
            {
                const kAudioListener &al = getAudioListeners().at(j);
                audioListenersData.push_back({
                    {"uuid",   al.uuid},
                    {"active", al.isActive},
                });
            }
        }

        // Particle systems
        json particlesData = json::array();
        if (getParticles().size() > 0)
        {
            for (size_t j = 0; j < getParticles().size(); ++j)
            {
                const kParticle &p = getParticles().at(j);
                json pj;
                pj["uuid"]            = p.uuid;
                pj["name"]            = p.name;
                pj["active"]          = p.isActive;
                pj["looping"]         = p.looping;
                pj["max_particles"]   = p.maxParticles;
                pj["emission_rate"]   = p.emissionRate;
                pj["lifetime"]        = p.lifetime;
                pj["gravity_scale"]   = p.gravityScale;
                pj["start_velocity"]  = {{"x", p.startVelocity.x}, {"y", p.startVelocity.y}, {"z", p.startVelocity.z}};
                pj["start_speed"]     = p.startSpeed;
                pj["velocity_variance"] = {{"x", p.velocityVariance.x}, {"y", p.velocityVariance.y}, {"z", p.velocityVariance.z}};
                pj["color_start"]     = {{"r", p.colorStart.r}, {"g", p.colorStart.g}, {"b", p.colorStart.b}, {"a", p.colorStart.a}};
                pj["color_end"]       = {{"r", p.colorEnd.r},   {"g", p.colorEnd.g},   {"b", p.colorEnd.b},   {"a", p.colorEnd.a}};
                pj["size_start"]      = p.sizeStart;
                pj["size_end"]        = p.sizeEnd;
                pj["emission_shape"]  = (int)p.emissionShape;
                pj["shape_size"]      = {{"x", p.shapeSize.x}, {"y", p.shapeSize.y}, {"z", p.shapeSize.z}};
                pj["texture_path"]    = p.texturePath;
                particlesData.push_back(pj);
            }
        }

        // Emit the correct type string based on node type so loadObjectFromJson
        // can route to the right deserialization branch (audio, etc.).
        std::string typeStr = "object";
        switch (getType())
        {
        case kNodeType::NODE_TYPE_MESH:    typeStr = "mesh";    break;
        case kNodeType::NODE_TYPE_CAMERA:  typeStr = "camera";  break;
        case kNodeType::NODE_TYPE_LIGHT:   typeStr = "light";   break;
        case kNodeType::NODE_TYPE_AUDIO:   typeStr = "audio";   break;
        case kNodeType::NODE_TYPE_TERRAIN: typeStr = "terrain"; break;
        case kNodeType::NODE_TYPE_DECAL:   typeStr = "decal";   break;
        default:                           typeStr = "object";  break;
        }

        json data =
            {
                {"type", typeStr},
                {"uuid", getUuid()},
                {"name", getName()},
                {"tag", getTag()},
                {"tags", tags},
                {"layer_mask", layerMask},
                {"active", getActive()},
                {"static", getStatic()},
                {"position",
                 {{"x", getPosition().x},
                  {"y", getPosition().y},
                  {"z", getPosition().z}}},
                {"rotation",
                 {{"x", getRotationEuler().x},
                  {"y", getRotationEuler().y},
                  {"z", getRotationEuler().z}}},
                {"scale",
                 {{"x", getScale().x},
                  {"y", getScale().y},
                  {"z", getScale().z}}},
                {"children", childrenData},
                {"script", scriptsData},
                {"audio_sources", audioSourcesData},
                {"audio_listeners", audioListenersData},
                {"particle", particlesData},
            };

        // Prefab linkage — only emit when set so unrelated objects stay unchanged.
        if (!prefabRef.empty())    data["prefab_ref"]    = prefabRef;
        if (!templateUuid.empty()) data["template_uuid"] = templateUuid;

        // Assigned material asset — only emit when set. The editor re-applies
        // the material from this UUID on load (see Manager::loadObjectFromJson).
        if (!materialUuid.empty()) data["material_uuid"] = materialUuid;

        // Assigned animator asset — only emit when set.
        if (!animatorRef.empty())  data["animator_ref"] = animatorRef;

        // Per-sub-mesh overrides for import-derived sub-meshes (which are not
        // serialized as children): material plus any changed flag (active,
        // visible, static, shadows). Re-applied on load by index-path. The key
        // stays "submesh_materials" for backward compatibility.
        json submeshMats = json::array();
        collectSubmeshMaterials(this, "", submeshMats);
        if (!submeshMats.empty()) data["submesh_materials"] = submeshMats;

        // Physics body descriptor — only emitted when the object opted in.
        // The fields mirror kPhysicsObjectDesc; readers should populate the
        // descriptor and call setHasPhysicsDesc(true).
        if (hasPhysicsDesc)
        {
            json phys =
            {
                {"shape_type",      (int)physicsDesc.shape.type},
                {"half_extents",
                 {{"x", physicsDesc.shape.halfExtents.x},
                  {"y", physicsDesc.shape.halfExtents.y},
                  {"z", physicsDesc.shape.halfExtents.z}}},
                {"offset",
                 {{"x", physicsDesc.shape.offset.x},
                  {"y", physicsDesc.shape.offset.y},
                  {"z", physicsDesc.shape.offset.z}}},
                {"radius",          physicsDesc.shape.radius},
                {"height",          physicsDesc.shape.height},
                {"body_type",       (int)physicsDesc.type},
                {"mass",            physicsDesc.mass},
                {"friction",        physicsDesc.friction},
                {"restitution",     physicsDesc.restitution},
                {"linear_damping",  physicsDesc.linearDamping},
                {"angular_damping", physicsDesc.angularDamping},
                {"gravity_factor",  physicsDesc.gravityFactor},
                {"layer",           physicsDesc.layer},
            };
            data["physics"] = phys;
        }

        // Character controller descriptor — only emitted when opted in.
        if (hasCharacterDesc)
        {
            data["character"] =
            {
                {"radius",         characterDesc.radius},
                {"height",         characterDesc.height},
                {"mass",           characterDesc.mass},
                {"friction",       characterDesc.friction},
                {"gravity_factor", characterDesc.gravityFactor},
                {"slope_limit",    characterDesc.slopeLimit},
                {"step_height",    characterDesc.stepHeight},
                {"layer",          characterDesc.layer},
            };
        }

        // Navigation surface descriptor — bake settings only; the baked mesh is
        // regenerated on demand and never serialised.
        if (hasNavMeshDesc)
        {
            const kNavBuildConfig &c = navMeshDesc.config;
            data["navmesh_surface"] =
            {
                {"use_area",   navMeshDesc.useArea},
                {"area_size",  {{"x", navMeshDesc.areaSize.x},
                                {"y", navMeshDesc.areaSize.y},
                                {"z", navMeshDesc.areaSize.z}}},
                {"cell_size",        c.cellSize},
                {"cell_height",      c.cellHeight},
                {"agent_height",     c.agentHeight},
                {"agent_radius",     c.agentRadius},
                {"agent_max_climb",  c.agentMaxClimb},
                {"agent_max_slope",  c.agentMaxSlope},
                {"tile_size",        c.tileSize},
            };
        }

        return data;
    }

    void kObject::deserialize(json data)
    {
    }

    // --- Physics -------------------------------------------------------------

    void kObject::attachPhysics(kPhysicsObject *physicsObj)
    {
        physicsObject = physicsObj;
    }

    void kObject::detachPhysics()
    {
        physicsObject = nullptr;
    }

    kPhysicsObject *kObject::getPhysicsObject()
    {
        return physicsObject;
    }

    void kObject::syncFromPhysics()
    {
        if (!physicsObject || isStatic) return;

        position = physicsObject->getPosition();
        rotation = physicsObject->getRotation();
    }

    // --- Character controller -------------------------------------------------

    void kObject::attachCharacter(kCharacterController *character)
    {
        characterController = character;
    }

    void kObject::detachCharacter()
    {
        characterController = nullptr;
    }

    kCharacterController *kObject::getCharacterController()
    {
        return characterController;
    }

    void kObject::syncFromCharacter()
    {
        if (!characterController || isStatic) return;

        position = characterController->getPosition();
        rotation = characterController->getRotation();
    }
}
