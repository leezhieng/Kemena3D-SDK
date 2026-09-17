#include "koffscreenrenderer.h"
// Only the abstract kDriver interface and raw GL entry points are used here;
// the concrete desktop driver is never referenced.  Its header is skipped on
// ES platforms, where <GL/glew.h> does not exist.  kgl_internal.h resolves the
// correct GL header set (GLEW on desktop, GLES3/gl3.h on mobile).
#ifndef KEMENA_GLES
#include "kopengldriver.h"
#endif
#include "kgl_internal.h"
#include "kdriver.h"
#include "kmesh.h"
#include "kscene.h"
#include "kworld.h"
#include "klight.h"
#include "kmaterial.h"
#include "ktexture.h"
#include "kshader.h"
#include "kobject.h"
#include "kassetmanager.h"

#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace kemena
{
    // -----------------------------------------------------------------------
    // Minimal inline white shader — fallback when no asset manager is set.
    // Once setAssetManager() is called the real shaders are loaded from
    // embedded resources (SHADER_OFFSCREEN_PREVIEW, SHADER_OFFSCREEN_SHADOW).
    // -----------------------------------------------------------------------

    static const char *kWhiteVS = R"(
        #version 330 core
        layout(location = 0) in vec3 aPosition;
        layout(location = 6) in ivec4 boneIDs;
        layout(location = 7) in vec4 weights;
        uniform mat4 modelMatrix;
        uniform mat4 viewMatrix;
        uniform mat4 projectionMatrix;
        const int MAX_BONES          = 128;
        const int MAX_BONE_INFLUENCE = 4;
        uniform mat4 finalBoneMatrices[MAX_BONES];
        void main() {
            vec4 totalPosition = vec4(aPosition, 1.0);
            float totalWeight = 0.0;
            for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
            {
                int boneID = boneIDs[i];
                float weight = weights[i];
                if (boneID == -1 || weight <= 0.0) continue;
                if (boneID >= MAX_BONES) { totalPosition = vec4(aPosition, 1.0); break; }
                totalPosition += (finalBoneMatrices[boneID] * vec4(aPosition, 1.0)) * weight;
                totalWeight += weight;
            }
            if (totalWeight == 0.0) totalPosition = vec4(aPosition, 1.0);
            gl_Position = projectionMatrix * viewMatrix * modelMatrix * totalPosition;
        }
    )";

    static const char *kWhiteFS = R"(
        #version 330 core
        out vec4 fragColor;
        void main() { fragColor = vec4(1.0, 1.0, 1.0, 1.0); }
    )";

    static const char *kShadowFS = R"(#version 330 core
