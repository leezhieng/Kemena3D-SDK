/**
 * @file kopengldriver.h
 * @brief OpenGL 3.3 Core Profile implementation of the kDriver interface.
 */

#ifndef KOPENGLDRIVER_H
#define KOPENGLDRIVER_H

#include "kdriver.h"
#include "kwindow.h"

#ifndef KEMENA_GLES
#include <GL/glew.h>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif
#endif

#define NO_SDL_GLEXT
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <iostream>
#include <vector>
#include <unordered_map>

namespace kemena
{
    /**
     * @brief OpenGL 3.3 Core Profile graphics driver.
     *
     * Created by kRenderer when the RENDERER_GL backend is selected. Wraps all
     * raw OpenGL calls so that the rest of the engine remains API-agnostic.
     *
     * Do not instantiate this class directly — use kRenderer::init() instead.
     */
    class KEMENA3D_API kOpenGLDriver : public kDriver
    {
    public:
        kOpenGLDriver() = default;
        ~kOpenGLDriver() override;

        // --- Lifecycle -------------------------------------------------------

        /**
         * @brief Creates an OpenGL 3.3 Core Profile context for the given window.
         *
         * Sets SDL GL attributes, creates the context, initialises GLEW, and
         * applies default render state.
         * @param window SDL-backed window to create the context for.
         * @return true on success.
         */
        bool init(kWindow *window) override;

        /** @brief Destroys the SDL GL context. */
        void destroy() override;

        /** @brief Makes this driver's GL context current on the given window via SDL_GL_MakeCurrent. */
        void makeCurrent(kWindow *window) override;

        /** @brief Returns the OpenGL version kString. */
        kString getApiVersion() override;

        /** @brief Returns the GLSL version kString. */
        kString getShaderVersion() override;

        /**
         * @brief Returns the raw SDL_GLContext handle.
         * @return Opaque pointer; cast to SDL_GLContext when needed.
         */
        void *getNativeContext() override;

        // --- Frame state -----------------------------------------------------

        /**
         * @brief Sets the colour used to clear the colour buffer.
         * @param r Red component in [0,1].
         * @param g Green component in [0,1].
         * @param b Blue component in [0,1].
         * @param a Alpha component in [0,1].
         */
        void setClearColor(float r, float g, float b, float a) override;

        /**
         * @brief Clears the selected buffers of the current framebuffer.
         * @param color   Clear the colour buffer when true.
         * @param depth   Clear the depth buffer when true.
         * @param stencil Clear the stencil buffer when true.
         */
        void clear(bool color, bool depth, bool stencil) override;

        /**
         * @brief Sets the rendering viewport rectangle.
         * @param x      Lower-left x origin in pixels.
         * @param y      Lower-left y origin in pixels.
         * @param width  Viewport width in pixels.
         * @param height Viewport height in pixels.
         */
        void setViewport(int x, int y, int width, int height) override;

        // --- Pipeline state --------------------------------------------------

        /** @brief Enables or disables depth testing. @param enable true to enable. */
        void setDepthTest(bool enable) override;

        /** @brief Enables or disables writing to the depth buffer. @param enable true to enable. */
        void setDepthWrite(bool enable) override;

        /** @brief Enables or disables alpha blending. @param enable true to enable. */
        void setBlend(bool enable) override;

        /**
         * @brief Sets the source and destination blend factors.
         * @param src Source colour blend factor.
         * @param dst Destination colour blend factor.
         */
        void setBlendFunc(kBlendFactor src, kBlendFactor dst) override;

        /** @brief Enables or disables face culling. @param enable true to enable. */
        void setCullFace(bool enable) override;

        /** @brief Selects which faces are culled. @param mode Front, back, or front-and-back. */
        void setCullMode(kCullMode mode) override;

        /** @brief Sets the winding order treated as front-facing. @param face CW or CCW. */
        void setFrontFace(kFrontFace face) override;

        /** @brief Enables or disables multisample anti-aliasing. @param enable true to enable. */
        void setMultisample(bool enable) override;

        /** @brief Enables or disables sRGB framebuffer encoding. @param enable true to enable. */
        void setSRGBEncoding(bool enable) override;

