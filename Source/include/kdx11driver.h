/**
 * @file kdx11driver.h
 * @brief DirectX 11 implementation of the kDriver interface for Windows desktop.
 *
 * Targets Direct3D 11 with feature level 11.0. Uses the SDL3 window's native
 * HWND to create the device and swap chain.  HLSL shaders are compiled at
 * runtime via D3DCompile.
 */

#ifndef KDX11DRIVER_H
#define KDX11DRIVER_H

#ifdef KEMENA_D3D11

// Windows headers MUST come before kemena headers to avoid macro pollution
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

#include "kdriver.h"
#include "kwindow.h"

#include <unordered_map>
#include <vector>
#include <memory>
#include <cstring>

namespace kemena
{
    // -------------------------------------------------------------------------
    // Internal data structures
    // -------------------------------------------------------------------------

    /** @brief Kind of a reflected HLSL type, used to emulate cbuffer packing. */
    enum class D3D11TypeKind
    {
        Scalar,
        Vector,
        Matrix,
        Struct,
        Other
    };

    /**
     * @brief Compact copy of a reflected HLSL type.
     *
     * D3D11 reflection only exposes the top-level variables of a constant buffer,
     * while the engine addresses uniforms the way GL does — @c "material.diffuse",
     * @c "sunLights[3].position", @c "u_Tiling[1]".  This tree is captured once at
     * compile time so those paths can be resolved to a byte offset.  Member offsets
     * are not reported by D3DReflect, so they are recomputed with HLSL constant
     * buffer packing rules (see kDX11Driver::resolveUniformPath()).
     */
    struct D3D11TypeInfo
    {
        D3D11TypeKind kind = D3D11TypeKind::Other;
        uint32_t      rows = 1;         ///< Vector/matrix rows.
        uint32_t      columns = 1;      ///< Vector/matrix columns.
        uint32_t      elements = 0;     ///< Array length (0 = not an array).
        std::vector<kString>         memberNames;
        std::vector<D3D11TypeInfo>   memberTypes;
    };

    /** @brief Describes a single uniform variable inside a constant buffer. */
    struct D3D11UniformInfo
    {
        uint32_t cbSlot;    ///< Constant buffer register slot (b0, b1, ...).
        uint32_t offset;    ///< Byte offset within the constant buffer.
        uint32_t size;      ///< Size in bytes (must be ≤ 16 for a single vec4/mat4 row).
        D3D11TypeInfo type; ///< Reflected type, for member/index path resolution.
    };

    /** @brief Holds all GPU objects for one compiled shader program. */
    struct D3D11ProgramData
    {
        ID3D11VertexShader *vs = nullptr;
        ID3D11PixelShader  *ps = nullptr;
        ID3D11InputLayout  *inputLayout = nullptr;

        /// Maps uniform name → constant-buffer slot + offset.
        std::unordered_map<kString, D3D11UniformInfo> uniforms;

        /// Maps sampler name → texture/sampler register (t#, s#) it was compiled
        /// into.  HLSL registers are fixed at compile time, unlike GL's dynamic
        /// texture units.
        std::unordered_map<kString, uint32_t> samplers;

        /// Per-slot constant buffers (created lazily, sized to the largest needed).
        std::unordered_map<uint32_t, ID3D11Buffer *> constantBuffers;

        /// CPU-side shadow of each constant buffer, so we can update sub-regions.
        std::unordered_map<uint32_t, std::vector<uint8_t>> cbShadows;

        /// Maximum size in bytes for each CB slot discovered during reflection.
        std::unordered_map<uint32_t, uint32_t> cbSizes;

        /// Dirty flag per CB slot — true when the shadow has been modified and
        /// needs uploading before the next draw call.
        std::unordered_map<uint32_t, bool> cbDirty;

        /// Vertex-shader byte code, kept alive after compilation so that input
        /// layouts can be created lazily from the shader's input signature
        /// (D3D11 requires the VS signature to build a layout).
        ID3DBlob *vsBlob = nullptr;

