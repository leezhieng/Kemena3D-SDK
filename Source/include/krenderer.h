/**
 * @file krenderer.h
 * @brief High-level renderer that orchestrates the render pipeline.
 *
 * kRenderer owns the kDriver instance, manages framebuffers, shadow maps,
 * screen-space post-processing, and drives the scene-graph rendering loop.
 */

#ifndef KRENDERER_H
#define KRENDERER_H

#include "kexport.h"
#include "kdriver.h"

#include "kwindow.h"
#include "kdatatype.h"

#include <string>
#include <vector>
#include <set>
#include <assert.h>
#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "kmesh.h"
#include "kcamera.h"
#include "kshader.h"
#include "klight.h"
#include "kworld.h"
#include "kscene.h"
#include "kobject.h"
#include "kparticle.h"
#include "koctree.h"

#include <unordered_set>
#include <memory>

#include <glm/gtx/string_cast.hpp>

namespace kemena
{
    /**
     * @brief Orchestrates the full render pipeline for a scene.
     *
     * Usage outline:
     * @code
     *   kWindow window;
     *   window.init(1280, 720, "My App");
     *
     *   kRenderer renderer;
     *   renderer.init(&window, kRendererType::RENDERER_GL);
     *   renderer.setEnableScreenBuffer(true);   // optional post-process FBO
     *   renderer.setEnableShadow(true);          // optional shadow map
     *
     *   // main loop
     *   while (window.getRunning()) {
     *       window.getTimer()->tick();
     *       renderer.render(&world, &scene, 0, 0, 1280, 720,
     *                       window.getTimer()->getDeltaTime());
     *   }
     *   renderer.destroy();
     * @endcode
     */
    class KEMENA3D_API kRenderer
    {
    public:
        kRenderer();

        /**
         * @brief Initialises the renderer and creates the graphics driver.
         * @param window Target window (may be nullptr for off-screen rendering).
         * @param type   Graphics backend to use.
         * @return true on success.
         */
        bool init(kWindow *window = nullptr, kRendererType type = kRendererType::RENDERER_GL);

        /**
         * @brief Initialises the renderer on an EXISTING driver (shared context).
         *
         * Unlike init(), this does not create a graphics context. Use it for a
         * second render target that draws the same scene as another renderer:
         * vertex-array objects are context-local, so both renderers must share a
         * single context or the second one cannot draw meshes uploaded by the
         * first. The renderer does NOT take ownership of @p sharedDriver; the
         * caller keeps it alive for this renderer's lifetime and destroys it via
         * the owning renderer.
         *
         * @param window       Window @p sharedDriver belongs to (size hint, may be nullptr).
         * @param sharedDriver Driver/context to render on. Must not be null.
         * @param type         Backend type of @p sharedDriver.
         * @return true on success.
         */
        bool initShared(kWindow *window, kDriver *sharedDriver,
                        kRendererType type = kRendererType::RENDERER_GL);

        /** @brief Destroys all GPU resources and the driver (only when owned). */
        void destroy();

        /**
         * @brief Stores an optional application name/version for driver diagnostics.
         * @param name    Application name.
         * @param version Application version number.
         */
        void setEngineInfo(const kString name, uint32_t version);

        /**
         * @brief Returns the window the renderer was initialised with.
         * @return Pointer to the kWindow, or nullptr if none was provided.
         */
        kWindow *getWindow();

        /**
         * @brief Returns the active graphics driver.
         * @return Pointer to the kDriver owned by this renderer.
         */
        kDriver *getDriver();

        /**
         * @brief Clears the active framebuffer to the clear colour.
         *
         * Binds the screen FBO (if enabled) before clearing so that subsequent
         * render() calls draw into the same buffer.
         */
        void clear();

        /**
         * @brief Renders the scene from the world's main camera.
         *
         * The render order is:
         * 1. Shadow map pass (if a sun light and shadow shader are present).
         * 2. Opaque scene geometry.
         * 3. Screen-space post-processing blit (if screen buffer is enabled).
         *
         * @param world               World containing the main camera.
         * @param scene               Scene to render (lights, meshes, etc.).
         * @param x,y                 Viewport origin in pixels.
         * @param width,height        Viewport dimensions in pixels.
         * @param deltaTime           Frame delta time in seconds (used for animation).
         * @param autoClearSwapWindow If true, automatically clears and swaps the window.
         */
        void render(kWorld *world, kScene *scene, int x, int y, int width, int height,
                    float deltaTime = 0.0f, bool autoClearSwapWindow = true);