        /** @brief Enables or disables alpha-to-coverage sampling. @param enable true to enable. */
        void setSampleAlphaToCoverage(bool enable) override;

        /** @brief Enables or disables wireframe rendering. @param enable true for wireframe. */
        void setWireframe(bool enable) override;

        // --- Shader programs -------------------------------------------------

        /**
         * @brief Compiles and links a shader program from GLSL source.
         * @param vertSrc Null-terminated vertex shader source.
         * @param fragSrc Null-terminated fragment shader source.
         * @return GL program id, or 0 on failure.
         */
        uint32_t compileShaderProgram(const char *vertSrc, const char *fragSrc) override;

        /**
         * @brief Compiles and links a shader program from SPIR-V binaries.
         *
         * Requires GL_ARB_gl_spirv or OpenGL 4.6; returns 0 if unsupported.
         * @param vertSpirv SPIR-V byte code for the vertex stage.
         * @param vertEntry Entry-point name for the vertex stage.
         * @param fragSpirv SPIR-V byte code for the fragment stage.
         * @param fragEntry Entry-point name for the fragment stage.
         * @return GL program id, or 0 on failure.
         */
        uint32_t compileShaderProgramSpirv(const std::vector<uint8_t> &vertSpirv,
                                           const kString &vertEntry,
                                           const std::vector<uint8_t> &fragSpirv,
                                           const kString &fragEntry) override;

        /** @brief Deletes a shader program. @param id GL program id. */
        void deleteShaderProgram(uint32_t id) override;

        /** @brief Makes a shader program current for subsequent draws. @param id GL program id. */
        void bindShaderProgram(uint32_t id) override;

        /** @brief Unbinds the current shader program. */
        void unbindShaderProgram() override;

        /** @brief Sets a bool uniform. @param progId Program id. @param name Uniform name. @param v Value. */
        void setUniformBool(uint32_t progId, const kString &name, bool v) override;

        /** @brief Sets an int uniform. @param progId Program id. @param name Uniform name. @param v Value. */
        void setUniformInt(uint32_t progId, const kString &name, int v) override;

        /** @brief Sets an unsigned int uniform. @param progId Program id. @param name Uniform name. @param v Value. */
        void setUniformUint(uint32_t progId, const kString &name, uint32_t v) override;

        /** @brief Sets a float uniform. @param progId Program id. @param name Uniform name. @param v Value. */
        void setUniformFloat(uint32_t progId, const kString &name, float v) override;

        /** @brief Sets a vec2 uniform. @param progId Program id. @param name Uniform name. @param v Value. */
        void setUniformVec2(uint32_t progId, const kString &name, const kVec2 &v) override;

        /** @brief Sets a vec3 uniform. @param progId Program id. @param name Uniform name. @param v Value. */
        void setUniformVec3(uint32_t progId, const kString &name, const kVec3 &v) override;

        /** @brief Sets a vec4 uniform. @param progId Program id. @param name Uniform name. @param v Value. */
        void setUniformVec4(uint32_t progId, const kString &name, const kVec4 &v) override;

        /** @brief Sets a mat4 uniform. @param progId Program id. @param name Uniform name. @param v Value. */
        void setUniformMat4(uint32_t progId, const kString &name, const kMat4 &v) override;

        /**
         * @brief Sets an array-of-mat4 uniform.
         * @param progId Program id.
         * @param name   Uniform array name.
         * @param v      Matrices to upload.
         */
        void setUniformMat4Array(uint32_t progId, const kString &name, const std::vector<kMat4> &v) override;

        // --- Vertex arrays ---------------------------------------------------

        /** @brief Creates a vertex array object. @return GL VAO id. */
        uint32_t createVertexArray() override;

        /** @brief Deletes a vertex array object. @param id GL VAO id. */
        void deleteVertexArray(uint32_t id) override;

        /** @brief Binds a vertex array object. @param id GL VAO id. */
        void bindVertexArray(uint32_t id) override;

        /** @brief Unbinds the current vertex array object. */
        void unbindVertexArray() override;

        // --- Buffers ---------------------------------------------------------

