/**
 * @file kdriver.h
 * @brief Abstract graphics driver interface used by the engine.
 *
 * All graphics API calls made by the engine go through this interface so that
 * the underlying backend (OpenGL, Vulkan, DirectX, …) can be swapped at
 * initialisation time without changing any higher-level code.
 */

#ifndef KDRIVER_H
#define KDRIVER_H

#include "kexport.h"
#include "kdatatype.h"

#include <vector>
#include <cstdint>

namespace kemena
{
    class kWindow;

    // -------------------------------------------------------------------------
    // Enums
    // -------------------------------------------------------------------------

    /**
     * @brief Source/destination blend factors for alpha blending.
     */
    enum class kBlendFactor
    {
        ZERO,                ///< Factor = 0.
        ONE,                 ///< Factor = 1.
        SRC_ALPHA,           ///< Factor = source alpha.
        ONE_MINUS_SRC_ALPHA, ///< Factor = 1 - source alpha.
        SRC_COLOR,           ///< Factor = source colour.
        ONE_MINUS_SRC_COLOR, ///< Factor = 1 - source colour.
        DST_ALPHA,           ///< Factor = destination alpha.
        ONE_MINUS_DST_ALPHA, ///< Factor = 1 - destination alpha.
    };

    /**
     * @brief Which polygon faces to cull.
     */
    enum class kCullMode
    {
        BACK,          ///< Cull back-facing polygons.
        FRONT,         ///< Cull front-facing polygons.
        FRONT_AND_BACK ///< Cull all polygons (geometry only, still runs shaders).
    };

    /**
     * @brief Winding order that defines the front face of a polygon.
     */
    enum class kFrontFace
    {
        CCW, ///< Counter-clockwise winding = front face.
        CW   ///< Clockwise winding = front face.
    };

    /**
     * @brief Primitive topology for draw calls.
     */
    enum class kPrimitiveType
    {
        TRIANGLES,      ///< Independent triangles (3 vertices each).
        TRIANGLE_STRIP, ///< Triangle strip.
        TRIANGLE_FAN,   ///< Triangle fan.
        LINES,          ///< Independent line segments (2 vertices each).
        LINE_STRIP,     ///< Connected line strip.
        POINTS,         ///< Individual point sprites.
    };

    // -------------------------------------------------------------------------
    // Abstract driver base class
    // No graphics API headers included here — keep this file API-agnostic.
    // -------------------------------------------------------------------------

    /**
     * @brief Pure-virtual graphics driver interface.
     *
     * Concrete implementations (e.g. kOpenGLDriver) must override every method.
     * Obtain the currently-active driver via kDriver::getCurrent().
     */
    class KEMENA3D_API kDriver
    {
    public:
        virtual ~kDriver() = default;

        // --- Lifecycle -------------------------------------------------------

        /**
         * @brief Initialises the graphics context for the given window.
         * @param window Target window; must not be nullptr for GL backends.
         * @return true on success.
         */
        virtual bool init(kWindow *window) = 0;

        /** @brief Destroys the graphics context and releases all driver resources. */
        virtual void destroy() = 0;

        /**
         * @brief Makes this driver's graphics context current on the given window.
         *
         * Must be called before issuing any draw calls through this driver when
         * multiple drivers (and therefore multiple GL/D3D contexts) are in use.
         * OpenGL backends call SDL_GL_MakeCurrent; D3D11 backends are a no-op
         * (the immediate context is always current).
         *
         * @param window The window to bind the context to.
         */
        virtual void makeCurrent(kWindow *window) = 0;

        /**
         * @brief Returns the native context handle (e.g. SDL_GLContext for OpenGL).
         * @return Opaque pointer; cast to the appropriate type for your backend.
         */
        virtual void *getNativeContext() = 0;

        /**
         * @brief Returns a human-readable API version kString.
         * @return e.g. "4.6.0 NVIDIA 546.01".
         */
        virtual kString getApiVersion() = 0;