        /**
         * @brief Returns the background clear colour.
         * @return RGBA colour in linear space.
         */
        kVec4 getClearColor();

        /**
         * @brief Sets the background clear colour.
         *
         * The input is expected to be in sRGB space and is converted to linear
         * before being stored.
         * @param newColor sRGB RGBA colour.
         */
        void setClearColor(kVec4 newColor);

        /**
         * @brief Enables or disables the off-screen screen buffer (post-process FBO).
         *
         * When enabled the scene is first rendered into an MSAA FBO, resolved to
         * a single-sample texture, and then drawn to the screen via a full-screen
         * quad shader.
         * @param newEnable        true to enable.
         * @param useDefaultShader If true, a built-in tone-mapping/gamma shader is
         *                         compiled and set automatically.
         */
        void setEnableScreenBuffer(bool newEnable, bool useDefaultShader = true);

        /** @brief Returns whether the screen buffer post-process FBO is active. */
        bool getEnableScreenBuffer();

        /**
         * @brief When true, the fullscreen quad blit that draws the resolved
         *        FBO to the default framebuffer is skipped.  The FBO texture
         *        is still populated and can be read via getFboTexture().
         *
         * Use this when the rendered output is consumed as an ImGui image
         * rather than drawn directly to the window (e.g. editor viewports).
         *
         * @param skip true to skip the fullscreen quad, false (default) to draw it.
         */
        void setSkipScreenQuad(bool skip) { skipScreenQuad = skip; }

        /** @brief Returns whether the fullscreen quad is being skipped. */
        bool getSkipScreenQuad() const { return skipScreenQuad; }

        /**
         * @brief Sets a custom screen-space shader for post-processing.
         * @param newShader Shader to use; the renderer takes ownership.
         */
        void setScreenShader(kShader *newShader);

        /**
         * @brief Returns the current screen-space shader.
         * @return Pointer to the screen shader, or nullptr if none is set.
         */
        kShader *getScreenShader();

        /**
         * @brief Enables or disables shadow mapping.
         * @param newEnable        true to enable.
         * @param useDefaultShader If true, a built-in depth-only shadow shader is
         *                         compiled and set automatically.
         */
        void setEnableShadow(bool newEnable, bool useDefaultShader = true);

        /** @brief Returns whether shadow mapping is active. */
        bool getEnableShadow();

        /**
         * @brief Sets the shadow-map depth shader.
         * @param newShader Shader used for the shadow pass.
         */
        void setShadowShader(kShader *newShader);

        /**
         * @brief Returns the current shadow-map shader.
         * @return Pointer to the shadow shader, or nullptr if none is set.
         */
        kShader *getShadowShader();

        /**
         * @brief Sets the number of cascades (1..4). Cheap; the underlying
         *        texture array always allocates the maximum, so no realloc.
         */
        void setShadowCascadeCount(int count);
        /** @brief Returns the active cascade count. */
        int  getShadowCascadeCount() const { return shadowCascadeCount; }

        /**
         * @brief Sets the per-cascade shadow-map resolution (e.g. 1024/2048/4096).
         *        Rebuilds the shadow texture if shadows are enabled.
         */
        void setShadowResolution(int resolution);
        /** @brief Returns the per-cascade shadow-map resolution. */
        int  getShadowResolution() const { return shadowResolution; }

        /** @brief Sets the cascade split blend: 0 = uniform, 1 = logarithmic. */
        void  setShadowSplitLambda(float lambda) { shadowSplitLambda = lambda; }
        /** @brief Returns the cascade split blend factor. */
        float getShadowSplitLambda() const { return shadowSplitLambda; }

        /** @brief Toggles a debug view that tints each cascade a distinct colour. */
        void setShadowDebug(bool enable) { shadowDebug = enable; }
        /** @brief Returns whether the cascade debug view is active. */
        bool getShadowDebug() const { return shadowDebug; }