        /** @brief Creates a GPU buffer object. @return GL buffer id. */
        uint32_t createBuffer() override;

        /** @brief Deletes a GPU buffer object. @param id GL buffer id. */
        void deleteBuffer(uint32_t id) override;

        /**
         * @brief Uploads index data into an element array buffer (static draw).
         * @param bufferId GL buffer id.
         * @param data     Pointer to index data.
         * @param size     Size of the data in bytes.
         */
        void uploadIndexBuffer(uint32_t bufferId, const void *data, size_t size) override;

        /**
         * @brief Uploads vertex data into an array buffer (static draw).
         * @param bufferId GL buffer id.
         * @param data     Pointer to vertex data.
         * @param size     Size of the data in bytes.
         */
        void uploadVertexBuffer(uint32_t bufferId, const void *data, size_t size) override;

        /**
         * @brief Updates a sub-region of an existing vertex buffer without reallocation.
         * @param bufferId Target buffer handle.
         * @param data     Pointer to new data.
         * @param size     Size of the sub-region in bytes.
         * @param offset   Byte offset from the start of the buffer.
         */
        void updateBufferSubData(uint32_t bufferId, const void *data, size_t size, size_t offset) override;

        /**
         * @brief Defines a floating-point vertex attribute layout.
         * @param location   Attribute location index.
         * @param components Number of components per vertex (1-4).
         * @param stride     Byte stride between consecutive vertices.
         * @param offset     Byte offset of the attribute within the vertex.
         */
        void setVertexAttribFloat(int location, int components, int stride, size_t offset) override;

        /**
         * @brief Defines an integer vertex attribute layout.
         * @param location   Attribute location index.
         * @param components Number of components per vertex (1-4).
         * @param stride     Byte stride between consecutive vertices.
         * @param offset     Byte offset of the attribute within the vertex.
         */
        void setVertexAttribInt(int location, int components, int stride, size_t offset) override;

        /**
         * @brief Sets the instance divisor for a vertex attribute on the currently-bound VAO.
         * @param location Attribute location index.
         * @param divisor  Number of instances that share the same attribute value (0 = per-vertex).
         */
        void setVertexAttribDivisor(int location, int divisor) override;

        // --- Draw calls ------------------------------------------------------

        /**
         * @brief Issues an indexed draw call (triangles) for the given VAO.
         * @param vaoId      GL VAO id to bind.
         * @param indexCount Number of indices to draw.
         */
        void drawIndexed(uint32_t vaoId, int indexCount) override;

        /**
         * @brief Issues an indexed, instanced draw call for the given VAO.
         * @param vaoId         GL VAO id to bind.
         * @param indexCount    Number of indices per instance.
         * @param instanceCount Number of instances to draw.
         */
        void drawIndexedInstanced(uint32_t vaoId, int indexCount, int instanceCount) override;

        /**
         * @brief Issues a non-indexed draw call for the given VAO.
         * @param vaoId       GL VAO id to bind.
         * @param type        Primitive topology to assemble.
         * @param vertexCount Number of vertices to draw.
         */
        void drawArrays(uint32_t vaoId, kPrimitiveType type, int vertexCount) override;

        /**
         * @brief Issues a non-indexed, instanced draw call for the given VAO.
         * @param vaoId         GL VAO id to bind.
         * @param type          Primitive topology to assemble.
         * @param vertexCount   Number of vertices per instance.
         * @param instanceCount Number of instances to draw.
         */
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

        /** @brief Binds a 2D texture to a texture unit. @param unit Texture unit index. @param id GL texture id. */
        void bindTexture2D(int unit, uint32_t id) override;

        /** @brief Binds a 2D-array texture to a texture unit. @param unit Texture unit index. @param id GL texture id. */
        void bindTexture2DArray(int unit, uint32_t id) override;

        /** @brief Binds a cubemap texture to a texture unit. @param unit Texture unit index. @param id GL texture id. */
        void bindTextureCube(int unit, uint32_t id) override;

        /** @brief Unbinds the 2D texture on a unit. @param unit Texture unit index. */
        void unbindTexture2D(int unit) override;