        /**
         * @brief Returns the shading language version kString.
         * @return e.g. "4.60 NVIDIA".
         */
        virtual kString getShaderVersion() = 0;

        /**
         * @brief Identifies which renderer type this driver implements.
         *
         * Default implementation reports @c RENDERER_GL, which also describes the
         * OpenGL ES backend for asset-selection purposes (both compile GLSL).  The
         * DirectX backends override it so callers can pick a backend-specific asset
         * variant — for example loading a .hlsl shader instead of its .glsl
         * counterpart.  Behaviour of the existing backends is unaffected.
         *
         * @return kRendererType of this driver.
         */
        virtual kRendererType getRendererType() const { return kRendererType::RENDERER_GL; }

        // --- Frame state -----------------------------------------------------

        /**
         * @brief Sets the colour used by the next clear() call.
         * @param r Red   [0, 1].
         * @param g Green [0, 1].
         * @param b Blue  [0, 1].
         * @param a Alpha [0, 1].
         */
        virtual void setClearColor(float r, float g, float b, float a) = 0;

        /**
         * @brief Clears the selected buffers of the currently-bound framebuffer.
         * @param color   Clear the colour attachment.
         * @param depth   Clear the depth attachment.
         * @param stencil Clear the stencil attachment.
         */
        virtual void clear(bool color, bool depth, bool stencil) = 0;

        /**
         * @brief Sets the rendering viewport.
         * @param x      Left edge in pixels.
         * @param y      Bottom edge in pixels (OpenGL convention).
         * @param width  Viewport width in pixels.
         * @param height Viewport height in pixels.
         */
        virtual void setViewport(int x, int y, int width, int height) = 0;

        // --- Pipeline state --------------------------------------------------

        /**
         * @brief Enables or disables the depth test.
         * @param enable true to enable.
         */
        virtual void setDepthTest(bool enable) = 0;

        /**
         * @brief Enables or disables writes to the depth buffer.
         * @param enable true to allow writes.
         */
        virtual void setDepthWrite(bool enable) = 0;

        /**
         * @brief Enables or disables colour blending.
         * @param enable true to enable.
         */
        virtual void setBlend(bool enable) = 0;

        /**
         * @brief Sets the blend equation source and destination factors.
         * @param src Source factor.
         * @param dst Destination factor.
         */
        virtual void setBlendFunc(kBlendFactor src, kBlendFactor dst) = 0;

        /**
         * @brief Enables or disables face culling.
         * @param enable true to enable.
         */
        virtual void setCullFace(bool enable) = 0;

        /**
         * @brief Selects which polygon faces are culled when culling is enabled.
         * @param mode Cull mode.
         */
        virtual void setCullMode(kCullMode mode) = 0;

        /**
         * @brief Sets the winding order that defines the front face.
         * @param face Front-face winding.
         */
        virtual void setFrontFace(kFrontFace face) = 0;

        /**
         * @brief Enables or disables multisampling (MSAA).
         * @param enable true to enable.
         */
        virtual void setMultisample(bool enable) = 0;

        /**
         * @brief Enables or disables sRGB framebuffer encoding.
         * @param enable true to write in sRGB; false for linear output.
         */
        virtual void setSRGBEncoding(bool enable) = 0;

        /**
         * @brief Enables or disables alpha-to-coverage (useful for MSAA foliage).
         * @param enable true to enable.
         */
        virtual void setSampleAlphaToCoverage(bool enable) = 0;

        /**
         * @brief Enables or disables wireframe rendering.
         *
         * When enabled, filled primitives are rendered as lines (wireframe).
         * The driver automatically applies polygon offset to prevent z-fighting
         * between the wireframe and the underlying filled geometry.
         *
         * On backends that don't support @c glPolygonMode (GLES), this is a
         * no-op and wireframe debug modes are silently ignored.
         *
         * @param enable true to render wireframe, false for solid fill.
         */
        virtual void setWireframe(bool enable) = 0;

        // --- Shader programs -------------------------------------------------