        /** @brief Sets the constant shadow bias passed to lit shaders. */
        void  setShadowBias(float bias) { shadowBias = bias; }
        /** @brief Returns the constant shadow bias. */
        float getShadowBias() const { return shadowBias; }

        /** @brief Sets the slope-scaled (normal) shadow bias passed to lit shaders. */
        void  setShadowNormalBias(float bias) { shadowNormalBias = bias; }
        /** @brief Returns the slope-scaled shadow bias. */
        float getShadowNormalBias() const { return shadowNormalBias; }

        /** @brief Sets the normal-offset distance (shadow-map texels) passed to lit shaders. */
        void  setShadowNormalOffset(float offset) { shadowNormalOffset = offset; }
        /** @brief Returns the normal-offset distance in shadow-map texels. */
        float getShadowNormalOffset() const { return shadowNormalOffset; }

        /** @brief Sets the PCF tap spacing (in shadow-map texels) passed to lit shaders. */
        void  setShadowSoftness(float softness) { shadowSoftness = softness; }
        /** @brief Returns the PCF tap spacing. */
        float getShadowSoftness() const { return shadowSoftness; }

        /**
         * @brief Sets the on-screen size (world units) of the editor billboard
         *        gizmo icons drawn for non-mesh nodes.
         *
         * Applies to the light, camera, decal, particle and audio icons. The
         * default is 0.48 (40% smaller than the former 0.8) so icons do not
         * dominate the viewport.
         * @param size Billboard half-extent in world units (clamped to >= 0.01).
         */
        void  setIconGizmoSize(float size) { iconGizmoSize = (size < 0.01f) ? 0.01f : size; }
        /** @brief Returns the editor node gizmo icon billboard size. */
        float getIconGizmoSize() const { return iconGizmoSize; }

        /**
         * @brief Enables/disables the editor-only billboard gizmo icons
         *        (light, camera, decal, audio).
         *
         * These icons are authoring aids and must not appear in the Game panel
         * (or any play/preview view). The editor viewport leaves this enabled;
         * the Game panel disables it for its render pass.
         */
        void setEditorGizmosEnabled(bool enabled) { editorGizmosEnabled = enabled; }
        /** @brief Returns whether editor billboard gizmo icons are drawn. */
        bool getEditorGizmosEnabled() const { return editorGizmosEnabled; }

        /**
         * @brief Enables or disables automatic exposure adjustment.
         *
         * When enabled, the average luminance of the resolved FBO colour texture
         * is sampled each frame (via mipmaps) and used to drive an exposure value
         * that is passed to the screen shader.
         * @param newEnable true to enable.
         */
        void setEnableAutoExposure(bool newEnable);

        /** @brief Returns whether automatic exposure adjustment is active. */
        bool getEnableAutoExposure();

        /**
         * @brief Resizes the screen-buffer FBOs to match a new viewport size.
         *
         * Called automatically by render() when the viewport dimensions change.
         * @param newWidth  New FBO width in pixels.
         * @param newHeight New FBO height in pixels.
         */
        void resizeFbo(int newWidth, int newHeight);

        /**
         * @brief Returns the resolved (single-sample) FBO colour texture handle.
         * @return Texture handle suitable for use as an ImGui image or sampler.
         */
        uint32_t getFboTexture();

        /** @brief Returns the current FBO width in pixels. */
        int getFboWidth();

        /** @brief Returns the current FBO height in pixels. */
        int getFboHeight();

        /**
         * @brief Converts an sRGB component value to linear space.
         * @param c sRGB channel value in [0, 1].
         * @return Linearised value.
         */
        float srgbToLinear(float c);

        /**
         * @brief Converts a packed integer ID to an RGB colour for object picking.
         * @param i Object ID.
         * @return RGB colour with each channel in [0, 255].
         */
        kVec3 idToRgb(unsigned int i);

        /**
         * @brief Converts an RGB colour read back from the GPU to an object ID.
         * @param r Red channel value [0, 255].
         * @param g Green channel value [0, 255].
         * @param b Blue channel value [0, 255].
         * @return Packed object ID.
         */
        unsigned int rgbToId(unsigned int r, unsigned int g, unsigned int b);

