#include "kopenglesdriver.h"

#include <glm/gtc/type_ptr.hpp>

#include <cctype>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// This translation unit is only meaningful on a mobile / embedded target.
// CMake excludes it from desktop builds; the guard below catches a misconfigured
// build early instead of producing a pile of missing-symbol link errors.
// ---------------------------------------------------------------------------
#if defined(KEMENA_GLES_PLATFORM_UNSUPPORTED)
#  error "kopenglesdriver.cpp requires an OpenGL ES platform (Android, iOS, Web, \
embedded Linux) or an explicit KEMENA_GLES_EMULATED build. Exclude this file \
from desktop targets and use kOpenGLDriver instead."
#endif

// ---------------------------------------------------------------------------
// Enumerants that are extensions (or new in EGL 1.5) and may be missing from
// older vendor headers.  Defined defensively so the file always compiles.
// ---------------------------------------------------------------------------
#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x0040
#endif

#ifndef EGL_CONTEXT_MAJOR_VERSION
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#endif

#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2 0x9274
#endif
#ifndef GL_COMPRESSED_SRGB8_ETC2
#define GL_COMPRESSED_SRGB8_ETC2 0x9275
#endif
#ifndef GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2
#define GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 0x9276
#endif
#ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#endif
#ifndef GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC
#define GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC 0x9279
#endif

// ASTC (GL_KHR_texture_compression_astc_ldr) — not part of ES 3.0 core.
#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
#define GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_6x6_KHR
#define GL_COMPRESSED_RGBA_ASTC_6x6_KHR 0x93B4
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_8x8_KHR
#define GL_COMPRESSED_RGBA_ASTC_8x8_KHR 0x93B7
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_12x12_KHR
#define GL_COMPRESSED_RGBA_ASTC_12x12_KHR 0x93BD
#endif

namespace kemena
{
    namespace
    {
        /// Name of the synthetic fragment output that replaces gl_FragColor.
        const char *kFragColorOutName = "kemenaFragColorOut";

        /// Active ES driver, used by the static isGLES() probe.
        kOpenGLESDriver *g_activeESDriver = nullptr;

        bool isIdentChar(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_';
        }

        /// Replaces @p from with @p to only where it is a whole identifier.
        void replaceWordAll(kString &s, const kString &from, const kString &to)
        {
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != kString::npos)
            {
                const size_t after = pos + from.size();
                const bool leftOk = (pos == 0) || !isIdentChar(s[pos - 1]);
                const bool rightOk = (after >= s.size()) || !isIdentChar(s[after]);

                if (leftOk && rightOk)
                {
                    s.replace(pos, from.size(), to);
                    pos += to.size();
                }
                else
                {
                    pos = after;
                }
            }
        }

        /// True when @p word appears as a standalone identifier in @p s.
        bool containsWord(const kString &s, const kString &word)
        {
            size_t pos = 0;
            while ((pos = s.find(word, pos)) != kString::npos)
            {
                const size_t after = pos + word.size();
                const bool leftOk = (pos == 0) || !isIdentChar(s[pos - 1]);
                const bool rightOk = (after >= s.size()) || !isIdentChar(s[after]);
                if (leftOk && rightOk)
                    return true;
                pos = after;
            }
            return false;
        }

        /**
         * Removes `layout(binding = N, ...)` qualifiers.
         *
         * GLSL ES 3.00 has no sampler binding qualifier; the unit must be
         * assigned with glUniform1i instead.  `layout(location = N)` on vertex
         * inputs is legal in ES 3.00 and is deliberately left untouched.
         */
        void stripLayoutBinding(kString &s)
        {
            size_t pos = 0;
            while ((pos = s.find("layout", pos)) != kString::npos)
            {
                const size_t after = pos + 6;
                if (after < s.size() && isIdentChar(s[after]))
                {
                    pos = after;
                    continue;
                }

                size_t open = s.find('(', after);
                if (open == kString::npos)
                    break;

                // Reject anything other than whitespace between layout and '('.
                bool blankBetween = true;
                for (size_t i = after; i < open; ++i)
                {
                    if (!std::isspace(static_cast<unsigned char>(s[i])))
                    {
                        blankBetween = false;
                        break;
                    }
                }
                if (!blankBetween)
                {
                    pos = open;
                    continue;
                }

                const size_t close = s.find(')', open);
                if (close == kString::npos)
                    break;

                const kString inner = s.substr(open + 1, close - open - 1);
                if (inner.find("binding") != kString::npos)
                {
                    size_t consumeEnd = close + 1;
                    while (consumeEnd < s.size() &&
                           (s[consumeEnd] == ' ' || s[consumeEnd] == '\t'))
                        ++consumeEnd;
                    s.erase(pos, consumeEnd - pos);
                }
                else
                {
                    pos = close + 1;
                }
            }
        }

        /**
         * Replaces `gl_FragData[n]` (bracket included) with @p to.
         *
         * Handles the index expression so the result is a legal l-value.
         */
        void replaceFragData(kString &s, const kString &to)
        {
            size_t pos = 0;
            const kString token = "gl_FragData";
            while ((pos = s.find(token, pos)) != kString::npos)
            {
                size_t end = pos + token.size();
                while (end < s.size() && std::isspace(static_cast<unsigned char>(s[end])))
                    ++end;

                if (end < s.size() && s[end] == '[')
                {
                    const size_t close = s.find(']', end);
                    if (close != kString::npos)
                        end = close + 1;
                }

                s.replace(pos, end - pos, to);
                pos += to.size();
            }
        }