        /**
         * @brief Compiles and links a vertex + fragment shader pair from source.
         *
         * The source language is the one the active backend compiles and is never
         * translated: GLSL for kOpenGLDriver / kOpenGLESDriver, HLSL for the
         * DirectX 11 backend.  Callers therefore supply whichever language matches
         * the backend they are running on (see kShader's per-API loaders).
         *
         * @param vertSrc Vertex shader source, or nullptr to skip the stage.
         * @param fragSrc Fragment shader source, or nullptr to skip the stage.
         * @return Opaque program handle (0 on failure).
         */
        virtual uint32_t compileShaderProgram(const char *vertSrc, const char *fragSrc) = 0;

        /**
         * @brief Compiles and links a vertex + fragment shader pair from SPIR-V binary.
         *
         * Requires OpenGL 4.6 or the @c GL_ARB_gl_spirv extension.
         *
         * @param vertSpirv  SPIR-V binary for the vertex stage.
         * @param vertEntry  Entry-point name inside the vertex SPIR-V module.
         * @param fragSpirv  SPIR-V binary for the fragment stage.
         * @param fragEntry  Entry-point name inside the fragment SPIR-V module.
         * @return Opaque program handle (0 on failure or if SPIR-V is unsupported).
         */
        virtual uint32_t compileShaderProgramSpirv(const std::vector<uint8_t> &vertSpirv,
                                                   const kString &vertEntry,
                                                   const std::vector<uint8_t> &fragSpirv,
                                                   const kString &fragEntry) = 0;

        /**
         * @brief Destroys a previously-compiled shader program.
         * @param id Program handle returned by compileShaderProgram().
         */
        virtual void deleteShaderProgram(uint32_t id) = 0;

        /**
         * @brief Binds a shader program for subsequent draw calls.
         * @param id Program handle.
         */
        virtual void bindShaderProgram(uint32_t id) = 0;

        /** @brief Unbinds the current shader program. */
        virtual void unbindShaderProgram() = 0;

        /** @brief Sets a boolean uniform on the given program. */
        virtual void setUniformBool(uint32_t progId, const kString &name, bool v) = 0;
        /** @brief Sets an integer uniform on the given program. */
        virtual void setUniformInt(uint32_t progId, const kString &name, int v) = 0;
        /** @brief Sets an unsigned-integer uniform on the given program. */
        virtual void setUniformUint(uint32_t progId, const kString &name, uint32_t v) = 0;
        /** @brief Sets a float uniform on the given program. */
        virtual void setUniformFloat(uint32_t progId, const kString &name, float v) = 0;
        /** @brief Sets a kVec2 uniform on the given program. */
        virtual void setUniformVec2(uint32_t progId, const kString &name, const kVec2 &v) = 0;
        /** @brief Sets a kVec3 uniform on the given program. */
        virtual void setUniformVec3(uint32_t progId, const kString &name, const kVec3 &v) = 0;
        /** @brief Sets a kVec4 uniform on the given program. */
        virtual void setUniformVec4(uint32_t progId, const kString &name, const kVec4 &v) = 0;
        /** @brief Sets a kMat4 uniform on the given program. */
        virtual void setUniformMat4(uint32_t progId, const kString &name, const kMat4 &v) = 0;
        /**
         * @brief Sets a kMat4 array uniform on the given program.
         * @param progId Program handle.
         * @param name   Uniform array name.
         * @param v      Array of kMat4 values.
         */
        virtual void setUniformMat4Array(uint32_t progId, const kString &name, const std::vector<kMat4> &v) = 0;

        // --- Vertex arrays ---------------------------------------------------

        /**
         * @brief Creates and returns a new vertex array object (VAO).
         * @return Handle to the new VAO.
         */
        virtual uint32_t createVertexArray() = 0;

        /** @brief Destroys the given VAO. */
        virtual void deleteVertexArray(uint32_t id) = 0;

        /** @brief Binds the given VAO as the active vertex array. */
        virtual void bindVertexArray(uint32_t id) = 0;

        /** @brief Unbinds the current vertex array. */
        virtual void unbindVertexArray() = 0;

