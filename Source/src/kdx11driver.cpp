/**
 * @file kdx11driver.cpp
 * @brief DirectX 11 implementation of the kDriver interface.
 */

#include "kdx11driver.h"

#ifdef KEMENA_D3D11

#include <algorithm>
#include <glm/gtc/type_ptr.hpp>

// Link required D3D libraries (MSVC only; MinGW/Clang link via CMake)
#ifdef _MSC_VER
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace kemena
{
    // =========================================================================
    // D3D11ProgramData destructor
    // =========================================================================

    D3D11ProgramData::~D3D11ProgramData()
    {
        if (vs) { vs->Release(); vs = nullptr; }
        if (ps) { ps->Release(); ps = nullptr; }
        if (inputLayout) { inputLayout->Release(); inputLayout = nullptr; }
        if (vsBlob) { vsBlob->Release(); vsBlob = nullptr; }
        for (auto &kv : constantBuffers)
            if (kv.second) { kv.second->Release(); kv.second = nullptr; }
    }

    // The implicitly generated move operations would copy the COM pointers
    // (leaving both objects owning them), so they are written out by hand.
    D3D11ProgramData::D3D11ProgramData(D3D11ProgramData &&other) noexcept
        : vs(other.vs),
          ps(other.ps),
          inputLayout(other.inputLayout),
          uniforms(std::move(other.uniforms)),
          constantBuffers(std::move(other.constantBuffers)),
          cbShadows(std::move(other.cbShadows)),
          cbSizes(std::move(other.cbSizes)),
          cbDirty(std::move(other.cbDirty)),
          vsBlob(other.vsBlob)
    {
        other.vs          = nullptr;
        other.ps          = nullptr;
        other.inputLayout = nullptr;
        other.vsBlob      = nullptr;
    }

    D3D11ProgramData &D3D11ProgramData::operator=(D3D11ProgramData &&other) noexcept
    {
        if (this == &other)
            return *this;

        if (vs) { vs->Release(); vs = nullptr; }
        if (ps) { ps->Release(); ps = nullptr; }
        if (inputLayout) { inputLayout->Release(); inputLayout = nullptr; }
        if (vsBlob) { vsBlob->Release(); vsBlob = nullptr; }
        for (auto &kv : constantBuffers)
            if (kv.second) { kv.second->Release(); kv.second = nullptr; }

        vs              = other.vs;
        ps              = other.ps;
        inputLayout     = other.inputLayout;
        vsBlob          = other.vsBlob;
        uniforms        = std::move(other.uniforms);
        constantBuffers = std::move(other.constantBuffers);
        cbShadows       = std::move(other.cbShadows);
        cbSizes         = std::move(other.cbSizes);
        cbDirty         = std::move(other.cbDirty);

        other.vs          = nullptr;
        other.ps          = nullptr;
        other.inputLayout = nullptr;
        other.vsBlob      = nullptr;
        return *this;
    }

    // =========================================================================
    // kDX11Driver construction / destruction
    // =========================================================================

    kDX11Driver::kDX11Driver()
    {
        memset(clearColor, 0, sizeof(clearColor));
        memset(boundTextures, 0, sizeof(boundTextures));
        memset(currentRTVs, 0, sizeof(currentRTVs));
    }

    kDX11Driver::~kDX11Driver()
    {
        destroy();
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    bool kDX11Driver::init(kWindow *window)
    {
        if (window == nullptr)
            return false;

        SDL_Window *sdlWin = window->getSdlWindow();
        if (!sdlWin)
            return false;

        // Retrieve native HWND from SDL3
        SDL_PropertiesID props = SDL_GetWindowProperties(sdlWin);
        HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (!hwnd)
        {
            std::cout << "[kDX11Driver] Failed to get native HWND from SDL window." << std::endl;
            return false;
        }

        int width  = window->getWindowWidth();
        int height = window->getWindowHeight();

        // --- Create DXGI factory & enumerate adapters ------------------------
        IDXGIFactory *dxgiFactory = nullptr;
        HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&dxgiFactory);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] CreateDXGIFactory failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }

        IDXGIAdapter *adapter = nullptr;
        hr = dxgiFactory->EnumAdapters(0, &adapter);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] EnumAdapters failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            dxgiFactory->Release();
            return false;
        }

        // --- Create D3D11 device & context -----------------------------------
        // Feature level 11.1 only exists on Windows 8+ (or Windows 7 with the
        // platform update); requesting it on a machine without it makes
        // D3D11CreateDevice fail outright with E_INVALIDARG, so the fallback
        // chain below retries without it — and separately without the debug
        // layer, which is only present when the Graphics Tools SDK feature is
        // installed.
        D3D_FEATURE_LEVEL featureLevels111[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        D3D_FEATURE_LEVEL featureLevels110[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        UINT createFlags = 0;
#ifdef _DEBUG
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        hr = D3D11CreateDevice(
            adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            createFlags, featureLevels111, ARRAYSIZE(featureLevels111),
            D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);

        if (FAILED(hr))
        {
            // Retry without the debug layer first (its absence is the most
            // common cause), then without feature level 11.1.
            UINT flagsNoDebug = createFlags & ~D3D11_CREATE_DEVICE_DEBUG;

            hr = D3D11CreateDevice(
                adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                flagsNoDebug, featureLevels111, ARRAYSIZE(featureLevels111),
                D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);

            if (FAILED(hr))
            {
                hr = D3D11CreateDevice(
                    adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                    flagsNoDebug, featureLevels110, ARRAYSIZE(featureLevels110),
                    D3D11_SDK_VERSION, &d3dDevice, &featureLevel, &d3dContext);
            }
        }

        // The adapter must stay alive for every attempt above; passing a null
        // adapter together with D3D_DRIVER_TYPE_UNKNOWN is an E_INVALIDARG.
        adapter->Release();

        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] D3D11CreateDevice failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }

        std::cout << "[kDX11Driver] Device created. Feature level: 0x"
                  << std::hex << featureLevel << std::dec << std::endl;

        // --- Create swap chain -----------------------------------------------
        DXGI_SWAP_CHAIN_DESC scDesc = {};
        scDesc.BufferCount       = 2;
        scDesc.BufferDesc.Width  = width;
        scDesc.BufferDesc.Height = height;
        scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.BufferDesc.RefreshRate.Numerator   = 60;
        scDesc.BufferDesc.RefreshRate.Denominator = 1;
        scDesc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.OutputWindow      = hwnd;
        scDesc.SampleDesc.Count  = 1;
        scDesc.SampleDesc.Quality = 0;
        scDesc.Windowed          = TRUE;
        scDesc.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

        hr = dxgiFactory->CreateSwapChain(d3dDevice, &scDesc, &swapChain);
        dxgiFactory->Release();

        if (FAILED(hr) || !swapChain)
        {
            std::cout << "[kDX11Driver] CreateSwapChain failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }

        // Remember the back-buffer size so resizeSwapChain() can ignore the
        // window sizes it is called with every frame.
        swapChainWidth  = width;
        swapChainHeight = height;

        // --- Create back-buffer views ----------------------------------------
        if (!createBackBufferResources())
            return false;

        // --- Default pipeline state ------------------------------------------
        // Create default states; actual values are applied via applyPipelineState()
        D3D11_DEPTH_STENCIL_DESC dsDesc = {};
        dsDesc.DepthEnable    = TRUE;
        dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        dsDesc.DepthFunc      = D3D11_COMPARISON_LESS;
        d3dDevice->CreateDepthStencilState(&dsDesc, &depthStencilState);

        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable           = FALSE;
        blendDesc.RenderTarget[0].SrcBlend              = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend             = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        d3dDevice->CreateBlendState(&blendDesc, &blendState);

        D3D11_RASTERIZER_DESC rsDesc = {};
        rsDesc.FillMode              = D3D11_FILL_SOLID;
        rsDesc.CullMode              = D3D11_CULL_BACK;
        rsDesc.FrontCounterClockwise = FALSE; // CCW = front
        rsDesc.DepthClipEnable       = TRUE;
        d3dDevice->CreateRasterizerState(&rsDesc, &rasterizerState);

        // Apply initial state
        depthStencilDirty = true;
        blendDirty        = true;
        rasterizerDirty   = true;

        // Default viewport
        currentViewport.TopLeftX = 0;
        currentViewport.TopLeftY = 0;
        currentViewport.Width    = (FLOAT)width;
        currentViewport.Height   = (FLOAT)height;
        currentViewport.MinDepth = 0.0f;
        currentViewport.MaxDepth = 1.0f;
        d3dContext->RSSetViewports(1, &currentViewport);

        return true;
    }

    void kDX11Driver::destroy()
    {
        releaseBackBufferResources();

        // Delete all programs
        programs.clear();

        // Delete all VAOs
        for (auto &kv : vertexArrays)
        {
            if (kv.second.inputLayout) kv.second.inputLayout->Release();
            if (kv.second.indexBuffer) kv.second.indexBuffer->Release();
            for (auto &vb : kv.second.vertexBuffers)
                if (vb.buffer) vb.buffer->Release();
        }
        vertexArrays.clear();

        // Delete all buffers
        for (auto &kv : buffers)
            if (kv.second) kv.second->Release();
        buffers.clear();

        // Delete all textures
        for (auto &kv : textures)
        {
            if (kv.second.texture) kv.second.texture->Release();
            if (kv.second.srv)     kv.second.srv->Release();
            if (kv.second.sampler) kv.second.sampler->Release();
        }
        textures.clear();

        // Delete all FBOs
        for (auto &kv : framebuffers)
        {
            for (auto *rtv : kv.second.colorRTVs) if (rtv) rtv->Release();
            if (kv.second.depthDSV) kv.second.depthDSV->Release();
            for (auto *tex : kv.second.colorTextures) if (tex) tex->Release();
            if (kv.second.depthTexture) kv.second.depthTexture->Release();
            for (auto *srv : kv.second.colorSRVs) if (srv) srv->Release();
        }
        framebuffers.clear();

        bufferShadows.clear();

        // Release pipeline states
        if (depthStencilState) { depthStencilState->Release(); depthStencilState = nullptr; }
        if (blendState)        { blendState->Release();        blendState        = nullptr; }
        if (rasterizerState)   { rasterizerState->Release();   rasterizerState   = nullptr; }

        // Drop every cached binding: the handles above are gone, so keeping the
        // cached state would make a later init() draw from dangling resources.
        currentProgram       = 0;
        currentVAO           = 0;
        currentFBO           = 0;
        currentReadFBO       = 0;
        currentArrayBufferId = 0;
        swapChainWidth       = 0;
        swapChainHeight      = 0;
        deviceRemoved        = false;
        memset(currentRTVs, 0, sizeof(currentRTVs));
        memset(boundTextures, 0, sizeof(boundTextures));
        currentDSV = nullptr;
        currentViewport = {};
        depthStencilDirty = true;
        blendDirty        = true;
        rasterizerDirty   = true;

        if (swapChain)  { swapChain->Release();  swapChain  = nullptr; }
        if (d3dContext) { d3dContext->Release(); d3dContext = nullptr; }
        if (d3dDevice)  { d3dDevice->Release();  d3dDevice  = nullptr; }
    }

    void *kDX11Driver::getNativeContext()
    {
        return d3dDevice;
    }

    void kDX11Driver::makeCurrent(kWindow * /*window*/)
    {
        // D3D11 uses an immediate context that is always current on the
        // device; no explicit context-switching API exists.  This override
        // satisfies the kDriver pure-virtual interface without side effects.
    }

    kString kDX11Driver::getApiVersion()
    {
        if (!d3dDevice) return "DirectX 11 (no device)";

        char buf[128];
        snprintf(buf, sizeof(buf), "Direct3D 11 Feature Level 0x%04x", (unsigned)featureLevel);
        return kString(buf);
    }

    kString kDX11Driver::getShaderVersion()
    {
        // D3D11 uses HLSL Shader Model 5.0
        return "HLSL Shader Model 5.0";
    }

    int kDX11Driver::getTextureUnitForSampler(uint32_t progId, const kString &name)
    {
        auto it = programs.find(progId);
        if (it == programs.end())
            return -1;

        // Exact sampler name (e.g. "albedoMap").
        auto s = it->second.samplers.find(name);
        if (s != it->second.samplers.end())
            return (int)s->second;

        // Array element (e.g. "u_AlbedoMap[2]"): the elements occupy consecutive
        // registers, so the base register plus the index is the unit to bind to.
        const size_t open = name.find('[');
        if (open != kString::npos && name.back() == ']')
        {
            const kString base = name.substr(0, open);
            auto b = it->second.samplers.find(base);
            if (b != it->second.samplers.end())
            {
                const int index = std::atoi(name.c_str() + open + 1);
                if (index >= 0)
                    return (int)b->second + index;
            }
        }

        return -1;
    }

    kRendererType kDX11Driver::getRendererType() const
    {
        // Lets callers load backend-specific asset variants (HLSL rather than
        // GLSL shader sources).
        return kRendererType::RENDERER_D3D11;
    }

    void *kDX11Driver::getImTextureID(uint32_t id)
    {
        // ImGui's DX11 backend dereferences this as an ID3D11ShaderResourceView*,
        // so an unknown texture must yield nullptr rather than a bogus handle.
        auto it = textures.find(id);
        if (it != textures.end() && it->second.srv)
            return static_cast<void *>(it->second.srv);
        return nullptr;
    }

    void kDX11Driver::swapBuffers()
    {
        if (!swapChain)
            return;

        const HRESULT hr = swapChain->Present(1, 0); // VSync on

        // A minimised or fully occluded window is not an error: the frame just
        // is not shown.  Present() keeps reporting this until the window comes
        // back, so return quietly instead of spamming the log.
        if (hr == DXGI_STATUS_OCCLUDED)
            return;

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            if (!deviceRemoved)
            {
                deviceRemoved = true;
                const HRESULT reason = d3dDevice ? d3dDevice->GetDeviceRemovedReason() : hr;
                std::cout << "[kDX11Driver] Device removed/reset: 0x"
                          << std::hex << hr << " (reason 0x" << reason << std::dec
                          << "). Rendering is suspended; recreate the renderer to recover."
                          << std::endl;
            }
            return;
        }

        if (FAILED(hr))
            std::cout << "[kDX11Driver] Present failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
    }

    void kDX11Driver::resizeSwapChain(int width, int height)
    {
        if (!swapChain || !d3dContext || width <= 0 || height <= 0)
            return;
        if (width == swapChainWidth && height == swapChainHeight)
            return;
        if (deviceRemoved)
            return;

        // ResizeBuffers() requires every reference to the existing back buffer to
        // be gone, so unbind and release the render-target/depth views first.
        d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
        releaseBackBufferResources();

        // The references the engine holds through the input-assembler stages do
        // not touch the swap chain, but clearing them keeps the cached state from
        // claiming bindings that no longer exist after the resize.
        memset(currentRTVs, 0, sizeof(currentRTVs));
        currentDSV = nullptr;

        HRESULT hr = swapChain->ResizeBuffers(
            0, (UINT)width, (UINT)height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] ResizeBuffers failed: 0x"
                      << std::hex << hr << std::dec
                      << " (back buffer left at " << swapChainWidth << "x"
                      << swapChainHeight << ")" << std::endl;
            // Try to restore a usable state with the old size.
            createBackBufferResources();
            return;
        }

        swapChainWidth  = width;
        swapChainHeight = height;

        if (!createBackBufferResources())
        {
            std::cout << "[kDX11Driver] Failed to recreate back-buffer views after resize."
                      << std::endl;
            return;
        }

        const D3D11_VIEWPORT vp = { 0.0f, 0.0f, (FLOAT)width, (FLOAT)height, 0.0f, 1.0f };
        currentViewport = vp;
        d3dContext->RSSetViewports(1, &currentViewport);

        // Re-apply cached pipeline state on the next draw — the depth/blend/
        // rasteriser objects themselves survive a resize, but the dirty flags make
        // sure the context is in the state the cache believes it is in.
        depthStencilDirty = true;
        blendDirty        = true;
        rasterizerDirty   = true;
    }

    // =========================================================================
    // Frame state
    // =========================================================================

    void kDX11Driver::setClearColor(float r, float g, float b, float a)
    {
        clearColor[0] = r;
        clearColor[1] = g;
        clearColor[2] = b;
        clearColor[3] = a;
    }

    void kDX11Driver::clear(bool color, bool depth, bool stencil)
    {
        // Determine the active RTVs and DSV
        ID3D11RenderTargetView **rtvs = nullptr;
        UINT rtvCount = 0;
        ID3D11DepthStencilView *dsv = nullptr;

        if (currentFBO == 0)
        {
            if (backBufferRTV)
            {
                rtvs     = currentRTVs;
                rtvs[0]  = backBufferRTV;
                rtvCount = 1;
            }
            dsv = backBufferDSV;
        }
        else
        {
            auto it = framebuffers.find(currentFBO);
            if (it != framebuffers.end())
            {
                if (!it->second.colorRTVs.empty())
                {
                    rtvs     = it->second.colorRTVs.data();
                    rtvCount = (UINT)it->second.colorRTVs.size();
                }
                dsv = it->second.depthDSV;
            }
        }

        if (color)
        {
            // Every colour attachment of the target must be cleared, not just
            // the first one (MRT targets).
            for (UINT i = 0; i < rtvCount; ++i)
                if (rtvs[i])
                    d3dContext->ClearRenderTargetView(rtvs[i], clearColor);
        }

        if (dsv)
        {
            UINT clearFlags = 0;
            if (depth)   clearFlags |= D3D11_CLEAR_DEPTH;
            if (stencil) clearFlags |= D3D11_CLEAR_STENCIL;
            if (clearFlags)
                d3dContext->ClearDepthStencilView(dsv, clearFlags, 1.0f, 0);
        }
    }

    void kDX11Driver::setViewport(int x, int y, int width, int height)
    {
        currentViewport.TopLeftX = (FLOAT)x;
        currentViewport.TopLeftY = (FLOAT)y;
        currentViewport.Width    = (FLOAT)width;
        currentViewport.Height   = (FLOAT)height;
        d3dContext->RSSetViewports(1, &currentViewport);
    }

    // =========================================================================
    // Pipeline state
    // =========================================================================

    void kDX11Driver::setDepthTest(bool enable)
    {
        if (depthTestEnabled != enable)
        {
            depthTestEnabled = enable;
            depthStencilDirty = true;
        }
    }

    void kDX11Driver::setDepthWrite(bool enable)
    {
        if (depthWriteEnabled != enable)
        {
            depthWriteEnabled = enable;
            depthStencilDirty = true;
        }
    }

    void kDX11Driver::setBlend(bool enable)
    {
        if (blendEnabled != enable)
        {
            blendEnabled = enable;
            blendDirty = true;
        }
    }

    void kDX11Driver::setBlendFunc(kBlendFactor src, kBlendFactor dst)
    {
        if (blendSrc != src || blendDst != dst)
        {
            blendSrc = src;
            blendDst = dst;
            blendDirty = true;
        }
    }

    void kDX11Driver::setCullFace(bool enable)
    {
        if (cullFaceEnabled != enable)
        {
            cullFaceEnabled = enable;
            rasterizerDirty = true;
        }
    }

    void kDX11Driver::setCullMode(kCullMode mode)
    {
        if (cullMode != mode)
        {
            cullMode = mode;
            rasterizerDirty = true;
        }
    }

    void kDX11Driver::setFrontFace(kFrontFace face)
    {
        if (frontFace != face)
        {
            frontFace = face;
            rasterizerDirty = true;
        }
    }

    void kDX11Driver::setMultisample(bool enable)
    {
        if (msaaEnabled == enable)
            return;

        msaaEnabled = enable;

        // D3D11 configures multisampling on the resource at creation time (the FBO
        // colour/depth textures), and RasterizerState.MultisampleEnable selects
        // whether a multisampled target is resolved per sample or per pixel — so
        // the rasteriser state has to be re-applied when the flag flips.
        rasterizerDirty = true;
    }

    void kDX11Driver::setSRGBEncoding(bool enable)
    {
        if (srgbEnabled == enable)
            return;

        srgbEnabled = enable;

        // D3D11 does sRGB encode/decode through the resource format (_SRGB vs
        // _UNORM), not through bindable state; there is no glEnable(GL_FRAMEBUFFER_SRGB)
        // equivalent.  The back buffer deliberately stays UNORM, because ImGui's
        // DX11 backend and the engine's screen shader (which applies gamma itself)
        // both expect that, while FBO colour textures are created with whichever
        // format this flag selects.  Textures created before the change keep the
        // format they were created with.
    }

    void kDX11Driver::setSampleAlphaToCoverage(bool enable)
    {
        alphaToCoverage = enable;
        // D3D11 handles alpha-to-coverage via BlendState.AlphaToCoverageEnable.
        blendDirty = true;
    }

    void kDX11Driver::setWireframe(bool enable)
    {
        if (wireframeEnabled != enable)
        {
            wireframeEnabled = enable;
            rasterizerDirty = true;
        }
    }

    // =========================================================================
    // Shader programs
    // =========================================================================

    ID3DBlob *kDX11Driver::compileHlslStage(const char *src, const char *entryPoint,
                                             const char *target)
    {
        if (!src || !src[0])
            return nullptr;

        ID3DBlob *codeBlob = nullptr;
        ID3DBlob *errorBlob = nullptr;

        HRESULT hr = D3DCompile(
            src, strlen(src), nullptr, // source name (optional)
            nullptr,                   // defines
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint, target,
            D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR,
            0, &codeBlob, &errorBlob);

        if (FAILED(hr))
        {
            if (errorBlob)
            {
                std::cout << "[kDX11Driver] HLSL compile error (" << target << "): "
                          << (const char *)errorBlob->GetBufferPointer() << std::endl;
                errorBlob->Release();
            }
            else
            {
                std::cout << "[kDX11Driver] HLSL compile error (" << target
                          << "): HRESULT 0x" << std::hex << hr << std::dec << std::endl;
            }
            return nullptr;
        }

        if (errorBlob) errorBlob->Release();
        return codeBlob;
    }

    /// @brief Copies a reflected HLSL type into the compact tree used for uniform
    ///        path resolution (the reflection object is released after compile).
    static void copyTypeInfo(ID3D11ShaderReflectionType *type, D3D11TypeInfo &out)
    {
        if (!type)
            return;

        D3D11_SHADER_TYPE_DESC d = {};
        if (FAILED(type->GetDesc(&d)))
            return;

        switch (d.Class)
        {
        case D3D_SVC_SCALAR:         out.kind = D3D11TypeKind::Scalar; break;
        case D3D_SVC_VECTOR:         out.kind = D3D11TypeKind::Vector; break;
        case D3D_SVC_MATRIX_ROWS:
        case D3D_SVC_MATRIX_COLUMNS: out.kind = D3D11TypeKind::Matrix; break;
        case D3D_SVC_STRUCT:         out.kind = D3D11TypeKind::Struct; break;
        default:                     out.kind = D3D11TypeKind::Other;  break;
        }

        out.rows     = (d.Rows    > 0) ? d.Rows    : 1;
        out.columns  = (d.Columns > 0) ? d.Columns : 1;
        out.elements = (d.Elements > 1) ? d.Elements : 0;

        if (out.kind == D3D11TypeKind::Struct)
        {
            out.memberNames.reserve(d.Members);
            out.memberTypes.reserve(d.Members);
            for (UINT i = 0; i < d.Members; ++i)
            {
                const char *memberName = type->GetMemberTypeName(i);
                out.memberNames.push_back(kString(memberName ? memberName : ""));
                D3D11TypeInfo child;
                copyTypeInfo(type->GetMemberTypeByIndex(i), child);
                out.memberTypes.push_back(child);
            }
        }
    }

    void kDX11Driver::reflectConstantBuffers(ID3DBlob *vsBlob, ID3DBlob *psBlob,
                                              D3D11ProgramData &prog)
    {
        auto reflectStage = [&](ID3DBlob *blob, ID3D11ShaderReflection *reflector)
        {
            D3D11_SHADER_DESC shaderDesc;
            reflector->GetDesc(&shaderDesc);

            // Sampler registers are fixed at compile time in HLSL, unlike GL's
            // dynamic texture units, so remember where each texture landed and let
            // the renderer bind to that unit (getTextureUnitForSampler()).
            for (UINT r = 0; r < shaderDesc.BoundResources; ++r)
            {
                D3D11_SHADER_INPUT_BIND_DESC bindDesc = {};
                if (FAILED(reflector->GetResourceBindingDesc(r, &bindDesc)))
                    continue;
                if (bindDesc.Type == D3D_SIT_TEXTURE)
                    prog.samplers[kString(bindDesc.Name)] = bindDesc.BindPoint;
            }

            for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i)
            {
                ID3D11ShaderReflectionConstantBuffer *cb =
                    reflector->GetConstantBufferByIndex(i);

                D3D11_SHADER_BUFFER_DESC cbDesc;
                cb->GetDesc(&cbDesc);

                // Only process bound constant buffers (skip $Globals etc. if not bound)
                if (cbDesc.Type != D3D_CT_CBUFFER)
                    continue;

                // Determine register slot from the bind point
                D3D11_SHADER_INPUT_BIND_DESC bindDesc;
                // Iterate resources to find this CB's bind point
                for (UINT r = 0; r < shaderDesc.BoundResources; ++r)
                {
                    reflector->GetResourceBindingDesc(r, &bindDesc);
                    if (bindDesc.Type == D3D_SIT_CBUFFER &&
                        strcmp(bindDesc.Name, cbDesc.Name) == 0)
                    {
                        uint32_t slot = bindDesc.BindPoint;

                        // Ensure CB exists
                        if (prog.cbSizes.find(slot) == prog.cbSizes.end() ||
                            cbDesc.Size > prog.cbSizes[slot])
                        {
                            prog.cbSizes[slot] = cbDesc.Size;
                            prog.cbShadows[slot].resize(cbDesc.Size, 0);
                            prog.cbDirty[slot] = false;
                        }

                        // Enumerate variables
                        for (UINT v = 0; v < cbDesc.Variables; ++v)
                        {
                            ID3D11ShaderReflectionVariable *var =
                                cb->GetVariableByIndex(v);

                            D3D11_SHADER_VARIABLE_DESC varDesc;
                            var->GetDesc(&varDesc);

                            D3D11UniformInfo info;
                            info.cbSlot = slot;
                            info.offset = varDesc.StartOffset;
                            info.size   = varDesc.Size;
                            copyTypeInfo(var->GetType(), info.type);

                            prog.uniforms[kString(varDesc.Name)] = info;
                        }
                        break;
                    }
                }
            }
        };

        if (vsBlob)
        {
            ID3D11ShaderReflection *vsRefl = nullptr;
            D3DReflect(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                       IID_ID3D11ShaderReflection, (void **)&vsRefl);
            if (vsRefl)
            {
                reflectStage(vsBlob, vsRefl);
                vsRefl->Release();
            }
        }

        if (psBlob)
        {
            ID3D11ShaderReflection *psRefl = nullptr;
            D3DReflect(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                       IID_ID3D11ShaderReflection, (void **)&psRefl);
            if (psRefl)
            {
                reflectStage(psBlob, psRefl);
                psRefl->Release();
            }
        }
    }

    void kDX11Driver::attribSemantic(int location, const char *&name, UINT &index)
    {
        // Convention: GL attribute locations map to these fixed D3D11 semantics.
        // Any HLSL/translated vertex shader fed to this driver must declare the
        // matching semantic for each location.
        //   0=POSITION 1=COLOR 2=TEXCOORD 3=NORMAL 4=TANGENT
        //   5=BINORMAL 6=BLENDINDICES 7=BLENDWEIGHT
        switch (location)
        {
        case 0: name = "POSITION";     index = 0; break;
        case 1: name = "COLOR";        index = 0; break;
        case 2: name = "TEXCOORD";     index = 0; break;
        case 3: name = "NORMAL";       index = 0; break;
        case 4: name = "TANGENT";      index = 0; break;
        case 5: name = "BINORMAL";     index = 0; break;
        case 6: name = "BLENDINDICES"; index = 0; break;
        case 7: name = "BLENDWEIGHT";  index = 0; break;
        default:
            name  = "TEXCOORD";
            index = (UINT)((location > 2) ? (location - 2) : 0);
            break;
        }
    }

    /// Spare input-assembler slot used for vertex shader inputs the mesh does not
    /// provide.  A null buffer is bound there (see bindVAOState), so those reads
    /// return zero — which is exactly what the GL path produces for an attribute
    /// array that was never enabled.
    static const UINT kDummyVertexInputSlot = 15;

    /// @brief Picks a DXGI format matching a reflected shader input.
    static DXGI_FORMAT formatForSignature(const D3D11_SIGNATURE_PARAMETER_DESC &param)
    {
        UINT components = 0;
        for (UINT mask = param.Mask; mask != 0; mask >>= 1)
            components += (mask & 1u);
        if (components == 0)
            components = 1;

        switch (param.ComponentType)
        {
        case D3D_REGISTER_COMPONENT_SINT32:
            switch (components)
            {
            case 1:  return DXGI_FORMAT_R32_SINT;
            case 2:  return DXGI_FORMAT_R32G32_SINT;
            case 3:  return DXGI_FORMAT_R32G32B32_SINT;
            default: return DXGI_FORMAT_R32G32B32A32_SINT;
            }
        case D3D_REGISTER_COMPONENT_UINT32:
            switch (components)
            {
            case 1:  return DXGI_FORMAT_R32_UINT;
            case 2:  return DXGI_FORMAT_R32G32_UINT;
            case 3:  return DXGI_FORMAT_R32G32B32_UINT;
            default: return DXGI_FORMAT_R32G32B32A32_UINT;
            }
        default:
            switch (components)
            {
            case 1:  return DXGI_FORMAT_R32_FLOAT;
            case 2:  return DXGI_FORMAT_R32G32_FLOAT;
            case 3:  return DXGI_FORMAT_R32G32B32_FLOAT;
            default: return DXGI_FORMAT_R32G32B32A32_FLOAT;
            }
        }
    }

    ID3D11InputLayout *kDX11Driver::createInputLayout(
        ID3DBlob *vsBlob, const std::vector<D3D11AttribDesc> &attribs)
    {
        if (!vsBlob || attribs.empty())
            return nullptr;

        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        elements.reserve(attribs.size());

        for (const auto &a : attribs)
        {
            D3D11_INPUT_ELEMENT_DESC elem = {};
            attribSemantic(a.location, elem.SemanticName, elem.SemanticIndex);

            switch (a.components)
            {
            case 1:
                elem.Format = a.isInteger ? DXGI_FORMAT_R32_SINT : DXGI_FORMAT_R32_FLOAT;
                break;
            case 2:
                elem.Format = a.isInteger ? DXGI_FORMAT_R32G32_SINT : DXGI_FORMAT_R32G32_FLOAT;
                break;
            case 3:
                elem.Format = a.isInteger ? DXGI_FORMAT_R32G32B32_SINT : DXGI_FORMAT_R32G32B32_FLOAT;
                break;
            default:
                elem.Format = a.isInteger ? DXGI_FORMAT_R32G32B32A32_SINT
                                          : DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            }

            // Each attribute reads from the vertex-buffer slot recorded for it by
            // setVertexAttrib*(), mirroring GL's per-buffer attribute pointers.
            elem.InputSlot         = a.slot;
            elem.AlignedByteOffset = (UINT)a.offset;
            if (a.divisor > 0)
            {
                elem.InputSlotClass       = D3D11_INPUT_PER_INSTANCE_DATA;
                elem.InstanceDataStepRate = a.divisor;
            }
            else
            {
                elem.InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
                elem.InstanceDataStepRate = 0;
            }

            elements.push_back(elem);
        }

        // D3D11 requires the layout to cover every input the vertex shader
        // consumes, whereas GLSL is content with attributes that were never
        // enabled.  Meshes frequently lack a stream (no vertex colours, no
        // tangents, no skinning data), which would otherwise make
        // CreateInputLayout fail outright and the mesh not draw at all, so the
        // remaining signature entries are appended on a spare slot.
        std::vector<kString> dummySemantics;
        ID3D11ShaderReflection *vsRefl = nullptr;
        if (SUCCEEDED(D3DReflect(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                 IID_ID3D11ShaderReflection, (void **)&vsRefl)) && vsRefl)
        {
            D3D11_SHADER_DESC shaderDesc = {};
            vsRefl->GetDesc(&shaderDesc);

            // Reserved so the stored semantic strings keep stable addresses.
            dummySemantics.reserve(shaderDesc.InputParameters);

            for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
            {
                D3D11_SIGNATURE_PARAMETER_DESC param = {};
                if (FAILED(vsRefl->GetInputParameterDesc(i, &param)))
                    continue;

                bool covered = false;
                for (const auto &element : elements)
                {
                    if (element.SemanticIndex == param.SemanticIndex &&
                        param.SemanticName != nullptr &&
                        strcmp(element.SemanticName, param.SemanticName) == 0)
                    {
                        covered = true;
                        break;
                    }
                }
                if (covered)
                    continue;

                dummySemantics.push_back(kString(param.SemanticName ? param.SemanticName : ""));

                D3D11_INPUT_ELEMENT_DESC dummy = {};
                dummy.SemanticName         = dummySemantics.back().c_str();
                dummy.SemanticIndex        = param.SemanticIndex;
                dummy.Format               = formatForSignature(param);
                dummy.InputSlot            = kDummyVertexInputSlot;
                dummy.AlignedByteOffset    = 0;
                dummy.InputSlotClass       = D3D11_INPUT_PER_VERTEX_DATA;
                dummy.InstanceDataStepRate = 0;
                elements.push_back(dummy);
            }

            vsRefl->Release();
        }

        ID3D11InputLayout *layout = nullptr;
        HRESULT hr = d3dDevice->CreateInputLayout(
            elements.data(), (UINT)elements.size(),
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            &layout);

        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] CreateInputLayout failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
        }

        return layout;
    }

    void kDX11Driver::flushConstantBuffers(D3D11ProgramData &prog)
    {
        // Iterate the reflected slot list rather than the dirty flags so every
        // constant buffer is (re-)bound to the pipeline on every draw.  Binding
        // only dirty slots left a stale binding behind after a program switch
        // (A -> B -> A made A read B's constant buffer, because A's dirty flags
        // had already been cleared).
        for (auto &slotKv : prog.cbSizes)
        {
            const uint32_t slot   = slotKv.first;
            const uint32_t cbSize = slotKv.second;
            if (cbSize == 0)
                continue;

            ID3D11Buffer *cb = nullptr;
            auto it = prog.constantBuffers.find(slot);
            if (it != prog.constantBuffers.end())
                cb = it->second;

            // Create the constant buffer lazily, sized from reflection data.
            if (!cb)
            {
                D3D11_BUFFER_DESC cbDesc = {};
                cbDesc.ByteWidth      = cbSize;
                cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
                cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
                cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

                HRESULT hr = d3dDevice->CreateBuffer(&cbDesc, nullptr, &cb);
                if (FAILED(hr))
                {
                    std::cout << "[kDX11Driver] Failed to create CB slot " << slot << std::endl;
                    prog.cbDirty[slot] = false;
                    continue;
                }
                prog.constantBuffers[slot] = cb;
            }

            // Only re-upload when a uniform actually changed.
            if (prog.cbDirty[slot])
            {
                std::vector<uint8_t> &shadow = prog.cbShadows[slot];
                if (shadow.size() < cbSize)
                    shadow.resize(cbSize, 0);

                D3D11_MAPPED_SUBRESOURCE mapped;
                HRESULT hr = d3dContext->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                if (SUCCEEDED(hr))
                {
                    memcpy(mapped.pData, shadow.data(), cbSize);
                    d3dContext->Unmap(cb, 0);
                    prog.cbDirty[slot] = false;
                }
            }

            d3dContext->VSSetConstantBuffers(slot, 1, &cb);
            d3dContext->PSSetConstantBuffers(slot, 1, &cb);
        }
    }

    /// @brief Heuristic: does this source look like GLSL rather than HLSL?
    static bool looksLikeGlsl(const char *src)
    {
        if (!src || !src[0])
            return false;

        return strstr(src, "#version") != nullptr ||
               strstr(src, "gl_Position") != nullptr ||
               strstr(src, "gl_FragColor") != nullptr ||
               strstr(src, "gl_FragCoord") != nullptr;
    }

    uint32_t kDX11Driver::compileShaderProgram(const char *vertSrc, const char *fragSrc)
    {
        if (!d3dDevice)
            return 0;

        // Shader source is supplied by the caller and is never translated by the
        // engine: the OpenGL backends take GLSL, this backend takes HLSL and hands
        // it to D3DCompile as-is.  GLSL that reaches this point is reported once
        // instead of being compiled as HLSL, which would only produce a wall of
        // parse errors — pass HLSL for D3D11 builds instead
        // (kShader::loadHlslCodeDX11 / loadHlslFileDX11).
        if (looksLikeGlsl(vertSrc) || looksLikeGlsl(fragSrc))
        {
            std::cout << "[kDX11Driver] GLSL source was handed to the D3D11 backend, which "
                         "compiles HLSL (vs_5_0 / ps_5_0, entry points VSMain / PSMain). "
                         "Supply HLSL for D3D11 builds and keep the GLSL for the OpenGL "
                         "backends. No program was created." << std::endl;
            return 0;
        }

        // Compile vertex shader
        ID3DBlob *vsBlob = compileHlslStage(vertSrc, "VSMain", "vs_5_0");
        // If VSMain entry fails, try "main"
        if (!vsBlob && vertSrc && vertSrc[0])
            vsBlob = compileHlslStage(vertSrc, "main", "vs_5_0");

        // Compile pixel shader
        ID3DBlob *psBlob = nullptr;
        if (fragSrc && fragSrc[0])
        {
            psBlob = compileHlslStage(fragSrc, "PSMain", "ps_5_0");
            if (!psBlob)
                psBlob = compileHlslStage(fragSrc, "main", "ps_5_0");
        }

        if (!vsBlob && !psBlob)
            return 0;

        D3D11ProgramData prog;

        // Create vertex shader
        if (vsBlob)
        {
            HRESULT hr = d3dDevice->CreateVertexShader(
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                nullptr, &prog.vs);
            if (FAILED(hr))
            {
                std::cout << "[kDX11Driver] CreateVertexShader failed: 0x"
                          << std::hex << hr << std::dec << std::endl;
                vsBlob->Release();
                if (psBlob) psBlob->Release();
                return 0;
            }
        }

        // Create pixel shader
        if (psBlob)
        {
            HRESULT hr = d3dDevice->CreatePixelShader(
                psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                nullptr, &prog.ps);
            if (FAILED(hr))
            {
                std::cout << "[kDX11Driver] CreatePixelShader failed: 0x"
                          << std::hex << hr << std::dec << std::endl;
                if (vsBlob) vsBlob->Release();
                psBlob->Release();
                return 0;
            }
        }

        // Reflect constant buffers
        reflectConstantBuffers(vsBlob, psBlob, prog);

        // The vertex-shader byte code has to outlive compilation: D3D11 needs the
        // VS input signature to create input layouts, and those are built lazily
        // on the first draw.  Ownership is transferred to the program.
        prog.vsBlob = vsBlob;
        if (psBlob) psBlob->Release();

        uint32_t id = nextProgramId++;
        programs[id] = std::move(prog);
        return id;
    }

    uint32_t kDX11Driver::compileShaderProgramSpirv(const std::vector<uint8_t> &vertSpirv,
                                                     const kString &vertEntry,
                                                     const std::vector<uint8_t> &fragSpirv,
                                                     const kString &fragEntry)
    {
        std::cout << "[kDX11Driver] SPIR-V shaders are not supported on D3D11." << std::endl;
        return 0;
    }

    void kDX11Driver::deleteShaderProgram(uint32_t id)
    {
        programs.erase(id);
    }

    void kDX11Driver::bindShaderProgram(uint32_t id)
    {
        currentProgram = id;

        auto it = programs.find(id);
        if (it == programs.end())
            return;

        D3D11ProgramData &prog = it->second;
        d3dContext->VSSetShader(prog.vs, nullptr, 0);
        d3dContext->PSSetShader(prog.ps, nullptr, 0);
        // Deliberately leaves the input layout alone: it is bound per VAO by
        // bindVAOState() right before each draw, and clearing it here would
        // invalidate the layout that draw call is about to need.
    }

    void kDX11Driver::unbindShaderProgram()
    {
        currentProgram = 0;
        d3dContext->VSSetShader(nullptr, nullptr, 0);
        d3dContext->PSSetShader(nullptr, nullptr, 0);
        d3dContext->IASetInputLayout(nullptr);
    }

    // =========================================================================
    // Uniform helpers
    // =========================================================================

    // -------------------------------------------------------------------------
    // Uniform path resolution
    //
    // D3DReflect reports only the top-level variables of a constant buffer, while
    // the engine addresses uniforms the way GL does — "material.diffuse",
    // "u_Tiling[2]", "sunLights[3].position".  The reflected type tree is captured
    // at compile time (copyTypeInfo) and member/element offsets are recomputed here
    // with HLSL constant-buffer packing rules, because D3DReflect does not expose
    // them:
    //   * scalars/vectors are 4-byte aligned and pushed to the next 16-byte
    //     boundary when they would straddle one;
    //   * arrays, matrices and structs (and every element inside them) start on
    //     16-byte boundaries.
    // -------------------------------------------------------------------------

    static uint32_t alignUp(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) / alignment * alignment;
    }

    uint32_t kDX11Driver::packedTypeSize(const D3D11TypeInfo &type)
    {
        if (type.kind == D3D11TypeKind::Struct)
        {
            uint32_t offset = 0;
            for (size_t i = 0; i < type.memberTypes.size(); ++i)
            {
                uint32_t memberSize = 0;
                offset = memberOffset(type, i, memberSize) + memberSize;
            }
            return alignUp(offset, 16);
        }

        if (type.kind == D3D11TypeKind::Matrix)
            return 16u * type.rows;      // one 16-byte row per matrix row

        return 4u * type.columns;        // scalar (1 column) or vector
    }

    uint32_t kDX11Driver::memberOffset(const D3D11TypeInfo &type, size_t index,
                                       uint32_t &outSize)
    {
        uint32_t offset = 0;

        for (size_t i = 0; i < type.memberTypes.size() && i <= index; ++i)
        {
            const D3D11TypeInfo &member = type.memberTypes[i];
            const uint32_t elementSize = packedTypeSize(member);
            const bool     isArray     = (member.elements > 0);
            const uint32_t total = isArray
                                       ? alignUp(elementSize, 16) * member.elements
                                       : elementSize;

            const bool needs16 = isArray ||
                                 member.kind == D3D11TypeKind::Struct ||
                                 member.kind == D3D11TypeKind::Matrix;

            if (needs16)
                offset = alignUp(offset, 16);
            else if ((offset % 16) + total > 16)
                offset = alignUp(offset, 16);

            if (i == index)
            {
                outSize = total;
                return offset;
            }

            offset += total;
        }

        outSize = 0;
        return 0;
    }

    bool kDX11Driver::resolveUniformPath(D3D11ProgramData &prog, const kString &name,
                                         uint32_t &outCbSlot, uint32_t &outOffset,
                                         uint32_t &outSize)
    {
        // Fast path: a flat, top-level uniform such as "viewMatrix".
        auto it = prog.uniforms.find(name);
        if (it != prog.uniforms.end())
        {
            outCbSlot = it->second.cbSlot;
            outOffset = it->second.offset;
            outSize   = it->second.size;
            return true;
        }

        // Otherwise walk "base.member[index].member…" against the type tree.
        const size_t firstBreak = name.find_first_of(".[");
        if (firstBreak == kString::npos)
            return false;

        const kString base = name.substr(0, firstBreak);
        auto baseIt = prog.uniforms.find(base);
        if (baseIt == prog.uniforms.end())
            return false;

        const D3D11TypeInfo *type = &baseIt->second.type;
        uint32_t offset = baseIt->second.offset;
        uint32_t size   = baseIt->second.size;

        size_t pos = firstBreak;
        while (pos < name.size())
        {
            if (name[pos] == '[')
            {
                const size_t close = name.find(']', pos);
                if (close == kString::npos || type->elements == 0)
                    return false;

                const int index = std::atoi(name.substr(pos + 1, close - pos - 1).c_str());
                if (index < 0 || (uint32_t)index >= type->elements)
                    return false;

                const uint32_t elementSize = packedTypeSize(*type);
                offset += (uint32_t)index * alignUp(elementSize, 16);
                size    = elementSize;
                pos     = close + 1;
            }
            else if (name[pos] == '.')
            {
                if (type->kind != D3D11TypeKind::Struct)
                    return false;

                const size_t next = name.find_first_of(".[", pos + 1);
                const kString member = name.substr(
                    pos + 1, (next == kString::npos) ? kString::npos : next - pos - 1);

                size_t memberIndex = type->memberNames.size();
                for (size_t i = 0; i < type->memberNames.size(); ++i)
                {
                    if (type->memberNames[i] == member)
                    {
                        memberIndex = i;
                        break;
                    }
                }
                if (memberIndex >= type->memberNames.size())
                    return false;

                uint32_t memberSize = 0;
                offset += memberOffset(*type, memberIndex, memberSize);
                size    = memberSize;
                type    = &type->memberTypes[memberIndex];
                pos     = (next == kString::npos) ? name.size() : next;
            }
            else
            {
                return false;
            }
        }

        outCbSlot = baseIt->second.cbSlot;
        outOffset = offset;
        outSize   = size;
        return true;
    }

    void kDX11Driver::writeUniform(D3D11ProgramData &prog, const kString &name,
                                   const void *data, size_t size)
    {
        if (!data || size == 0)
            return;

        uint32_t cbSlot = 0, offset = 0, targetSize = 0;
        if (!resolveUniformPath(prog, name, cbSlot, offset, targetSize))
        {
            // Not present in this shader: optional uniforms and parameters the
            // shader does not declare are simply skipped, as GL does for a
            // failed glGetUniformLocation lookup.
            return;
        }

        auto shadowIt = prog.cbShadows.find(cbSlot);
        if (shadowIt == prog.cbShadows.end() || offset >= shadowIt->second.size())
            return;

        size_t copySize = (size < targetSize) ? size : targetSize;
        if (offset + copySize > shadowIt->second.size())
            copySize = shadowIt->second.size() - offset;

        memcpy(shadowIt->second.data() + offset, data, copySize);
        prog.cbDirty[cbSlot] = true;
    }

    void kDX11Driver::setUniformBool(uint32_t progId, const kString &name, bool v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        int iv = v ? 1 : 0;
        writeUniform(it->second, name, &iv, sizeof(int));
    }

    void kDX11Driver::setUniformInt(uint32_t progId, const kString &name, int v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, &v, sizeof(int));
    }

    void kDX11Driver::setUniformUint(uint32_t progId, const kString &name, uint32_t v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, &v, sizeof(uint32_t));
    }

    void kDX11Driver::setUniformFloat(uint32_t progId, const kString &name, float v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, &v, sizeof(float));
    }

    void kDX11Driver::setUniformVec2(uint32_t progId, const kString &name, const kVec2 &v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, glm::value_ptr(v), sizeof(kVec2));
    }

    void kDX11Driver::setUniformVec3(uint32_t progId, const kString &name, const kVec3 &v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, glm::value_ptr(v), sizeof(kVec3));
    }

    void kDX11Driver::setUniformVec4(uint32_t progId, const kString &name, const kVec4 &v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        writeUniform(it->second, name, glm::value_ptr(v), sizeof(kVec4));
    }

    void kDX11Driver::setUniformMat4(uint32_t progId, const kString &name, const kMat4 &v)
    {
        auto it = programs.find(progId);
        if (it == programs.end()) return;
        // GLM matrices are column-major; D3D11 HLSL defaults to column-major
        // with D3DCOMPILE_PACK_MATRIX_ROW_MAJOR we told the compiler to use
        // row-major.  However, we still need to match the layout the shader
        // expects.  Our HLSL shaders use row_major matrices.  GLM's memory
        // layout is column-major, so we need to transpose when uploading.
        kMat4 transposed = glm::transpose(v);
        writeUniform(it->second, name, glm::value_ptr(transposed), sizeof(kMat4));
    }

    void kDX11Driver::setUniformMat4Array(uint32_t progId, const kString &name,
                                           const std::vector<kMat4> &v)
    {
        auto it = programs.find(progId);
        if (it == programs.end() || v.empty()) return;

        // Array paths ("finalBoneMatrices", "lightSpaceMatrices") resolve through
        // the same path logic as scalar uniforms.
        uint32_t cbSlot = 0, offset = 0, elementSize = 0;
        if (!resolveUniformPath(it->second, name, cbSlot, offset, elementSize))
            return;

        auto shadowIt = it->second.cbShadows.find(cbSlot);
        if (shadowIt == it->second.cbShadows.end() || offset >= shadowIt->second.size())
            return;

        size_t totalSize = v.size() * sizeof(kMat4);
        if (offset + totalSize > shadowIt->second.size())
            totalSize = shadowIt->second.size() - offset;

        // Transpose each matrix before upload
        std::vector<kMat4> transposed(v.size());
        for (size_t i = 0; i < v.size(); ++i)
            transposed[i] = glm::transpose(v[i]);

        memcpy(shadowIt->second.data() + offset, transposed.data(), totalSize);
        it->second.cbDirty[cbSlot] = true;
    }

    // =========================================================================
    // Vertex arrays
    // =========================================================================

    uint32_t kDX11Driver::createVertexArray()
    {
        uint32_t id = nextVAOId++;
        vertexArrays[id] = D3D11VertexArrayData();
        return id;
    }

    void kDX11Driver::deleteVertexArray(uint32_t id)
    {
        auto it = vertexArrays.find(id);
        if (it != vertexArrays.end())
        {
            // The VAO owns references on the layout and on every buffer it was
            // configured with, so all of them must be released here.
            if (it->second.inputLayout) it->second.inputLayout->Release();
            if (it->second.indexBuffer) it->second.indexBuffer->Release();
            for (auto &vb : it->second.vertexBuffers)
                if (vb.buffer) vb.buffer->Release();
            it->second.vertexBuffers.clear();
            it->second.attribs.clear();
            vertexArrays.erase(it);
        }

        if (currentVAO == id)
            currentVAO = 0;
    }

    void kDX11Driver::bindVertexArray(uint32_t id)
    {
        currentVAO = id;
    }

    void kDX11Driver::unbindVertexArray()
    {
        currentVAO = 0;
    }

    // =========================================================================
    // Buffers
    // =========================================================================

    uint32_t kDX11Driver::createBuffer()
    {
        uint32_t id = nextBufferId++;
        buffers[id] = nullptr;
        return id;
    }

    void kDX11Driver::deleteBuffer(uint32_t id)
    {
        auto it = buffers.find(id);
        if (it != buffers.end())
        {
            if (it->second) it->second->Release();
            buffers.erase(it);
        }
    }

    void kDX11Driver::uploadIndexBuffer(uint32_t bufferId, const void *data, size_t size)
    {
        // Detach the previous buffer object, if any.
        ID3D11Buffer *oldBuf = nullptr;
        auto it = buffers.find(bufferId);
        if (it != buffers.end() && it->second)
        {
            oldBuf = it->second;
            it->second = nullptr;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = (UINT)size;
        // D3D11_USAGE_IMMUTABLE requires initial data.  A buffer uploaded with a
        // null pointer is going to be streamed later, so make it DYNAMIC.
        desc.Usage          = data ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_INDEX_BUFFER;
        desc.CPUAccessFlags = data ? 0 : D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = data;

        ID3D11Buffer *buf = nullptr;
        HRESULT hr = d3dDevice->CreateBuffer(&desc, data ? &initData : nullptr, &buf);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] uploadIndexBuffer failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return;
        }

        buffers[bufferId] = buf;

        // CPU mirror of the contents, used by updateBufferSubData().
        if (data)
            bufferShadows[bufferId].assign(static_cast<const uint8_t *>(data),
                                           static_cast<const uint8_t *>(data) + size);
        else
            bufferShadows[bufferId].assign(size, 0);

        // Any VAO that already pointed at the previous buffer object follows the
        // new one (retargetVAOBuffers moves those references across).
        if (oldBuf)
        {
            retargetVAOBuffers(oldBuf, buf);
            oldBuf->Release();
        }

        // If this buffer is associated with a VAO, store it
        auto vaIt = vertexArrays.find(currentVAO);
        if (vaIt != vertexArrays.end())
        {
            if (vaIt->second.indexBuffer != buf)
            {
                if (vaIt->second.indexBuffer) vaIt->second.indexBuffer->Release();
                vaIt->second.indexBuffer = buf;
                buf->AddRef(); // VAO also holds a reference
            }
            vaIt->second.indexCount = (uint32_t)(size / sizeof(uint32_t));
            vaIt->second.indexFormat = DXGI_FORMAT_R32_UINT;
        }
    }

    void kDX11Driver::uploadVertexBuffer(uint32_t bufferId, const void *data, size_t size)
    {
        ID3D11Buffer *oldBuf = nullptr;
        auto it = buffers.find(bufferId);
        if (it != buffers.end())
        {
            oldBuf = it->second;
            it->second = nullptr;
        }

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = (UINT)size;
        // D3D11_USAGE_IMMUTABLE requires initial data; buffers uploaded with a
        // null pointer (streamed vertex/instance data) must be DYNAMIC instead.
        desc.Usage          = data ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = data ? 0 : D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = data;

        ID3D11Buffer *buf = nullptr;
        HRESULT hr = d3dDevice->CreateBuffer(&desc, data ? &initData : nullptr, &buf);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] uploadVertexBuffer failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return;
        }

        buffers[bufferId] = buf;

        // Any VAO already pointing at the previous buffer object must follow the
        // new one, otherwise it would keep feeding the GPU the stale buffer.
        if (oldBuf)
        {
            retargetVAOBuffers(oldBuf, buf);
            oldBuf->Release();
        }

        // CPU mirror of the contents, used by updateBufferSubData().
        if (data)
            bufferShadows[bufferId].assign(static_cast<const uint8_t *>(data),
                                           static_cast<const uint8_t *>(data) + size);
        else
            bufferShadows[bufferId].assign(size, 0);

        // GL binds GL_ARRAY_BUFFER here; the following setVertexAttrib*() call is
        // what associates attributes with this buffer.
        currentArrayBufferId = bufferId;
    }

    void kDX11Driver::updateBufferSubData(uint32_t bufferId, const void *data, size_t size, size_t offset)
    {
        if (!data || size == 0)
            return;

        auto it = buffers.find(bufferId);
        if (it == buffers.end() || !it->second)
            return;

        // Patch the CPU mirror first, then re-upload it in full.  Uploading only
        // the patched bytes is not possible here: IMMUTABLE buffers cannot be
        // mapped, and a WRITE_DISCARD map would throw away every other region of
        // a partially updated buffer.
        std::vector<uint8_t> &shadow = bufferShadows[bufferId];
        if (shadow.empty())
        {
            D3D11_BUFFER_DESC existing;
            it->second->GetDesc(&existing);
            shadow.assign(existing.ByteWidth, 0);
        }
        if (offset + size > shadow.size())
        {
            std::cout << "[kDX11Driver] updateBufferSubData out of range (buffer "
                      << bufferId << ", " << offset << "+" << size << " > "
                      << shadow.size() << ")" << std::endl;
            return;
        }
        memcpy(shadow.data() + offset, data, size);

        ID3D11Buffer *gpuBuf = it->second;

        // Promote the GPU buffer to DYNAMIC so it can be mapped.
        D3D11_BUFFER_DESC desc;
        gpuBuf->GetDesc(&desc);
        if (desc.Usage != D3D11_USAGE_DYNAMIC)
        {
            D3D11_BUFFER_DESC newDesc = desc;
            newDesc.Usage          = D3D11_USAGE_DYNAMIC;
            newDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

            ID3D11Buffer *newBuf = nullptr;
            HRESULT hr = d3dDevice->CreateBuffer(&newDesc, nullptr, &newBuf);
            if (FAILED(hr))
            {
                std::cout << "[kDX11Driver] updateBufferSubData: could not create "
                             "dynamic buffer: 0x"
                          << std::hex << hr << std::dec << std::endl;
                return;
            }

            it->second = newBuf;
            retargetVAOBuffers(gpuBuf, newBuf);
            gpuBuf->Release();
            gpuBuf = newBuf;
        }

        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = d3dContext->Map(gpuBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            memcpy(mapped.pData, shadow.data(), shadow.size());
            d3dContext->Unmap(gpuBuf, 0);
        }

        // Mirrors GL: the uploaded buffer stays the current array buffer so a
        // following setVertexAttrib*() call binds to it.
        currentArrayBufferId = bufferId;
    }

    // Mirrors GL's "currently bound GL_ARRAY_BUFFER" semantics: the buffer set up
    // by the preceding uploadVertexBuffer()/updateBufferSubData() call is the one
    // the next setVertexAttrib*() call reads from.  Attributes that share a
    // buffer (interleaved layouts) reuse the same D3D11 input slot.
    static uint32_t findOrAddVBSlot(D3D11VertexArrayData &va, ID3D11Buffer *buf, uint32_t stride)
    {
        for (size_t i = 0; i < va.vertexBuffers.size(); ++i)
        {
            if (va.vertexBuffers[i].buffer == buf && va.vertexBuffers[i].stride == stride)
                return (uint32_t)i;
        }

        D3D11VertexArrayData::VB vb;
        vb.buffer = buf;
        vb.stride = stride;
        vb.offset = 0;
        if (buf)
            buf->AddRef(); // the VAO owns its own reference
        va.vertexBuffers.push_back(vb);
        return (uint32_t)(va.vertexBuffers.size() - 1);
    }

    void kDX11Driver::setVertexAttribFloat(int location, int components, int stride, size_t offset)
    {
        auto it = vertexArrays.find(currentVAO);
        if (it == vertexArrays.end())
            return;

        // GL treats stride 0 as tightly packed; D3D11 rejects a zero stride.
        if (stride <= 0)
            stride = components * (int)sizeof(float);

        D3D11VertexArrayData &va = it->second;

        D3D11AttribDesc desc;
        desc.location   = location;
        desc.components = components;
        desc.stride     = stride;
        desc.offset     = offset;
        desc.isInteger  = false;
        desc.slot       = findOrAddVBSlot(
            va, buffers.count(currentArrayBufferId) ? buffers[currentArrayBufferId] : nullptr,
            (uint32_t)stride);

        // Re-declaring a location (particle instance attributes are declared
        // again after the instance buffer is resized) replaces the entry instead
        // of adding a duplicate input-layout element.
        for (auto &existing : va.attribs)
        {
            if (existing.location == location)
            {
                desc.divisor = existing.divisor;
                existing     = desc;
                va.inputLayoutProgram = 0;
                return;
            }
        }
        va.attribs.push_back(desc);
        va.inputLayoutProgram = 0;
    }

    void kDX11Driver::setVertexAttribInt(int location, int components, int stride, size_t offset)
    {
        auto it = vertexArrays.find(currentVAO);
        if (it == vertexArrays.end())
            return;

        if (stride <= 0)
            stride = components * (int)sizeof(int32_t);

        D3D11VertexArrayData &va = it->second;

        D3D11AttribDesc desc;
        desc.location   = location;
        desc.components = components;
        desc.stride     = stride;
        desc.offset     = offset;
        desc.isInteger  = true;
        desc.slot       = findOrAddVBSlot(
            va, buffers.count(currentArrayBufferId) ? buffers[currentArrayBufferId] : nullptr,
            (uint32_t)stride);

        for (auto &existing : va.attribs)
        {
            if (existing.location == location)
            {
                desc.divisor = existing.divisor;
                existing     = desc;
                va.inputLayoutProgram = 0;
                return;
            }
        }
        va.attribs.push_back(desc);
        va.inputLayoutProgram = 0;
    }

    void kDX11Driver::setVertexAttribDivisor(int location, int divisor)
    {
        // D3D11 stores the divisor in the input-layout element
        // (InstanceDataStepRate / InputSlotClass), so record it on the attribute
        // and invalidate the cached layout.
        auto it = vertexArrays.find(currentVAO);
        if (it == vertexArrays.end())
            return;

        D3D11VertexArrayData &va = it->second;
        for (auto &attrib : va.attribs)
        {
            if (attrib.location != location)
                continue;

            attrib.divisor = (divisor > 0) ? (uint32_t)divisor : 0;

            if (va.inputLayout)
            {
                va.inputLayout->Release();
                va.inputLayout = nullptr;
            }
            va.inputLayoutProgram = 0;
            return;
        }
    }

    // =========================================================================
    // Draw calls
    // =========================================================================

    void kDX11Driver::applyPipelineState()
    {
        // Depth-stencil state
        if (depthStencilDirty && depthStencilState)
        {
            depthStencilState->Release();
            depthStencilState = nullptr;

            D3D11_DEPTH_STENCIL_DESC dsDesc = {};
            dsDesc.DepthEnable    = depthTestEnabled ? TRUE : FALSE;
            dsDesc.DepthWriteMask = depthWriteEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
            dsDesc.DepthFunc      = D3D11_COMPARISON_LESS;
            d3dDevice->CreateDepthStencilState(&dsDesc, &depthStencilState);

            if (depthStencilState)
                d3dContext->OMSetDepthStencilState(depthStencilState, 0);
            depthStencilDirty = false;
        }

        // Blend state
        if (blendDirty && blendState)
        {
            blendState->Release();
            blendState = nullptr;

            D3D11_BLEND_DESC bDesc = {};
            bDesc.AlphaToCoverageEnable  = alphaToCoverage ? TRUE : FALSE;
            bDesc.IndependentBlendEnable = FALSE;
            bDesc.RenderTarget[0].BlendEnable           = blendEnabled ? TRUE : FALSE;
            bDesc.RenderTarget[0].SrcBlend              = toD3DBlend(blendSrc);
            bDesc.RenderTarget[0].DestBlend             = toD3DBlend(blendDst);
            bDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
            // GL's glBlendFunc() applies the same factors to the alpha channel,
            // so mirror the colour factors here instead of hard-wiring ONE/ZERO
            // (which broke additive and premultiplied-alpha blending).
            bDesc.RenderTarget[0].SrcBlendAlpha         = toD3DBlend(blendSrc);
            bDesc.RenderTarget[0].DestBlendAlpha        = toD3DBlend(blendDst);
            bDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
            bDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            d3dDevice->CreateBlendState(&bDesc, &blendState);

            if (blendState)
                d3dContext->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
            blendDirty = false;
        }

        // Rasterizer state
        if (rasterizerDirty && rasterizerState)
        {
            rasterizerState->Release();
            rasterizerState = nullptr;

            D3D11_RASTERIZER_DESC rsDesc = {};
            rsDesc.FillMode = wireframeEnabled ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
            rsDesc.CullMode = cullFaceEnabled ? toD3DCullMode(cullMode) : D3D11_CULL_NONE;
            rsDesc.FrontCounterClockwise = (frontFace == kFrontFace::CCW) ? TRUE : FALSE;
            rsDesc.DepthClipEnable       = TRUE;
            rsDesc.ScissorEnable         = FALSE;
            rsDesc.MultisampleEnable     = msaaEnabled ? TRUE : FALSE;
            // Polygon offset for wireframe to prevent z-fighting
            if (wireframeEnabled)
            {
                rsDesc.DepthBias             = -1;
                rsDesc.SlopeScaledDepthBias  = -1.0f;
            }
            d3dDevice->CreateRasterizerState(&rsDesc, &rasterizerState);

            if (rasterizerState)
                d3dContext->RSSetState(rasterizerState);
            rasterizerDirty = false;
        }
    }

    void kDX11Driver::retargetVAOBuffers(ID3D11Buffer *oldBuf, ID3D11Buffer *newBuf)
    {
        if (!oldBuf || !newBuf || oldBuf == newBuf)
            return;

        for (auto &kv : vertexArrays)
        {
            D3D11VertexArrayData &va = kv.second;

            if (va.indexBuffer == oldBuf)
            {
                newBuf->AddRef();
                oldBuf->Release();
                va.indexBuffer = newBuf;
            }

            for (auto &vb : va.vertexBuffers)
            {
                if (vb.buffer == oldBuf)
                {
                    newBuf->AddRef();
                    oldBuf->Release();
                    vb.buffer = newBuf;
                }
            }
        }
    }

    void kDX11Driver::bindVAOState(D3D11VertexArrayData &va)
    {
        // Input layout: D3D11 needs the vertex shader's input signature to build
        // one, so it is created lazily from the byte code cached in the program
        // and rebuilt whenever the VAO is drawn with a different program.
        D3D11ProgramData *prog = nullptr;
        if (currentProgram != 0)
        {
            auto progIt = programs.find(currentProgram);
            if (progIt != programs.end())
                prog = &progIt->second;
        }

        if (prog && prog->vsBlob && !va.attribs.empty() &&
            (!va.inputLayout || va.inputLayoutProgram != currentProgram))
        {
            if (va.inputLayout)
            {
                va.inputLayout->Release();
                va.inputLayout = nullptr;
            }

            va.inputLayout = createInputLayout(prog->vsBlob, va.attribs);
            va.inputLayoutProgram = va.inputLayout ? currentProgram : 0;
        }

        if (va.indexBuffer)
            d3dContext->IASetIndexBuffer(va.indexBuffer, va.indexFormat, 0);

        // Bind every vertex buffer the attributes reference.  The engine uses one
        // buffer per attribute (position / uv / normal / …), which maps onto
        // separate input slots, but an interleaved buffer is handled too because
        // its attributes simply share one slot.
        const UINT slotCount =
            (UINT)std::min<size_t>(va.vertexBuffers.size(),
                                   D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT);
        if (slotCount > 0)
        {
            std::vector<ID3D11Buffer *> vbs(slotCount);
            std::vector<UINT> strides(slotCount);
            std::vector<UINT> offsets(slotCount);
            for (UINT i = 0; i < slotCount; ++i)
            {
                vbs[i]     = va.vertexBuffers[i].buffer;
                strides[i] = va.vertexBuffers[i].stride;
                offsets[i] = va.vertexBuffers[i].offset;
            }
            d3dContext->IASetVertexBuffers(0, slotCount, vbs.data(), strides.data(), offsets.data());
        }

        // nullptr is a valid argument and means "no layout"; a draw issued in that
        // state is invalid, which is why the layout is built above whenever the
        // shader and the VAO attributes allow it.
        // The spare slot used by dummy input-layout elements needs *something*
        // bound, otherwise a draw referencing it fails validation; a null buffer
        // makes the reads yield zero.
        {
            ID3D11Buffer *nullBuffer = nullptr;
            UINT nullStride = 0;
            UINT nullOffset = 0;
            d3dContext->IASetVertexBuffers(kDummyVertexInputSlot, 1,
                                           &nullBuffer, &nullStride, &nullOffset);
        }

        d3dContext->IASetInputLayout(va.inputLayout);
    }

    void kDX11Driver::drawIndexed(uint32_t vaoId, int indexCount)
    {
        applyPipelineState();

        auto vaIt = vertexArrays.find(vaoId);
        if (vaIt == vertexArrays.end())
            return;

        D3D11VertexArrayData &va = vaIt->second;

        // Binds the index/vertex buffers and the input layout for this VAO.
        bindVAOState(va);

        d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Flush constant buffers before drawing
        if (currentProgram != 0)
        {
            auto progIt = programs.find(currentProgram);
            if (progIt != programs.end())
                flushConstantBuffers(progIt->second);
        }

        // Bind current RTV + DSV
        if (currentFBO == 0)
        {
            d3dContext->OMSetRenderTargets(1, &backBufferRTV, backBufferDSV);
        }
        else
        {
            auto fboIt = framebuffers.find(currentFBO);
            if (fboIt != framebuffers.end())
            {
                d3dContext->OMSetRenderTargets(
                    (UINT)fboIt->second.colorRTVs.size(),
                    fboIt->second.colorRTVs.data(),
                    fboIt->second.depthDSV);
            }
        }

        // Bind textures (set shader resource views)
        for (int i = 0; i < 16; ++i)
        {
            if (boundTextures[i].srv)
            {
                d3dContext->PSSetShaderResources(i, 1, &boundTextures[i].srv);
                if (boundTextures[i].sampler)
                    d3dContext->PSSetSamplers(i, 1, &boundTextures[i].sampler);
            }
        }

        if (indexCount <= 0)
            indexCount = (int)va.indexCount;

        if (indexCount > 0)
            d3dContext->DrawIndexed((UINT)indexCount, 0, 0);
    }

    void kDX11Driver::drawIndexedInstanced(uint32_t vaoId, int indexCount, int instanceCount)
    {
        applyPipelineState();

        auto vaIt = vertexArrays.find(vaoId);
        if (vaIt == vertexArrays.end())
            return;

        D3D11VertexArrayData &va = vaIt->second;

        bindVAOState(va);

        d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // Flush CBs
        if (currentProgram != 0)
        {
            auto progIt = programs.find(currentProgram);
            if (progIt != programs.end())
                flushConstantBuffers(progIt->second);
        }

        // Bind RTV + DSV
        if (currentFBO == 0)
            d3dContext->OMSetRenderTargets(1, &backBufferRTV, backBufferDSV);
        else
        {
            auto fboIt = framebuffers.find(currentFBO);
            if (fboIt != framebuffers.end())
                d3dContext->OMSetRenderTargets(
                    (UINT)fboIt->second.colorRTVs.size(),
                    fboIt->second.colorRTVs.data(),
                    fboIt->second.depthDSV);
        }

        // Bind textures
        for (int i = 0; i < 16; ++i)
        {
            if (boundTextures[i].srv)
            {
                d3dContext->PSSetShaderResources(i, 1, &boundTextures[i].srv);
                if (boundTextures[i].sampler)
                    d3dContext->PSSetSamplers(i, 1, &boundTextures[i].sampler);
            }
        }

        if (indexCount <= 0)
            indexCount = (int)va.indexCount;

        if (indexCount > 0)
            d3dContext->DrawIndexedInstanced((UINT)indexCount, (UINT)instanceCount, 0, 0, 0);
    }

    void kDX11Driver::drawArrays(uint32_t vaoId, kPrimitiveType type, int vertexCount)
    {
        applyPipelineState();

        auto vaIt = vertexArrays.find(vaoId);
        if (vaIt == vertexArrays.end())
            return;

        // drawArrays() previously bound no vertex buffer at all, so it drew with
        // whatever the previous draw had left in the input assembler.
        bindVAOState(vaIt->second);

        d3dContext->IASetPrimitiveTopology(toD3DTopology(type));

        // Flush CBs
        if (currentProgram != 0)
        {
            auto progIt = programs.find(currentProgram);
            if (progIt != programs.end())
                flushConstantBuffers(progIt->second);
        }

        // Bind RTV + DSV
        if (currentFBO == 0)
            d3dContext->OMSetRenderTargets(1, &backBufferRTV, backBufferDSV);
        else
        {
            auto fboIt = framebuffers.find(currentFBO);
            if (fboIt != framebuffers.end())
                d3dContext->OMSetRenderTargets(
                    (UINT)fboIt->second.colorRTVs.size(),
                    fboIt->second.colorRTVs.data(),
                    fboIt->second.depthDSV);
        }

        if (vertexCount > 0)
            d3dContext->Draw((UINT)vertexCount, 0);
    }

    void kDX11Driver::drawArraysInstanced(uint32_t vaoId, kPrimitiveType type,
                                           int vertexCount, int instanceCount)
    {
        applyPipelineState();

        auto vaIt = vertexArrays.find(vaoId);
        if (vaIt == vertexArrays.end())
            return;

        bindVAOState(vaIt->second);

        d3dContext->IASetPrimitiveTopology(toD3DTopology(type));

        // Flush CBs
        if (currentProgram != 0)
        {
            auto progIt = programs.find(currentProgram);
            if (progIt != programs.end())
                flushConstantBuffers(progIt->second);
        }

        // Bind RTV + DSV
        if (currentFBO == 0)
            d3dContext->OMSetRenderTargets(1, &backBufferRTV, backBufferDSV);
        else
        {
            auto fboIt = framebuffers.find(currentFBO);
            if (fboIt != framebuffers.end())
                d3dContext->OMSetRenderTargets(
                    (UINT)fboIt->second.colorRTVs.size(),
                    fboIt->second.colorRTVs.data(),
                    fboIt->second.depthDSV);
        }

        if (vertexCount > 0)
            d3dContext->DrawInstanced((UINT)vertexCount, (UINT)instanceCount, 0, 0);
    }

    // =========================================================================
    // Texture sampling
    // =========================================================================

    void kDX11Driver::bindTexture2D(int unit, uint32_t id)
    {
        if (unit < 0 || unit >= 16) return;

        auto it = textures.find(id);
        if (it != textures.end())
        {
            boundTextures[unit].id      = id;
            boundTextures[unit].srv     = it->second.srv;
            boundTextures[unit].sampler = it->second.sampler;
            boundTextures[unit].isCube  = false;
            boundTextures[unit].isArray = false;
        }
    }

    void kDX11Driver::bindTexture2DArray(int unit, uint32_t id)
    {
        if (unit < 0 || unit >= 16) return;

        auto it = textures.find(id);
        if (it != textures.end())
        {
            boundTextures[unit].id      = id;
            boundTextures[unit].srv     = it->second.srv;
            boundTextures[unit].sampler = it->second.sampler;
            boundTextures[unit].isCube  = false;
            boundTextures[unit].isArray = true;
        }
    }

    void kDX11Driver::bindTextureCube(int unit, uint32_t id)
    {
        if (unit < 0 || unit >= 16) return;

        auto it = textures.find(id);
        if (it != textures.end())
        {
            boundTextures[unit].id      = id;
            boundTextures[unit].srv     = it->second.srv;
            boundTextures[unit].sampler = it->second.sampler;
            boundTextures[unit].isCube  = true;
            boundTextures[unit].isArray = false;
        }
    }

    void kDX11Driver::unbindTexture2D(int unit)
    {
        if (unit < 0 || unit >= 16) return;
        boundTextures[unit] = BoundTexture();
        ID3D11ShaderResourceView *nullSrv = nullptr;
        d3dContext->PSSetShaderResources(unit, 1, &nullSrv);
    }

    void kDX11Driver::unbindTexture2DArray(int unit)
    {
        unbindTexture2D(unit);
    }

    void kDX11Driver::unbindTextureCube(int unit)
    {
        unbindTexture2D(unit);
    }

    void kDX11Driver::generateMipmaps2D(uint32_t id)
    {
        auto it = textures.find(id);
        if (it == textures.end() || !it->second.srv)
            return;

        // D3D11 can auto-generate mipmaps by calling GenerateMips on the SRV
        d3dContext->GenerateMips(it->second.srv);
    }

    void kDX11Driver::readTexture2DRGB(uint32_t id, int mipLevel, float *pixels)
    {
        auto it = textures.find(id);
        if (it == textures.end() || !it->second.texture)
            return;

        // Create a staging texture to read back
        D3D11_TEXTURE2D_DESC desc;
        it->second.texture->GetDesc(&desc);
        desc.Usage          = D3D11_USAGE_STAGING;
        desc.BindFlags      = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags      = 0;

        ID3D11Texture2D *staging = nullptr;
        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &staging);
        if (FAILED(hr)) return;

        d3dContext->CopySubresourceRegion(staging, 0, 0, 0, 0,
                                          it->second.texture, mipLevel, nullptr);

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = d3dContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            int mipWidth  = std::max(1, (int)desc.Width  >> mipLevel);
            int mipHeight = std::max(1, (int)desc.Height >> mipLevel);
            for (int y = 0; y < mipHeight; ++y)
            {
                for (int x = 0; x < mipWidth; ++x)
                {
                    uint8_t *src = (uint8_t *)mapped.pData + y * mapped.RowPitch + x * 4;
                    float *dst   = pixels + (y * mipWidth + x) * 3;
                    dst[0] = src[0] / 255.0f;
                    dst[1] = src[1] / 255.0f;
                    dst[2] = src[2] / 255.0f;
                }
            }
            d3dContext->Unmap(staging, 0);
        }
        staging->Release();
    }

    void kDX11Driver::readPixelsRGBA(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a)
    {
        r = g = b = 0; a = 255;

        // D3D11 has no glReadPixels; copy the requested texel out of the active
        // render target into a 1x1 staging texture.  Only reading from the back
        // buffer made picking return black whenever a framebuffer was bound (the
        // picking pass renders into its own FBO).
        ID3D11Texture2D *sourceTex = nullptr;
        bool sourceOwnsRef = false;

        if (currentFBO != 0)
        {
            auto fboIt = framebuffers.find(currentFBO);
            if (fboIt != framebuffers.end() && !fboIt->second.colorTextures.empty())
            {
                // The framebuffer owns this reference; do not release it.
                sourceTex = fboIt->second.colorTextures[0];
            }
        }
        else if (backBufferRTV)
        {
            backBufferRTV->GetResource((ID3D11Resource **)&sourceTex);
            sourceOwnsRef = true;
        }

        if (!sourceTex)
            return;

        D3D11_TEXTURE2D_DESC srcDesc;
        sourceTex->GetDesc(&srcDesc);

        if (x < 0 || y < 0 || (UINT)x >= srcDesc.Width || (UINT)y >= srcDesc.Height)
        {
            if (sourceOwnsRef) sourceTex->Release();
            return;
        }

        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width              = 1;
        stagingDesc.Height             = 1;
        stagingDesc.MipLevels          = 1;
        stagingDesc.ArraySize          = 1;
        stagingDesc.Format             = srcDesc.Format;
        stagingDesc.SampleDesc.Count   = 1;
        stagingDesc.Usage              = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags          = 0;
        stagingDesc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D *staging = nullptr;
        HRESULT hr = d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &staging);
        if (FAILED(hr))
        {
            if (sourceOwnsRef) sourceTex->Release();
            return;
        }

        // Callers pass GL-style coordinates (origin bottom-left, as used by
        // glReadPixels); D3D11 textures are addressed top-down.
        const UINT srcTop = srcDesc.Height - 1 - (UINT)y;

        D3D11_BOX srcBox;
        srcBox.left   = (UINT)x;
        srcBox.top    = srcTop;
        srcBox.right  = (UINT)x + 1;
        srcBox.bottom = srcTop + 1;
        srcBox.front  = 0;
        srcBox.back   = 1;

        // A multisampled render target must be resolved before it can be copied.
        if (srcDesc.SampleDesc.Count > 1)
        {
            std::cout << "[kDX11Driver] readPixelsRGBA: multisampled target is not "
                         "readable; resolve it first." << std::endl;
            staging->Release();
            if (sourceOwnsRef) sourceTex->Release();
            return;
        }

        d3dContext->CopySubresourceRegion(staging, 0, 0, 0, 0, sourceTex, 0, &srcBox);

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = d3dContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(hr))
        {
            uint8_t *pixel = (uint8_t *)mapped.pData;
            r = pixel[0];
            g = pixel[1];
            b = pixel[2];
            a = pixel[3];
            d3dContext->Unmap(staging, 0);
        }

        staging->Release();
        if (sourceOwnsRef)
            sourceTex->Release();
    }

    // =========================================================================
    // Framebuffers
    // =========================================================================

    uint32_t kDX11Driver::createFramebuffer()
    {
        uint32_t id = nextFBOId++;
        framebuffers[id] = D3D11FramebufferData();
        return id;
    }

    void kDX11Driver::deleteFramebuffer(uint32_t id)
    {
        auto it = framebuffers.find(id);
        if (it != framebuffers.end())
        {
            for (auto *rtv : it->second.colorRTVs) if (rtv) rtv->Release();
            if (it->second.depthDSV) it->second.depthDSV->Release();
            for (auto *tex : it->second.colorTextures) if (tex) tex->Release();
            if (it->second.depthTexture) it->second.depthTexture->Release();
            for (auto *srv : it->second.colorSRVs) if (srv) srv->Release();
            framebuffers.erase(it);
        }
    }

    void kDX11Driver::bindFramebuffer(uint32_t id)
    {
        currentFBO     = id;
        currentReadFBO = id;
    }

    void kDX11Driver::bindReadFramebuffer(uint32_t id)
    {
        // D3D11 has no separate read framebuffer object, but the source of a
        // following blitFramebufferColor() must be tracked apart from the draw
        // target — otherwise the resolve would read and write one resource.
        currentReadFBO = id;
    }

    void kDX11Driver::bindDrawFramebuffer(uint32_t id)
    {
        currentFBO = id;
    }

    void kDX11Driver::unbindFramebuffer()
    {
        currentFBO     = 0;
        currentReadFBO = 0;
    }

    bool kDX11Driver::isFramebufferComplete()
    {
        if (currentFBO == 0)
            return backBufferRTV != nullptr;

        auto it = framebuffers.find(currentFBO);
        if (it == framebuffers.end())
            return false;

        return !it->second.colorRTVs.empty() || it->second.depthDSV != nullptr;
    }

    void kDX11Driver::blitFramebufferColor(int srcX0, int srcY0, int srcX1, int srcY1,
                                            int dstX0, int dstY0, int dstX1, int dstY1)
    {
        // D3D11 has no glBlitFramebuffer equivalent.  The only blit the renderer
        // performs is the screen-buffer MSAA resolve: read framebuffer =
        // multisampled, draw framebuffer = single-sample.  That maps onto
        // ResolveSubresource(); anything else falls back to a straight copy.
        auto readIt = framebuffers.find(currentReadFBO);
        auto drawIt = framebuffers.find(currentFBO);

        if (readIt == framebuffers.end() || drawIt == framebuffers.end())
            return;
        if (readIt->second.colorTextures.empty() || drawIt->second.colorTextures.empty())
            return;

        ID3D11Texture2D *src = readIt->second.colorTextures[0];
        ID3D11Texture2D *dst = drawIt->second.colorTextures[0];
        if (!src || !dst || src == dst)
            return;

        D3D11_TEXTURE2D_DESC srcDesc = {};
        D3D11_TEXTURE2D_DESC dstDesc = {};
        src->GetDesc(&srcDesc);
        dst->GetDesc(&dstDesc);

        // The source must not still be bound as a render target while resolving.
        d3dContext->OMSetRenderTargets(0, nullptr, nullptr);

        if (srcDesc.SampleDesc.Count > 1 && dstDesc.SampleDesc.Count == 1 &&
            srcDesc.Format == dstDesc.Format &&
            srcDesc.Width == dstDesc.Width && srcDesc.Height == dstDesc.Height)
        {
            d3dContext->ResolveSubresource(dst, 0, src, 0, srcDesc.Format);
        }
        else if (srcDesc.SampleDesc.Count == dstDesc.SampleDesc.Count &&
                 srcDesc.Format == dstDesc.Format &&
                 srcDesc.Width == dstDesc.Width && srcDesc.Height == dstDesc.Height)
        {
            d3dContext->CopySubresourceRegion(dst, 0, (UINT)dstX0, (UINT)dstY0, 0,
                                              src, 0, nullptr);
        }
        else
        {
            std::cout << "[kDX11Driver] blitFramebufferColor: unsupported source/target "
                         "combination (msaa " << srcDesc.SampleDesc.Count << " -> "
                      << dstDesc.SampleDesc.Count << ")" << std::endl;
        }

        (void)srcX0; (void)srcY0; (void)srcX1; (void)srcY1;
        (void)dstX1; (void)dstY1;
    }

    void kDX11Driver::setFramebufferDrawBuffer()
    {
        // D3D11 automatically draws to all bound RTVs. No-op for D3D11.
    }

    // =========================================================================
    // Renderbuffers
    // =========================================================================

    uint32_t kDX11Driver::createRenderbuffer()
    {
        // In D3D11, renderbuffers are just depth textures or standard textures.
        // We reuse the texture ID space for RBOs.
        uint32_t id = nextTextureId++;
        D3D11TextureData td;
        td.isDepth = true;
        textures[id] = td;
        return id;
    }

    void kDX11Driver::deleteRenderbuffer(uint32_t id)
    {
        auto it = textures.find(id);
        if (it != textures.end())
        {
            if (it->second.texture) it->second.texture->Release();
            if (it->second.srv)     it->second.srv->Release();
            if (it->second.sampler) it->second.sampler->Release();
            textures.erase(it);
        }
    }

    void kDX11Driver::setupRenderbuffer(uint32_t rboId, int width, int height)
    {
        auto it = textures.find(rboId);
        if (it == textures.end()) return;

        D3D11TextureData &td = it->second;

        if (td.texture) td.texture->Release();
        if (td.srv)     { td.srv->Release(); td.srv = nullptr; }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = DXGI_FORMAT_R24G8_TYPELESS;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] setupRenderbuffer failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
        }

        td.width   = width;
        td.height  = height;
        td.samples = 1;
        td.isDepth = true;
    }

    void kDX11Driver::setupRenderbufferMSAA(uint32_t rboId, int samples, int width, int height)
    {
        auto it = textures.find(rboId);
        if (it == textures.end()) return;

        D3D11TextureData &td = it->second;

        if (td.texture) td.texture->Release();
        if (td.srv)     { td.srv->Release(); td.srv = nullptr; }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = DXGI_FORMAT_R24G8_TYPELESS;
        desc.SampleDesc.Count   = (UINT)samples;
        desc.SampleDesc.Quality = 0;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] setupRenderbufferMSAA failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
        }

        td.width   = width;
        td.height  = height;
        td.samples = samples;
        td.isDepth = true;
    }

    void kDX11Driver::attachRenderbufferDepthStencil(uint32_t fboId, uint32_t rboId)
    {
        auto texIt = textures.find(rboId);
        if (texIt == textures.end() || !texIt->second.texture)
            return;

        auto fboIt = framebuffers.find(fboId);
        if (fboIt == framebuffers.end())
            return;

        D3D11FramebufferData &fb = fboIt->second;

        // Release old DSV
        if (fb.depthDSV) { fb.depthDSV->Release(); fb.depthDSV = nullptr; }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        HRESULT hr = d3dDevice->CreateDepthStencilView(
            texIt->second.texture, &dsvDesc, &fb.depthDSV);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] attachRenderbufferDepthStencil failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
        }
    }

    // =========================================================================
    // Texture creation (for asset loading)
    // =========================================================================

    static DXGI_FORMAT toD3DTextureFormat(kTextureFormat format, bool &isSRGB)
    {
        isSRGB = false;
        switch (format)
        {
        case kTextureFormat::TEX_FORMAT_RGB:   return DXGI_FORMAT_R8G8B8A8_UNORM;
        case kTextureFormat::TEX_FORMAT_RGBA:  return DXGI_FORMAT_R8G8B8A8_UNORM;
        case kTextureFormat::TEX_FORMAT_SRGB:
            isSRGB = true;
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case kTextureFormat::TEX_FORMAT_SRGBA:
            isSRGB = true;
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        default: return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    }

    static D3D11_TEXTURE_ADDRESS_MODE toD3DWrap(kTextureWrap w)
    {
        switch (w)
        {
        case kTextureWrap::REPEAT:          return D3D11_TEXTURE_ADDRESS_WRAP;
        case kTextureWrap::CLAMP_TO_EDGE:   return D3D11_TEXTURE_ADDRESS_CLAMP;
        case kTextureWrap::CLAMP_TO_BORDER: return D3D11_TEXTURE_ADDRESS_BORDER;
        case kTextureWrap::MIRRORED_REPEAT: return D3D11_TEXTURE_ADDRESS_MIRROR;
        default: return D3D11_TEXTURE_ADDRESS_WRAP;
        }
    }

    static D3D11_FILTER toD3DFilter(kTextureFilter minF, kTextureFilter magF, bool hasMips)
    {
        if (!hasMips)
        {
            if (minF == kTextureFilter::NEAREST && magF == kTextureFilter::NEAREST)
                return D3D11_FILTER_MIN_MAG_MIP_POINT;
            if (minF == kTextureFilter::LINEAR && magF == kTextureFilter::LINEAR)
                return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            if (minF == kTextureFilter::NEAREST)
                return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
            return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        }
        bool minNear = (minF == kTextureFilter::NEAREST || minF == kTextureFilter::NEAREST_MIPMAP_NEAREST || minF == kTextureFilter::NEAREST_MIPMAP_LINEAR);
        bool magNear = (magF == kTextureFilter::NEAREST);
        bool mipNear = (minF == kTextureFilter::NEAREST_MIPMAP_NEAREST || minF == kTextureFilter::LINEAR_MIPMAP_NEAREST);
        if (minNear && magNear && mipNear)  return D3D11_FILTER_MIN_MAG_MIP_POINT;
        if (minNear && magNear && !mipNear) return D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
        if (minNear && !magNear && mipNear) return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
        if (!minNear && magNear && mipNear) return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
        if (!minNear && !magNear && !mipNear) return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    }

    uint32_t kDX11Driver::createTexture2D(int width, int height, kTextureFormat format,
                                           const void *data,
                                           kTextureWrap wrap,
                                           kTextureFilter minFilter,
                                           kTextureFilter magFilter,
                                           bool generateMips)
    {
        uint32_t id = nextTextureId++;
        D3D11TextureData td;
        td.width  = width;
        td.height = height;
        td.layers = 1;
        bool isSRGB = false;
        td.format = toD3DTextureFormat(format, isSRGB);
        td.mips   = generateMips;
        UINT mipLevels = generateMips ? 0 : 1;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = (UINT)width;
        desc.Height = (UINT)height;
        desc.MipLevels = mipLevels;
        desc.ArraySize = 1;
        desc.Format = td.format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        // GenerateMips() only works on a resource that is bound as a render
        // target as well as a shader resource; without BIND_RENDER_TARGET the
        // call is rejected and the mip chain stays uninitialised.
        desc.BindFlags = generateMips
                             ? (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)
                             : D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        if (data)
        {
            int channels = (format == kTextureFormat::TEX_FORMAT_RGB || format == kTextureFormat::TEX_FORMAT_SRGB) ? 3 : 4;
            std::vector<uint8_t> rgbaData;
            const void *uploadData = data;
            if (channels == 3)
            {
                rgbaData.resize(width * height * 4);
                const uint8_t *src = (const uint8_t *)data;
                uint8_t *dst = rgbaData.data();
                for (int i = 0; i < width * height; ++i) {
                    dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; dst[3]=255;
                    src+=3; dst+=4;
                }
                uploadData = rgbaData.data();
            }
            D3D11_SUBRESOURCE_DATA initData = {};
            initData.pSysMem = uploadData;
            initData.SysMemPitch = (UINT)width * 4;
            d3dDevice->CreateTexture2D(&desc, &initData, &td.texture);
        }
        else
        {
            d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        }
        if (td.texture)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = td.format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = mipLevels;
            d3dDevice->CreateShaderResourceView(td.texture, &srvDesc, &td.srv);
            D3D11_SAMPLER_DESC sampDesc = {};
            sampDesc.Filter = toD3DFilter(minFilter, magFilter, generateMips);
            sampDesc.AddressU = toD3DWrap(wrap);
            sampDesc.AddressV = toD3DWrap(wrap);
            sampDesc.AddressW = toD3DWrap(wrap);
            sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sampDesc.MinLOD = 0;
            sampDesc.MaxLOD = generateMips ? D3D11_FLOAT32_MAX : 0;
            d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);
            if (generateMips && td.srv)
                d3dContext->GenerateMips(td.srv);
        }
        textures[id] = td;
        return id;
    }

    uint32_t kDX11Driver::createTextureCube(int width, int height,
                                             const void *faceData[6],
                                             bool generateMips)
    {
        uint32_t id = nextTextureId++;
        D3D11TextureData td;
        td.width = width; td.height = height; td.layers = 6;
        td.isCube = true;
        td.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        td.mips   = generateMips;
        UINT mipLevels = generateMips ? 0 : 1;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = (UINT)width; desc.Height = (UINT)height;
        desc.MipLevels = mipLevels; desc.ArraySize = 6;
        desc.Format = td.format; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        // See createTexture2D(): GenerateMips() needs BIND_RENDER_TARGET too.
        desc.BindFlags = generateMips
                             ? (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)
                             : D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | (generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0);
        D3D11_SUBRESOURCE_DATA initData[6] = {};
        for (int i = 0; i < 6; ++i) {
            if (faceData[i]) {
                initData[i].pSysMem = faceData[i];
                initData[i].SysMemPitch = (UINT)width * 4;
            }
        }
        d3dDevice->CreateTexture2D(&desc, initData, &td.texture);
        if (td.texture)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = td.format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MipLevels = mipLevels;
            d3dDevice->CreateShaderResourceView(td.texture, &srvDesc, &td.srv);
            D3D11_SAMPLER_DESC sampDesc = {};
            sampDesc.Filter = generateMips ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_LINEAR;
            sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sampDesc.MinLOD = 0;
            sampDesc.MaxLOD = generateMips ? D3D11_FLOAT32_MAX : 0;
            d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);
            if (generateMips && td.srv) d3dContext->GenerateMips(td.srv);
        }
        textures[id] = td;
        return id;
    }

    void kDX11Driver::uploadTexture2D(uint32_t id, int level, int width, int height,
                                       kTextureFormat format, const void *data)
    {
        // UpdateSubresource() dereferences pSrcData, so a null pointer (or an
        // empty region) must never reach it.
        if (!data || width <= 0 || height <= 0 || level < 0)
            return;

        auto it = textures.find(id);
        if (it == textures.end() || !it->second.texture) return;
        int channels = (format == kTextureFormat::TEX_FORMAT_RGB || format == kTextureFormat::TEX_FORMAT_SRGB) ? 3 : 4;
        std::vector<uint8_t> rgbaData;
        const void *uploadData = data;
        if (channels == 3 && data) {
            rgbaData.resize(width * height * 4);
            const uint8_t *src = (const uint8_t *)data;
            uint8_t *dst = rgbaData.data();
            for (int i = 0; i < width * height; ++i) {
                dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; dst[3]=255;
                src+=3; dst+=4;
            }
            uploadData = rgbaData.data();
        }
        D3D11_BOX box;
        box.left=0; box.top=0; box.right=(UINT)width; box.bottom=(UINT)height;
        box.front=0; box.back=1;
        d3dContext->UpdateSubresource(it->second.texture, level, &box, uploadData, (UINT)width*4, 0);

        // The new contents invalidate the existing mip chain.
        if (level == 0 && it->second.mips && it->second.srv)
            d3dContext->GenerateMips(it->second.srv);
    }

    void kDX11Driver::uploadTexture2DSub(uint32_t id, int level, int x, int y,
                                          int width, int height,
                                          kTextureFormat format, const void *data)
    {
        if (!data || width <= 0 || height <= 0 || x < 0 || y < 0 || level < 0)
            return;

        auto it = textures.find(id);
        if (it == textures.end() || !it->second.texture) return;
        int channels = (format == kTextureFormat::TEX_FORMAT_RGB || format == kTextureFormat::TEX_FORMAT_SRGB) ? 3 : 4;
        std::vector<uint8_t> rgbaData;
        const void *uploadData = data;
        if (channels == 3 && data) {
            rgbaData.resize(width * height * 4);
            const uint8_t *src = (const uint8_t *)data;
            uint8_t *dst = rgbaData.data();
            for (int i = 0; i < width * height; ++i) {
                dst[0]=src[0]; dst[1]=src[1]; dst[2]=src[2]; dst[3]=255;
                src+=3; dst+=4;
            }
            uploadData = rgbaData.data();
        }
        D3D11_BOX box;
        box.left=(UINT)x; box.top=(UINT)y; box.right=(UINT)(x+width); box.bottom=(UINT)(y+height);
        box.front=0; box.back=1;
        d3dContext->UpdateSubresource(it->second.texture, level, &box, uploadData, (UINT)width*4, 0);

        if (level == 0 && it->second.mips && it->second.srv)
            d3dContext->GenerateMips(it->second.srv);
    }

    void kDX11Driver::uploadCompressedTexture2D(uint32_t id, int level,
                                                 int width, int height,
                                                 kTextureFormat format,
                                                 const void *data, size_t dataSize)
    {
        (void)width; (void)height; (void)format;
        auto it = textures.find(id);
        if (it == textures.end() || !it->second.texture) return;
        d3dContext->UpdateSubresource(it->second.texture, level, nullptr, data, (UINT)dataSize, 0);
    }

    void kDX11Driver::uploadTextureCubeFace(uint32_t id, int face, int width, int height,
                                             const void *data)
    {
        auto it = textures.find(id);
        if (it == textures.end() || !it->second.texture) return;
        UINT subresource = D3D11CalcSubresource(0, (UINT)face, 1);
        D3D11_BOX box;
        box.left=0; box.top=0; box.right=(UINT)width; box.bottom=(UINT)height;
        box.front=0; box.back=1;
        d3dContext->UpdateSubresource(it->second.texture, subresource, &box, data, (UINT)width*4, 0);
    }

    void kDX11Driver::deleteTexture(uint32_t id)
    {
        auto it = textures.find(id);
        if (it != textures.end()) {
            if (it->second.texture) it->second.texture->Release();
            if (it->second.srv) it->second.srv->Release();
            if (it->second.sampler) it->second.sampler->Release();
            textures.erase(it);
        }
    }

    // =========================================================================
    // FBO-managed textures
    // =========================================================================

    uint32_t kDX11Driver::createFBOColorTexture(int width, int height)
    {
        uint32_t id = nextTextureId++;

        D3D11TextureData td;
        td.width  = width;
        td.height = height;
        td.format = srgbEnabled ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = 1;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags          = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (SUCCEEDED(hr))
        {
            d3dDevice->CreateShaderResourceView(td.texture, nullptr, &td.srv);
        }

        // Default sampler
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.MaxAnisotropy  = 1;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampDesc.MinLOD         = 0;
        sampDesc.MaxLOD         = D3D11_FLOAT32_MAX;
        d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);

        textures[id] = td;
        return id;
    }

    uint32_t kDX11Driver::createFBOColorTextureMSAA(int samples, int width, int height)
    {
        uint32_t id = nextTextureId++;

        D3D11TextureData td;
        td.width   = width;
        td.height  = height;
        td.samples = samples;
        td.format  = srgbEnabled ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = (UINT)samples;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_RENDER_TARGET;

        // No shader-resource view: D3D11 forbids an SRV on a multisampled
        // resource, so CreateShaderResourceView() would always fail here (and
        // log a debug-layer error).  This texture is only ever a render target;
        // the resolve target is a separate single-sample texture, so sampling
        // happens through that one.
        d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);

        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
        d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);

        textures[id] = td;
        return id;
    }

    uint32_t kDX11Driver::createFBODepthTexture(int width, int height)
    {
        uint32_t id = nextTextureId++;

        D3D11TextureData td;
        td.width   = width;
        td.height  = height;
        td.isDepth = true;
        td.format  = DXGI_FORMAT_R32_TYPELESS;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = 1;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format               = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension        = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels  = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;
            d3dDevice->CreateShaderResourceView(td.texture, &srvDesc, &td.srv);
        }

        // Depth comparison sampler (for shadow mapping)
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS;
        sampDesc.BorderColor[0] = 1.0f;
        sampDesc.BorderColor[1] = 1.0f;
        sampDesc.BorderColor[2] = 1.0f;
        sampDesc.BorderColor[3] = 1.0f;
        d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);

        textures[id] = td;
        return id;
    }

    uint32_t kDX11Driver::createFBODepthTextureArray(int width, int height, int layers)
    {
        uint32_t id = nextTextureId++;

        D3D11TextureData td;
        td.width   = width;
        td.height  = height;
        td.layers  = layers;
        td.isDepth = true;
        td.format  = DXGI_FORMAT_R32_TYPELESS;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = (UINT)layers;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = 1;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (SUCCEEDED(hr))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                  = DXGI_FORMAT_R32_FLOAT;
            srvDesc.ViewDimension           = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            srvDesc.Texture2DArray.MipLevels = 1;
            srvDesc.Texture2DArray.MostDetailedMip = 0;
            srvDesc.Texture2DArray.FirstArraySlice = 0;
            srvDesc.Texture2DArray.ArraySize       = (UINT)layers;
            d3dDevice->CreateShaderResourceView(td.texture, &srvDesc, &td.srv);
        }

        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_BORDER;
        sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS;
        sampDesc.BorderColor[0] = 1.0f;
        sampDesc.BorderColor[1] = 1.0f;
        sampDesc.BorderColor[2] = 1.0f;
        sampDesc.BorderColor[3] = 1.0f;
        d3dDevice->CreateSamplerState(&sampDesc, &td.sampler);

        textures[id] = td;
        return id;
    }

    void kDX11Driver::deleteFBOTexture(uint32_t id)
    {
        auto it = textures.find(id);
        if (it != textures.end())
        {
            if (it->second.texture) it->second.texture->Release();
            if (it->second.srv)     it->second.srv->Release();
            if (it->second.sampler) it->second.sampler->Release();
            textures.erase(it);
        }
    }

    void kDX11Driver::attachFBOColorTexture(uint32_t fboId, uint32_t texId)
    {
        auto texIt = textures.find(texId);
        if (texIt == textures.end() || !texIt->second.texture)
            return;

        auto fboIt = framebuffers.find(fboId);
        if (fboIt == framebuffers.end())
            return;

        D3D11FramebufferData &fb = fboIt->second;

        // Release the old attachment.  The texture and SRV were AddRef'd when the
        // previous attachment was made, so those references have to go too —
        // otherwise every re-attach (resize, MSAA toggle) leaked them.
        for (auto *rtv : fb.colorRTVs) if (rtv) rtv->Release();
        for (auto *tex : fb.colorTextures) if (tex) tex->Release();
        for (auto *srv : fb.colorSRVs) if (srv) srv->Release();
        fb.colorRTVs.clear();
        fb.colorTextures.clear();
        fb.colorSRVs.clear();

        ID3D11RenderTargetView *rtv = nullptr;
        HRESULT hr = d3dDevice->CreateRenderTargetView(texIt->second.texture, nullptr, &rtv);
        if (SUCCEEDED(hr))
        {
            fb.colorRTVs.push_back(rtv);
            fb.colorTextures.push_back(texIt->second.texture);
            texIt->second.texture->AddRef();
            if (texIt->second.srv)
            {
                texIt->second.srv->AddRef();
                fb.colorSRVs.push_back(texIt->second.srv);
            }
        }
        else
        {
            std::cout << "[kDX11Driver] attachFBOColorTexture failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
        }
    }

    void kDX11Driver::attachFBOColorTextureMSAA(uint32_t fboId, uint32_t texId)
    {
        // Same as non-MSAA for D3D11 — the texture itself is multisampled
        attachFBOColorTexture(fboId, texId);
    }

    void kDX11Driver::attachFBODepthTexture(uint32_t fboId, uint32_t texId)
    {
        auto texIt = textures.find(texId);
        if (texIt == textures.end() || !texIt->second.texture)
            return;

        auto fboIt = framebuffers.find(fboId);
        if (fboIt == framebuffers.end())
            return;

        D3D11FramebufferData &fb = fboIt->second;

        if (fb.depthDSV) { fb.depthDSV->Release(); fb.depthDSV = nullptr; }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format             = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;

        d3dDevice->CreateDepthStencilView(texIt->second.texture, &dsvDesc, &fb.depthDSV);

        if (fb.depthTexture) fb.depthTexture->Release();
        fb.depthTexture = texIt->second.texture;
        fb.depthTexture->AddRef();
    }

    void kDX11Driver::attachFBODepthTextureLayer(uint32_t fboId, uint32_t texId, int layer)
    {
        auto texIt = textures.find(texId);
        if (texIt == textures.end() || !texIt->second.texture)
            return;

        auto fboIt = framebuffers.find(fboId);
        if (fboIt == framebuffers.end())
            return;

        D3D11FramebufferData &fb = fboIt->second;

        if (fb.depthDSV) { fb.depthDSV->Release(); fb.depthDSV = nullptr; }

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format                        = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension                 = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice       = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = (UINT)layer;
        dsvDesc.Texture2DArray.ArraySize       = 1;

        d3dDevice->CreateDepthStencilView(texIt->second.texture, &dsvDesc, &fb.depthDSV);

        if (fb.depthTexture) fb.depthTexture->Release();
        fb.depthTexture = texIt->second.texture;
        fb.depthTexture->AddRef();
    }

    void kDX11Driver::resizeFBOColorTexture(uint32_t texId, int width, int height)
    {
        auto it = textures.find(texId);
        if (it == textures.end()) return;

        D3D11TextureData &td = it->second;

        // Release old
        if (td.texture) td.texture->Release();
        if (td.srv)     { td.srv->Release(); td.srv = nullptr; }

        td.width  = width;
        td.height = height;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = 1;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags          = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
        if (SUCCEEDED(hr))
        {
            d3dDevice->CreateShaderResourceView(td.texture, nullptr, &td.srv);
        }

        // Re-attach to any FBOs that reference this texture
        for (auto &fboKv : framebuffers)
        {
            for (size_t i = 0; i < fboKv.second.colorTextures.size(); ++i)
            {
                // We don't track which texture ID an FBO uses, so this is
                // best-effort. The renderer will re-attach after resize.
            }
        }
    }

    void kDX11Driver::resizeFBOColorTextureMSAA(uint32_t texId, int samples, int width, int height)
    {
        auto it = textures.find(texId);
        if (it == textures.end()) return;

        D3D11TextureData &td = it->second;

        if (td.texture) td.texture->Release();
        if (td.srv)     { td.srv->Release(); td.srv = nullptr; }

        td.width   = width;
        td.height  = height;
        td.samples = samples;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width              = (UINT)width;
        desc.Height             = (UINT)height;
        desc.MipLevels          = 1;
        desc.ArraySize          = 1;
        desc.Format             = td.format;
        desc.SampleDesc.Count   = (UINT)samples;
        desc.Usage              = D3D11_USAGE_DEFAULT;
        desc.BindFlags          = D3D11_BIND_RENDER_TARGET;

        // Multisampled targets cannot have an SRV (see createFBOColorTextureMSAA).
        d3dDevice->CreateTexture2D(&desc, nullptr, &td.texture);
    }

    // =========================================================================
    // Back-buffer helpers
    // =========================================================================

    bool kDX11Driver::createBackBufferResources()
    {
        releaseBackBufferResources();

        ID3D11Texture2D *backBuffer = nullptr;
        HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&backBuffer);
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] GetBuffer failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }

        // Query the swap-chain description while the buffer is still alive: the
        // code below used to call backBuffer->GetDesc() *after* backBuffer had
        // been released, which is a use-after-free.
        DXGI_SWAP_CHAIN_DESC scDesc = {};
        hr = swapChain->GetDesc(&scDesc);
        if (FAILED(hr))
            return false;

        hr = d3dDevice->CreateRenderTargetView(backBuffer, nullptr, &backBufferRTV);
        backBuffer->Release();
        backBuffer = nullptr;
        if (FAILED(hr))
        {
            std::cout << "[kDX11Driver] CreateRenderTargetView for back buffer failed: 0x"
                      << std::hex << hr << std::dec << std::endl;
            return false;
        }

        // Create a depth-stencil buffer for the back buffer
        D3D11_TEXTURE2D_DESC dsDesc = {};

        dsDesc.Width              = scDesc.BufferDesc.Width;
        dsDesc.Height             = scDesc.BufferDesc.Height;
        dsDesc.MipLevels          = 1;
        dsDesc.ArraySize          = 1;
        dsDesc.Format             = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsDesc.SampleDesc.Count   = 1;
        dsDesc.SampleDesc.Quality = 0;
        dsDesc.Usage              = D3D11_USAGE_DEFAULT;
        dsDesc.BindFlags          = D3D11_BIND_DEPTH_STENCIL;

        ID3D11Texture2D *dsTexture = nullptr;
        hr = d3dDevice->CreateTexture2D(&dsDesc, nullptr, &dsTexture);
        if (SUCCEEDED(hr))
        {
            d3dDevice->CreateDepthStencilView(dsTexture, nullptr, &backBufferDSV);
            dsTexture->Release();
        }

        return true;
    }

    void kDX11Driver::releaseBackBufferResources()
    {
        if (backBufferRTV) { backBufferRTV->Release(); backBufferRTV = nullptr; }
        if (backBufferDSV) { backBufferDSV->Release(); backBufferDSV = nullptr; }
    }

    // =========================================================================
    // Private helpers — conversions
    // =========================================================================

    D3D11_BLEND kDX11Driver::toD3DBlend(kBlendFactor factor)
    {
        switch (factor)
        {
        case kBlendFactor::ZERO:                return D3D11_BLEND_ZERO;
        case kBlendFactor::ONE:                 return D3D11_BLEND_ONE;
        case kBlendFactor::SRC_ALPHA:           return D3D11_BLEND_SRC_ALPHA;
        case kBlendFactor::ONE_MINUS_SRC_ALPHA: return D3D11_BLEND_INV_SRC_ALPHA;
        case kBlendFactor::SRC_COLOR:           return D3D11_BLEND_SRC_COLOR;
        case kBlendFactor::ONE_MINUS_SRC_COLOR: return D3D11_BLEND_INV_SRC_COLOR;
        case kBlendFactor::DST_ALPHA:           return D3D11_BLEND_DEST_ALPHA;
        case kBlendFactor::ONE_MINUS_DST_ALPHA: return D3D11_BLEND_INV_DEST_ALPHA;
        default:                                return D3D11_BLEND_ONE;
        }
    }

    D3D11_CULL_MODE kDX11Driver::toD3DCullMode(kCullMode mode)
    {
        switch (mode)
        {
        case kCullMode::BACK:  return D3D11_CULL_BACK;
        case kCullMode::FRONT: return D3D11_CULL_FRONT;
        case kCullMode::FRONT_AND_BACK:
            // D3D11 has no way to cull both winding orders (there is no
            // "cull everything" enum), so this degrades to no culling.  The
            // engine only uses it for overlays that must remain visible.
            return D3D11_CULL_NONE;
        default:
            return D3D11_CULL_BACK;
        }
    }

    D3D11_PRIMITIVE_TOPOLOGY kDX11Driver::toD3DTopology(kPrimitiveType type)
    {
        switch (type)
        {
        case kPrimitiveType::TRIANGLES:      return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case kPrimitiveType::TRIANGLE_STRIP: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case kPrimitiveType::TRIANGLE_FAN:   return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; // No fan in D3D11
        case kPrimitiveType::LINES:          return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case kPrimitiveType::LINE_STRIP:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case kPrimitiveType::POINTS:         return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        default:                             return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
    }

    D3D11_PRIMITIVE_TOPOLOGY kDX11Driver::toD3DTopologyTriangles()
    {
        return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }

} // namespace kemena

#endif // KEMENA_D3D11
