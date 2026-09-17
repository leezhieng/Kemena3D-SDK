/**
 * @file kopenglesdriver.h
 * @brief OpenGL ES 3.0 graphics driver for mobile and embedded platforms.
 *
 * This backend is intentionally restricted to platforms where OpenGL ES is the
 * native / only available graphics API:
 *
 *  - Android          (GLESv3 + EGL from the NDK)
 *  - iOS              (OpenGLES/ES3 + EAGL; deprecated but functional)
 *  - Embedded Linux   (EGL + GLESv2/v3, e.g. Mali / Adreno / PowerVR / Vivante)
 *  - Web              (WebGL 2.0 / Emscripten, GLES-compatible subset)
 *
 * The desktop (Windows / macOS / desktop Linux) OpenGL path is served by
 * kOpenGLDriver.  Building this file for a desktop target is a configuration
 * error and is rejected at compile time, because the ES 3.0 feature set and
 * the EGL/GLES header layout are not interchangeable with desktop GL.
 *
 * There is no GLEW dependency: GLES entry points are resolved either by the
 * platform loader (Android / iOS / Web) or through EGL on embedded Linux.
 */

#ifndef KOPENGLESDRIVER_H
#define KOPENGLESDRIVER_H

#include "kdriver.h"
#include "kwindow.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Platform detection
//
// Exactly one of the KEMENA_GLES_PLATFORM_* macros must be defined before this
// header is processed.  KEMENA_GLES_EMULATED is an explicit developer opt-in
// that lets a desktop build translate to the ES subset (via ANGLE or Mesa) so
// shader/pipeline code can be exercised without a device.  It is never enabled
// implicitly.
// ---------------------------------------------------------------------------

#if defined(__ANDROID__)
#  define KEMENA_GLES_PLATFORM_ANDROID 1
#  define KEMENA_GLES_PLATFORM_NAME "Android"
#elif defined(__APPLE__) && (defined(__IPHONE_OS_VERSION_MIN_REQUIRED) || defined(TARGET_OS_IPHONE))
#  define KEMENA_GLES_PLATFORM_IOS 1
#  define KEMENA_GLES_PLATFORM_NAME "iOS"
#elif defined(__EMSCRIPTEN__)
#  define KEMENA_GLES_PLATFORM_WEB 1
#  define KEMENA_GLES_PLATFORM_NAME "Web (WebGL 2)"
#elif defined(KEMENA_GLES_EMBEDDED) || defined(__linux__)
#  define KEMENA_GLES_PLATFORM_EMBEDDED 1
#  define KEMENA_GLES_PLATFORM_NAME "Embedded Linux (EGL)"
#elif defined(KEMENA_GLES_EMULATED)
#  define KEMENA_GLES_PLATFORM_EMULATED 1
#  define KEMENA_GLES_PLATFORM_NAME "Desktop (emulated GLES)"
#else
// No ES platform macro was set by the toolchain.  Decide whether the ES
// headers exist at all:
//   * Headers present (ANGLE / Mesa / PowerVR SDK installed) → emulated mode,
//     so the pipeline can still be exercised off-device.
//   * Headers absent → the class is declared but inert.  The source file is
//     excluded from desktop builds by CMake, and kRenderer refuses to select
//     RENDERER_GLES when KEMENA_GLES is undefined.
#  if defined(__has_include)
#    if __has_include(<GLES3/gl3.h>)
#      define KEMENA_GLES_PLATFORM_EMULATED 1
#      define KEMENA_GLES_PLATFORM_NAME "Desktop (emulated GLES)"
#    else
#      define KEMENA_GLES_PLATFORM_UNSUPPORTED 1
#      define KEMENA_GLES_PLATFORM_NAME "Unsupported target (no GLES3/EGL headers)"
#    endif
#  else
#    define KEMENA_GLES_PLATFORM_UNSUPPORTED 1
#    define KEMENA_GLES_PLATFORM_NAME "Unsupported target (no __has_include)"
#  endif
#endif

// ---------------------------------------------------------------------------
// EGL availability
//
// EGL is the windowing/context layer on Android and embedded Linux.  iOS and
// Web provide their own context layer (EAGL / HTML canvas), so EGL is not
// required there.
// ---------------------------------------------------------------------------
// EGL is probed rather than assumed: several host SDKs ship GLES3 headers
// without the matching EGL ones (the Windows SDK is a notable example), and
// iOS/Web supply their own context layer instead of EGL.
#if defined(KEMENA_GLES_PLATFORM_ANDROID) || defined(KEMENA_GLES_PLATFORM_EMBEDDED) || \
    defined(KEMENA_GLES_PLATFORM_EMULATED)