        /// Byte offset just past the `#version` line, or kString::npos.
        size_t findVersionLineEnd(const kString &s)
        {
            const size_t v = s.find("#version");
            if (v == kString::npos)
                return kString::npos;

            const size_t nl = s.find('\n', v);
            return (nl == kString::npos) ? s.size() : nl + 1;
        }

#if defined(KEMENA_GLES_HAS_EGL)
        /// Human-readable EGL error name for logging.
        const char *eglErrorName(EGLint err)
        {
            switch (err)
            {
            case EGL_SUCCESS:             return "EGL_SUCCESS";
            case EGL_NOT_INITIALIZED:     return "EGL_NOT_INITIALIZED";
            case EGL_BAD_ACCESS:          return "EGL_BAD_ACCESS";
            case EGL_BAD_ALLOC:           return "EGL_BAD_ALLOC";
            case EGL_BAD_ATTRIBUTE:       return "EGL_BAD_ATTRIBUTE";
            case EGL_BAD_CONFIG:          return "EGL_BAD_CONFIG";
            case EGL_BAD_CONTEXT:         return "EGL_BAD_CONTEXT";
            case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
            case EGL_BAD_DISPLAY:         return "EGL_BAD_DISPLAY";
            case EGL_BAD_MATCH:           return "EGL_BAD_MATCH";
            case EGL_BAD_NATIVE_WINDOW:   return "EGL_BAD_NATIVE_WINDOW";
            case EGL_BAD_NATIVE_PIXMAP:   return "EGL_BAD_NATIVE_PIXMAP";
            case EGL_BAD_PARAMETER:       return "EGL_BAD_PARAMETER";
            case EGL_BAD_SURFACE:         return "EGL_BAD_SURFACE";
            case EGL_CONTEXT_LOST:        return "EGL_CONTEXT_LOST";
            default:                      return "EGL_UNKNOWN_ERROR";
            }
        }
#endif // KEMENA_GLES_HAS_EGL
    } // anonymous namespace

    // =------------------------------------------------------------------------
    // Lifecycle
    // =------------------------------------------------------------------------

    kOpenGLESDriver::~kOpenGLESDriver()
    {
        destroy();
    }

    const char *kOpenGLESDriver::getPlatformName()
    {
        return KEMENA_GLES_PLATFORM_NAME;
    }

    bool kOpenGLESDriver::init(kWindow *window)
    {
#if !defined(KEMENA_GLES_HAS_SDL3)
        (void)window;
        std::cout << "[kOpenGLESDriver] Built without SDL3 support "
                     "(KEMENA_GLES_NO_SDL); use initNative() instead." << std::endl;
        return false;
#else
        if (window == nullptr || window->getSdlWindow() == nullptr)
        {
            std::cout << "[kOpenGLESDriver] init() requires a valid window." << std::endl;
            return false;
        }

        // Request an OpenGL ES 3.0 context.  ES has no forward-compatible or
        // core-profile flags — those are desktop concepts.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        // Share with an existing context so resources can migrate between
        // drivers (the engine supports several live backends at once).
        if (SDL_GL_GetCurrentContext() != nullptr)
            SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

        glContext = SDL_GL_CreateContext(window->getSdlWindow());
        if (glContext == nullptr)
        {
            std::cout << "[kOpenGLESDriver] Failed to create OpenGL ES 3.0 context: "
                      << SDL_GetError() << std::endl;
            return false;
        }

        SDL_GL_MakeCurrent(window->getSdlWindow(), glContext);

        // On iOS SDL creates an EAGL-backed context underneath; label it
        // accurately so callers do not assume an EGL surface.
#if defined(KEMENA_GLES_PLATFORM_IOS)
        surfaceType = kGLESSurfaceType::EAGL;
#else
        surfaceType = kGLESSurfaceType::SDL;
#endif

        g_activeESDriver = this;
        modeIsES = true;

        std::cout << "[kOpenGLESDriver] Platform: " << getPlatformName() << std::endl;
        std::cout << "[kOpenGLESDriver] Version:  "
                  << (glGetString(GL_VERSION) ? reinterpret_cast<const char *>(glGetString(GL_VERSION))
                                              : "(unknown)")
                  << std::endl;
        std::cout << "[kOpenGLESDriver] GLSL:     "
                  << (glGetString(GL_SHADING_LANGUAGE_VERSION)
                          ? reinterpret_cast<const char *>(glGetString(GL_SHADING_LANGUAGE_VERSION))
                          : "(unknown)")
                  << std::endl;
        std::cout << "[kOpenGLESDriver] Renderer: "
                  << (glGetString(GL_RENDERER) ? reinterpret_cast<const char *>(glGetString(GL_RENDERER))
                                               : "(unknown)")
                  << std::endl;

        // ES has no default VAO concept equivalent to desktop GL 3.3 core,
        // but binding 0 here matches the desktop driver's post-init state.
        glBindVertexArray(0);

        loadCapabilities();

        if (!cachedETC2)
        {
            std::cout << "[kOpenGLESDriver] Warning: ETC2 is reported as missing. "
                         "ETC2 is mandatory in ES 3.0, so the context is probably "
                         "not ES 3.0 — compressed mobile textures may fail."
                      << std::endl;
        }

        return true;
#endif // KEMENA_GLES_HAS_SDL3
    }

    bool kOpenGLESDriver::initNative(void *nativeWindow, int width, int height)
    {
#if !defined(KEMENA_GLES_HAS_EGL)
        (void)nativeWindow;
        (void)width;
        (void)height;
        std::cout << "[kOpenGLESDriver] initNative() requires EGL, which is not "
                     "available on this platform (iOS/Web use their own context "
                     "layer via init())." << std::endl;
        return false;
#else
        if (width <= 0 || height <= 0)
        {
            std::cout << "[kOpenGLESDriver] initNative() requires a positive size."
                      << std::endl;
            return false;
        }

        eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (eglDisplay == EGL_NO_DISPLAY)
        {
            std::cout << "[kOpenGLESDriver] eglGetDisplay() failed." << std::endl;
            return false;
        }

        EGLint major = 0, minor = 0;
        if (eglInitialize(eglDisplay, &major, &minor) != EGL_TRUE)
        {
            std::cout << "[kOpenGLESDriver] eglInitialize() failed: "
                      << eglErrorName(eglGetError()) << std::endl;
            eglDisplay = EGL_NO_DISPLAY;
            return false;
        }

        // ES 3.0 requires EGL 1.4 or newer.
        if (major < 1 || (major == 1 && minor < 4))
        {
            std::cout << "[kOpenGLESDriver] EGL " << major << "." << minor
                      << " is too old; ES 3.0 needs EGL 1.4+." << std::endl;
            eglTerminate(eglDisplay);
            eglDisplay = EGL_NO_DISPLAY;
            return false;
        }

        const EGLint configAttribs[] = {
            EGL_SURFACE_TYPE,    (nativeWindow != nullptr)
                                     ? static_cast<EGLint>(EGL_WINDOW_BIT | EGL_PBUFFER_BIT)
                                     : static_cast<EGLint>(EGL_PBUFFER_BIT),
            EGL_RENDERABLE_TYPE, static_cast<EGLint>(EGL_OPENGL_ES3_BIT_KHR),
            EGL_RED_SIZE,        8,
            EGL_GREEN_SIZE,      8,
            EGL_BLUE_SIZE,       8,
            EGL_ALPHA_SIZE,      8,
            EGL_DEPTH_SIZE,      24,
            EGL_STENCIL_SIZE,    8,
            EGL_NONE
        };

        EGLint numConfigs = 0;
        if (eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &numConfigs) != EGL_TRUE ||
            numConfigs == 0)
        {
            std::cout << "[kOpenGLESDriver] eglChooseConfig() found no ES 3.0 config: "
                      << eglErrorName(eglGetError()) << std::endl;
            eglTerminate(eglDisplay);
            eglDisplay = EGL_NO_DISPLAY;
            return false;
        }

        if (nativeWindow != nullptr)
        {
            eglSurface = eglCreateWindowSurface(
                eglDisplay, eglConfig,
                reinterpret_cast<EGLNativeWindowType>(nativeWindow), nullptr);
            surfaceType = kGLESSurfaceType::EGL_NATIVE;
        }
        else
        {
            const EGLint pbufferAttribs[] = {
                EGL_WIDTH,  width,
                EGL_HEIGHT, height,
                EGL_NONE
            };
            eglSurface = eglCreatePbufferSurface(eglDisplay, eglConfig, pbufferAttribs);
            surfaceType = kGLESSurfaceType::EGL_PBUFFER;
        }

        if (eglSurface == EGL_NO_SURFACE)
        {
            std::cout << "[kOpenGLESDriver] eglCreateSurface() failed: "
                      << eglErrorName(eglGetError()) << std::endl;
            eglTerminate(eglDisplay);
            eglDisplay = EGL_NO_DISPLAY;
            return false;
        }

        const EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
        };
        eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);
        if (eglContext == EGL_NO_CONTEXT)
        {
            std::cout << "[kOpenGLESDriver] eglCreateContext() failed: "
                      << eglErrorName(eglGetError()) << std::endl;
            eglDestroySurface(eglDisplay, eglSurface);
            eglSurface = EGL_NO_SURFACE;
            eglTerminate(eglDisplay);
            eglDisplay = EGL_NO_DISPLAY;
            return false;
        }

        if (eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) != EGL_TRUE)
        {
            std::cout << "[kOpenGLESDriver] eglMakeCurrent() failed: "
                      << eglErrorName(eglGetError()) << std::endl;
            eglDestroyContext(eglDisplay, eglContext);
            eglDestroySurface(eglDisplay, eglSurface);
            eglTerminate(eglDisplay);
            eglContext = EGL_NO_CONTEXT;
            eglSurface = EGL_NO_SURFACE;
            eglDisplay = EGL_NO_DISPLAY;
            return false;
        }

        ownsEGL = true;
        surfaceWidth = width;
        surfaceHeight = height;
        modeIsES = true;
        g_activeESDriver = this;

        std::cout << "[kOpenGLESDriver] EGL " << major << "." << minor
                  << " context created ("
                  << (surfaceType == kGLESSurfaceType::EGL_PBUFFER ? "pbuffer" : "native window")
                  << " " << width << "x" << height << ")" << std::endl;
        std::cout << "[kOpenGLESDriver] Version:  "
                  << (glGetString(GL_VERSION) ? reinterpret_cast<const char *>(glGetString(GL_VERSION))
                                              : "(unknown)")
                  << std::endl;
        std::cout << "[kOpenGLESDriver] Renderer: "
                  << (glGetString(GL_RENDERER) ? reinterpret_cast<const char *>(glGetString(GL_RENDERER))
                                               : "(unknown)")
                  << std::endl;

        glBindVertexArray(0);
        loadCapabilities();
        return true;
