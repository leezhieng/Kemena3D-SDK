/**
 * @file kshader.h
 * @brief GLSL shader program wrapper.
 */

#ifndef KSHADER_H
#define KSHADER_H

#include "kexport.h"
#include "kdriver.h"
#include "kdatatype.h"

#include <iostream>
#include <string>
#include <vector>
#include <fstream>

namespace kemena
{


    /**
     * @brief Holds the split stages of a combined shader source.
     *
     * Combined shader files use section markers to separate stages:
     *   - vertex source appears before any marker (or after "// --- VERTEX ---")
     *   - "// --- GEOMETRY ---" introduces an optional geometry stage
     *   - "// --- FRAGMENT ---" introduces the fragment / pixel stage
     */
    struct KEMENA3D_API kShaderSource
    {
        kString vertex;   ///< Vertex stage source.
        kString geometry; ///< Optional geometry stage source (empty if absent).
        kString fragment; ///< Fragment / pixel stage source.
    };

    /**
     * @brief Wraps a compiled GLSL shader program.
     *
     * Shaders can be loaded from source files or inline code strings.
     * All GPU operations are dispatched through kDriver::getCurrent(), so no
     * GL headers need to be included in client code.
     *
     * Example:
     * @code
     *   kShader shader;
     *   shader.loadShadersFile("vertex.glsl", "fragment.glsl");
     *   shader.use();
     *   shader.setValue("modelMatrix", modelMat);
     *   // draw ...
     *   shader.unuse();
     * @endcode
     */
    class KEMENA3D_API kShader
    {
    public:
        /** @brief Constructs an empty shader with no compiled program. */
        kShader();

        /** @brief Destroys the shader, deleting the GPU program if one was compiled. */
        virtual ~kShader();

        /**
         * @brief Reads the entire contents of a text file into a string.
         * @param filePath Path to the file to read.
         * @return The file contents, or an empty string if the file cannot be opened.
         */
        kString readFile(const kString filePath);

        /**
         * @brief Splits a combined shader source string into per-stage strings.
         *
         * Recognised section markers (on their own line):
         *   // --- VERTEX ---      (optional; top of file is implicitly vertex)
         *   // --- GEOMETRY ---    (optional geometry stage)
         *   // --- FRAGMENT ---    (fragment / pixel stage)
         */
        static kShaderSource splitSource(const kString& src);

        // --- OpenGL (GLSL) ---------------------------------------------------

        /** Load a combined GLSL source file (vertex + optional geometry + fragment). */
        void loadGlslFile(const kString& path);
        /** Load a combined GLSL source string. */
        void loadGlslCode(const kString& src);

        /** Load separate GLSL source files (vertex, fragment). */
        void loadShadersFile(const kString vertexShaderPath, const kString fragmentShaderPath);
        /** Load separate GLSL source strings (vertex, fragment). */
        void loadShadersCode(const char *vertexShaderCode, const char *fragmentShaderCode);

        // --- DirectX 11 (HLSL / DXBC) ----------------------------------------

        /**
         * @brief Loads a combined HLSL source file for the DirectX 11 backend.
         *
         * The engine does not translate shader source, so an application that runs
         * on D3D11 supplies HLSL here and GLSL through loadGlsl*() for the OpenGL
         * backends.  Sections use the same markers as splitSource():
         * @code
         *   // --- VERTEX ---       // VSMain (vs_5_0)
         *   // --- FRAGMENT ---     // PSMain (ps_5_0)
         * @endcode
         * Either section may be omitted; a source without markers is treated as a
         * single stage, or as a file holding both entry points when it declares
         * both VSMain and PSMain (the file is then compiled once per stage).
         *
         * A vertex shader must declare its inputs with the semantics used by the
         * D3D11 driver (location 0 = POSITION, 1 = COLOR, 2 = TEXCOORD, 3 = NORMAL,
         * 4 = TANGENT, 5 = BINORMAL, 6 = BLENDINDICES, 7 = BLENDWEIGHT), and
         * uniforms must live in a constant buffer so they can be set through
         * setValue().
         */
        void loadHlslFileDX11(const kString& path);

        /** @brief Loads a combined HLSL source string (see loadHlslFileDX11()). */
        void loadHlslCodeDX11(const kString& src);

        // --- DirectX 12 (HLSL / DXIL via DXC) --------------------------------

        /** Placeholder: load a combined HLSL source file for a DirectX 12 backend. */
        void loadHlslFileDX12(const kString& path);
        /** Placeholder: load a combined HLSL source string for a DirectX 12 backend. */
        void loadHlslCodeDX12(const kString& src);

        // --- Vulkan 1.3 (SPIR-V) ---------------------------------------------

        /** Placeholder: load a combined GLSL/HLSL source file for Vulkan 1.3 (compiled to SPIR-V). */
        void loadSpirvFile(const kString& path);
        /** Placeholder: load a combined GLSL/HLSL source string for Vulkan 1.3. */
        void loadSpirvCode(const kString& src);
        /** Placeholder: load pre-compiled SPIR-V binary blobs for Vulkan 1.3. */
        void loadSpirvBinary(const std::vector<uint8_t>& vertSpv, const std::vector<uint8_t>& fragSpv);

        // --- Metal (via MoltenVK / metal-cpp) --------------------------------

        /** Placeholder: load a combined Metal Shading Language source file. */
        void loadMetalFile(const kString& path);
        /** Placeholder: load a combined Metal Shading Language source string. */
        void loadMetalCode(const kString& src);

        /** @brief Binds this shader program for subsequent draw calls. */
        void use();

        /** @brief Unbinds the current shader program. */
        void unuse();

        /**
         * @brief Overrides the internal program handle.
         *
         * Use this when the program was compiled externally (e.g. via kDriver).
         * @param program Compiled program handle.
         */
        void setShaderProgram(uint32_t program);

        /**
         * @brief Returns the underlying program handle.
         * @return Opaque GPU handle (0 if not yet compiled).
         */
        uint32_t getShaderProgram();

        /** @brief Sets a kMat4 array uniform. */
        void setValue(kString name, std::vector<kMat4> value);
        /** @brief Sets a kMat4 uniform. */
        void setValue(kString name, kMat4 value);
        /** @brief Sets a kVec4 uniform. */
        void setValue(kString name, kVec4 value);
        /** @brief Sets a kVec3 uniform. */
        void setValue(kString name, kVec3 value);
        /** @brief Sets a kVec2 uniform. */
        void setValue(kString name, kVec2 value);
        /** @brief Sets a float uniform. */
        void setValue(kString name, float value);
        /** @brief Sets an integer uniform. */
        void setValue(kString name, int value);
        /** @brief Sets an unsigned-integer uniform. */
        void setValue(kString name, unsigned int value);
        /** @brief Sets a boolean uniform (uploaded as int). */
        void setValue(kString name, bool value);

    protected:
    private:
        uint32_t shaderProgram = 0; ///< Compiled program handle; 0 until loaded.
    };
}

#endif // KSHADER_H
