#ifndef KEMENA_H
#define KEMENA_H

#include "kexport.h"

#include "kdatatype.h"
#include "kwindow.h"
#include "krenderer.h"
#include "kassetmanager.h"
#include "kworld.h"
#include "kprefab.h"
#include "kmeshgenerator.h"
#include "kscriptmanager.h"
#include "kguimanager.h"
#include "kinputmanager.h"
#include "kaudiomanager.h"
#include "kphysicsmanager.h"
#include "knavmanager.h"

/**
 * @brief Top-level Kemena3D engine namespace.
 *
 * Aggregates the public engine headers and exposes the high-level factory
 * functions used to bootstrap an application: creating the window, renderer,
 * and the various subsystem managers (assets, world, scripting, GUI, audio,
 * physics, navigation).
 */
namespace kemena
{
    const kString engineName = "Kemena3D";  ///< Human-readable engine name, reported to the renderer/backend.
    const uint32_t engineVersion = 1;        ///< Engine version number, reported to the renderer/backend.

    /**
     * @brief Create and initialise a new application window.
     *
     * Allocates a kWindow, initialises it with the given parameters, and returns
     * it. Returns nullptr if initialisation fails.
     *
     * @param width        Window client area width, in pixels.
     * @param height       Window client area height, in pixels.
     * @param title        Window title text.
     * @param maximized    Whether the window should start maximized (default: false).
     * @param type         Window type/style to create (default: kWindowType::WINDOW_DEFAULT).
     * @param nativeHandle Optional existing native window handle to wrap, or nullptr to create a new one.
     * @return Pointer to the initialised kWindow, or nullptr on failure.
     */
    KEMENA3D_API kWindow *createWindow(int width, int height, kString title, bool maximized = false, kWindowType type = kWindowType::WINDOW_DEFAULT, void *nativeHandle = nullptr);

    /**
     * @brief Create and initialise the renderer for a given window.
     *
     * Allocates a kRenderer, stamps it with the engine name/version, and
     * initialises it against the supplied window.
     *
     * @param window The window the renderer will draw into.
     * @return Pointer to the initialised kRenderer, or nullptr on failure.
     */
    KEMENA3D_API kRenderer *createRenderer(kWindow *window);

    /**
     * @brief Returns the renderer type createRenderer() uses by default.
     *
     * The backend is a build-time choice: a build configured with KEMENA_D3D11
     * creates DirectX 11 renderers — and then loads the HLSL shader variants —
     * while every other configuration creates OpenGL renderers, which also covers
     * the OpenGL ES targets.  createWindow() matches the window to this type, so a
     * DirectX swap chain is never created on a window carrying SDL's OpenGL flag.
     *
     * @return kRendererType new renderers are created with.
     */
    KEMENA3D_API kRendererType defaultRendererType();

    /**
     * @brief Create and initialise a renderer of an explicit type.
     *
     * Only the backends compiled into the SDK are available; requesting one that
     * was not built fails and returns nullptr.
     *
     * @param window The window the renderer will draw into.
     * @param type   Backend to initialise.
     * @return Pointer to the initialised kRenderer, or nullptr on failure.
     */
    KEMENA3D_API kRenderer *createRenderer(kWindow *window, kRendererType type);

    /**
     * @brief Create a new asset manager.
     * @return Pointer to a newly allocated kAssetManager.
     */
    KEMENA3D_API kAssetManager *createAssetManager();

    /**
     * @brief Create a new world (scene) bound to an asset manager.
     * @param assetManager The asset manager the world will use to load resources.
     * @return Pointer to a newly allocated kWorld.
     */
    KEMENA3D_API kWorld *createWorld(kAssetManager *assetManager);

    /**
     * @brief Create a new script manager.
     * @return Pointer to a newly allocated kScriptManager.
     */
    KEMENA3D_API kScriptManager *createScriptManager();

    /**
     * @brief Create and initialise a GUI manager for a given renderer.
     * @param renderer The renderer the GUI will be drawn with.
     * @return Pointer to the initialised kGuiManager.
     */
    KEMENA3D_API kGuiManager *createGuiManager(kRenderer *renderer);

    /**
     * @brief Create and initialise the named input manager.
     *
     * Provides platform-independent action/axis mapping over keyboard, mouse,
     * and gamepad devices. Call kInputManager::update() once per frame before
     * querying actions and axes.
     *
     * @return Pointer to the initialised kInputManager.
     */
    KEMENA3D_API kInputManager *createInputManager();

    /**
     * @brief Create and initialise the audio manager.
     * @return Pointer to the initialised kAudioManager.
     */
    KEMENA3D_API kAudioManager *createAudioManager();

    /**
     * @brief Create and initialise the physics manager.
     * @return Pointer to the initialised kPhysicsManager.
     */
    KEMENA3D_API kPhysicsManager *createPhysicsManager();

    /**
     * @brief Create and initialise a navigation manager for a navigation mesh.
     * @param mesh      The navigation mesh agents will path over.
     * @param maxAgents Maximum number of simultaneous crowd agents (default: 128).
     * @return Pointer to the initialised kNavManager.
     */
    KEMENA3D_API kNavManager *createNavManager(kNavMesh *mesh, int maxAgents = 128);
}

#endif // KEMENA_H