        D3D11ProgramData() = default;
        // The struct owns COM pointers, so copies are forbidden and the moves
        // must be written by hand — an implicitly generated move would leave the
        // source holding the same pointers and double-release them.
        D3D11ProgramData(const D3D11ProgramData &) = delete;
        D3D11ProgramData &operator=(const D3D11ProgramData &) = delete;
        D3D11ProgramData(D3D11ProgramData &&other) noexcept;
        D3D11ProgramData &operator=(D3D11ProgramData &&other) noexcept;
        ~D3D11ProgramData();
    };

    /** @brief Per-vertex-attribute descriptor for an input layout. */
    struct D3D11AttribDesc
    {
        int   location;
        int   components;   ///< 1–4
        int   stride;       ///< 0 = tightly packed (GL semantics)
        size_t offset;
        bool  isInteger;

        /// Index into D3D11VertexArrayData::vertexBuffers that this attribute
        /// reads from.  The GL backend derives it from the currently bound
        /// GL_ARRAY_BUFFER, so each attribute may live in its own buffer.
        uint32_t slot = 0;

        /// Vertex attribute divisor (0 = per vertex, 1 = per instance).
        uint32_t divisor = 0;
    };

    /** @brief Holds vertex/index buffer bindings and input layout for one VAO. */
    struct D3D11VertexArrayData
    {
        ID3D11InputLayout *inputLayout = nullptr;
        /// Program the cached inputLayout was built for (layouts are
        /// signature-specific, so they must be rebuilt when the shader changes).
        uint32_t           inputLayoutProgram = 0;

        /// Index buffer — owned (AddRef'd) by this VAO.
        ID3D11Buffer      *indexBuffer = nullptr;
        DXGI_FORMAT        indexFormat = DXGI_FORMAT_R32_UINT;
        uint32_t           indexCount  = 0;

        struct VB binding
        {
            ID3D11Buffer *buffer = nullptr;   ///< Owned (AddRef'd) by this VAO.
            uint32_t      stride = 0;
            uint32_t      offset = 0;
        };
        std::vector<VB> vertexBuffers;

        std::vector<D3D11AttribDesc> attribs;
    };

    /** @brief Holds texture objects and their SRV / sampler. */
    struct D3D11TextureData
    {
        ID3D11Texture2D          *texture = nullptr;
        ID3D11ShaderResourceView *srv     = nullptr;
        ID3D11SamplerState       *sampler = nullptr;
        DXGI_FORMAT               format  = DXGI_FORMAT_R8G8B8A8_UNORM;
        int                       width   = 0;
        int                       height  = 0;
        int                       samples = 1;
        int                       layers  = 1;  ///< >1 for texture arrays
        bool                      isCube  = false;
        bool                      isDepth = false;
        bool                      mips    = false; ///< Auto-generated mip chain present.
    };

    /** @brief Holds FBO colour + depth attachments. */
    struct D3D11FramebufferData
    {
        /// Colour attachments (render-target views).
        std::vector<ID3D11RenderTargetView *> colorRTVs;
        /// Depth-stencil view (optional).
        ID3D11DepthStencilView *depthDSV = nullptr;
        /// Underlying textures owned by this FBO.
        std::vector<ID3D11Texture2D *> colorTextures;
        ID3D11Texture2D *depthTexture = nullptr;
        /// Cached SRVs so we can read colour attachments back.
        std::vector<ID3D11ShaderResourceView *> colorSRVs;
    };

    // -------------------------------------------------------------------------
    // kDX11Driver
    // -------------------------------------------------------------------------

    /**
     * @brief DirectX 11 graphics driver for Windows desktop.
     *
     * Implements the kDriver interface using D3D11 calls.  Because D3D11 does
     * not have the GL state-machine model, the driver caches pipeline state
     * internally and applies it lazily before each draw call.
     *
     * Do not instantiate this class directly — use kRenderer::init() with
     * kRendererType::RENDERER_D3D11.
     */
    class KEMENA3D_API kDX11Driver : public kDriver
    {
    public:
        kDX11Driver();
        ~kDX11Driver() override;

        // --- Lifecycle -------------------------------------------------------
        bool init(kWindow *window) override;
        void destroy() override;
        void makeCurrent(kWindow *window) override;  ///< No-op: D3D11 immediate context is always current.
        void *getNativeContext() override;
        kString getApiVersion() override;
        kString getShaderVersion() override;
        kRendererType getRendererType() const override;
        void swapBuffers() override;
        void resizeSwapChain(int width, int height) override;
        void *getImTextureID(uint32_t id) override;

