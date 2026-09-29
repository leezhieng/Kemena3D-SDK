#include "kscene.h"
#include <algorithm>

namespace kemena
{
    kScene::kScene()
    {
        rootNode = new kObject();
    }

    kScene::~kScene()
    {
    }

    void kScene::setAssetManager(kAssetManager *manager)
    {
        assetManager = manager;
    }

    kAssetManager *kScene::getAssetManager()
    {
        return assetManager;
    }

    void kScene::setWorld(kWorld *newWorld)
    {
        world = newWorld;
    }

    kWorld *kScene::getWorld()
    {
        return world;
    }

    bool kScene::getActive()
    {
        return isActive;
    }

    void kScene::setActive(bool newActive)
    {
        isActive = newActive;
    }

    kString kScene::getUuid()
    {
        return uuid;
    }

    void kScene::setUuid(kString newUuid)
    {
        uuid = newUuid;
    }

    kString kScene::getName()
    {
        return name;
    }

    void kScene::setName(kString newName)
    {
        name = newName;
    }
	
	std::vector<kObject *> kScene::getObjects()
	{
		return objects;
	}
	
	std::vector<kMesh *> kScene::getMeshes()
    {
        return meshes;
    }

    std::vector<kLight *> kScene::getLights()
    {
        return lights;
    }

    kObject *kScene::getRootNode()
    {
        return rootNode;
    }

    void kScene::addObject(kObject *object, kString objectUuid)
    {
        object->setParent(rootNode);

        if (objectUuid.empty())
            object->setUuid(generateUuid());
        else
            object->setUuid(objectUuid);

        if (std::find(objects.begin(), objects.end(), object) == objects.end())
            objects.push_back(object);
    }

    kMesh *kScene::addMesh(kString fileName, kString objectUuid)
    {
        kMesh *mesh = assetManager->loadMesh(fileName);
        mesh->setParent(rootNode);

        if (objectUuid.empty())
            mesh->setUuid(generateUuid());
        else
            mesh->setUuid(objectUuid);

        return mesh;
    }

    void kScene::addMesh(kMesh *mesh, kString objectUuid)
    {
        mesh->setParent(rootNode);

        if (objectUuid.empty())
            mesh->setUuid(generateUuid());
        else
            mesh->setUuid(objectUuid);
    }

    kVec3 kScene::getAmbientLightColor()
    {
        return ambientLightColor;
    }

    void kScene::setAmbientLightColor(kVec3 newColor)
    {
        ambientLightColor = newColor;
    }

    bool kScene::getShadowsEnabled() const
    {
        return shadowsEnabled;
    }

    void kScene::setShadowsEnabled(bool enabled)
    {
        shadowsEnabled = enabled;
    }

    float kScene::getShadowBias() const
    {
        return shadowBias;
    }

    void kScene::setShadowBias(float bias)
    {
        shadowBias = bias;
    }

    float kScene::getShadowNormalBias() const
    {
        return shadowNormalBias;
    }

    void kScene::setShadowNormalBias(float bias)
    {
        shadowNormalBias = bias;
    }

    float kScene::getShadowNormalOffset() const
    {
        return shadowNormalOffset;
    }

    void kScene::setShadowNormalOffset(float offset)
    {
        shadowNormalOffset = offset;
    }

    int kScene::getShadowMapResolution() const
    {
        return shadowMapResolution;
    }

    void kScene::setShadowMapResolution(int resolution)
    {
        shadowMapResolution = resolution;
    }

    float kScene::getShadowSoftness() const
    {
        return shadowSoftness;
    }

    void kScene::setShadowSoftness(float softness)
    {
        shadowSoftness = softness;
    }

    bool kScene::getSkyboxAmbientEnabled()
    {
        return skyboxAmbientEnabled;
    }

    void kScene::setSkyboxAmbientEnabled(bool enabled)
    {
        skyboxAmbientEnabled = enabled;
    }