void main() {}
)";

    // -----------------------------------------------------------------------
    // Construction / destruction
    // -----------------------------------------------------------------------

    kOffscreenRenderer::kOffscreenRenderer(int width, int height)
        : width(width), height(height)
    {
        driver = kDriver::getCurrent();
        createFBO();
    }

    kOffscreenRenderer::~kOffscreenRenderer()
    {
        destroyFBO();
        if (shadowTexArray) { driver->deleteFBOTexture(shadowTexArray); shadowTexArray = 0; }
        if (shadowFbo)      { driver->deleteFramebuffer(shadowFbo);     shadowFbo      = 0; }
        if (shadowShader)   { delete shadowShader;  shadowShader  = nullptr; }
        if (builtinShader)  { delete builtinShader; builtinShader = nullptr; }
    }

    void kOffscreenRenderer::setAssetManager(kAssetManager *am)
    {
        assetManager = am;
    }

    void kOffscreenRenderer::ensureBuiltinShader()
    {
        if (builtinShader) return;

        // Prefer loading from embedded resources via the asset manager;
        // fall back to the inline white shader when none is available.
        if (assetManager)
        {
            builtinShader = assetManager->loadGlslFromResource("SHADER_OFFSCREEN_PREVIEW");
            if (builtinShader) return;
        }

        builtinShader = new kShader();
        builtinShader->loadShadersCode(kWhiteVS, kWhiteFS);
    }

    // -----------------------------------------------------------------------
    // FBO lifecycle
    // -----------------------------------------------------------------------

    void kOffscreenRenderer::createFBO()
    {
        if (!driver) return;

        int rw = width  * ssaaScale;
        int rh = height * ssaaScale;

        fbo      = driver->createFramebuffer();
        colorTex = driver->createFBOColorTexture(rw, rh);
        driver->attachFBOColorTexture(fbo, colorTex);

        depthRbo = driver->createRenderbuffer();
        driver->setupRenderbuffer(depthRbo, rw, rh);
        driver->attachRenderbufferDepthStencil(fbo, depthRbo);

        driver->bindFramebuffer(fbo);
        driver->setFramebufferDrawBuffer();
        driver->unbindFramebuffer();
    }

    void kOffscreenRenderer::destroyFBO()
    {
        if (!driver) return;
        if (colorTex) { driver->deleteFBOTexture(colorTex); colorTex = 0; }
        if (depthRbo) { driver->deleteRenderbuffer(depthRbo); depthRbo = 0; }
        if (fbo)      { driver->deleteFramebuffer(fbo);       fbo      = 0; }
    }

    void kOffscreenRenderer::resize(int newWidth, int newHeight)
    {
        width  = newWidth;
        height = newHeight;
        destroyFBO();
        createFBO();
    }

    // -----------------------------------------------------------------------
    // Shadow pass — lazy alloc + per-frame depth pass into a texture array
    // -----------------------------------------------------------------------

    void kOffscreenRenderer::ensureShadowResources()
    {
        if (shadowTexArray != 0) return;
        shadowTexArray = driver->createFBODepthTextureArray(
            shadowResolution, shadowResolution, kMaxShadowCascades);
        shadowFbo = driver->createFramebuffer();
        driver->attachFBODepthTextureLayer(shadowFbo, shadowTexArray, 0);
        driver->unbindFramebuffer();

        // Prefer loading from embedded resources; fall back to inline.
        if (assetManager)
        {
            shadowShader = assetManager->loadGlslFromResource("SHADER_OFFSCREEN_SHADOW");
            if (shadowShader) return;
        }

        static const char *shadowVS = R"(#version 330 core
layout (location = 0) in vec3 vertexPosition;
layout (location = 6) in ivec4 boneIDs;
layout (location = 7) in vec4 weights;
uniform mat4 lightSpaceMatrix;
uniform mat4 modelMatrix;
const int MAX_BONES          = 128;
const int MAX_BONE_INFLUENCE = 4;
uniform mat4 finalBonesMatrices[MAX_BONES];
void main()
{
    vec4 totalPosition = vec4(0.0);
    float totalWeight = 0.0;
    for(int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int boneID = boneIDs[i];
        float weight = weights[i];
        if(boneID == -1 || weight <= 0.0) continue;
        if(boneID >= MAX_BONES) { totalPosition = vec4(0.0); break; }
        totalPosition += (finalBonesMatrices[boneID] * vec4(vertexPosition, 1.0)) * weight;
        totalWeight += weight;
    }
    if (totalWeight == 0.0) totalPosition = vec4(vertexPosition, 1.0);
    gl_Position = lightSpaceMatrix * (modelMatrix * totalPosition);
})";
        shadowShader = new kShader();
        shadowShader->loadShadersCode(shadowVS, kShadowFS);
    }

    void kOffscreenRenderer::applyShadowResolution(int resolution)
    {
        if (resolution <= 0 || resolution == shadowResolution)
            return;
        shadowResolution = resolution;

        // Rebuild the cascade array at the new size if it was already allocated;
        // otherwise ensureShadowResources() will create it on first use.
        if (shadowTexArray != 0)
        {
            driver->deleteFBOTexture(shadowTexArray);
            shadowTexArray = driver->createFBODepthTextureArray(
                shadowResolution, shadowResolution, kMaxShadowCascades);
            driver->attachFBODepthTextureLayer(shadowFbo, shadowTexArray, 0);
            driver->unbindFramebuffer();
        }
    }

    void kOffscreenRenderer::renderShadowNode(kObject *node, const kMat4 &lightSpace, kShader *shader)
    {
        if (!node || !node->getActive()) return;
        node->calculateModelMatrix();

        if (node->getType() == kNodeType::NODE_TYPE_MESH)
        {
            kMesh *mesh = static_cast<kMesh *>(node);
            if (mesh->getLoaded() && mesh->getVisible() && mesh->getCastShadow())
            {
                shader->setValue("lightSpaceMatrix", lightSpace);
                shader->setValue("modelMatrix",      mesh->getModelMatrixWorld());
                // Bone transforms: identity by default; populated from animator
                // when the mesh is skinned (mirrors kRenderer::renderSceneGraphShadow).
                std::vector<kMat4> bones(128, kMat4(1.0f));
                if (mesh->getSkinned() && mesh->getAnimator() != nullptr)
                    bones = mesh->getAnimator()->getFinalBoneMatrices();
                shader->setValue("finalBonesMatrices", bones);
                mesh->draw();
            }
        }

        for (kObject *child : node->getChildren())
            renderShadowNode(child, lightSpace, shader);
    }

    void kOffscreenRenderer::renderShadowPass(kWorld *world, kScene *scene, kCamera *camera)
    {
        if (!scene || !camera) return;

        // Find the first active sun light — same convention as kRenderer.
        kVec3 lightDir(0.0f, -1.0f, 0.0f);
        bool  hasSun = false;
        for (kLight *lt : scene->getLights())
        {
            if (lt && lt->getActive() && lt->getLightType() == kLightType::LIGHT_TYPE_SUN)
            {
                // Refresh the sun's world transform so a nested sun light
                // inherits its parent chain's rotation.
                lt->calculateModelMatrix();
                lightDir = glm::normalize(lt->getGlobalRotation() * kVec3(0.0f, -1.0f, 0.0f));
                hasSun = true;
                break;
            }
        }
        if (!hasSun) return;

        ensureShadowResources();

        float camNear = camera->getNearClip();
        float camFar  = camera->getFarClip();
        kMat4 camView = camera->getViewMatrix();

        const int   cascCount = std::max(1, std::min(shadowCascadeCount, kMaxShadowCascades));
        const float lambda    = shadowSplitLambda;
        for (int i = 0; i < cascCount; ++i)
        {
            float ratio = (float)(i + 1) / (float)cascCount;
            float cLog  = camNear * std::pow(camFar / camNear, ratio);
            float cUni  = camNear + (camFar - camNear) * ratio;
            cascadeSplits[i] = lambda * cLog + (1.0f - lambda) * cUni;
        }

        kVec3 up = (std::abs(glm::dot(lightDir, kVec3(0, 1, 0))) > 0.99f)
                   ? kVec3(1, 0, 0) : kVec3(0, 1, 0);

        driver->bindFramebuffer(shadowFbo);
        driver->setDepthTest(true);
        driver->setDepthWrite(true);

        shadowShader->use();
        for (int cascade = 0; cascade < cascCount; ++cascade)
        {
            float splitNear = (cascade == 0) ? camNear : cascadeSplits[cascade - 1];
            float splitFar  = cascadeSplits[cascade];

            kMat4 subProj = glm::perspective(
                glm::radians(camera->getFOV()),
                camera->getAspectRatio(),
                splitNear, splitFar);
            kMat4 invPV = glm::inverse(subProj * camView);

            kVec3 corners[8];
            int idx = 0;
            for (int ix = 0; ix < 2; ++ix)
            for (int iy = 0; iy < 2; ++iy)
            for (int iz = 0; iz < 2; ++iz)
            {
                kVec4 pt = invPV * kVec4(ix * 2.0f - 1.0f, iy * 2.0f - 1.0f, iz * 2.0f - 1.0f, 1.0f);
                corners[idx++] = kVec3(pt / pt.w);
            }

            kVec3 center(0.0f);
            for (int c = 0; c < 8; ++c) center += corners[c];
            center /= 8.0f;
            float radius = 0.0f;
            for (int c = 0; c < 8; ++c)
                radius = std::max(radius, glm::length(corners[c] - center));
            radius = std::ceil(radius * 16.0f) / 16.0f;

            float texelsPerUnit = (float)shadowResolution / (2.0f * radius);
            kMat4 snapView    = glm::scale(kMat4(1.0f), kVec3(texelsPerUnit)) *
                                glm::lookAt(kVec3(0.0f), lightDir, up);
            kMat4 snapViewInv = glm::inverse(snapView);
            kVec4 cLS = snapView * kVec4(center, 1.0f);
            cLS.x = std::floor(cLS.x);
            cLS.y = std::floor(cLS.y);
            center = kVec3(snapViewInv * cLS);

            const float zExtent = radius * 6.0f;
            kVec3 eye = center - lightDir * zExtent;
            kMat4 lightView = glm::lookAt(eye, center, up);
            kMat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * zExtent);
            lightSpaceMatrices[cascade] = lightProj * lightView;

            driver->attachFBODepthTextureLayer(shadowFbo, shadowTexArray, cascade);
            driver->setViewport(0, 0, shadowResolution, shadowResolution);
            driver->clear(false, true, false);
            renderShadowNode(scene->getRootNode(), lightSpaceMatrices[cascade], shadowShader);
        }
        shadowShader->unuse();
        driver->unbindFramebuffer();
    }

    // -----------------------------------------------------------------------
    // render — full scene
    // -----------------------------------------------------------------------

    void kOffscreenRenderer::render(kWorld *world, kScene *scene, kCamera *camera)
    {
        if (!driver || !scene || !camera || !fbo) return;

        GLint savedVP[4];
        glGetIntegerv(GL_VIEWPORT, savedVP);

        // Shadow depth pass — must run before binding the colour FBO since it
        // changes the bound framebuffer and viewport.
        if (scene->getShadowsEnabled())
        {
            // Honor the scene's shadow-map resolution so the game view matches
            // the editor view (and reflects inspector edits on the fly). The
            // cascade array is reallocated only when the value actually changes.
            applyShadowResolution(scene->getShadowMapResolution());
            renderShadowPass(world, scene, camera);
        }

        // Lit shaders sample a sampler2DArray shadow map; guarantee a valid array
        // exists to bind even when no shadow pass ran (an unbound array sampler
        // makes some drivers drop the whole draw). Allocate it BEFORE binding the
        // colour FBO so we don't disturb that binding mid-draw.
        ensureShadowResources();

        driver->bindFramebuffer(fbo);
        driver->setViewport(0, 0, width * ssaaScale, height * ssaaScale);
        driver->setClearColor(bgColor.r, bgColor.g, bgColor.b, bgColor.a);
        driver->clear(true, true, false);
        driver->setDepthTest(true);
        driver->setDepthWrite(true);
        driver->setCullFace(true);
        driver->setBlend(false);

        // Skybox — drawn first (depth disabled) so it appears behind all geometry
        kMaterial *skyboxMaterial = scene->getSkyboxMaterial();
        kMesh     *skyboxMesh     = scene->getSkyboxMesh();
        if (skyboxMaterial && skyboxMesh &&
            skyboxMesh->getLoaded() && skyboxMaterial->getShader())
        {
            kShader *skyboxShader = skyboxMaterial->getShader();
            skyboxShader->use();
            driver->setDepthTest(false);
            driver->setDepthWrite(false);
            driver->setCullFace(false);

            // Strip the translation from the view matrix so the skybox never moves
            skyboxShader->setValue("viewMatrix",       kMat4(kMat3(camera->getViewMatrix())));
            skyboxShader->setValue("projectionMatrix", camera->getProjectionMatrix());

            if (!skyboxMaterial->getTextures().empty() &&
                skyboxMaterial->getTexture(0)->getType() == kTextureType::TEX_TYPE_CUBE)
            {
                driver->bindTextureCube(0, skyboxMaterial->getTexture(0)->getTextureID());
                skyboxShader->setValue(skyboxMaterial->getTexture(0)->getTextureName(), 0u);
            }

            skyboxMesh->calculateModelMatrix();
            skyboxMesh->draw();
            skyboxShader->unuse();

            driver->setDepthTest(true);
            driver->setDepthWrite(true);
            driver->setCullFace(true);
        }

        renderNodeFull(scene->getRootNode(), scene, camera);

        driver->unbindFramebuffer();
        glViewport(savedVP[0], savedVP[1], savedVP[2], savedVP[3]);
    }

    void kOffscreenRenderer::renderNodeFull(kObject *node, kScene *scene, kCamera *camera)
    {
        if (!node || !node->getActive()) return;

        node->calculateModelMatrix();

        if (node->getType() == kNodeType::NODE_TYPE_MESH)
        {
            kMesh *mesh = static_cast<kMesh *>(node);

            if (mesh->getLoaded() && mesh->getVisible() &&
                mesh->getMaterial() != nullptr &&
                mesh->getMaterial()->getShader() != nullptr)
            {
                if (mesh->getMaterial()->getTransparent() == kTransparentType::TRANSP_TYPE_BLEND)
                {
                    driver->setBlend(true);
                    driver->setBlendFunc(kBlendFactor::SRC_ALPHA, kBlendFactor::ONE_MINUS_SRC_ALPHA);
                }
                else
                {
                    driver->setBlend(false);
                }

                if (mesh->getMaterial()->getSingleSided())
                {
                    driver->setCullFace(true);
                    driver->setFrontFace(kFrontFace::CCW);
                    driver->setCullMode(mesh->getMaterial()->getCullBack()
                                        ? kCullMode::BACK : kCullMode::FRONT);
                }
                else
                {
                    driver->setCullFace(false);
                }

                int sun = 0, point = 0, spot = 0;
                kShader *shader = mesh->getMaterial()->getShader();
                shader->use();

                setupLightsFromScene(shader, scene, sun, point, spot);
                drawMeshWithMaterial(mesh, scene, camera, sun, point, spot);

                shader->unuse();
            }
        }

        for (kObject *child : node->getChildren())
            renderNodeFull(child, scene, camera);
    }

    // -----------------------------------------------------------------------
    // renderMesh — single mesh, auto-framed
    // -----------------------------------------------------------------------

    void kOffscreenRenderer::renderMesh(kMesh *mesh, kCamera *camera)
    {
        if (!driver || !mesh || !fbo) return;

        // Auto-frame camera using the bounding box of the entire hierarchy
        kCamera autoCamera;
        kCamera *cam = camera;
        if (!cam)
        {
            // Compute combined AABB of root + all children recursively
            kAABB combined;
            std::function<void(kMesh*)> expandAABB = [&](kMesh *m) {
                m->calculateModelMatrix();
                kAABB b = m->getWorldAABB();
                if (b.isValid())
                {
                    combined.expandBy(b.min);
                    combined.expandBy(b.max);
                }
                for (kObject *child : m->getChildren())
                    if (child->getType() == kNodeType::NODE_TYPE_MESH)
                        expandAABB(static_cast<kMesh*>(child));
            };
            expandAABB(mesh);

            kVec3 center = combined.isValid() ? combined.center() : kVec3(0.0f);
            kVec3 he     = combined.isValid() ? combined.halfExtents() : kVec3(1.0f);
            float radius = glm::length(he);
            if (radius < 0.001f) radius = 1.0f;

            float fov  = 45.0f;
            float dist = (radius / glm::tan(glm::radians(fov * 0.5f))) * 1.1f;
            kVec3 dir  = glm::normalize(kVec3(0.5f, 0.5f, 1.0f));
            kVec3 eye  = center + dir * dist;

            // AABB extent along the view direction for tight near/far clips
            float heAlongDir = std::abs(dir.x) * he.x + std::abs(dir.y) * he.y + std::abs(dir.z) * he.z;

            autoCamera.setPosition(eye);
            autoCamera.setLookAt(center);
            autoCamera.setFOV(fov);
            autoCamera.setAspectRatio((float)width / (float)height);
            autoCamera.setNearClip(std::max(0.0001f, dist - heAlongDir - radius * 0.05f));
            autoCamera.setFarClip(dist * 1000.0f);
            cam = &autoCamera;
        }

        GLint savedVP[4];
        glGetIntegerv(GL_VIEWPORT, savedVP);

        driver->bindFramebuffer(fbo);
        driver->setViewport(0, 0, width * ssaaScale, height * ssaaScale);
        driver->setClearColor(bgColor.r, bgColor.g, bgColor.b, bgColor.a);
        driver->clear(true, true, false);
        driver->setDepthTest(true);
        driver->setDepthWrite(true);
        driver->setCullFace(false);
        driver->setBlend(false);

        drawMeshHierarchy(mesh, cam);

        driver->unbindFramebuffer();
        glViewport(savedVP[0], savedVP[1], savedVP[2], savedVP[3]);
    }

    // -----------------------------------------------------------------------
    // renderMeshWithMaterial — single mesh, uses its own material shader
    // -----------------------------------------------------------------------

    void kOffscreenRenderer::renderMeshWithMaterial(kMesh *mesh, kCamera *camera)
    {
        if (!driver || !mesh || !fbo) return;
        if (!mesh->getMaterial() || !mesh->getMaterial()->getShader()) return;
        if (mesh->getMaterial()->getShader()->getShaderProgram() == 0) return;

        // Auto-frame camera (same logic as renderMesh)
        kCamera autoCamera;
        kCamera *cam = camera;
        if (!cam)
        {
            kAABB combined;
            std::function<void(kMesh*)> expandAABB = [&](kMesh *m) {
                m->calculateModelMatrix();
                kAABB b = m->getWorldAABB();
                if (b.isValid())
                {
                    combined.expandBy(b.min);
                    combined.expandBy(b.max);
                }
                for (kObject *child : m->getChildren())
                    if (child->getType() == kNodeType::NODE_TYPE_MESH)
                        expandAABB(static_cast<kMesh*>(child));
            };
            expandAABB(mesh);

            kVec3 center = combined.isValid() ? combined.center()      : kVec3(0.0f);
            kVec3 he     = combined.isValid() ? combined.halfExtents() : kVec3(1.0f);
            float radius = glm::length(he);
            if (radius < 0.001f) radius = 1.0f;

            float fov  = 45.0f;
            float dist = (radius / glm::tan(glm::radians(fov * 0.5f))) * 1.1f;
            kVec3 dir  = glm::normalize(kVec3(0.5f, 0.5f, 1.0f));
            kVec3 eye  = center + dir * dist;

            // AABB extent along the view direction for tight near/far clips
            float heAlongDir = std::abs(dir.x) * he.x + std::abs(dir.y) * he.y + std::abs(dir.z) * he.z;

            autoCamera.setPosition(eye);
            autoCamera.setLookAt(center);
            autoCamera.setFOV(fov);
            autoCamera.setAspectRatio((float)width / (float)height);
            autoCamera.setNearClip(std::max(0.0001f, dist - heAlongDir - radius * 0.05f));
            autoCamera.setFarClip(dist * 1000.0f);
            cam = &autoCamera;
        }

        GLint savedVP[4];
        glGetIntegerv(GL_VIEWPORT, savedVP);

        // Allocate the shadow array before binding the FBO (see render()).
        ensureShadowResources();

        driver->bindFramebuffer(fbo);
        driver->setViewport(0, 0, width * ssaaScale, height * ssaaScale);
        driver->setClearColor(bgColor.r, bgColor.g, bgColor.b, bgColor.a);
        driver->clear(true, true, false);
        driver->setDepthTest(true);
        driver->setDepthWrite(true);
        driver->setCullFace(false);
        driver->setBlend(false);

        mesh->calculateModelMatrix();

        kShader *shader = mesh->getMaterial()->getShader();
        shader->use();

        // Inject a simple sun light (no scene object needed)
        setupSingleSunLight(shader,
                            glm::normalize(kVec3(-0.5f, -1.0f, -0.8f)),
                            kVec3(1.0f, 1.0f, 1.0f), 1.0f);
        shader->setValue("sunLightNum",           1);
        shader->setValue("pointLightNum",         0);
        shader->setValue("spotLightNum",          0);
        shader->setValue("sceneAmbient",          kVec3(0.15f, 0.15f, 0.15f));
        shader->setValue("skyboxAmbientEnabled",  false);
        shader->setValue("skyboxAmbientStrength", 0.0f);

        drawMeshWithMaterial(mesh, nullptr, cam, 1, 0, 0);

        shader->unuse();

        driver->unbindFramebuffer();
        glViewport(savedVP[0], savedVP[1], savedVP[2], savedVP[3]);
    }

    void kOffscreenRenderer::drawMeshHierarchy(kMesh *mesh, kCamera *camera)
    {
        if (!mesh) return;
        mesh->calculateModelMatrix();

        if (mesh->getLoaded())
            drawMeshBuiltin(mesh, camera);

        for (kObject *child : mesh->getChildren())
            if (child->getType() == kNodeType::NODE_TYPE_MESH)
                drawMeshHierarchy(static_cast<kMesh*>(child), camera);
    }

    void kOffscreenRenderer::drawMeshBuiltin(kMesh *mesh, kCamera *camera)
    {
        ensureBuiltinShader();
        builtinShader->use();
        mesh->calculateModelMatrix();
        mesh->calculateNormalMatrix();
        builtinShader->setValue("modelMatrix",      mesh->getModelMatrixWorld());
        builtinShader->setValue("viewMatrix",       camera->getViewMatrix());
        builtinShader->setValue("projectionMatrix", camera->getProjectionMatrix());
        builtinShader->setValue("viewPos",          camera->getPosition());

        // Bone transforms for skinned meshes — mirrors the pattern in
        // drawMeshWithMaterial() so the white/preview shader supports
        // skeletal animation when lighting is toggled off.
        std::vector<kMat4> bones(128, kMat4(1.0f));
        if (mesh->getSkinned() && mesh->getAnimator() != nullptr)
            bones = mesh->getAnimator()->getFinalBoneMatrices();
        builtinShader->setValue("finalBoneMatrices", bones);

        mesh->draw();
        builtinShader->unuse();
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    void kOffscreenRenderer::drawMeshWithMaterial(kMesh *mesh, kScene *scene,
                                                   kCamera *camera,
                                                   int /*sunCount*/, int /*pointCount*/, int /*spotCount*/)
    {
        kShader *shader = mesh->getMaterial()->getShader();

        mesh->calculateNormalMatrix();

        shader->setValue("modelMatrix",      mesh->getModelMatrixWorld());
        shader->setValue("viewMatrix",       camera->getViewMatrix());
        shader->setValue("projectionMatrix", camera->getProjectionMatrix());
        shader->setValue("normalMatrix",     mesh->getNormalMatrix());
        shader->setValue("viewPos",          camera->getPosition());

        shader->setValue("material.tiling",    mesh->getMaterial()->getUvTiling());
        shader->setValue("material.ambient",   mesh->getMaterial()->getAmbientColor());
        shader->setValue("material.diffuse",   mesh->getMaterial()->getDiffuseColor());
        shader->setValue("material.specular",  mesh->getMaterial()->getSpecularColor());
        shader->setValue("material.shininess", mesh->getMaterial()->getShininess());
        shader->setValue("material.metallic",  mesh->getMaterial()->getMetallic());
        shader->setValue("material.roughness", mesh->getMaterial()->getRoughness());

        // Bone transforms: identity by default; populated from animator when
        // the mesh is skinned (mirrors kRenderer::renderSceneGraph pattern).
        std::vector<kMat4> bones(128, kMat4(1.0f));
        if (mesh->getSkinned() && mesh->getAnimator() != nullptr)
            bones = mesh->getAnimator()->getFinalBoneMatrices();
        shader->setValue("finalBonesMatrices", bones);

        // Reset texture-presence flags so a previous draw whose material had
        // (e.g.) an albedoMap doesn't leave has_albedoMap=true on the shader
        // program. With stale flags the lit shader would sample a sampler2D
        // that defaults to unit 0 — where the shadow array's 2D_ARRAY texture
        // is bound — producing garbage that propagates as NaN and dims the
        // whole mesh to ambient-only.
        shader->setValue("has_albedoMap",            false);
        shader->setValue("has_normalMap",            false);
        shader->setValue("has_specularMap",          false);
        shader->setValue("has_glossinessMap",        false);
        shader->setValue("has_emissiveMap",          false);
        shader->setValue("has_metallicRoughnessMap", false);
        shader->setValue("has_aoMap",                false);

        // Texture units: material textures take 0..N-1, shadow array sits at
        // a fixed high unit so sampler2D defaults (which all bind to unit 0)
        // never collide with the sampler2DArray binding. Skybox follows.
        const int shadowUnit = 8;
        const int skyboxUnit = 9;

        // Skybox cubemap for IBL ambient — only when called from the full-scene
        // path. Thumbnail previews pass scene=nullptr.
        kMaterial *skyboxMaterial = scene ? scene->getSkyboxMaterial() : nullptr;
        const bool skyboxBound =
            skyboxMaterial && !skyboxMaterial->getTextures().empty() &&
            skyboxMaterial->getTexture(0)->getType() == kTextureType::TEX_TYPE_CUBE;
        // Always point skyboxMap at its own unit, even with no skybox bound. The
        // lit shaders declare samplerCube skyboxMap, sampler2DArray shadowMapArray
        // and sampler2D maps; if skyboxMap is left unassigned it defaults to unit
        // 0 alongside the sampler2D maps, and sampling one unit as two sampler
        // types is GL_INVALID_OPERATION — which silently drops the whole draw.
        shader->setValue("skyboxMap", skyboxUnit);
        if (skyboxBound)
            driver->bindTextureCube(skyboxUnit, skyboxMaterial->getTexture(0)->getTextureID());

        // Shadow-map uniforms — always set so the offscreen pass never inherits
        // stale values left by an earlier main-renderer draw on the same shader
        // (would otherwise read the editor camera's lightSpaceMatrices and
        // render everything as fully shadowed). When scene is null (thumbnail
        // path), shadows are simply disabled.
        const bool shadowsOn = scene && scene->getShadowsEnabled() && shadowTexArray != 0;
        shader->setValue("enableShadow",     shadowsOn);
        shader->setValue("receiveShadow",    mesh->getReceiveShadow());
        // cascadeCount<=0 is the lit shaders' signal that shadow uniforms aren't
        // set up — keep it 0 unless we actually have a shadow map this draw.
        shader->setValue("cascadeCount",     shadowsOn ? shadowCascadeCount : 0);
        shader->setValue("shadowResolution", (float)shadowResolution);
        shader->setValue("shadowBias",       scene ? scene->getShadowBias()       : 0.0008f);
        shader->setValue("shadowNormalBias", scene ? scene->getShadowNormalBias() : 0.003f);
        shader->setValue("shadowSoftness",   scene ? scene->getShadowSoftness()   : 1.5f);
        shader->setValue("cascadeSplits",
            kVec4(cascadeSplits[0], cascadeSplits[1], cascadeSplits[2], cascadeSplits[3]));
        std::vector<kMat4> lsm(lightSpaceMatrices, lightSpaceMatrices + shadowCascadeCount);
        shader->setValue("lightSpaceMatrices", lsm);

        // The lit shaders (Phong/PBR) declare a `sampler2DArray shadowMapArray`.
        // Even when shadows are off, leaving that sampler unbound (or pointing at
        // a 2D texture on unit 0) makes many drivers drop the whole draw — the
        // symptom is lit materials rendering blank in previews/thumbnails while
        // Unlit (which has no such sampler) renders fine. So always bind the
        // depth array (allocated by the caller before the FBO was bound) and set
        // the sampler; enableShadow/cascadeCount gate whether it's sampled.
        if (shadowTexArray != 0)
        {
            driver->bindTexture2DArray(shadowUnit, shadowTexArray);
            shader->setValue("shadowMapArray", shadowUnit);
        }

        bindMaterialTextures(mesh, shader);

        // Dynamic, shader-driven parameters (from `// @var` annotations). Mirrors
        // kRenderer: scalars/vectors set the uniform of the same name; sampler
        // params bind their texture to a free unit (kept below the shadow unit).
        int paramTexUnit = (int)mesh->getMaterial()->getTextures().size();
        for (const auto &kv : mesh->getMaterial()->getParams())
        {
            const kString        &pn = kv.first;
            const kMaterialParam &p  = kv.second;
            switch (p.type)
            {
                case kMaterialParamType::FLOAT: shader->setValue(pn, p.value.x); break;
                case kMaterialParamType::INT:   shader->setValue(pn, (int)p.value.x); break;
                case kMaterialParamType::BOOL:  shader->setValue(pn, p.value.x != 0.0f); break;
                case kMaterialParamType::VEC2:  shader->setValue(pn, kVec2(p.value.x, p.value.y)); break;
                case kMaterialParamType::VEC3:  shader->setValue(pn, kVec3(p.value.x, p.value.y, p.value.z)); break;
                case kMaterialParamType::VEC4:  shader->setValue(pn, p.value); break;
                case kMaterialParamType::SAMPLER2D:
                    if (p.texture && paramTexUnit < shadowUnit)
                    {
                        driver->bindTexture2D(paramTexUnit, p.texture->getTextureID());
                        shader->setValue(pn, (int)paramTexUnit); // sampler units must use glUniform1i
                        shader->setValue("has_" + pn, true);
                        paramTexUnit++;
                    }
                    break;
                case kMaterialParamType::SAMPLERCUBE:
                    if (p.texture && paramTexUnit < shadowUnit)
                    {
                        driver->bindTextureCube(paramTexUnit, p.texture->getTextureID());
                        shader->setValue(pn, (int)paramTexUnit); // sampler units must use glUniform1i
                        shader->setValue("has_" + pn, true);
                        paramTexUnit++;
                    }
                    break;
            }
        }

        mesh->draw();

        unbindMaterialTextures(mesh);
        for (int k = (int)mesh->getMaterial()->getTextures().size(); k < paramTexUnit; k++)
        {
            driver->unbindTexture2D(k);
            driver->unbindTextureCube(k);
        }

        driver->unbindTexture2DArray(shadowUnit);
        if (skyboxBound)
            driver->unbindTextureCube(skyboxUnit);
    }

    void kOffscreenRenderer::setupLightsFromScene(kShader *shader, kScene *scene,
                                                   int &outSun, int &outPoint, int &outSpot)
    {
        outSun = outPoint = outSpot = 0;

        shader->setValue("sceneAmbient",          scene->getAmbientLightColor());
        shader->setValue("skyboxAmbientEnabled",   scene->getSkyboxAmbientEnabled());
        shader->setValue("skyboxAmbientStrength",  scene->getSkyboxAmbientStrength());

        for (kLight *light : scene->getLights())
        {
            if (!light || !light->getActive()) continue;

            // Refresh the light's world transform so point/spot positions and
            // sun/spot directions inherit any parent chain's transform.
            light->calculateModelMatrix();

            if (light->getLightType() == LIGHT_TYPE_SUN)
            {
                kString idx = std::to_string(outSun);
                shader->setValue("sunLights[" + idx + "].power",
                                 light->getPower());
                shader->setValue("sunLights[" + idx + "].direction",
                                 glm::normalize(light->getGlobalRotation() * kVec3(0.0f, -1.0f, 0.0f)));
                shader->setValue("sunLights[" + idx + "].diffuse",
                                 light->getDiffuseColor());
                shader->setValue("sunLights[" + idx + "].specular",
                                 light->getSpecularColor());
                outSun++;
            }
            else if (light->getLightType() == LIGHT_TYPE_POINT)
            {
                kString idx = std::to_string(outPoint);
                shader->setValue("pointLights[" + idx + "].power",     light->getPower());
                shader->setValue("pointLights[" + idx + "].position",  light->getGlobalPosition());
                shader->setValue("pointLights[" + idx + "].constant",  light->getConstant());
                shader->setValue("pointLights[" + idx + "].linear",    light->getLinear());
                shader->setValue("pointLights[" + idx + "].quadratic", light->getQuadratic());
                shader->setValue("pointLights[" + idx + "].diffuse",   light->getDiffuseColor());
                shader->setValue("pointLights[" + idx + "].specular",  light->getSpecularColor());
                outPoint++;
            }
            else if (light->getLightType() == LIGHT_TYPE_SPOT)
            {
                kString idx = std::to_string(outSpot);
                shader->setValue("spotLights[" + idx + "].power",       light->getPower());
                shader->setValue("spotLights[" + idx + "].position",    light->getGlobalPosition());
                shader->setValue("spotLights[" + idx + "].direction",
                                 glm::normalize(light->getGlobalRotation() * kVec3(0.0f, -1.0f, 0.0f)));
                shader->setValue("spotLights[" + idx + "].cutOff",      light->getCutOff());
                shader->setValue("spotLights[" + idx + "].outerCutOff", light->getOuterCutOff());
                shader->setValue("spotLights[" + idx + "].constant",    light->getConstant());
                shader->setValue("spotLights[" + idx + "].linear",      light->getLinear());
                shader->setValue("spotLights[" + idx + "].quadratic",   light->getQuadratic());
                shader->setValue("spotLights[" + idx + "].diffuse",     light->getDiffuseColor());
                shader->setValue("spotLights[" + idx + "].specular",    light->getSpecularColor());
                outSpot++;
            }
        }

        shader->setValue("sunLightNum",   outSun);
        shader->setValue("pointLightNum", outPoint);
        shader->setValue("spotLightNum",  outSpot);
    }

    void kOffscreenRenderer::setupSingleSunLight(kShader *shader,
                                                  kVec3 direction, kVec3 diffuse, float power)
    {
        shader->setValue("sunLights[0].power",     power);
        shader->setValue("sunLights[0].direction", direction);
        shader->setValue("sunLights[0].diffuse",   diffuse);
        shader->setValue("sunLights[0].specular",  diffuse);
    }

    void kOffscreenRenderer::bindMaterialTextures(kMesh *mesh, kShader *shader)
    {
        const auto &textures = mesh->getMaterial()->getTextures();
        for (size_t i = 0; i < textures.size(); ++i)
        {
            kTexture *tex = textures[i];
            if (!tex) continue;

            if (tex->getType() == kTextureType::TEX_TYPE_2D)
                driver->bindTexture2D((int)i, tex->getTextureID());
            else if (tex->getType() == kTextureType::TEX_TYPE_CUBE)
                driver->bindTextureCube((int)i, tex->getTextureID());

            shader->setValue(tex->getTextureName().c_str(), (int)i); // sampler -> glUniform1i
            shader->setValue("has_" + tex->getTextureName(), true);
        }
    }

    void kOffscreenRenderer::unbindMaterialTextures(kMesh *mesh)
    {
        int count = (int)mesh->getMaterial()->getTextures().size();
        for (int i = count - 1; i >= 0; --i)
        {
            driver->unbindTexture2D(i);
            driver->unbindTextureCube(i);
        }
    }

    // -----------------------------------------------------------------------
    // saveToFile
    // -----------------------------------------------------------------------

    bool kOffscreenRenderer::saveToFile(const kString &filePath) const
    {
        if (!fbo || !colorTex || filePath.empty()) return false;

        int rw = width  * ssaaScale;
        int rh = height * ssaaScale;

        std::vector<uint8_t> pixels(rw * rh * 4);

        driver->bindFramebuffer(fbo);
        glReadPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        driver->unbindFramebuffer();

        // Box-filter downsample from rw×rh → width×height, and flip vertically (GL bottom-left → top-left)
        std::vector<uint8_t> out(width * height * 4);
        for (int oy = 0; oy < height; ++oy)
        {
            int srcY = (height - 1 - oy) * ssaaScale; // flip while sampling
            for (int ox = 0; ox < width; ++ox)
            {
                int srcX = ox * ssaaScale;
                int r = 0, g = 0, b = 0, a = 0;
                for (int dy = 0; dy < ssaaScale; ++dy)
                    for (int dx = 0; dx < ssaaScale; ++dx)
                    {
                        int idx = ((srcY + dy) * rw + (srcX + dx)) * 4;
                        r += pixels[idx + 0];
                        g += pixels[idx + 1];
                        b += pixels[idx + 2];
                        a += pixels[idx + 3];
                    }
                int s = ssaaScale * ssaaScale;
                int idx = (oy * width + ox) * 4;
                out[idx + 0] = static_cast<uint8_t>(r / s);
                out[idx + 1] = static_cast<uint8_t>(g / s);
                out[idx + 2] = static_cast<uint8_t>(b / s);
                out[idx + 3] = static_cast<uint8_t>(a / s);
            }
        }

        // Determine format from extension
        kString ext = filePath;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        int rowBytes = width * 4;
        if (ext.size() >= 4 && ext.substr(ext.size() - 4) == ".png")
            return stbi_write_png(filePath.c_str(), width, height, 4,
                                  out.data(), rowBytes) != 0;
        if (ext.size() >= 4 && ext.substr(ext.size() - 4) == ".jpg")
            return stbi_write_jpg(filePath.c_str(), width, height, 4,
                                  out.data(), 90) != 0;
        if (ext.size() >= 4 && ext.substr(ext.size() - 4) == ".bmp")
            return stbi_write_bmp(filePath.c_str(), width, height, 4,
                                  out.data()) != 0;
        if (ext.size() >= 4 && ext.substr(ext.size() - 4) == ".tga")
            return stbi_write_tga(filePath.c_str(), width, height, 4,
                                  out.data()) != 0;

        return false;
    }
}