#  if defined(__has_include)
#    if __has_include(<EGL/egl.h>)
#      define KEMENA_GLES_HAS_EGL 1
#    endif
#  else
#    define KEMENA_GLES_HAS_EGL 1
#  endif
#endif

// ---------------------------------------------------------------------------
// Graphics headers
// ---------------------------------------------------------------------------
#if !defined(KEMENA_GLES_PLATFORM_UNSUPPORTED)
  #if defined(KEMENA_GLES_PLATFORM_IOS)
    #include <OpenGLES/ES3/gl.h>
    #include <OpenGLES/ES3/glext.h>
  #else
    #ifndef GL_GLEXT_PROTOTYPES
    #define GL_GLEXT_PROTOTYPES 1
    #endif
    // Only the core ES 3.0 header is included.  The optional companion headers
    // (gl3ext.h / eglext.h) are deliberately NOT pulled in: everything this
    // driver needs from them — ASTC enums, EGL_OPENGL_ES3_BIT_KHR, the EGL
    // context attribute — is defined defensively in the .cpp instead.  That
    // keeps the backend buildable against minimal vendor SDKs and host SDKs
    // that ship gl3.h without its extension companions.
    #include <GLES3/gl3.h>
  #endif

  #if defined(KEMENA_GLES_HAS_EGL)
    #include <EGL/egl.h>
    #if defined(KEMENA_GLES_PLATFORM_ANDROID)
      #include <android/native_window.h>
    #endif
  #endif

  // ES 3.0 enumerants that some vendor headers omit.  Guarded so a complete
  // header set is never redefined.
  #ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
  #define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
  #endif

  #ifndef GL_DEPTH24_STENCIL8_OES
  #define GL_DEPTH24_STENCIL8_OES 0x88F0
  #endif

  #ifndef GL_MAX_SAMPLES
  #define GL_MAX_SAMPLES 0x8D57
  #endif
#endif // !KEMENA_GLES_PLATFORM_UNSUPPORTED

// ---------------------------------------------------------------------------
// SDL3 (optional)
//
// SDL is used on Android/iOS to own the platform window and event loop.  A
// bare-metal / vendor-SDK embedded target can omit it entirely by defining
// KEMENA_GLES_NO_SDL and driving the driver through initNative().
// ---------------------------------------------------------------------------
#if !defined(KEMENA_GLES_NO_SDL)
#  define KEMENA_GLES_HAS_SDL3 1
#  define NO_SDL_GLEXT
#  include <SDL3/SDL.h>
#endif

// ES 3.0 has no GL_TEXTURE_2D_MULTISAMPLE; multisample colour targets are
// renderbuffers, not textures.  See createFBOColorTextureMSAA().

namespace kemena
{
    /**
     * @brief OpenGL ES 3.0 graphics driver for mobile and embedded platforms.
     *
     * Implements kDriver on top of GLES 3.0.  Compared with kOpenGLDriver the
     * following desktop-only facilities are unavailable and are handled
     * explicitly rather than silently:
     *
     *  | Desktop GL feature             | ES 3.0 handling in this driver        |
     *  | ------------------------------ | ------------------------------------- |
     *  | glPolygonMode (wireframe)      | no-op (see setWireframe)              |
     *  | GL_MULTISAMPLE                 | no-op; MSAA is a framebuffer property |
     *  | glGetTexImage                  | FBO + glReadPixels                    |
     *  | GL_TEXTURE_2D_MULTISAMPLE      | multisample renderbuffer resolve      |
     *  | glDrawBuffer                   | glDrawBuffers                         |
     *  | GL_TEXTURE_COMPARE_MODE        | depth textures sampled in-shader      |
     *  | S3TC/DXT compression           | ETC2/EAC (core) and ASTC (extension)  |
     *  | GL_SAMPLE_ALPHA_TO_COVERAGE    | supported natively in ES 3.0          |
     *
     * Two context-creation paths are provided:
     *  - init(kWindow *)            — SDL3-managed window (Android / iOS).
     *  - initNative(void *, w, h)   — raw EGL on an ANativeWindow (Android) or
     *                                 an EGLNativeWindowType (embedded Linux).
     *
     * Do not instantiate directly — use kRenderer::init() with
     * kRendererType::RENDERER_GLES.
     */
    class KEMENA3D_API kOpenGLESDriver : public kDriver
    {
    public:
        /**
         * @brief How the ES context and its default framebuffer are obtained.
         */
        enum class kGLESSurfaceType
        {
            SDL,             ///< Context created through SDL_GL_CreateContext.
            EGL_NATIVE,      ///< EGL surface wrapped around a native window.
            EGL_PBUFFER,     ///< Headless EGL pbuffer (off-screen / compute).
            EAGL             ///< iOS CAEAGLLayer (context owned by the app).
        };