        // --- Color ID object picking -----------------------------------------

        /**
         * @brief Enables or disables the color-ID picking FBO and compiles the
         *        built-in picking shader.
         *
         * When enabled, pickObject() can be called to determine which scene object
         * is under a given screen coordinate by rendering all meshes with unique
         * solid ID colors and reading back the clicked pixel.
         *
         * Intended for editor use — works for any object regardless of whether it
         * has a physics body attached.
         *
         * @param enable           true to enable.
         * @param useDefaultShader If true, a built-in picking shader is compiled
         *                         automatically.
         */
        void setEnableObjectPicking(bool enable, bool useDefaultShader = true);

        /** @brief Returns whether color-ID object picking is active. */
        bool getEnableObjectPicking();

        /**
         * @brief Enable or disable octree-based frustum culling (default: enabled).
         *
         * When enabled, an octree is rebuilt each frame from all scene meshes and
         * only meshes whose world AABB intersects the camera frustum are rendered.
         */
        void setOctreeCullingEnabled(bool enable) { octreeCullingEnabled = enable; }

        /**
         * @brief Marks the static-mesh octree as needing a rebuild.
         *
         * Call this whenever a static mesh is added, removed, or moved.
         * The octree rebuilds once on the next rendered frame, then stays
         * valid until marked dirty again.
         */
        void setOctreeDirty() { octreeDirty = true; }

        /** @brief Returns whether octree frustum culling is active. */
        bool getOctreeCullingEnabled() const { return octreeCullingEnabled; }

        /**
         * @brief Override the camera used to compute the culling frustum.
         *
         * By default (nullptr) the world's main camera is used.  Set a different
         * camera here to freeze or redirect the frustum — useful for debugging
         * culling without changing the render viewpoint.
         *
         * @param camera Camera to use for frustum extraction, or nullptr to revert
         *               to the main camera.
         */
        void setCullingCamera(kCamera *camera) { cullingCamera = camera; }

        /** @brief Returns the override culling camera, or nullptr if using main camera. */
        kCamera *getCullingCamera() const { return cullingCamera; }

        /** @brief Returns a reference to the scene octree (read-only). */
        const kOctree &getOctree() const { return *sceneOctree; }

        /**
         * @brief Returns the scene object under the given viewport-relative
         *        pixel coordinate using a color-ID render pass.
         *
         * Renders all mesh nodes into a dedicated picking FBO with each mesh
         * colored by its unique ID, reads the pixel at (mouseX, mouseY), decodes
         * the ID, and returns the corresponding kObject.
         *
         * @param world      World containing the active camera.
         * @param scene      Scene to pick from.
         * @param mouseX     Viewport-relative X coordinate (0 = left edge).
         * @param mouseY     Viewport-relative Y coordinate (0 = top edge).
         * @param viewWidth  Viewport width in pixels.
         * @param viewHeight Viewport height in pixels.
         * @return Pointer to the picked kObject, or nullptr if nothing was hit.
         *         setEnableObjectPicking(true) must have been called first.
         */
        kObject *pickObject(kWorld *world, kScene *scene,
                            int mouseX, int mouseY,
                            int viewWidth, int viewHeight);

        /**
         * @brief Renders the color-ID picking pass into the picking FBO.
         *
         * Should be called once per frame so the picking texture is always fresh.
         * pickObject() will read from this texture instead of re-rendering.
         *
         * @param world      World containing the active camera.
         * @param scene      Scene to render.
         * @param viewWidth  Viewport width in physical pixels.
         * @param viewHeight Viewport height in physical pixels.
         */
        void renderPickingPass(kWorld *world, kScene *scene, int viewWidth, int viewHeight);

        /**
         * @brief Sets the debug visualization mode for subsequent render() calls.
         * @param mode One of the kRenderMode values.
         */
        void setRenderMode(kRenderMode mode);

        /** @brief Returns the current debug visualization mode. */
        kRenderMode getRenderMode();