        // --- Buffers ---------------------------------------------------------

        /**
         * @brief Creates and returns a new GPU buffer object.
         * @return Handle to the new buffer.
         */
        virtual uint32_t createBuffer() = 0;

        /** @brief Destroys the given buffer. */
        virtual void deleteBuffer(uint32_t id) = 0;

        /**
         * @brief Uploads data to an index (element) buffer.
         * @param bufferId Target buffer handle.
         * @param data     Pointer to index data.
         * @param size     Size of the data in bytes.
         */
        virtual void uploadIndexBuffer(uint32_t bufferId, const void *data, size_t size) = 0;

        /**
         * @brief Uploads data to a vertex buffer.
         * @param bufferId Target buffer handle.
         * @param data     Pointer to vertex data.
         * @param size     Size of the data in bytes.
         */
        virtual void uploadVertexBuffer(uint32_t bufferId, const void *data, size_t size) = 0;

        /**
         * @brief Updates a sub-region of an existing vertex buffer without reallocation.
         * @param bufferId Target buffer handle.
         * @param data     Pointer to new data.
         * @param size     Size of the sub-region in bytes.
         * @param offset   Byte offset from the start of the buffer.
         */
        virtual void updateBufferSubData(uint32_t bufferId, const void *data, size_t size, size_t offset) = 0;

        /**
         * @brief Defines a float vertex attribute on the currently-bound VAO.
         *
         * Also enables the attribute array at @p location.
         * @param location  Shader attribute location.
         * @param components Number of float components (1–4).
         * @param stride    Byte distance between consecutive elements.
         * @param offset    Byte offset of the first element within the buffer.
         */
        virtual void setVertexAttribFloat(int location, int components, int stride, size_t offset) = 0;

        /**
         * @brief Defines an integer vertex attribute on the currently-bound VAO.
         *
         * Uses the integer variant (glVertexAttribIPointer). Also enables the
         * attribute array at @p location.
         * @param location   Shader attribute location.
         * @param components Number of integer components (1–4).
         * @param stride     Byte distance between consecutive elements.
         * @param offset     Byte offset of the first element within the buffer.
         */
        virtual void setVertexAttribInt(int location, int components, int stride, size_t offset) = 0;

        /**
         * @brief Sets the instance divisor for a vertex attribute on the currently-bound VAO.
         *
         * A divisor of 0 means the attribute advances per vertex (default).
         * A divisor of 1 means the attribute advances per instance (instanced rendering).
         * @param location Attribute location index.
         * @param divisor  Number of instances that share the same attribute value.
         */
        virtual void setVertexAttribDivisor(int location, int divisor) = 0;

        // --- Draw calls ------------------------------------------------------

        /**
         * @brief Draws indexed triangles from the given VAO.
         * @param vaoId      VAO handle.
         * @param indexCount Number of indices to draw.
         */
        virtual void drawIndexed(uint32_t vaoId, int indexCount) = 0;

        /**
         * @brief Draws indexed, instanced triangles from the given VAO.
         * @param vaoId         VAO handle.
         * @param indexCount    Number of indices per instance.
         * @param instanceCount Number of instances to draw.
         */
        virtual void drawIndexedInstanced(uint32_t vaoId, int indexCount, int instanceCount) = 0;

        /**
         * @brief Draws non-indexed primitives from the given VAO.
         * @param vaoId       VAO handle.
         * @param type        Primitive topology.
         * @param vertexCount Number of vertices to draw.
         */
        virtual void drawArrays(uint32_t vaoId, kPrimitiveType type, int vertexCount) = 0;

        /**
         * @brief Draws non-indexed, instanced primitives from the given VAO.
         * @param vaoId         VAO handle.
         * @param type          Primitive topology.
         * @param vertexCount   Number of vertices per instance.
         * @param instanceCount Number of instances to draw.
         */
        virtual void drawArraysInstanced(uint32_t vaoId, kPrimitiveType type, int vertexCount, int instanceCount) = 0;