        kOpenGLESDriver() = default;
        ~kOpenGLESDriver() override;

        // --- Lifecycle -------------------------------------------------------

        /**
         * @brief Creates an OpenGL ES 3.0 context for an SDL-managed window.
         *
         * Used on Android and iOS where SDL3 owns the platform window.  When
         * KEMENA_GLES_NO_SDL is defined this returns false — use initNative().
         *
         * @param window Window whose SDL_Window backs the EGL surface.
         * @return true on success.
         */
        bool init(kWindow *window) override;

        /**
         * @brief Creates an ES 3.0 context directly through EGL.
         *
         * Intended for embedded Linux (a vendor EGLNativeWindowType) and
         * Android surfaces not managed by SDL (an ANativeWindow *).  Passing
         * nullptr for @p nativeWindow creates a headless pbuffer surface,
         * which is useful for off-screen rendering and automated tests.
         *
         * @param nativeWindow Platform window handle, or nullptr for a pbuffer.
         * @param width        Surface width in pixels.
         * @param height       Surface height in pixels.
         * @return true on success.
         */
        bool initNative(void *nativeWindow, int width, int height);

        /** @brief Releases EGL/SDL context resources and clears all caches. */
        void destroy() override;

        void makeCurrent(kWindow *window) override;

        /**
         * @brief Makes this driver's EGL context current without an SDL window.
         * @return true on success.
         */
        bool makeCurrentNative();

        void *getNativeContext() override;

        kString getApiVersion() override;
        kString getShaderVersion() override;

        // --- Frame presentation ----------------------------------------------

        /**
         * @brief Presents the back buffer.
         *
         * SDL-backed contexts use SDL_GL_SwapWindow (called by kRenderer), so
         * this is only meaningful for EGL_NATIVE / EGL_PBUFFER surfaces where
         * the driver owns the EGLSurface.  A no-op otherwise.
         */
        void swapBuffers() override;

        /** @brief Resizes the owned EGL surface (no-op for SDL surfaces). */
        void resizeNativeSurface(int width, int height);

        /** @brief Returns the surface type selected at init time. */
        kGLESSurfaceType getSurfaceType() const;

        /** @brief True when EGL owns the context and default framebuffer. */
        bool isNativeEGL() const;

        /** @brief Returns the EGLDisplay, or EGL_NO_DISPLAY when unused. */
        void *getEGLDisplay() const;

        /** @brief Returns the EGLContext, or EGL_NO_CONTEXT when unused. */
        void *getEGLContext() const;

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
         * @brief Compiles and links a shader program from GLSL ES source.
         *
         * Desktop-style sources are accepted and translated:
         *  - `#version 330 core` (and legacy 120/150) → `#version 300 es`
         *  - `attribute`  → `in`, `varying` → `in` / `out`
         *  - `texture2D` / `textureCube` → `texture`
         *  - `gl_FragColor` / `gl_FragData[n]` → a synthetic `out` variable
         *  - `layout(binding = N)` qualifiers are stripped (set via glUniform1i)
         *  - a default float precision qualifier is injected per stage
         */
        uint32_t compileShaderProgram(const char *vertSrc, const char *fragSrc) override;

        /** @brief SPIR-V is not supported by ES 3.0; always returns 0. */
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

        // --- Capability queries (mobile hardware varies wildly) --------------

        /** @brief True when the device can sample the given compressed format. */
        bool supportsCompressedFormat(kTextureFormat format);

        /** @brief True when ETC2/EAC is available (always true on ES 3.0). */
        bool hasETC2();

        /** @brief True when ASTC LDR is available (GL_KHR_texture_compression_astc_ldr). */
        bool hasASTC();

        /** @brief True when the device supports half-float render targets. */
        bool hasColorBufferHalfFloat();

        /** @brief Maximum 2D texture dimension reported by GL_MAX_TEXTURE_SIZE. */
        int getMaxTextureSize();

        /** @brief Maximum MSAA sample count reported by GL_MAX_SAMPLES. */
        int getMaxSamples();

        /** @brief Maximum number of texture image units. */
        int getMaxTextureUnits();

        /** @brief Maximum number of vertex attributes allowed by the device. */
        int getMaxVertexAttribs();