#endif // KEMENA_GLES_HAS_EGL
    }

    void kOpenGLESDriver::destroy()
    {
        if (g_activeESDriver == this)
            g_activeESDriver = nullptr;

        texture2DSizes.clear();
        renderbufferNames.clear();
        capsLoaded = false;

#if defined(KEMENA_GLES_HAS_EGL)
        if (ownsEGL && eglDisplay != EGL_NO_DISPLAY)
        {
            eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

            if (eglContext != EGL_NO_CONTEXT)
                eglDestroyContext(eglDisplay, eglContext);
            if (eglSurface != EGL_NO_SURFACE)
                eglDestroySurface(eglDisplay, eglSurface);

            eglTerminate(eglDisplay);

            eglContext = EGL_NO_CONTEXT;
            eglSurface = EGL_NO_SURFACE;
            eglDisplay = EGL_NO_DISPLAY;
            eglConfig  = nullptr;
            ownsEGL    = false;
        }
#endif

#if defined(KEMENA_GLES_HAS_SDL3)
        if (glContext != nullptr)
        {
            SDL_GL_DestroyContext(glContext);
            glContext = nullptr;
        }
#endif
    }

    void kOpenGLESDriver::makeCurrent(kWindow *window)
    {
#if defined(KEMENA_GLES_HAS_SDL3)
        if (glContext != nullptr && window != nullptr && window->getSdlWindow() != nullptr)
            SDL_GL_MakeCurrent(window->getSdlWindow(), glContext);
#else
        (void)window;
#endif
    }

    bool kOpenGLESDriver::makeCurrentNative()
    {
#if defined(KEMENA_GLES_HAS_EGL)
        if (eglDisplay == EGL_NO_DISPLAY || eglContext == EGL_NO_CONTEXT ||
            eglSurface == EGL_NO_SURFACE)
            return false;

        return eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) == EGL_TRUE;
#else
        return false;