        // --- Texture creation (for asset loading) ----------------------------

        /**
         * @brief Creates a 2D texture and uploads pixel data at mip level 0.
         *
         * Automatically generates a full mipmap chain after upload when
         * @p generateMips is true.  The texture is created with the given wrap
         * and filter parameters.
         *
         * @param width        Texture width in pixels.
         * @param height       Texture height in pixels.
         * @param format       Pixel format (RGB, RGBA, sRGB variants).
         * @param data         Raw pixel bytes; must match the chosen format.
         * @param wrap         UV wrap mode for both S and T axes.
         * @param minFilter    Minification filter.
         * @param magFilter    Magnification filter.
         * @param generateMips If true, a full mipmap chain is generated.
         * @return Opaque texture handle (0 on failure).
         */
        virtual uint32_t createTexture2D(int width, int height, kTextureFormat format,
                                         const void *data,
                                         kTextureWrap wrap = kTextureWrap::REPEAT,
                                         kTextureFilter minFilter = kTextureFilter::LINEAR_MIPMAP_LINEAR,
                                         kTextureFilter magFilter = kTextureFilter::LINEAR,
                                         bool generateMips = true) = 0;

        /**
         * @brief Creates a cube-map texture from six face images.
         *
         * @param width        Per-face width in pixels.
         * @param height       Per-face height in pixels.
         * @param faceData     Array of 6 raw pixel buffers (one per face in the
         *                     order +X, -X, +Y, -Y, +Z, -Z).
         * @param generateMips If true, a full mipmap chain is generated.
         * @return Opaque texture handle (0 on failure).
         */
        virtual uint32_t createTextureCube(int width, int height,
                                           const void *faceData[6],
                                           bool generateMips = true) = 0;

        /**
         * @brief Uploads pixel data to a specific mip level of a 2D texture.
         *
         * Used for DDS / manual mip-chain loading.  The texture must already
         * exist (created via createTexture2D with generateMips=false, or via
         * the FBO texture helpers).
         *
         * @param id     Texture handle.
         * @param level  Mip level (0 = base).
         * @param width  Width of this mip level in pixels.
         * @param height Height of this mip level in pixels.
         * @param format Pixel format.
         * @param data   Raw pixel bytes.
         */
        virtual void uploadTexture2D(uint32_t id, int level, int width, int height,
                                     kTextureFormat format, const void *data) = 0;

        /**
         * @brief Uploads a sub-region of a 2D texture (for partial updates).
         *
         * @param id     Texture handle.
         * @param level  Mip level.
         * @param x,y    Pixel offset within the texture.
         * @param width  Sub-region width in pixels.
         * @param height Sub-region height in pixels.
         * @param format Pixel format.
         * @param data   Raw pixel bytes for the sub-region.
         */
        virtual void uploadTexture2DSub(uint32_t id, int level, int x, int y,
                                        int width, int height,
                                        kTextureFormat format, const void *data) = 0;

        /**
         * @brief Uploads compressed (DXT/BCn) data to a mip level of a 2D texture.
         *
         * @param id       Texture handle.
         * @param level    Mip level.
         * @param width    Width of this mip level in pixels.
         * @param height   Height of this mip level in pixels.
         * @param format   Compression format hint (used to derive block size).
         * @param data     Compressed data.
         * @param dataSize Size of @p data in bytes.
         */
        virtual void uploadCompressedTexture2D(uint32_t id, int level,
                                               int width, int height,
                                               kTextureFormat format,
                                               const void *data, size_t dataSize) = 0;

        /**
         * @brief Uploads pixel data to a single face of a cube-map texture.
         *
         * @param id     Cube-map texture handle.
         * @param face   Face index (0 = +X, 1 = -X, 2 = +Y, 3 = -Y, 4 = +Z, 5 = -Z).
         * @param width  Face width in pixels.
         * @param height Face height in pixels.
         * @param data   Raw pixel bytes (RGB or RGBA depending on format).
         */
        virtual void uploadTextureCubeFace(uint32_t id, int face, int width, int height,
                                           const void *data) = 0;