        /** @brief Returns the D3D11 device context (needed by ImGui DX11 backend). */
        ID3D11DeviceContext *getDeviceContext() { return d3dContext; }

        /**
         * @brief Returns the texture unit a sampler was compiled to.
         *
         * HLSL sampler registers are fixed at compile time (t0, t1, …), so callers
         * must bind textures to the register the shader actually samples.  Returns
         * -1 when the program or the sampler is unknown, which tells the caller to
         * fall back to a free unit (the OpenGL behaviour).
         */
        int getTextureUnitForSampler(uint32_t progId, const kString &name) override;

        // --- Frame state -----------------------------------------------------
        void setClearColor(float r, float g, float b, float a) override;
        void clear(bool color, bool depth, bool stencil) override;
        void setViewport(int x, int y, int width, int height) override;

        // --- Pipeline state --------------------------------------------------
        void setDepthTest(bool enable) override;
        void setDepthWrite(bool enable) override;
        void setBlend(bool enable) override;
        void setBlendFunc(kBlendFactor src, kBlendFactor dst) override;
        void setCullFace(bool enable) override;
        void setCullMode(kCullMode mode) override;
        void setFrontFace(kFrontFace face) override;
        void setMultisample(bool enable) override;
        void setSRGBEncoding(bool enable) override;
        void setSampleAlphaToCoverage(bool enable) override;
        void setWireframe(bool enable) override;

        // --- Shader programs -------------------------------------------------

        /**
         * @brief Compiles a shader program from user-supplied source.
         *
         * Sources are not translated by the engine, so which language to pass
         * depends on the active backend: GLSL for kOpenGLDriver / kOpenGLESDriver,
         * HLSL for this backend (compiled with D3DCompile as vs_5_0 / ps_5_0 using
         * the entry points @c VSMain and @c PSMain, falling back to @c main).
         *
         * See kShader::loadHlslCodeDX11() / kShader::loadHlslFileDX11() for the
         * HLSL entry points.  A user vertex shader must declare its inputs with
         * the semantic names produced by kDX11Driver::attribSemantic().
         */
        uint32_t compileShaderProgram(const char *vertSrc, const char *fragSrc) override;
        uint32_t compileShaderProgramSpirv(const std::vector<uint8_t> &vertSpirv,
                                           const kString &vertEntry,
                                           const std::vector<uint8_t> &fragSpirv,
                                           const kString &fragEntry) override;
        void deleteShaderProgram(uint32_t id) override;
        void bindShaderProgram(uint32_t id) override;
        void unbindShaderProgram() override;

        void setUniformBool(uint32_t progId, const kString &name, bool v) override;
        void setUniformInt(uint32_t progId, const kString &name, int v) override;
        void setUniformUint(uint32_t progId, const kString &name, uint32_t v) override;
        void setUniformFloat(uint32_t progId, const kString &name, float v) override;
        void setUniformVec2(uint32_t progId, const kString &name, const kVec2 &v) override;
        void setUniformVec3(uint32_t progId, const kString &name, const kVec3 &v) override;
        void setUniformVec4(uint32_t progId, const kString &name, const kVec4 &v) override;
        void setUniformMat4(uint32_t progId, const kString &name, const kMat4 &v) override;
        void setUniformMat4Array(uint32_t progId, const kString &name, const std::vector<kMat4> &v) override;

        // --- Vertex arrays ---------------------------------------------------
        uint32_t createVertexArray() override;
        void deleteVertexArray(uint32_t id) override;
        void bindVertexArray(uint32_t id) override;
        void unbindVertexArray() override;

        // --- Buffers ---------------------------------------------------------
        uint32_t createBuffer() override;
        void deleteBuffer(uint32_t id) override;
        void uploadIndexBuffer(uint32_t bufferId, const void *data, size_t size) override;
        void uploadVertexBuffer(uint32_t bufferId, const void *data, size_t size) override;
        void updateBufferSubData(uint32_t bufferId, const void *data, size_t size, size_t offset) override;
        void setVertexAttribFloat(int location, int components, int stride, size_t offset) override;
        void setVertexAttribInt(int location, int components, int stride, size_t offset) override;
        void setVertexAttribDivisor(int location, int divisor) override;