        /** @brief Unbinds the 2D-array texture on a unit. @param unit Texture unit index. */
        void unbindTexture2DArray(int unit) override;

        /** @brief Unbinds the cubemap texture on a unit. @param unit Texture unit index. */
        void unbindTextureCube(int unit) override;

        /** @brief Generates the full mipmap chain for a 2D texture. @param id GL texture id. */
        void generateMipmaps2D(uint32_t id) override;

        /**
         * @brief Reads back an RGB float image from a 2D texture mip level.
         * @param id       GL texture id.
         * @param mipLevel Mip level to read.
         * @param pixels   Destination buffer (RGB floats), sized by caller.
         */
        void readTexture2DRGB(uint32_t id, int mipLevel, float *pixels) override;

        /**
         * @brief Reads a single RGBA pixel from the current read framebuffer.
         * @param x Pixel x coordinate.
         * @param y Pixel y coordinate.
         * @param r Out: red component.
         * @param g Out: green component.
         * @param b Out: blue component.
         * @param a Out: alpha component.
         */
        void readPixelsRGBA(int x, int y, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) override;

        // --- Framebuffers ----------------------------------------------------

        /** @brief Creates a framebuffer object. @return GL FBO id. */
        uint32_t createFramebuffer() override;

        /** @brief Deletes a framebuffer object. @param id GL FBO id. */
        void deleteFramebuffer(uint32_t id) override;

        /** @brief Binds an FBO as both read and draw target. @param id GL FBO id. */
        void bindFramebuffer(uint32_t id) override;

        /** @brief Binds an FBO as the read target. @param id GL FBO id. */
        void bindReadFramebuffer(uint32_t id) override;

        /** @brief Binds an FBO as the draw target. @param id GL FBO id. */
        void bindDrawFramebuffer(uint32_t id) override;

        /** @brief Unbinds the current FBO, restoring the default framebuffer. */
        void unbindFramebuffer() override;

        /** @brief Checks framebuffer completeness. @return true if the bound FBO is complete. */
        bool isFramebufferComplete() override;

        /**
         * @brief Blits the colour buffer between the read and draw framebuffers (nearest filter).
         * @param srcX0 Source rectangle left.
         * @param srcY0 Source rectangle bottom.
         * @param srcX1 Source rectangle right.
         * @param srcY1 Source rectangle top.
         * @param dstX0 Destination rectangle left.
         * @param dstY0 Destination rectangle bottom.
         * @param dstX1 Destination rectangle right.
         * @param dstY1 Destination rectangle top.
         */
        void blitFramebufferColor(int srcX0, int srcY0, int srcX1, int srcY1,
                                  int dstX0, int dstY0, int dstX1, int dstY1) override;

        /** @brief Sets the draw buffer of the bound FBO to colour attachment 0. */
        void setFramebufferDrawBuffer() override;

        // --- Renderbuffers ---------------------------------------------------

        /** @brief Creates a renderbuffer object. @return GL RBO id. */
        uint32_t createRenderbuffer() override;

        /** @brief Deletes a renderbuffer object. @param id GL RBO id. */
        void deleteRenderbuffer(uint32_t id) override;

        /**
         * @brief Allocates depth/stencil storage for a renderbuffer.
         * @param rboId  GL RBO id.
         * @param width  Storage width in pixels.
         * @param height Storage height in pixels.
         */
        void setupRenderbuffer(uint32_t rboId, int width, int height) override;

        /**
         * @brief Allocates multisampled depth/stencil storage for a renderbuffer.
         * @param rboId   GL RBO id.
         * @param samples Number of MSAA samples.
         * @param width   Storage width in pixels.
         * @param height  Storage height in pixels.
         */
        void setupRenderbufferMSAA(uint32_t rboId, int samples, int width, int height) override;

        /**
         * @brief Attaches a renderbuffer as the depth/stencil attachment of an FBO.
         * @param fboId GL FBO id.
         * @param rboId GL RBO id.
         */
        void attachRenderbufferDepthStencil(uint32_t fboId, uint32_t rboId) override;

        // --- FBO-managed textures --------------------------------------------