        /**
         * @brief Deletes a texture created by createTexture2D / createTextureCube.
         *
         * Distinct from deleteFBOTexture() so the driver can track which pool
         * the texture belongs to.
         * @param id Texture handle.
         */
        virtual void deleteTexture(uint32_t id) = 0;

        // --- Texture sampling (bind/unbind for shader use) -------------------

        /**
         * @brief Binds a 2D texture to the given texture unit.
         * @param unit Texture unit index (0-based).
         * @param id   Texture handle.
         */
        virtual void bindTexture2D(int unit, uint32_t id) = 0;

        /** @brief Binds a GL_TEXTURE_2D_ARRAY to a texture unit (e.g. CSM maps). */
        virtual void bindTexture2DArray(int unit, uint32_t id) = 0;

        /**
         * @brief Binds a cube-map texture to the given texture unit.
         * @param unit Texture unit index.
         * @param id   Texture handle.
         */
        virtual void bindTextureCube(int unit, uint32_t id) = 0;

        /** @brief Unbinds any 2D texture from the given unit. */
        virtual void unbindTexture2D(int unit) = 0;

        /** @brief Unbinds any GL_TEXTURE_2D_ARRAY from a texture unit. */
        virtual void unbindTexture2DArray(int unit) = 0;

        /** @brief Unbinds any cube-map texture from the given unit. */
        virtual void unbindTextureCube(int unit) = 0;

        /**
         * @brief Generates a full mipmap chain for the given 2D texture.
         * @param id Texture handle.
         */
        virtual void generateMipmaps2D(uint32_t id) = 0;

        /**
         * @brief Reads one mip level of a 2D texture as 3-float-per-pixel RGB data.
         *
         * Primarily used for auto-exposure luminance sampling.
         * @param id       Texture handle.
         * @param mipLevel Mip level to read.
         * @param pixels   Destination buffer; must be large enough for the mip's texels.
         */
        virtual void readTexture2DRGB(uint32_t id, int mipLevel, float *pixels) = 0;

        /**
         * @brief Reads a single pixel from the currently-bound framebuffer.
         *
         * Uses the bottom-left origin convention (OpenGL standard).
         * Primarily used for color-ID object picking.
         * @param x  Pixel x coordinate from the left edge.
         * @param y  Pixel y coordinate from the bottom edge.
         * @param r  Output red channel [0, 255].
         * @param g  Output green channel [0, 255].
         * @param b  Output blue channel [0, 255].
         * @param a  Output alpha channel [0, 255].
         */
        virtual void readPixelsRGBA(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) = 0;

        // --- Framebuffers ----------------------------------------------------

        /**
         * @brief Creates and returns a new framebuffer object (FBO).
         * @return Handle to the new FBO.
         */
        virtual uint32_t createFramebuffer() = 0;

        /** @brief Destroys the given FBO. */
        virtual void deleteFramebuffer(uint32_t id) = 0;

        /** @brief Binds an FBO as both read and draw framebuffer. */
        virtual void bindFramebuffer(uint32_t id) = 0;

        /** @brief Binds an FBO as the read framebuffer only. */
        virtual void bindReadFramebuffer(uint32_t id) = 0;

        /** @brief Binds an FBO as the draw framebuffer only. */
        virtual void bindDrawFramebuffer(uint32_t id) = 0;

        /** @brief Binds the default (window) framebuffer. */
        virtual void unbindFramebuffer() = 0;

        /**
         * @brief Checks whether the currently-bound FBO is complete.
         * @return true if the FBO is ready for rendering.
         */
        virtual bool isFramebufferComplete() = 0;

        /**
         * @brief Copies a colour region from the read FBO to the draw FBO.
         * @param srcX0,srcY0 Source region bottom-left.
         * @param srcX1,srcY1 Source region top-right.
         * @param dstX0,dstY0 Destination region bottom-left.
         * @param dstX1,dstY1 Destination region top-right.
         */
        virtual void blitFramebufferColor(int srcX0, int srcY0, int srcX1, int srcY1,
                                          int dstX0, int dstY0, int dstX1, int dstY1) = 0;