        // --- Draw calls ------------------------------------------------------
        void drawIndexed(uint32_t vaoId, int indexCount) override;
        void drawIndexedInstanced(uint32_t vaoId, int indexCount, int instanceCount) override;
        void drawArrays(uint32_t vaoId, kPrimitiveType type, int vertexCount) override;
        void drawArraysInstanced(uint32_t vaoId, kPrimitiveType type, int vertexCount, int instanceCount) override;

        // --- Texture creation (for asset loading) ----------------------------
        uint32_t createTexture2D(int width, int height, kTextureFormat format,
                                 const void *data,
                                 kTextureWrap wrap = kTextureWrap::REPEAT,
                                 kTextureFilter minFilter = kTextureFilter::LINEAR_MIPMAP_LINEAR,
                                 kTextureFilter magFilter = kTextureFilter::LINEAR,
                                 bool generateMips = true) override;
        uint32_t createTextureCube(int width, int height,
                                   const void *faceData[6],
                                   bool generateMips = true) override;
        void uploadTexture2D(uint32_t id, int level, int width, int height,
                             kTextureFormat format, const void *data) override;
        void uploadTexture2DSub(uint32_t id, int level, int x, int y,
                                int width, int height,
                                kTextureFormat format, const void *data) override;
        void uploadCompressedTexture2D(uint32_t id, int level,
                                       int width, int height,
                                       kTextureFormat format,
                                       const void *data, size_t dataSize) override;
        void uploadTextureCubeFace(uint32_t id, int face, int width, int height,
                                   const void *data) override;
        void deleteTexture(uint32_t id) override;

        // --- Texture sampling ------------------------------------------------
        void bindTexture2D(int unit, uint32_t id) override;
        void bindTexture2DArray(int unit, uint32_t id) override;
        void bindTextureCube(int unit, uint32_t id) override;
        void unbindTexture2D(int unit) override;
        void unbindTexture2DArray(int unit) override;
        void unbindTextureCube(int unit) override;
        void generateMipmaps2D(uint32_t id) override;
        void readTexture2DRGB(uint32_t id, int mipLevel, float *pixels) override;
        void readPixelsRGBA(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) override;

        // --- Framebuffers ----------------------------------------------------
        uint32_t createFramebuffer() override;
        void deleteFramebuffer(uint32_t id) override;
        void bindFramebuffer(uint32_t id) override;
        void bindReadFramebuffer(uint32_t id) override;
        void bindDrawFramebuffer(uint32_t id) override;
        void unbindFramebuffer() override;
        bool isFramebufferComplete() override;
        void blitFramebufferColor(int srcX0, int srcY0, int srcX1, int srcY1,
                                  int dstX0, int dstY0, int dstX1, int dstY1) override;
        void setFramebufferDrawBuffer() override;

        // --- Renderbuffers ---------------------------------------------------
        uint32_t createRenderbuffer() override;
        void deleteRenderbuffer(uint32_t id) override;
        void setupRenderbuffer(uint32_t rboId, int width, int height) override;
        void setupRenderbufferMSAA(uint32_t rboId, int samples, int width, int height) override;
        void attachRenderbufferDepthStencil(uint32_t fboId, uint32_t rboId) override;

        // --- FBO-managed textures --------------------------------------------
        uint32_t createFBOColorTexture(int width, int height) override;
        uint32_t createFBOColorTextureMSAA(int samples, int width, int height) override;
        uint32_t createFBODepthTexture(int width, int height) override;
        uint32_t createFBODepthTextureArray(int width, int height, int layers) override;
        void deleteFBOTexture(uint32_t id) override;
        void attachFBOColorTexture(uint32_t fboId, uint32_t texId) override;
        void attachFBOColorTextureMSAA(uint32_t fboId, uint32_t texId) override;
        void attachFBODepthTexture(uint32_t fboId, uint32_t texId) override;
        void attachFBODepthTextureLayer(uint32_t fboId, uint32_t texId, int layer) override;
        void resizeFBOColorTexture(uint32_t texId, int width, int height) override;
        void resizeFBOColorTextureMSAA(uint32_t texId, int samples, int width, int height) override;