        /**
         * @brief Enables or disables the editor-only object gizmo billboards.
         *
         * Cameras, lights, audio sources and decals draw a billboard icon so
         * they stay visible/selectable in the editor viewport. Those icons are
         * editor scaffolding, so they are OFF by default: the runtime and the
         * in-editor Game panel therefore never show them. The editor viewports
         * (World and Prefab panels) turn them on explicitly.
         *
         * @param enable true to draw gizmo icon billboards during render().
         */
        void setEditorGizmosEnabled(bool enable) { editorGizmosEnabled = enable; }

        /** @brief Returns whether editor gizmo icon billboards are drawn. */
        bool getEditorGizmosEnabled() const { return editorGizmosEnabled; }

        /**
         * @brief Renders a color-ID-based outline around selected objects.
         *
         * Requires renderPickingPass() to have been called this frame.
         * Uses the picking texture to detect selection boundaries in screen space,
         * then composites an outline with alpha blending — gives uniform pixel-width
         * outlines on any mesh shape.
         *
         * @param world         World containing the active camera.
         * @param scene         Scene owning the selected objects.
         * @param selectedUuids UUIDs of selected objects to outline.
         * @param color         Outline RGBA colour (default: orange).
         * @param thickness     Outline radius in pixels (default: 3).
         */
        void renderOutline(kWorld *world, kScene *scene,
                           const std::vector<kString> &selectedUuids,
                           kVec4 color     = kVec4(1.0f, 0.6f, 0.0f, 1.0f),
                           float thickness = 3.0f);

        /**
         * @brief Draws editor debug shapes for selected lights and cameras.
         *
         * Renders wire shapes (light range spheres/cones, camera frustums, sun
         * direction arrows) for any selected light or camera node.
         *
         * Must be called after render() and before the final ImGui pass.
         * Requires setEnableScreenBuffer(true).
         *
         * @param world         World containing the active editor camera.
         * @param scene         Scene whose lights and cameras are inspected.
         * @param selectedUuids UUIDs of currently selected objects.
         */
        void renderDebugShapes(kWorld *world, kScene *scene,
                               const std::vector<kString> &selectedUuids);

        /** @brief Enable or disable octree AABB wireframe debug visualization. */
        void setOctreeDebugEnabled(bool enable) { octreeDebugEnabled = enable; }

        /** @brief Returns whether octree debug visualization is active. */
        bool getOctreeDebugEnabled() const { return octreeDebugEnabled; }

        /**
         * @brief Draws octree node AABBs and mesh world AABBs as wireframe boxes.
         *
         * Octree leaf nodes: green, internal nodes: grey.
         * Static mesh AABBs: yellow, dynamic mesh AABBs: cyan.
         * Call after renderDebugShapes each frame.
         */
        void renderOctreeDebug(kWorld *world, kScene *scene);

        /**
         * @brief Draws world-space line segments in a single colour into the
         *        view FBO. Used for editor debug overlays (e.g. nav-mesh
         *        wireframe). Each consecutive pair of points is one line.
         * @param world    Supplies the main camera (view/projection).
         * @param segments Flat list of point pairs.
         * @param color    Line colour (RGB).
         */
        void renderDebugLines(kWorld *world, const std::vector<kVec3> &segments, kVec3 color);

        /**
         * @brief Renders all live particles from the world's particle manager.
         *
         * Draws particles as instanced camera-facing billboard quads with
         * alpha blending. Called automatically from render() after opaque
         * geometry and before screen-space post-processing.
         *
         * Requires the world's particle manager to be initialised and
         * a valid main camera to be set.
         *
         * @param world World whose particle manager is queried.
         */
        void renderParticles(kWorld *world);

    protected:
    private:
        kString engineName;           ///< Optional application name for diagnostics.
        uint32_t engineVersion = 0;  ///< Optional application version for diagnostics.

        // Octree frustum culling
        std::unique_ptr<kOctree> sceneOctree = std::make_unique<kOctree>(); ///< Spatial index of scene meshes for frustum culling.
        bool octreeCullingEnabled = true; ///< Master toggle for octree-based frustum culling.
        bool octreeDirty = true;         ///< Rebuild static-mesh octree on next frame when true.
        bool octreeDebugEnabled = false; ///< Draw octree node AABBs as wireframes.
        kCamera *cullingCamera = nullptr; ///< Override camera for frustum; nullptr = main camera.
        std::unordered_set<kMesh*> visibleMeshSet; ///< Populated per frame from octree query.
        kFrustum currentFrustum; ///< Frustum for the current frame, used for dynamic mesh culling.
        bool currentFrustumValid = false; ///< True when currentFrustum has been extracted this frame.
        kWindow *appWindow = nullptr; ///< Target window the renderer was initialised with (may be nullptr).