    float kScene::getSkyboxAmbientStrength()
    {
        return skyboxAmbientStrength;
    }

    void kScene::setSkyboxAmbientStrength(float strength)
    {
        skyboxAmbientStrength = strength;
    }

    kLight *kScene::addSunLight(kVec3 position, kVec3 direction, kVec3 diffuseColor, kVec3 specularColor, kString objectUuid)
    {
        kLight *light = new kLight();
        light->setLightType(kLightType::LIGHT_TYPE_SUN);
        light->setPosition(position);
        light->setDirection(direction);
        light->setDiffuseColor(diffuseColor);
        light->setSpecularColor(specularColor);
        light->setParent(rootNode);

        if (objectUuid.empty())
            light->setUuid(generateUuid());
        else
            light->setUuid(objectUuid);

        lights.push_back(light);

        return light;
    }

    kLight *kScene::addPointLight(kVec3 position, kVec3 diffuseColor, kVec3 specularColor, kString objectUuid)
    {
        kLight *light = new kLight();
        light->setLightType(kLightType::LIGHT_TYPE_POINT);
        light->setPosition(position);
        light->setDiffuseColor(diffuseColor);
        light->setSpecularColor(specularColor);
        light->setParent(rootNode);

        if (objectUuid.empty())
            light->setUuid(generateUuid());
        else
            light->setUuid(objectUuid);

        lights.push_back(light);

        return light;
    }

    kLight *kScene::addSpotLight(kVec3 position, kVec3 diffuseColor, kVec3 specularColor, kString objectUuid)
    {
        kLight *light = new kLight();
        light->setLightType(kLightType::LIGHT_TYPE_SPOT);
        light->setPosition(position);
        light->setDiffuseColor(diffuseColor);
        light->setSpecularColor(specularColor);
        light->setParent(rootNode);

        if (objectUuid.empty())
            light->setUuid(generateUuid());
        else
            light->setUuid(objectUuid);

        lights.push_back(light);

        return light;
    }

    void kScene::removeObject(kObject *object)
    {
        object->detachFromParent();
        objects.erase(std::remove(objects.begin(), objects.end(), object), objects.end());
    }

    void kScene::removeMesh(kMesh *mesh)
    {
        mesh->detachFromParent();
    }

    void kScene::removeLight(kLight *light)
    {
        light->detachFromParent();
        lights.erase(std::remove(lights.begin(), lights.end(), light), lights.end());
    }

    void kScene::addLight(kLight *light)
    {
        light->setParent(rootNode);
        lights.push_back(light);
    }

    void kScene::setSkybox(kMaterial *newMaterial, kMesh *newMesh)
    {
        skyMaterial = newMaterial;
        skyMesh = newMesh;
    }

    kMaterial *kScene::getSkyboxMaterial()
    {
        return skyMaterial;
    }

    kMesh *kScene::getSkyboxMesh()
    {
        return skyMesh;
    }

    json kScene::serialize()
    {
        json objectsData = json::array();

        for (size_t i = 0; i < rootNode->getChildren().size(); ++i)
        {
            objectsData.push_back(rootNode->getChildren().at(i)->serialize());
        }

        json data =
            {
                {"uuid", getUuid()},
                {"name", getName()},
                {"active", getActive()},
                {"ambient_light", {{"r", ambientLightColor.r}, {"g", ambientLightColor.g}, {"b", ambientLightColor.b}}},
                {"shadows_enabled", shadowsEnabled},
                {"shadow_bias", shadowBias},
                {"shadow_normal_bias", shadowNormalBias},
                {"shadow_normal_offset", shadowNormalOffset},
                {"shadow_map_resolution", shadowMapResolution},
                {"shadow_softness", shadowSoftness},
                {"skybox_ambient_enabled", skyboxAmbientEnabled},
                {"skybox_ambient_strength", skyboxAmbientStrength},
                {"objects", objectsData},
            };

        return data;
    }

    void kScene::deserialize(json data)
    {
    }
}
