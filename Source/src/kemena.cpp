#include "kemena.h"

namespace kemena
{
    kRendererType defaultRendererType()
    {
#ifdef KEMENA_D3D11
        // Built with the DirectX 11 backend, so make it the default: applications
        // (the studio, the runtime) then use the D3D11 driver and the HLSL shader
        // variants without any change of their own.
        return kRendererType::RENDERER_D3D11;
#else
        return kRendererType::RENDERER_GL;
#endif
    }

    kWindow *createWindow(int width, int height, kString title)
    {
        kWindow *window = new kWindow;
        // Match the window to the backend new renderers will use: a DirectX swap
        // chain is created from the native HWND and must not carry SDL's OpenGL
        // flag.  OpenGL builds keep the flag, as before.
        window->setUseOpenGL(defaultRendererType() == kRendererType::RENDERER_GL);
        bool done = window->init(width, height, title);
        if (done)
            return window;
        else
            return nullptr;
    }

    kWindow *createWindow(int width, int height, kString title, bool maximized, kWindowType type, void *nativeHandle)
    {
        kWindow *window = new kWindow;
        window->setUseOpenGL(defaultRendererType() == kRendererType::RENDERER_GL);
        bool done = window->init(width, height, title, maximized, type, nativeHandle);
        if (done)
            return window;
        else
            return nullptr;
    }

    static kRenderer *createRendererOfType(kWindow *window, kRendererType type)
    {
        kRenderer *renderer = new kRenderer;
        renderer->setEngineInfo(engineName, engineVersion);
        if (renderer->init(window, type))
            return renderer;

        delete renderer;
        return nullptr;
    }

    kRenderer *createRenderer(kWindow *window)
    {
        return createRendererOfType(window, defaultRendererType());
    }

    kRenderer *createRenderer(kWindow *window, kRendererType type)
    {
        return createRendererOfType(window, type);
    }

    kAssetManager *createAssetManager()
    {
        kAssetManager *manager = new kAssetManager();
        return manager;
    }

    kWorld *createWorld(kAssetManager *assetManager)
    {
        kWorld *manager = new kWorld();
        manager->setAssetManager(assetManager);
        return manager;
    }

    kScriptManager *createScriptManager()
    {
        kScriptManager *manager = new kScriptManager();
        return manager;
    }

    kGuiManager *createGuiManager(kRenderer *renderer)
    {
        kGuiManager *manager = new kGuiManager();
        manager->init(renderer);
        return manager;
    }

    kInputManager *createInputManager()
    {
        kInputManager *manager = new kInputManager();
        manager->init();
        return manager;
    }

    kAudioManager *createAudioManager()
    {
        kAudioManager *manager = new kAudioManager();
        manager->init();
        return manager;
    }

    kPhysicsManager *createPhysicsManager()
    {
        kPhysicsManager *manager = new kPhysicsManager();
        manager->init();
        return manager;
    }

    kNavManager *createNavManager(kNavMesh *mesh, int maxAgents)
    {
        kNavManager *manager = new kNavManager(mesh, maxAgents);
        manager->init();
        return manager;
    }
}