        /**
         * @brief Sets the draw buffer of the current FBO to COLOR_ATTACHMENT0.
         *
         * Required after attaching colour textures to ensure correct rendering.
         */
        virtual void setFramebufferDrawBuffer() = 0;

        // --- Renderbuffers ---------------------------------------------------

        /**
         * @brief Creates and returns a new renderbuffer object (RBO).
         * @return Handle to the new RBO.
         */
        virtual uint32_t createRenderbuffer() = 0;

        /** @brief Destroys the given RBO. */
        virtual void deleteRenderbuffer(uint32_t id) = 0;

        /**
         * @brief Allocates single-sample depth+stencil storage for an RBO.
         * @param rboId  Target RBO handle.
         * @param width  Storage width in pixels.
         * @param height Storage height in pixels.
         */
        virtual void setupRenderbuffer(uint32_t rboId, int width, int height) = 0;

        /**
         * @brief Allocates multi-sample depth+stencil storage for an RBO.
         * @param rboId   Target RBO handle.
         * @param samples MSAA sample count.
         * @param width   Storage width in pixels.
         * @param height  Storage height in pixels.
         */
        virtual void setupRenderbufferMSAA(uint32_t rboId, int samples, int width, int height) = 0;

        /**
         * @brief Attaches an RBO to the depth+stencil attachment of an FBO.
         * @param fboId FBO handle.
         * @param rboId RBO handle.
         */
        virtual void attachRenderbufferDepthStencil(uint32_t fboId, uint32_t rboId) = 0;

        // --- FBO-managed textures --------------------------------------------

        /**
         * @brief Creates a single-sample colour texture suitable for FBO attachment.
         * @param width  Texture width in pixels.
         * @param height Texture height in pixels.
         * @return Handle to the new texture.
         */
        virtual uint32_t createFBOColorTexture(int width, int height) = 0;

        /**
         * @brief Creates a multi-sample colour texture suitable for MSAA FBO attachment.
         * @param samples Sample count.
         * @param width   Texture width in pixels.
         * @param height  Texture height in pixels.
         * @return Handle to the new texture.
         */
        virtual uint32_t createFBOColorTextureMSAA(int samples, int width, int height) = 0;

        /**
         * @brief Creates a depth texture suitable for shadow-map FBO attachment.
         * @param width  Texture width in pixels.
         * @param height Texture height in pixels.
         * @return Handle to the new texture.
         */
        virtual uint32_t createFBODepthTexture(int width, int height) = 0;

        /**
         * @brief Creates a layered depth texture (GL_TEXTURE_2D_ARRAY) for
         *        cascaded shadow maps — one layer per cascade.
         * @param width  Per-layer width in pixels.
         * @param height Per-layer height in pixels.
         * @param layers Number of array layers (cascades).
         * @return Handle to the new array texture.
         */
        virtual uint32_t createFBODepthTextureArray(int width, int height, int layers) = 0;

        /** @brief Destroys a texture created by createFBOColorTexture / createFBODepthTexture. */
        virtual void deleteFBOTexture(uint32_t id) = 0;

        /**
         * @brief Attaches a colour texture to the colour attachment of an FBO.
         * @param fboId FBO handle.
         * @param texId Texture handle.
         */
        virtual void attachFBOColorTexture(uint32_t fboId, uint32_t texId) = 0;

        /**
         * @brief Attaches a multi-sample colour texture to an MSAA FBO.
         * @param fboId FBO handle.
         * @param texId MSAA texture handle.
         */
        virtual void attachFBOColorTextureMSAA(uint32_t fboId, uint32_t texId) = 0;

        /**
         * @brief Attaches a depth texture to the depth attachment of an FBO.
         *
         * Also sets draw/read buffers to GL_NONE, making the FBO suitable for
         * shadow-map rendering.
         * @param fboId FBO handle.
         * @param texId Depth texture handle.
         */
        virtual void attachFBODepthTexture(uint32_t fboId, uint32_t texId) = 0;