        /**
         * @brief Creates a colour texture suitable for FBO attachment.
         * @param width  Texture width in pixels.
         * @param height Texture height in pixels.
         * @return GL texture id.
         */
        uint32_t createFBOColorTexture(int width, int height) override;

        /**
         * @brief Creates a multisampled colour texture suitable for FBO attachment.
         * @param samples Number of MSAA samples.
         * @param width   Texture width in pixels.
         * @param height  Texture height in pixels.
         * @return GL texture id.
         */
        uint32_t createFBOColorTextureMSAA(int samples, int width, int height) override;

        /**
         * @brief Creates a depth texture suitable for FBO attachment.
         * @param width  Texture width in pixels.
         * @param height Texture height in pixels.
         * @return GL texture id.
         */
        uint32_t createFBODepthTexture(int width, int height) override;

        /**
         * @brief Creates a layered depth texture array (e.g. for cascaded shadow maps).
         * @param width  Texture width in pixels.
         * @param height Texture height in pixels.
         * @param layers Number of array layers.
         * @return GL texture id.
         */
        uint32_t createFBODepthTextureArray(int width, int height, int layers) override;

        /** @brief Deletes an FBO-managed texture. @param id GL texture id. */
        void deleteFBOTexture(uint32_t id) override;

        /**
         * @brief Attaches a colour texture to colour attachment 0 of an FBO.
         * @param fboId GL FBO id.
         * @param texId GL texture id.
         */
        void attachFBOColorTexture(uint32_t fboId, uint32_t texId) override;

        /**
         * @brief Attaches a multisampled colour texture to colour attachment 0 of an FBO.
         * @param fboId GL FBO id.
         * @param texId GL texture id.
         */
        void attachFBOColorTextureMSAA(uint32_t fboId, uint32_t texId) override;

        /**
         * @brief Attaches a depth texture to the depth attachment of an FBO.
         * @param fboId GL FBO id.
         * @param texId GL texture id.
         */
        void attachFBODepthTexture(uint32_t fboId, uint32_t texId) override;

        /**
         * @brief Attaches a single layer of a depth texture array as the depth attachment.
         * @param fboId GL FBO id.
         * @param texId GL texture-array id.
         * @param layer Array layer to attach.
         */
        void attachFBODepthTextureLayer(uint32_t fboId, uint32_t texId, int layer) override;

        /**
         * @brief Reallocates an FBO colour texture to a new size.
         * @param texId  GL texture id.
         * @param width  New width in pixels.
         * @param height New height in pixels.
         */
        void resizeFBOColorTexture(uint32_t texId, int width, int height) override;

        /**
         * @brief Reallocates a multisampled FBO colour texture to a new size/sample count.
         * @param texId   GL texture id.
         * @param samples Number of MSAA samples.
         * @param width   New width in pixels.
         * @param height  New height in pixels.
         */
        void resizeFBOColorTextureMSAA(uint32_t texId, int samples, int width, int height) override;

    private:
        SDL_GLContext glContext = nullptr; ///< The SDL-managed OpenGL context.

        /// Per-program reflected uniform table (uniform path → location).
        ///
        /// Mirrors the DirectX backend, where a uniform can only be addressed
        /// through a reflected table because HLSL constant buffers expose no
        /// per-uniform location.  Filling it from glGetActiveUniform makes both
        /// backends resolve the same name paths ("material.diffuse",
        /// "u_Tiling[2]", "sunLights[3].position") in the same way.
        std::unordered_map<uint32_t, std::unordered_map<kString, GLint>> uniformTable;

        /**
         * @brief Resolves a uniform path to its location inside a program.
         *
         * Queries the reflected table first and falls back to
         * glGetUniformLocation() (caching the answer), so names the reflection did
         * not enumerate behave exactly as before.
         */
        GLint resolveUniformLocation(uint32_t progId, const kString &name);

        /** @brief Converts a kBlendFactor to the corresponding GL enum. */
        GLenum toGLBlendFactor(kBlendFactor factor);

        /** @brief Converts a kPrimitiveType to the corresponding GL enum. */
        GLenum toGLPrimitiveType(kPrimitiveType type);
    };
}

#endif // KOPENGLDRIVER_H