    private:
        // --- D3D11 core objects ----------------------------------------------
        ID3D11Device        *d3dDevice        = nullptr;
        ID3D11DeviceContext *d3dContext       = nullptr;
        IDXGISwapChain      *swapChain        = nullptr;
        ID3D11RenderTargetView *backBufferRTV = nullptr;
        ID3D11DepthStencilView *backBufferDSV = nullptr;

        D3D_FEATURE_LEVEL    featureLevel     = D3D_FEATURE_LEVEL_11_0;
        float                clearColor[4]    = {0.0f, 0.0f, 0.0f, 1.0f};

        /// Current back-buffer size, used to skip redundant ResizeBuffers calls.
        int                  swapChainWidth   = 0;
        int                  swapChainHeight  = 0;

        /// Set once the device reports DEVICE_REMOVED/RESET, so the failure is
        /// logged a single time instead of every frame.
        bool                 deviceRemoved    = false;

        // --- Resource maps ---------------------------------------------------
        std::unordered_map<uint32_t, D3D11ProgramData>    programs;
        std::unordered_map<uint32_t, D3D11VertexArrayData> vertexArrays;
        std::unordered_map<uint32_t, ID3D11Buffer *>       buffers;
        std::unordered_map<uint32_t, D3D11TextureData>     textures;
        std::unordered_map<uint32_t, D3D11FramebufferData> framebuffers;

        /// CPU-side copy of every buffer uploaded through uploadVertexBuffer /
        /// uploadIndexBuffer.  IMMUTABLE buffers cannot be mapped, and mapping a
        /// DYNAMIC buffer with WRITE_DISCARD wipes the untouched regions, so
        /// sub-data updates patch this copy and re-upload it in full.
        std::unordered_map<uint32_t, std::vector<uint8_t>> bufferShadows;

        // --- ID allocators ---------------------------------------------------
        uint32_t nextProgramId    = 1;
        uint32_t nextVAOId        = 1;
        uint32_t nextBufferId     = 1;
        uint32_t nextTextureId    = 1;
        uint32_t nextFBOId        = 1;

        // --- Pipeline state cache --------------------------------------------
        bool        depthTestEnabled   = true;
        bool        depthWriteEnabled  = true;
        bool        blendEnabled       = false;
        kBlendFactor blendSrc          = kBlendFactor::ONE;
        kBlendFactor blendDst          = kBlendFactor::ZERO;
        bool        cullFaceEnabled    = true;
        kCullMode   cullMode           = kCullMode::BACK;
        kFrontFace  frontFace          = kFrontFace::CCW;
        bool        msaaEnabled        = true;
        bool        srgbEnabled        = true;
        bool        alphaToCoverage    = false;
        bool        wireframeEnabled   = false;

        ID3D11DepthStencilState  *depthStencilState  = nullptr;
        ID3D11BlendState         *blendState         = nullptr;
        ID3D11RasterizerState    *rasterizerState    = nullptr;
        bool                      depthStencilDirty  = true;
        bool                      blendDirty         = true;
        bool                      rasterizerDirty    = true;

        // --- Current bindings ------------------------------------------------
        uint32_t            currentProgram  = 0;
        uint32_t            currentVAO      = 0;
        ID3D11RenderTargetView *currentRTVs[8] = {};
        ID3D11DepthStencilView *currentDSV      = nullptr;
        uint32_t            currentFBO      = 0;  ///< Active draw target (0 = back buffer)
        uint32_t            currentReadFBO  = 0;  ///< Source of blitFramebufferColor()

        /// Buffer most recently passed to uploadVertexBuffer()/updateBufferSubData().
        /// Equivalent to GL's currently bound GL_ARRAY_BUFFER: the following
        /// setVertexAttrib*() call binds its attributes to this buffer.
        uint32_t            currentArrayBufferId = 0;

        // --- Bound textures per slot -----------------------------------------
        struct BoundTexture
        {
            uint32_t              id = 0;
            ID3D11ShaderResourceView *srv = nullptr;
            ID3D11SamplerState       *sampler = nullptr;
            bool isCube  = false;
            bool isArray = false;
        };
        BoundTexture boundTextures[16];