        /**
         * @brief Attaches one layer of a depth texture array to an FBO's depth
         *        attachment (for rendering a single shadow cascade).
         * @param fboId FBO handle.
         * @param texId Depth texture-array handle.
         * @param layer Array layer (cascade index) to render into.
         */
        virtual void attachFBODepthTextureLayer(uint32_t fboId, uint32_t texId, int layer) = 0;

        /**
         * @brief Re-allocates a colour texture with a new size (for FBO resize).
         * @param texId  Texture handle.
         * @param width  New width in pixels.
         * @param height New height in pixels.
         */
        virtual void resizeFBOColorTexture(uint32_t texId, int width, int height) = 0;

        /**
         * @brief Re-allocates an MSAA colour texture with a new size.
         * @param texId   Texture handle.
         * @param samples Sample count.
         * @param width   New width in pixels.
         * @param height  New height in pixels.
         */
        virtual void resizeFBOColorTextureMSAA(uint32_t texId, int samples, int width, int height) = 0;

        // --- Global current driver -------------------------------------------

        /**
         * @brief Returns the currently active driver (set via setCurrent()).
         * @return Pointer to the active driver, or nullptr if none is set.
         */
        static kDriver *getCurrent() { return s_current; }

        /**
         * @brief Sets the globally active driver.
         * @param driver Driver to make current.
         */
        static void setCurrent(kDriver *driver) { s_current = driver; }

        // --- ImGui texture helper ---------------------------------------------

        /**
         * @brief Returns a native texture pointer suitable for use as ImTextureID.
         *
         * OpenGL drivers return the integer handle cast to @c void*;
         * D3D11 drivers return the @c ID3D11ShaderResourceView* pointer.
         *
         * @param id Texture handle from createTexture2D / createFBOColorTexture / etc.
         * @return Opaque pointer for ImGui::Image().
         */
        virtual void *getImTextureID(uint32_t id) { return reinterpret_cast<void *>(static_cast<intptr_t>(id)); }

        /**
         * @brief Returns the texture unit a sampler is bound to for a program.
         *
         * Backends whose sampler registers are fixed at compile time (DirectX)
         * return the register the sampler was compiled into, so the caller binds
         * the texture to that unit.  Backends that assign units dynamically
         * (OpenGL, OpenGL ES) return -1, meaning "pick a free unit and set the
         * sampler uniform yourself" — their behaviour is unchanged.
         *
         * @param progId Program handle returned by compileShaderProgram().
         * @param name   Sampler/uniform name as written in the shader.
         * @return Texture unit to bind to, or -1 if the backend assigns units
         *         dynamically (or the sampler is unknown).
         */
        virtual int getTextureUnitForSampler(uint32_t progId, const kString &name)
        {
            (void)progId;
            (void)name;
            return -1;
        }

        // --- Swap buffers ----------------------------------------------------

        /**
         * @brief Presents the rendered frame to the screen.
         *
         * Default implementation is a no-op; backends that manage their own
         * swap chain (DirectX, Vulkan) override this.  OpenGL backends rely on
         * the window's SDL_GL_SwapWindow instead.
         */
        virtual void swapBuffers() {}

        /**
         * @brief Resizes the backend's swap chain / default framebuffer.
         *
         * Default implementation is a no-op: the OpenGL backends draw straight
         * into the window's default framebuffer, which SDL resizes on their
         * behalf, so no resource has to be recreated.  Backends owning a swap
         * chain (DirectX, Vulkan) override this.  Implementations must ignore
         * non-positive sizes and sizes equal to the current one.
         *
         * @param width  New backing-buffer width in pixels.
         * @param height New backing-buffer height in pixels.
         */
        virtual void resizeSwapChain(int width, int height)
        {
            (void)width;
            (void)height;
        }

    private:
        static kDriver *s_current; ///< Globally active driver instance.
    };
}

#endif // KDRIVER_H