        /** @brief Device GL_RENDERER string (e.g. "Adreno (TM) 640"). */
        kString getRendererName();

        /** @brief Full GL_EXTENSIONS string, space separated. */
        kString getExtensions();

        // --- Helpers ---------------------------------------------------------

        /**
         * @brief Rewrites desktop GLSL into GLSL ES 3.00.
         *
         * See compileShaderProgram() for the full list of transformations.
         *
         * @param src        Original shader source.
         * @param isFragment If true, allows fragment-only rewrites such as the
         *                   gl_FragColor → `out` conversion.
         * @return Rewritten GLSL ES 3.00 source.
         */
        static kString adaptShaderSource(const char *src, bool isFragment);

        /**
         * @brief Returns true when the process is running against an ES context.
         *
         * Prefers the state captured by init(); falls back to inspecting the
         * live GL_VERSION string when no driver instance exists yet.
         */
        static bool isGLES();

        /** @brief Human-readable name of the compile-time target platform. */
        static const char *getPlatformName();

    private:
        // --- Context state ---------------------------------------------------

        kGLESSurfaceType surfaceType = kGLESSurfaceType::SDL;

#if defined(KEMENA_GLES_HAS_SDL3)
        SDL_GLContext glContext = nullptr;
#else
        void *glContext = nullptr;
#endif

#if defined(KEMENA_GLES_HAS_EGL)
        EGLDisplay eglDisplay = EGL_NO_DISPLAY;
        EGLContext eglContext = EGL_NO_CONTEXT;
        EGLSurface eglSurface = EGL_NO_SURFACE;
        EGLConfig  eglConfig  = nullptr;
        bool       ownsEGL    = false;   ///< True when destroy() must tear EGL down.
        int        surfaceWidth  = 0;
        int        surfaceHeight = 0;
#endif

        bool modeIsES = true;            ///< Cached result of the ES detection.

        // --- Capability cache ------------------------------------------------

        bool capsLoaded = false;
        int  cachedMaxTextureSize = 0;
        int  cachedMaxSamples = 0;
        int  cachedMaxTextureUnits = 0;
        int  cachedMaxVertexAttribs = 0;
        bool cachedETC2 = false;
        bool cachedASTC = false;
        bool cachedHalfFloatRT = false;
        kString cachedExtensions;
        kString cachedRenderer;

        /** @brief Populates the capability cache from the live context. */
        void loadCapabilities();

        // --- Resource bookkeeping --------------------------------------------
        //
        // ES has no glGetTexImage and no reliable way to query the object kind
        // behind a name, so 2D texture dimensions and renderbuffer identity are
        // tracked on the CPU side.  createFBOColorTextureMSAA() hands out a
        // renderbuffer name that shares the texture namespace, and blindly
        // deleting a name as both kinds would destroy an unrelated object.
        std::unordered_map<uint32_t, std::pair<int, int>> texture2DSizes;
        std::unordered_set<uint32_t> renderbufferNames;

        /** @brief Records the dimensions of a 2D texture for later readback. */
        void trackTexture2D(uint32_t id, int width, int height);

        /** @brief Formats a guarded glGetError() result for logging. */
        static kString glErrorString(const char *stage);

        /** @brief Clamps a requested sample count to the device maximum. */
        int clampSamples(int samples);

        /** @brief Block size of a compressed format, or 0 when uncompressed. */
        static int compressedBlockBytes(kTextureFormat format);

        /** @brief True when the value is one of the ETC2/EAC formats. */
        static bool isETC2Format(kTextureFormat format);

        /** @brief True when the value is one of the ASTC formats. */
        static bool isASTCFormat(kTextureFormat format);

        /** @brief Deletes whichever object kind @p id actually refers to. */
        void deleteTextureOrRenderbuffer(uint32_t id);

#if !defined(KEMENA_GLES_PLATFORM_UNSUPPORTED)
        // --- GL translation helpers ------------------------------------------
        // These reference GL types and are therefore only declared when the ES
        // headers were actually available.

        GLenum toGLBlendFactor(kBlendFactor factor);
        GLenum toGLPrimitiveType(kPrimitiveType type);
        GLuint compileShaderStage(GLenum stage, const char *src);

        /** @brief Internal depth-stencil format for the current platform. */
        GLenum depthStencilFormat();

        /** @brief Internal depth-only format for the current platform. */
        GLenum depthFormat();

        /** @brief GL internal format for a compressed kTextureFormat (0 if none). */
        GLenum compressedInternalFormat(kTextureFormat format);
#endif
    };

} // namespace kemena

#endif // KOPENGLESDRIVER_H