        // --- Viewport --------------------------------------------------------
        D3D11_VIEWPORT currentViewport = {};

        // --- Helper methods --------------------------------------------------
        /** Creates or re-creates the back-buffer RTV + DSV from the swap chain. */
        bool createBackBufferResources();

        /** Binds index/vertex buffers plus the input layout for a VAO. */
        void bindVAOState(D3D11VertexArrayData &va);

        /** Re-points every VAO reference from one GPU buffer to another. */
        void retargetVAOBuffers(ID3D11Buffer *oldBuf, ID3D11Buffer *newBuf);

        /** Maps a GL attribute location to a D3D11 input semantic. */
        static void attribSemantic(int location, const char *&name, UINT &index);

        /** Releases the back-buffer RTV and DSV. */
        void releaseBackBufferResources();

        /** Compiles a single HLSL shader stage into a blob. */
        ID3DBlob *compileHlslStage(const char *src, const char *entryPoint,
                                   const char *target);

        /** Reflects on a compiled shader to discover constant buffer variables. */
        void reflectConstantBuffers(ID3DBlob *vsBlob, ID3DBlob *psBlob,
                                    D3D11ProgramData &prog);

        /**
         * @brief Writes a value into a program's constant-buffer shadow.
         *
         * The name may be a flat uniform, a struct member or an array element
         * ("material.diffuse", "u_Tiling[2]", "sunLights[3].position"); the path is
         * resolved with resolveUniformPath().  Unknown paths are ignored, which is
         * how optional uniforms (a shader that does not declare a given parameter)
         * are handled.
         */
        void writeUniform(D3D11ProgramData &prog, const kString &name,
                          const void *data, size_t size);

        /**
         * @brief Resolves a uniform path to a constant-buffer slot and byte range.
         *
         * Implements the engine-wide uniform naming convention — the one the
         * DirectX backend requires because HLSL packs uniforms into constant
         * buffers rather than exposing one location per uniform:
         *   * flat top-level variables:  "viewMatrix"
         *   * struct members:            "material.diffuse"
         *   * array elements:            "u_Tiling[2]", "finalBoneMatrices[7]"
         *   * combinations:              "sunLights[3].position"
         * Members and elements are resolved against the reflected type tree, with
         * HLSL constant-buffer packing rules applied to compute their offsets.
         *
         * @return false when the path does not exist in this program.
         */
        static bool resolveUniformPath(D3D11ProgramData &prog, const kString &name,
                                       uint32_t &outCbSlot, uint32_t &outOffset,
                                       uint32_t &outSize);

        /** Packed size in bytes of one element of the given reflected type. */
        static uint32_t packedTypeSize(const D3D11TypeInfo &type);

        /** Byte offset (and size) of a struct member, per HLSL cbuffer packing. */
        static uint32_t memberOffset(const D3D11TypeInfo &type, size_t index,
                                     uint32_t &outSize);

        /** Creates an input layout from vertex shader reflection + attribute descriptors. */
        ID3D11InputLayout *createInputLayout(ID3DBlob *vsBlob,
                                             const std::vector<D3D11AttribDesc> &attribs);

        /** Uploads all dirty constant buffers for the given program. */
        void flushConstantBuffers(D3D11ProgramData &prog);

        /** Applies the current cached pipeline state to the device context. */
        void applyPipelineState();

        /** Converts kBlendFactor to D3D11 blend enum. */
        static D3D11_BLEND toD3DBlend(kBlendFactor factor);

        /** Converts kCullMode to D3D11 cull enum. */
        static D3D11_CULL_MODE toD3DCullMode(kCullMode mode);

        /** Converts kPrimitiveType to D3D11 primitive topology. */
        static D3D11_PRIMITIVE_TOPOLOGY toD3DTopology(kPrimitiveType type);

        /** Converts kPrimitiveType for triangle-based draws to D3D topology. */
        static D3D11_PRIMITIVE_TOPOLOGY toD3DTopologyTriangles();
    };

} // namespace kemena

#endif // KEMENA_D3D11

#endif // KDX11DRIVER_H