#endif
    }

    void *kOpenGLESDriver::getNativeContext()
    {
#if defined(KEMENA_GLES_HAS_EGL)
        if (ownsEGL)
            return static_cast<void *>(eglContext);
#endif
        return glContext;
    }

    kString kOpenGLESDriver::getApiVersion()
    {
        const GLubyte *v = glGetString(GL_VERSION);
        return v ? reinterpret_cast<const char *>(v) : kString();
    }

    kString kOpenGLESDriver::getShaderVersion()
    {
        const GLubyte *v = glGetString(GL_SHADING_LANGUAGE_VERSION);
        return v ? reinterpret_cast<const char *>(v) : kString();
    }

    void kOpenGLESDriver::swapBuffers()
    {
#if defined(KEMENA_GLES_HAS_EGL)
        if (ownsEGL && eglDisplay != EGL_NO_DISPLAY && eglSurface != EGL_NO_SURFACE)
            eglSwapBuffers(eglDisplay, eglSurface);
#endif
        // SDL-backed contexts are presented by kRenderer via SDL_GL_SwapWindow.
    }

    void kOpenGLESDriver::resizeNativeSurface(int width, int height)
    {
        if (width <= 0 || height <= 0)
            return;

        surfaceWidth = width;
        surfaceHeight = height;

#if defined(KEMENA_GLES_HAS_EGL)
        // Window surfaces are resized by the platform itself; only a pbuffer
        // has to be reallocated explicitly.
        if (ownsEGL && surfaceType == kGLESSurfaceType::EGL_PBUFFER &&
            eglDisplay != EGL_NO_DISPLAY)
        {
            eglDestroySurface(eglDisplay, eglSurface);

            const EGLint pbufferAttribs[] = {
                EGL_WIDTH,  width,
                EGL_HEIGHT, height,
                EGL_NONE
            };
            eglSurface = eglCreatePbufferSurface(eglDisplay, eglConfig, pbufferAttribs);
            eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
        }
#else
        (void)width;
        (void)height;
#endif
    }

    kOpenGLESDriver::kGLESSurfaceType kOpenGLESDriver::getSurfaceType() const
    {
        return surfaceType;
    }

    bool kOpenGLESDriver::isNativeEGL() const
    {
#if defined(KEMENA_GLES_HAS_EGL)
        return ownsEGL;
#else
        return false;
#endif
    }

    void *kOpenGLESDriver::getEGLDisplay() const
    {
#if defined(KEMENA_GLES_HAS_EGL)
        return (eglDisplay == EGL_NO_DISPLAY) ? nullptr : static_cast<void *>(eglDisplay);
#else
        return nullptr;
#endif
    }

    void *kOpenGLESDriver::getEGLContext() const
    {
#if defined(KEMENA_GLES_HAS_EGL)
        return (eglContext == EGL_NO_CONTEXT) ? nullptr : static_cast<void *>(eglContext);
#else
        return nullptr;
#endif
    }

    // =------------------------------------------------------------------------
    // Frame state
    // =------------------------------------------------------------------------

    void kOpenGLESDriver::setClearColor(float r, float g, float b, float a)
    {
        glClearColor(r, g, b, a);
    }

    void kOpenGLESDriver::clear(bool color, bool depth, bool stencil)
    {
        GLbitfield mask = 0;
        if (color)   mask |= GL_COLOR_BUFFER_BIT;
        if (depth)   mask |= GL_DEPTH_BUFFER_BIT;
        if (stencil) mask |= GL_STENCIL_BUFFER_BIT;
        if (mask != 0)
            glClear(mask);
    }

    void kOpenGLESDriver::setViewport(int x, int y, int width, int height)
    {
        glViewport(x, y, width, height);
    }

    // =------------------------------------------------------------------------
    // Pipeline state
    // =------------------------------------------------------------------------

    void kOpenGLESDriver::setDepthTest(bool enable)
    {
        enable ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    }

    void kOpenGLESDriver::setDepthWrite(bool enable)
    {
        glDepthMask(enable ? GL_TRUE : GL_FALSE);
    }

    void kOpenGLESDriver::setBlend(bool enable)
    {
        enable ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    }

    void kOpenGLESDriver::setBlendFunc(kBlendFactor src, kBlendFactor dst)
    {
        glBlendFunc(toGLBlendFactor(src), toGLBlendFactor(dst));
    }

    void kOpenGLESDriver::setCullFace(bool enable)
    {
        enable ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
    }

    void kOpenGLESDriver::setCullMode(kCullMode mode)
    {
        switch (mode)
        {
        case kCullMode::BACK:           glCullFace(GL_BACK);           break;
        case kCullMode::FRONT:          glCullFace(GL_FRONT);          break;
        case kCullMode::FRONT_AND_BACK: glCullFace(GL_FRONT_AND_BACK); break;
        }
    }

    void kOpenGLESDriver::setFrontFace(kFrontFace face)
    {
        glFrontFace(face == kFrontFace::CCW ? GL_CCW : GL_CW);
    }

    void kOpenGLESDriver::setMultisample(bool enable)
    {
        // ES 3.0 has no GL_MULTISAMPLE enable.  Multisampling is a property of
        // the framebuffer: it is on when an MSAA renderbuffer is attached and
        // off otherwise.  Kept as an explicit no-op so callers that toggle it
        // for desktop parity do not silently change behaviour on mobile.
#if defined(GL_MULTISAMPLE)
        enable ? glEnable(GL_MULTISAMPLE) : glDisable(GL_MULTISAMPLE);
#else
        (void)enable;
#endif
    }

    void kOpenGLESDriver::setSRGBEncoding(bool enable)
    {
        // sRGB write conversion is a framebuffer state.  It exists on ES 3.0
        // only when the platform header exposes GL_FRAMEBUFFER_SRGB; on most
        // mobile GPUs the sRGB-ness is instead baked into the *internal format*
        // of the attached texture (GL_SRGB8 / GL_SRGB8_ALPHA8), which this
        // driver selects through toGLESInternalFormat().
#if defined(GL_FRAMEBUFFER_SRGB)
        enable ? glEnable(GL_FRAMEBUFFER_SRGB) : glDisable(GL_FRAMEBUFFER_SRGB);
#else
        (void)enable;
#endif
    }

    void kOpenGLESDriver::setSampleAlphaToCoverage(bool enable)
    {
        // Unlike GL_MULTISAMPLE, alpha-to-coverage IS core in ES 3.0 and is
        // commonly used for alpha-tested foliage under MSAA.
        enable ? glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE)
               : glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    }

    void kOpenGLESDriver::setWireframe(bool enable)
    {
        // glPolygonMode does not exist in OpenGL ES.  Wireframe debug views are
        // silently unavailable on this backend.
        (void)enable;
    }

    // =------------------------------------------------------------------------
    // Shader source adaptation (desktop GLSL → GLSL ES 3.00)
    // =------------------------------------------------------------------------

    kString kOpenGLESDriver::adaptShaderSource(const char *src, bool isFragment)
    {
        if (src == nullptr || *src == '\0')
            return kString();

        kString result(src);

        const bool alreadyES = result.find("#version 300 es") != kString::npos;

        // --- 1. Sampler binding qualifiers are not part of GLSL ES 3.00 ------
        stripLayoutBinding(result);

        // --- 2. Legacy stage keywords ---------------------------------------
        // These never appear in GLSL 330 core either, so translating them
        // unconditionally is safe; without this a ported ES 2.0 shader would
        // fail to compile with a wall of syntax errors.
        replaceWordAll(result, "texture2D", "texture");
        replaceWordAll(result, "textureCube", "texture");
        replaceWordAll(result, "texture2DLod", "textureLod");
        replaceWordAll(result, "textureCubeLod", "textureLod");
        replaceWordAll(result, "texture2DProj", "textureProj");
        replaceWordAll(result, "texture2DGradEXT", "textureGrad");

        if (isFragment)
        {
            replaceWordAll(result, "varying", "in");
        }
        else
        {
            replaceWordAll(result, "attribute", "in");
            replaceWordAll(result, "varying", "out");
        }

        // --- 3. Fragment outputs --------------------------------------------
        // ES 3.00 removed gl_FragColor / gl_FragData in favour of user-defined
        // `out` variables.
        bool needsFragOut = false;
        if (isFragment)
        {
            if (containsWord(result, "gl_FragColor"))
            {
                replaceWordAll(result, "gl_FragColor", kFragColorOutName);
                needsFragOut = true;
            }
            if (result.find("gl_FragData") != kString::npos)
            {
                replaceFragData(result, kFragColorOutName);
                needsFragOut = true;
            }
        }

        // --- 4. Version directive -------------------------------------------
        if (!alreadyES)
        {
            const size_t v = result.find("#version");
            if (v != kString::npos)
            {
                size_t end = result.find('\n', v);
                if (end == kString::npos)
                    end = result.size();
                result.replace(v, end - v, "#version 300 es");
            }
            else
            {
                result = "#version 300 es\n" + result;
            }
        }

        // --- 5. Precision + synthetic output declarations -------------------
        //
        // ES requires an explicit default precision for float in the fragment
        // stage.  Vertex shaders default to highp, but being explicit keeps
        // results identical between Qualcomm / ARM / PowerVR compilers.
        const bool hasPrecision = result.find("precision ") != kString::npos;
        const bool hasExplicitOut = result.find("out ") != kString::npos;

        if (!hasPrecision || (needsFragOut && !hasExplicitOut))
        {
            const size_t insertAt = findVersionLineEnd(result);
            kString decls;

            if (!hasPrecision)
            {
                decls += isFragment ? "precision mediump float;\nprecision mediump int;\n"
                                    : "precision highp float;\nprecision highp int;\n";
            }

            if (needsFragOut && !hasExplicitOut)
                decls += kString("out mediump vec4 ") + kFragColorOutName + ";\n";

            if (insertAt == kString::npos)
                result = decls + result;
            else
                result.insert(insertAt, decls);
        }

        return result;
    }

    // =------------------------------------------------------------------------
    // Shader programs
    // =------------------------------------------------------------------------

    GLuint kOpenGLESDriver::compileShaderStage(GLenum stage, const char *src)
    {
        if (src == nullptr || *src == '\0')
            return 0;

        const bool isFrag = (stage == GL_FRAGMENT_SHADER);
        const kString adapted = adaptShaderSource(src, isFrag);
        const char *adaptedSrc = adapted.c_str();

        const GLuint shader = glCreateShader(stage);
        glShaderSource(shader, 1, &adaptedSrc, nullptr);
        glCompileShader(shader);

        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

        if (compiled == GL_FALSE)
        {
            GLint logLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
            if (logLen > 1)
            {
                std::vector<char> err(static_cast<size_t>(logLen));
                glGetShaderInfoLog(shader, logLen, nullptr, err.data());
                std::cout << "[kOpenGLESDriver] Shader compile error ("
                          << (isFrag ? "fragment" : "vertex") << "):\n"
                          << err.data() << std::endl;
            }

            // Echo the source that was actually submitted; debugging GLSL ES
            // port failures without the rewritten text is guesswork.
            std::cout << "[kOpenGLESDriver] Adapted source:\n"
                      << adapted << std::endl;

            glDeleteShader(shader);
            return 0;
        }

        return shader;
    }

    uint32_t kOpenGLESDriver::compileShaderProgram(const char *vertSrc, const char *fragSrc)
    {
        const GLuint vertShader = compileShaderStage(GL_VERTEX_SHADER, vertSrc);
        const GLuint fragShader = compileShaderStage(GL_FRAGMENT_SHADER, fragSrc);

        // A program needs at least one stage.  A vertex-only or fragment-only
        // program is legal and used by some depth/feedback passes.
        if (vertShader == 0 && fragShader == 0)
            return 0;

        const GLuint program = glCreateProgram();
        if (vertShader) glAttachShader(program, vertShader);
        if (fragShader) glAttachShader(program, fragShader);
        glLinkProgram(program);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);

        GLuint result = program;
        if (linked == GL_FALSE)
        {
            GLint logLen = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
            if (logLen > 1)
            {
                std::vector<char> err(static_cast<size_t>(logLen));
                glGetProgramInfoLog(program, logLen, nullptr, err.data());
                std::cout << "[kOpenGLESDriver] Program link error:\n"
                          << err.data() << std::endl;
            }
            glDeleteProgram(program);
            result = 0;
        }

        if (vertShader) glDeleteShader(vertShader);
        if (fragShader) glDeleteShader(fragShader);

        return static_cast<uint32_t>(result);
    }

    uint32_t kOpenGLESDriver::compileShaderProgramSpirv(const std::vector<uint8_t> &,
                                                        const kString &,
                                                        const std::vector<uint8_t> &,
                                                        const kString &)
    {
        std::cout << "[kOpenGLESDriver] SPIR-V ingestion requires ES 3.2 with "
                     "GL_EXT_spirv_intrinsics; it is unavailable on ES 3.0."
                  << std::endl;
        return 0;
    }

    void kOpenGLESDriver::deleteShaderProgram(uint32_t id)
    {
        if (id != 0)
            glDeleteProgram(static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::bindShaderProgram(uint32_t id)
    {
        glUseProgram(static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::unbindShaderProgram()
    {
        glUseProgram(0);
    }

    // --- Uniforms -------------------------------------------------------------

    void kOpenGLESDriver::setUniformBool(uint32_t progId, const kString &name, bool v)
    {
        if (progId == 0) return;
        glUniform1i(glGetUniformLocation(static_cast<GLuint>(progId), name.c_str()),
                    v ? 1 : 0);
    }

    void kOpenGLESDriver::setUniformInt(uint32_t progId, const kString &name, int v)
    {
        if (progId == 0) return;
        glUniform1i(glGetUniformLocation(static_cast<GLuint>(progId), name.c_str()), v);
    }

    void kOpenGLESDriver::setUniformUint(uint32_t progId, const kString &name, uint32_t v)
    {
        if (progId == 0) return;
        glUniform1ui(glGetUniformLocation(static_cast<GLuint>(progId), name.c_str()), v);
    }

    void kOpenGLESDriver::setUniformFloat(uint32_t progId, const kString &name, float v)
    {
        if (progId == 0) return;
        glUniform1f(glGetUniformLocation(static_cast<GLuint>(progId), name.c_str()), v);
    }

    void kOpenGLESDriver::setUniformVec2(uint32_t progId, const kString &name, const kVec2 &v)
    {
        if (progId == 0) return;
        glUniform2fv(glGetUniformLocation(static_cast<GLuint>(progId), name.c_str()),
                     1, glm::value_ptr(v));
    }

    void kOpenGLESDriver::setUniformVec3(uint32_t progId, const kString &name, const kVec3 &v)
    {
        if (progId == 0) return;
        glUniform3fv(glGetUniformLocation(static_cast<GLuint>(progId), name.c_str()),
                     1, glm::value_ptr(v));
    }

    void kOpenGLESDriver::setUniformVec4(uint32_t progId, const kString &name, const kVec4 &v)
    {
        if (progId == 0) return;
        glUniform4fv(glGetUniformLocation(static_cast<GLuint>(progId), name.c_str()),
                     1, glm::value_ptr(v));
    }

    void kOpenGLESDriver::setUniformMat4(uint32_t progId, const kString &name, const kMat4 &v)
    {
        if (progId == 0) return;
        glUniformMatrix4fv(glGetUniformLocation(static_cast<GLuint>(progId), name.c_str()),
                           1, GL_FALSE, glm::value_ptr(v));
    }

    void kOpenGLESDriver::setUniformMat4Array(uint32_t progId, const kString &name,
                                              const std::vector<kMat4> &v)
    {
        // glm::value_ptr(v[0]) on an empty vector is undefined behaviour and
        // would be a silent crash on mobile, where a zero-sized bone palette is
        // an easy mistake to make.
        if (progId == 0 || v.empty())
            return;

        glUniformMatrix4fv(glGetUniformLocation(static_cast<GLuint>(progId), name.c_str()),
                           static_cast<GLsizei>(v.size()), GL_FALSE,
                           glm::value_ptr(v[0]));
    }

    // =------------------------------------------------------------------------
    // Vertex arrays
    // =------------------------------------------------------------------------

    uint32_t kOpenGLESDriver::createVertexArray()
    {
        GLuint id = 0;
        glGenVertexArrays(1, &id);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::deleteVertexArray(uint32_t id)
    {
        const GLuint glId = static_cast<GLuint>(id);
        if (glId != 0)
            glDeleteVertexArrays(1, &glId);
    }

    void kOpenGLESDriver::bindVertexArray(uint32_t id)
    {
        glBindVertexArray(static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::unbindVertexArray()
    {
        glBindVertexArray(0);
    }

    // =------------------------------------------------------------------------
    // Buffers
    // =------------------------------------------------------------------------

    uint32_t kOpenGLESDriver::createBuffer()
    {
        GLuint id = 0;
        glGenBuffers(1, &id);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::deleteBuffer(uint32_t id)
    {
        const GLuint glId = static_cast<GLuint>(id);
        if (glId != 0)
            glDeleteBuffers(1, &glId);
    }

    void kOpenGLESDriver::uploadIndexBuffer(uint32_t bufferId, const void *data, size_t size)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(bufferId));
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data,
                     GL_STATIC_DRAW);
    }

    void kOpenGLESDriver::uploadVertexBuffer(uint32_t bufferId, const void *data, size_t size)
    {
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(bufferId));
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), data, GL_STATIC_DRAW);
    }

    void kOpenGLESDriver::updateBufferSubData(uint32_t bufferId, const void *data,
                                              size_t size, size_t offset)
    {
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(bufferId));
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset),
                        static_cast<GLsizeiptr>(size), data);
    }

    void kOpenGLESDriver::setVertexAttribFloat(int location, int components, int stride,
                                               size_t offset)
    {
        glEnableVertexAttribArray(static_cast<GLuint>(location));
        glVertexAttribPointer(static_cast<GLuint>(location), components, GL_FLOAT, GL_FALSE,
                              stride, reinterpret_cast<const void *>(offset));
    }

    void kOpenGLESDriver::setVertexAttribInt(int location, int components, int stride,
                                             size_t offset)
    {
        glEnableVertexAttribArray(static_cast<GLuint>(location));
        glVertexAttribIPointer(static_cast<GLuint>(location), components, GL_INT,
                               stride, reinterpret_cast<const void *>(offset));
    }

    void kOpenGLESDriver::setVertexAttribDivisor(int location, int divisor)
    {
        glVertexAttribDivisor(static_cast<GLuint>(location),
                              static_cast<GLuint>(divisor));
    }

    // =------------------------------------------------------------------------
    // Draw calls
    // =------------------------------------------------------------------------

    void kOpenGLESDriver::drawIndexed(uint32_t vaoId, int indexCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    void kOpenGLESDriver::drawIndexedInstanced(uint32_t vaoId, int indexCount, int instanceCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr,
                                static_cast<GLsizei>(instanceCount));
        glBindVertexArray(0);
    }

    void kOpenGLESDriver::drawArrays(uint32_t vaoId, kPrimitiveType type, int vertexCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawArrays(toGLPrimitiveType(type), 0, vertexCount);
        glBindVertexArray(0);
    }

    void kOpenGLESDriver::drawArraysInstanced(uint32_t vaoId, kPrimitiveType type,
                                              int vertexCount, int instanceCount)
    {
        glBindVertexArray(static_cast<GLuint>(vaoId));
        glDrawArraysInstanced(toGLPrimitiveType(type), 0, vertexCount,
                              static_cast<GLsizei>(instanceCount));
        glBindVertexArray(0);
    }

    // =------------------------------------------------------------------------
    // Texture sampling
    // =------------------------------------------------------------------------

    void kOpenGLESDriver::bindTexture2D(int unit, uint32_t id)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::bindTexture2DArray(int unit, uint32_t id)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::bindTextureCube(int unit, uint32_t id)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::unbindTexture2D(int unit)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void kOpenGLESDriver::unbindTexture2DArray(int unit)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    }

    void kOpenGLESDriver::unbindTextureCube(int unit)
    {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void kOpenGLESDriver::generateMipmaps2D(uint32_t id)
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    void kOpenGLESDriver::readTexture2DRGB(uint32_t id, int mipLevel, float *pixels)
    {
        // glGetTexImage does not exist in OpenGL ES.  The portable replacement
        // is to attach the texture to a scratch FBO and use glReadPixels.
        //
        // glGetTexLevelParameteriv — the obvious way to learn the dimensions —
        // is ES 3.1 only, so dimensions are taken from the CPU-side record
        // populated by createTexture2D()/uploadTexture2D().
        if (pixels == nullptr)
            return;

        auto it = texture2DSizes.find(id);
        if (it == texture2DSizes.end())
        {
            std::cout << "[kOpenGLESDriver] readTexture2DRGB: texture " << id
                      << " has no recorded dimensions (was it created through "
                         "this driver?)."
                      << std::endl;
            return;
        }

        int width = it->second.first;
        int height = it->second.second;
        if (width <= 0 || height <= 0)
            return;

        // Down-sample the mip chain dimensions.
        for (int i = 0; i < mipLevel; ++i)
        {
            width  = (width  > 1) ? width  / 2 : 1;
            height = (height > 1) ? height / 2 : 1;
        }

        // Restore whatever framebuffer was bound; leaving FBO 0 bound here
        // would silently redirect the next draw call to the default window.
        GLint prevFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, static_cast<GLuint>(id), mipLevel);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
        {
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, width, height, GL_RGB, GL_FLOAT, pixels);
        }
        else
        {
            std::cout << "[kOpenGLESDriver] readTexture2DRGB: texture " << id
                      << " is not colour-renderable; readback skipped." << std::endl;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        glDeleteFramebuffers(1, &fbo);
    }

    void kOpenGLESDriver::readPixelsRGBA(int x, int y, uint8_t &r, uint8_t &g,
                                         uint8_t &b, uint8_t &a)
    {
        uint8_t pixel[4] = {0, 0, 0, 0};
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        r = pixel[0];
        g = pixel[1];
        b = pixel[2];
        a = pixel[3];
    }

    // =------------------------------------------------------------------------
    // Texture conversion helpers
    // =------------------------------------------------------------------------

    static GLint toGLESWrap(kTextureWrap w)
    {
        switch (w)
        {
        case kTextureWrap::REPEAT:          return GL_REPEAT;
        case kTextureWrap::CLAMP_TO_EDGE:   return GL_CLAMP_TO_EDGE;
        // ES 3.0 has no GL_CLAMP_TO_BORDER; clamping to the edge is the closest
        // defined behaviour and avoids a black border bleeding into the mesh.
        case kTextureWrap::CLAMP_TO_BORDER: return GL_CLAMP_TO_EDGE;
        case kTextureWrap::MIRRORED_REPEAT: return GL_MIRRORED_REPEAT;
        default:                            return GL_REPEAT;
        }
    }

    static GLint toGLESMinFilter(kTextureFilter f, bool hasMips)
    {
        if (!hasMips)
        {
            // A mip-mapping filter with no mip chain makes the texture
            // incomplete and samples black on ES — a classic mobile bug.
            switch (f)
            {
            case kTextureFilter::NEAREST: return GL_NEAREST;
            default:                      return GL_LINEAR;
            }
        }

        switch (f)
        {
        case kTextureFilter::NEAREST:                return GL_NEAREST_MIPMAP_NEAREST;
        case kTextureFilter::LINEAR:                 return GL_LINEAR_MIPMAP_NEAREST;
        case kTextureFilter::NEAREST_MIPMAP_NEAREST: return GL_NEAREST_MIPMAP_NEAREST;
        case kTextureFilter::LINEAR_MIPMAP_NEAREST:  return GL_LINEAR_MIPMAP_NEAREST;
        case kTextureFilter::NEAREST_MIPMAP_LINEAR:  return GL_NEAREST_MIPMAP_LINEAR;
        case kTextureFilter::LINEAR_MIPMAP_LINEAR:   return GL_LINEAR_MIPMAP_LINEAR;
        default:                                     return GL_LINEAR_MIPMAP_LINEAR;
        }
    }

    static GLint toGLESMagFilter(kTextureFilter f)
    {
        switch (f)
        {
        case kTextureFilter::NEAREST: return GL_NEAREST;
        default:                      return GL_LINEAR;
        }
    }

    static GLint toGLESInternalFormat(kTextureFormat format)
    {
        switch (format)
        {
        case kTextureFormat::TEX_FORMAT_RGB:   return GL_RGB8;
        case kTextureFormat::TEX_FORMAT_RGBA:  return GL_RGBA8;
        case kTextureFormat::TEX_FORMAT_SRGB:  return GL_SRGB8;
        case kTextureFormat::TEX_FORMAT_SRGBA: return GL_SRGB8_ALPHA8;
        default:                               return GL_RGBA8;
        }
    }

    static GLenum toGLESBaseFormat(kTextureFormat format)
    {
        switch (format)
        {
        case kTextureFormat::TEX_FORMAT_RGB:
        case kTextureFormat::TEX_FORMAT_SRGB:  return GL_RGB;
        case kTextureFormat::TEX_FORMAT_RGBA:
        case kTextureFormat::TEX_FORMAT_SRGBA: return GL_RGBA;
        default:                               return GL_RGBA;
        }
    }

    // =------------------------------------------------------------------------
    // Texture creation (for asset loading)
    // =------------------------------------------------------------------------

    void kOpenGLESDriver::trackTexture2D(uint32_t id, int width, int height)
    {
        texture2DSizes[id] = std::make_pair(width, height);
    }

    uint32_t kOpenGLESDriver::createTexture2D(int width, int height, kTextureFormat format,
                                              const void *data,
                                              kTextureWrap wrap,
                                              kTextureFilter minFilter,
                                              kTextureFilter magFilter,
                                              bool generateMips)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, toGLESWrap(wrap));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, toGLESWrap(wrap));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        toGLESMinFilter(minFilter, generateMips));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, toGLESMagFilter(magFilter));

        if (data != nullptr)
        {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, toGLESInternalFormat(format),
                         width, height, 0, toGLESBaseFormat(format), GL_UNSIGNED_BYTE, data);
            if (generateMips)
                glGenerateMipmap(GL_TEXTURE_2D);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        trackTexture2D(static_cast<uint32_t>(id), width, height);
        return static_cast<uint32_t>(id);
    }

    uint32_t kOpenGLESDriver::createTextureCube(int width, int height,
                                                const void *faceData[6],
                                                bool generateMips)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_CUBE_MAP, id);

        if (faceData != nullptr)
        {
            for (int i = 0; i < 6; ++i)
            {
                if (faceData[i] == nullptr)
                    continue;

                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_SRGB8,
                             width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, faceData[i]);
            }
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                        generateMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Cube maps on ES require CLAMP_TO_EDGE on all three axes.
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        if (generateMips)
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::uploadTexture2D(uint32_t id, int level, int width, int height,
                                          kTextureFormat format, const void *data)
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, level, toGLESInternalFormat(format),
                     width, height, 0, toGLESBaseFormat(format), GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (level == 0)
            trackTexture2D(id, width, height);
    }

    void kOpenGLESDriver::uploadTexture2DSub(uint32_t id, int level, int x, int y,
                                             int width, int height,
                                             kTextureFormat format, const void *data)
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, level, x, y, width, height,
                        toGLESBaseFormat(format), GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (level == 0)
            trackTexture2D(id, x + width, y + height);
    }

    void kOpenGLESDriver::uploadCompressedTexture2D(uint32_t id, int level,
                                                    int width, int height,
                                                    kTextureFormat format,
                                                    const void *data, size_t dataSize)
    {
        const GLenum internalFmt = compressedInternalFormat(format);
        if (internalFmt == 0)
        {
            std::cout << "[kOpenGLESDriver] uploadCompressedTexture2D: format "
                      << static_cast<int>(format)
                      << " is not a supported compressed format on this backend."
                      << std::endl;
            return;
        }

        if (!supportsCompressedFormat(format))
        {
            std::cout << "[kOpenGLESDriver] uploadCompressedTexture2D: the device "
                         "does not advertise support for this compressed format "
                         "(id "
                      << static_cast<int>(format) << ")." << std::endl;
            return;
        }

        if (data == nullptr || dataSize == 0)
            return;

        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(id));
        glCompressedTexImage2D(GL_TEXTURE_2D, level, internalFmt,
                               width, height, 0,
                               static_cast<GLsizei>(dataSize), data);
        glBindTexture(GL_TEXTURE_2D, 0);

        if (level == 0)
            trackTexture2D(id, width, height);
    }

    void kOpenGLESDriver::uploadTextureCubeFace(uint32_t id, int face, int width, int height,
                                                const void *data)
    {
        glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(id));
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_SRGB8,
                     width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    void kOpenGLESDriver::deleteTexture(uint32_t id)
    {
        const GLuint glId = static_cast<GLuint>(id);
        if (glId != 0)
            glDeleteTextures(1, &glId);

        texture2DSizes.erase(id);
    }

    // =------------------------------------------------------------------------
    // Framebuffers
    // =------------------------------------------------------------------------

    uint32_t kOpenGLESDriver::createFramebuffer()
    {
        GLuint id = 0;
        glGenFramebuffers(1, &id);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::deleteFramebuffer(uint32_t id)
    {
        const GLuint glId = static_cast<GLuint>(id);
        if (glId != 0)
            glDeleteFramebuffers(1, &glId);
    }

    void kOpenGLESDriver::bindFramebuffer(uint32_t id)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::bindReadFramebuffer(uint32_t id)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::bindDrawFramebuffer(uint32_t id)
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(id));
    }

    void kOpenGLESDriver::unbindFramebuffer()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    bool kOpenGLESDriver::isFramebufferComplete()
    {
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    void kOpenGLESDriver::blitFramebufferColor(int srcX0, int srcY0, int srcX1, int srcY1,
                                               int dstX0, int dstY0, int dstX1, int dstY1)
    {
        glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                          dstX0, dstY0, dstX1, dstY1,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    void kOpenGLESDriver::setFramebufferDrawBuffer()
    {
        const GLenum buf = GL_COLOR_ATTACHMENT0;
        glDrawBuffers(1, &buf);
    }

    // =------------------------------------------------------------------------
    // Renderbuffers
    // =------------------------------------------------------------------------

    GLenum kOpenGLESDriver::depthStencilFormat()
    {
        // GL_DEPTH24_STENCIL8 is core in ES 3.0 (packed 24-bit depth + 8-bit
        // stencil).  GL_DEPTH24_STENCIL8_OES is the ES 2.0 extension spelling,
        // still returned by a few older mobile drivers.
        return GL_DEPTH24_STENCIL8;
    }

    GLenum kOpenGLESDriver::depthFormat()
    {
        // ES 3.0 guarantees GL_DEPTH_COMPONENT24 but not the 32F variant.
        return GL_DEPTH_COMPONENT24;
    }

    int kOpenGLESDriver::clampSamples(int samples)
    {
        const int maxSamples = getMaxSamples();
        if (samples < 0)
            return 0;
        if (maxSamples > 0 && samples > maxSamples)
            return maxSamples;
        return samples;
    }

    uint32_t kOpenGLESDriver::createRenderbuffer()
    {
        GLuint id = 0;
        glGenRenderbuffers(1, &id);
        renderbufferNames.insert(static_cast<uint32_t>(id));
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::deleteRenderbuffer(uint32_t id)
    {
        const GLuint glId = static_cast<GLuint>(id);
        if (glId != 0)
            glDeleteRenderbuffers(1, &glId);

        renderbufferNames.erase(id);
    }

    void kOpenGLESDriver::setupRenderbuffer(uint32_t rboId, int width, int height)
    {
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(rboId));
        glRenderbufferStorage(GL_RENDERBUFFER, depthStencilFormat(), width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    void kOpenGLESDriver::setupRenderbufferMSAA(uint32_t rboId, int samples,
                                                int width, int height)
    {
        const int effective = clampSamples(samples);
        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(rboId));

        if (effective <= 1)
        {
            // Requesting a single sample through glRenderbufferStorageMultisample
            // is legal but pointless; fall back to the plain path.
            glRenderbufferStorage(GL_RENDERBUFFER, depthStencilFormat(), width, height);
        }
        else
        {
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, effective,
                                             depthStencilFormat(), width, height);
        }

        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    void kOpenGLESDriver::attachRenderbufferDepthStencil(uint32_t fboId, uint32_t rboId)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, static_cast<GLuint>(rboId));
    }

    // =------------------------------------------------------------------------
    // FBO-managed textures
    // =------------------------------------------------------------------------

    uint32_t kOpenGLESDriver::createFBOColorTexture(int width, int height)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        trackTexture2D(static_cast<uint32_t>(id), width, height);
        return static_cast<uint32_t>(id);
    }

    uint32_t kOpenGLESDriver::createFBOColorTextureMSAA(int samples, int width, int height)
    {
        // ES 3.0 has no GL_TEXTURE_2D_MULTISAMPLE, so a multisampled colour
        // target must be a renderbuffer.  Renderbuffer names live in a separate
        // GL namespace from textures, so the returned handle is recorded in
        // renderbufferNames to keep deletion and attachment unambiguous.
        const int effective = clampSamples(samples);

        GLuint id = 0;
        glGenRenderbuffers(1, &id);
        glBindRenderbuffer(GL_RENDERBUFFER, id);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, (effective > 0) ? effective : 1,
                                         GL_RGBA8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        renderbufferNames.insert(static_cast<uint32_t>(id));
        return static_cast<uint32_t>(id);
    }

    uint32_t kOpenGLESDriver::createFBODepthTexture(int width, int height)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        // For a depth texture the ES 3.0 valid type for a 24-bit internal
        // format is GL_UNSIGNED_INT.  GL_FLOAT is only legal for
        // GL_DEPTH_COMPONENT32F, which ES 3.0 does not require.
        glTexImage2D(GL_TEXTURE_2D, 0, depthFormat(), width, height, 0,
                     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        trackTexture2D(static_cast<uint32_t>(id), width, height);
        return static_cast<uint32_t>(id);
    }

    uint32_t kOpenGLESDriver::createFBODepthTextureArray(int width, int height, int layers)
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D_ARRAY, id);

        // glTexImage3D for 2D arrays is core in ES 3.0.
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, depthFormat(),
                     width, height, layers, 0,
                     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // GL_TEXTURE_COMPARE_MODE defaults to GL_NONE (regular sampling);
        // shadow lookups opt in by setting GL_COMPARE_REF_TO_TEXTURE.
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        return static_cast<uint32_t>(id);
    }

    void kOpenGLESDriver::deleteTextureOrRenderbuffer(uint32_t id)
    {
        if (id == 0)
            return;

        // A renderbuffer name and a texture name may hold the same integer, so
        // the kind must be looked up rather than guessed.  Deleting a live
        // object of the wrong kind here would corrupt an unrelated resource.
        if (renderbufferNames.erase(id) > 0)
        {
            const GLuint glId = static_cast<GLuint>(id);
            glDeleteRenderbuffers(1, &glId);
        }
        else
        {
            const GLuint glId = static_cast<GLuint>(id);
            glDeleteTextures(1, &glId);
        }

        texture2DSizes.erase(id);
    }

    void kOpenGLESDriver::deleteFBOTexture(uint32_t id)
    {
        deleteTextureOrRenderbuffer(id);
    }

    void kOpenGLESDriver::attachFBOColorTexture(uint32_t fboId, uint32_t texId)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, static_cast<GLuint>(texId), 0);
    }

    void kOpenGLESDriver::attachFBOColorTextureMSAA(uint32_t fboId, uint32_t texId)
    {
        // On this backend texId is a renderbuffer name (see
        // createFBOColorTextureMSAA).
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, static_cast<GLuint>(texId));
        renderbufferNames.insert(texId);
    }

    void kOpenGLESDriver::attachFBODepthTexture(uint32_t fboId, uint32_t texId)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, static_cast<GLuint>(texId), 0);

        // glDrawBuffer() does not exist in OpenGL ES; glDrawBuffers() with
        // GL_NONE is the equivalent way to declare a depth-only target.
        const GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
        glReadBuffer(GL_NONE);
    }

    void kOpenGLESDriver::attachFBODepthTextureLayer(uint32_t fboId, uint32_t texId, int layer)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fboId));
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  static_cast<GLuint>(texId), 0, layer);

        const GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
        glReadBuffer(GL_NONE);
    }

    void kOpenGLESDriver::resizeFBOColorTexture(uint32_t texId, int width, int height)
    {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texId));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        trackTexture2D(texId, width, height);
    }

    void kOpenGLESDriver::resizeFBOColorTextureMSAA(uint32_t texId, int samples,
                                                    int width, int height)
    {
        // MSAA colour targets are renderbuffers on this backend.
        const int effective = clampSamples(samples);

        glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(texId));
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, (effective > 0) ? effective : 1,
                                         GL_RGBA8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    // =------------------------------------------------------------------------
    // Capabilities
    // =------------------------------------------------------------------------

    void kOpenGLESDriver::loadCapabilities()
    {
        if (capsLoaded)
            return;

        cachedExtensions.clear();
        const GLubyte *ext = glGetString(GL_EXTENSIONS);
        if (ext != nullptr)
            cachedExtensions = reinterpret_cast<const char *>(ext);

        cachedRenderer.clear();
        const GLubyte *renderer = glGetString(GL_RENDERER);
        if (renderer != nullptr)
            cachedRenderer = reinterpret_cast<const char *>(renderer);

        GLint value = 0;

        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
        cachedMaxTextureSize = static_cast<int>(value);

        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &value);
        cachedMaxTextureUnits = static_cast<int>(value);

        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &value);
        cachedMaxVertexAttribs = static_cast<int>(value);

        glGetIntegerv(GL_MAX_SAMPLES, &value);
        cachedMaxSamples = static_cast<int>(value);

        // ETC2/EAC is mandatory in ES 3.0, so it needs no extension check.
        cachedETC2 = true;

        cachedASTC = cachedExtensions.find("GL_KHR_texture_compression_astc_ldr") != kString::npos ||
                     cachedExtensions.find("GL_OES_texture_compression_astc") != kString::npos;

        // Half-float colour attachments are not core in ES 3.0.
        cachedHalfFloatRT =
            cachedExtensions.find("GL_EXT_color_buffer_half_float") != kString::npos ||
            cachedExtensions.find("GL_EXT_color_buffer_float") != kString::npos;

        capsLoaded = true;

        std::cout << "[kOpenGLESDriver] Caps: maxTex=" << cachedMaxTextureSize
                  << " units=" << cachedMaxTextureUnits
                  << " attribs=" << cachedMaxVertexAttribs
                  << " samples=" << cachedMaxSamples
                  << " ETC2=" << (cachedETC2 ? "yes" : "no")
                  << " ASTC=" << (cachedASTC ? "yes" : "no")
                  << " halfFloatRT=" << (cachedHalfFloatRT ? "yes" : "no")
                  << std::endl;
    }

    bool kOpenGLESDriver::isETC2Format(kTextureFormat format)
    {
        switch (format)
        {
        case kTextureFormat::TEX_FORMAT_ETC2_RGB:
        case kTextureFormat::TEX_FORMAT_ETC2_SRGB:
        case kTextureFormat::TEX_FORMAT_ETC2_RGBA:
        case kTextureFormat::TEX_FORMAT_ETC2_SRGBA:
        case kTextureFormat::TEX_FORMAT_ETC2_RGB_A1:
            return true;
        default:
            return false;
        }
    }

    bool kOpenGLESDriver::isASTCFormat(kTextureFormat format)
    {
        switch (format)
        {
        case kTextureFormat::TEX_FORMAT_ASTC_4x4:
        case kTextureFormat::TEX_FORMAT_ASTC_6x6:
        case kTextureFormat::TEX_FORMAT_ASTC_8x8:
        case kTextureFormat::TEX_FORMAT_ASTC_12x12:
            return true;
        default:
            return false;
        }
    }

    int kOpenGLESDriver::compressedBlockBytes(kTextureFormat format)
    {
        if (isETC2Format(format))
            return 8;   // ETC2/EAC use 64-bit (8 byte) blocks.

        if (isASTCFormat(format))
            return 16;  // ASTC always stores 128-bit (16 byte) blocks.

        return 0;       // Uncompressed.
    }

    GLenum kOpenGLESDriver::compressedInternalFormat(kTextureFormat format)
    {
        switch (format)
        {
        case kTextureFormat::TEX_FORMAT_ETC2_RGB:    return GL_COMPRESSED_RGB8_ETC2;
        case kTextureFormat::TEX_FORMAT_ETC2_SRGB:   return GL_COMPRESSED_SRGB8_ETC2;
        case kTextureFormat::TEX_FORMAT_ETC2_RGBA:   return GL_COMPRESSED_RGBA8_ETC2_EAC;
        case kTextureFormat::TEX_FORMAT_ETC2_SRGBA:  return GL_COMPRESSED_SRGB8_ALPHA8_ETC2_EAC;
        case kTextureFormat::TEX_FORMAT_ETC2_RGB_A1: return GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2;
        case kTextureFormat::TEX_FORMAT_ASTC_4x4:    return GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
        case kTextureFormat::TEX_FORMAT_ASTC_6x6:    return GL_COMPRESSED_RGBA_ASTC_6x6_KHR;
        case kTextureFormat::TEX_FORMAT_ASTC_8x8:    return GL_COMPRESSED_RGBA_ASTC_8x8_KHR;
        case kTextureFormat::TEX_FORMAT_ASTC_12x12:  return GL_COMPRESSED_RGBA_ASTC_12x12_KHR;
        default:                                     return 0;
        }
    }

    bool kOpenGLESDriver::supportsCompressedFormat(kTextureFormat format)
    {
        loadCapabilities();

        // Desktop-only S3TC/DXT is deliberately not offered: it is absent from
        // virtually every mobile and embedded GPU.
        if (isETC2Format(format))
            return cachedETC2;

        if (isASTCFormat(format))
            return cachedASTC;

        // Uncompressed formats are always uploadable via uploadTexture2D().
        return compressedBlockBytes(format) != 0 ? false : true;
    }

    bool kOpenGLESDriver::hasETC2()
    {
        loadCapabilities();
        return cachedETC2;
    }

    bool kOpenGLESDriver::hasASTC()
    {
        loadCapabilities();
        return cachedASTC;
    }

    bool kOpenGLESDriver::hasColorBufferHalfFloat()
    {
        loadCapabilities();
        return cachedHalfFloatRT;
    }

    int kOpenGLESDriver::getMaxTextureSize()
    {
        loadCapabilities();
        return cachedMaxTextureSize;
    }

    int kOpenGLESDriver::getMaxSamples()
    {
        loadCapabilities();
        return cachedMaxSamples;
    }

    int kOpenGLESDriver::getMaxTextureUnits()
    {
        loadCapabilities();
        return cachedMaxTextureUnits;
    }

    int kOpenGLESDriver::getMaxVertexAttribs()
    {
        loadCapabilities();
        return cachedMaxVertexAttribs;
    }

    kString kOpenGLESDriver::getRendererName()
    {
        loadCapabilities();
        return cachedRenderer;
    }

    kString kOpenGLESDriver::getExtensions()
    {
        loadCapabilities();
        return cachedExtensions;
    }

    // =------------------------------------------------------------------------
    // Private helpers
    // =------------------------------------------------------------------------

    kString kOpenGLESDriver::glErrorString(const char *stage)
    {
        const GLenum err = glGetError();
        if (err == GL_NO_ERROR)
            return kString();

        kString name;
        switch (err)
        {
        case GL_INVALID_ENUM:      name = "GL_INVALID_ENUM";      break;
        case GL_INVALID_VALUE:     name = "GL_INVALID_VALUE";     break;
        case GL_INVALID_OPERATION: name = "GL_INVALID_OPERATION"; break;
        case GL_OUT_OF_MEMORY:     name = "GL_OUT_OF_MEMORY";     break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            name = "GL_INVALID_FRAMEBUFFER_OPERATION";
            break;
        default:                   name = "GL_ERROR_UNKNOWN";     break;
        }

        return kString(stage) + ": " + name;
    }

    GLenum kOpenGLESDriver::toGLBlendFactor(kBlendFactor factor)
    {
        switch (factor)
        {
        case kBlendFactor::ZERO:                return GL_ZERO;
        case kBlendFactor::ONE:                 return GL_ONE;
        case kBlendFactor::SRC_ALPHA:           return GL_SRC_ALPHA;
        case kBlendFactor::ONE_MINUS_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
        case kBlendFactor::SRC_COLOR:           return GL_SRC_COLOR;
        case kBlendFactor::ONE_MINUS_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
        case kBlendFactor::DST_ALPHA:           return GL_DST_ALPHA;
        case kBlendFactor::ONE_MINUS_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
        default:                                return GL_ONE;
        }
    }

    GLenum kOpenGLESDriver::toGLPrimitiveType(kPrimitiveType type)
    {
        switch (type)
        {
        case kPrimitiveType::TRIANGLES:      return GL_TRIANGLES;
        case kPrimitiveType::TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
        case kPrimitiveType::TRIANGLE_FAN:   return GL_TRIANGLE_FAN;
        case kPrimitiveType::LINES:          return GL_LINES;
        case kPrimitiveType::LINE_STRIP:     return GL_LINE_STRIP;
        case kPrimitiveType::POINTS:         return GL_POINTS;
        default:                             return GL_TRIANGLES;
        }
    }

    bool kOpenGLESDriver::isGLES()
    {
        // Prefer the state captured by init()/initNative(): the static probe
        // may be called before any context is current, in which case
        // glGetString() returns null.
        if (g_activeESDriver != nullptr)
            return g_activeESDriver->modeIsES;

        const char *ver = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        if (ver == nullptr)
            return false;

        return std::strstr(ver, "OpenGL ES") != nullptr;
    }

} // namespace kemena