        kRendererType renderType; ///< Selected graphics backend type.
        kDriver *driver = nullptr; ///< Graphics driver abstraction.
        bool ownsDriver = true;    ///< False when the driver was supplied via initShared() (shared context).

        kVec4 clearColor = kVec4(0.0f, 0.0f, 0.0f, 1.0f); ///< Framebuffer clear colour (stored in linear space).

        /// Used to detect same-frame animation updates.
        int frameId = 0;

        /**
         * @brief Recursively renders the scene graph with full material/lighting.
         * @param world        World containing the camera.
         * @param scene        Active scene.
         * @param rootNode     Current node to process.
         * @param transparent  If true, only transparent objects are rendered.
         * @param deltaTime    Frame delta time in seconds.
         */
        void renderSceneGraph(kWorld *world, kScene *scene, kObject *rootNode,
                              bool transparent = false, float deltaTime = 0.0f);

        /**
         * @brief Recursively renders the scene graph into a cascade shadow-map FBO.
         *
         * Meshes whose (unskinned) world AABB lies fully outside the cascade's
         * light-space box are skipped, so a distant cascade never pays for
         * geometry it cannot capture ("Cascaded Shadow Maps", Microsoft DX
         * article — cull objects outside the cascade).
         *
         * @param world            World containing the camera.
         * @param scene            Active scene.
         * @param rootNode         Current node to process.
         * @param lightSpaceMatrix Combined light projection*view matrix for this cascade.
         * @param lightView        View matrix of the light for this cascade (culling).
         * @param lightRadius      Half-width/height of the light's ortho box (culling).
         * @param lightZExtent     Half-depth of the light's ortho box (culling).
         * @param deltaTime        Frame delta time in seconds.
         */
        void renderSceneGraphShadow(kWorld *world, kScene *scene, kObject *rootNode,
                                    const kMat4 &lightSpaceMatrix,
                                    const kMat4 &lightView,
                                    float lightRadius, float lightZExtent,
                                    float deltaTime = 0.0f);

        /**
         * @brief Recursively renders the scene graph into the picking FBO.
         *
         * Each mesh is drawn with its unique ID encoded as an RGB flat color.
         * @param world     World containing the camera.
         * @param scene     Active scene.
         * @param rootNode  Current node to process.
         */
        void renderSceneGraphPicking(kWorld *world, kScene *scene, kObject *rootNode);

        // Screen FBO
        bool enableScreenBuffer = false; ///< True when off-screen post-process FBO is active.
        bool skipScreenQuad = false;     ///< When true, skip the fullscreen-quad blit (FBO still populated).
        kShader *screenShader = nullptr; ///< Full-screen post-process shader.
        uint32_t quadVao = 0, quadVbo = 0, quadEbo = 0; ///< GPU buffers for the full-screen quad.
        uint32_t fbo = 0, fboTexColor = 0, rboDepth = 0; ///< Resolved single-sample FBO and its color/depth attachments.
        uint32_t fboMsaa = 0, fboTexColorMsaa = 0, rboDepthMsaa = 0; ///< Multisample FBO resolved into the single-sample FBO.

        int fboWidth = 0, fboHeight = 0; ///< Current screen-buffer FBO dimensions in pixels.

        // Cascaded shadow maps — one depth-texture-array layer per cascade.
        static constexpr int kMaxShadowCascades = 4;
        bool     enableShadow       = false;
        kShader *shadowShader       = nullptr;
        int      shadowResolution   = 2048;   ///< Per-cascade map size (configurable).
        int      shadowCascadeCount = 3;      ///< Active cascades (1..kMaxShadowCascades).
        float    shadowSplitLambda  = 0.85f;  ///< 0 = uniform splits, 1 = logarithmic.
        bool     shadowDebug        = false;  ///< Tint fragments by cascade for debugging.
        float    shadowBias         = 0.0006f;///< Constant shadow bias (passed to lit shaders).
        float    shadowNormalBias   = 0.0015f;///< Slope-scaled shadow bias (passed to lit shaders).
        float    shadowNormalOffset = 1.5f;   ///< Normal-offset distance in shadow-map texels.
        float    shadowSoftness     = 1.5f;   ///< PCF tap spacing in shadow-map texels.
        float    iconGizmoSize      = 0.48f;  ///< Editor node gizmo icon billboard size (world units).
        bool     editorGizmosEnabled = true;  ///< Draw editor-only billboard gizmo icons (light/camera/decal/audio); false for game/preview passes.
        uint32_t shadowFbo          = 0;      ///< Single FBO; layer re-attached per cascade.
        uint32_t shadowTexArray     = 0;      ///< GL_TEXTURE_2D_ARRAY depth texture.
        kMat4    lightSpaceMatrices[kMaxShadowCascades]; ///< Per-cascade light projection*view matrices.
        float    cascadeSplits[kMaxShadowCascades] = {}; ///< Per-cascade far-plane split distances.

        // Picking FBO
        bool enablePicking = false; ///< True when color-ID object picking is active.
        kShader *pickingShader = nullptr; ///< Shader that writes each mesh's unique ID as a flat color.
        kShader *pickingIconShader = nullptr; ///< Shader for picking editor icon billboards (lights/cameras).
        uint32_t pickingIconVAO = 0, pickingIconVBO = 0; ///< GPU buffers for picking icon quads.
        uint32_t pickFbo = 0, pickFboTex = 0, pickRboDepth = 0; ///< Picking FBO and its color/depth attachments.
        int pickFboWidth = 0, pickFboHeight = 0; ///< Current picking FBO dimensions in pixels.

        // Outline shader (compiled on first renderOutline call)
        kShader *outlineShader = nullptr; ///< Shader that composites selection outlines from the picking texture.

        // Passthrough shader for Object IDs display mode
        kShader *debugPickShader = nullptr; ///< Shader that displays the raw picking IDs as colours.

        // Debug / render-mode shaders (compiled on first use)
        kShader *debugAlbedoShader  = nullptr; ///< Visualises base colour only.
        kShader *debugNormalsShader = nullptr; ///< Visualises world/view-space normals.
        kShader *debugWireShader    = nullptr; ///< Renders meshes as wireframe.
        kShader *debugDepthShader   = nullptr; ///< Visualises linearised scene depth.

        // Debug shape line rendering
        kShader  *debugLineShader = nullptr; ///< Shader for solid-colour debug line segments.
        uint32_t  debugLineVao    = 0;       ///< Vertex array for debug line geometry.
        uint32_t  debugLineVbo    = 0;       ///< Vertex buffer for debug line geometry.

        kRenderMode renderMode = kRenderMode::RENDER_MODE_FULL; ///< Active debug visualization mode.

        /// Editor-only gizmo icon billboards (camera/light/audio/decal). Off by
        /// default so the runtime and in-editor Game view never draw them; the
        /// editor viewports enable it via setEditorGizmosEnabled().
        bool editorGizmosEnabled = false;

        /**
         * @brief Renders the scene graph using a single override shader (no lights/shadows).
         * @param world     World containing the active camera.
         * @param scene     Scene to render.
         * @param rootNode  Current node being processed.
         * @param shader    Override shader to use for every mesh.
         * @param wireframe If true, polygon mode is GL_LINE.
         */
        void renderSceneGraphDebug(kWorld *world, kScene *scene, kObject *rootNode,
                                   kShader *shader, bool wireframe);

        // Auto exposure
        bool enableAutoExposure = false; ///< True when automatic exposure adjustment is active.
        float averageLuminance = 0.0f; ///< Smoothed scene average luminance.
        float averageLuminanceColor[4] = {}; ///< Last sampled average colour (RGBA) from the resolved FBO.
        float exposureKey = 0.18f; ///< Target middle-grey key value for exposure.
        float exposureAdaptationRate = 2.0f; ///< Speed at which exposure adapts to luminance changes.
        float exposure = 1.0f; ///< Current exposure multiplier passed to the screen shader.
    };
}

#endif // KRENDERER_H
