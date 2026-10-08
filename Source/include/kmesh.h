/**
 * @file kmesh.h
 * @brief Polygonal mesh node with skeletal animation support.
 */

#ifndef KMESH_H
#define KMESH_H

#include "kexport.h"
#include "kdriver.h"

#include <map>
#include <vector>
#include <string>

#include "kobject.h"
#include "kbone.h"
// (Skeletal animation is reached through the forward-declared kAnimator
// pointer below — no animation header needed at the mesh-header level.)
#include "kanimator.h"

namespace kemena
{
    class kAnimator;

    /**
     * @brief One blend-shape (morph target) attached to a mesh.
     *
     * Stores the per-vertex *delta* from the base mesh in object space (target
     * minus base), which is exactly what the vertex shader accumulates:
     * @code
     *   morphedPos += weight * positionDeltas[vertex];
     * @endcode
     * The delta representation is format agnostic — glTF targets are already
     * deltas, and FBX absolute shapes are converted on import.
     */
    struct kMorphTarget
    {
        kString            name;           ///< Blend-shape name (e.g. "Blend0", "mouthSmile").
        std::vector<kVec3> positionDeltas; ///< Per-vertex position delta (object space).
        std::vector<kVec3> normalDeltas;   ///< Per-vertex normal delta; may be empty.
    };

    /**
     * @brief Scene-graph node that holds renderable geometry.
     *
     * Stores per-vertex attributes (position, UV, normal, tangent, bitangent,
     * colour, bone IDs, bone weights) together with an index buffer.  GPU
     * buffers are allocated lazily via generateVbo().  Supports both static
     * and skeletal-animated meshes through an optional kAnimator attachment.
     *
     * Also supports GPU blend shapes (morph targets): each target's deltas are
     * uploaded as their own vertex stream and blended in the vertex shader by
     * the per-target weights uploaded from getMorphWeights().
     */
    class KEMENA3D_API kMesh : public kObject
    {
    public:
        /// Number of morph targets the vertex shader can blend (mirrors the
        /// MAX_MORPH_TARGETS constant in the generated shader).
        static const int MAX_MORPH_TARGETS = 4;
        /**
         * @brief Constructs a mesh node and optionally attaches it to a parent.
         * @param parentNode Parent scene-graph node, or nullptr for a root node.
         */
        kMesh(kObject *parentNode = nullptr);

        /**
         * @brief Destroys the mesh and releases its GPU buffers.
         */
        ~kMesh();

        /**
         * @brief Marks whether the mesh geometry has been fully loaded.
         * @param newLoaded true once all vertex data has been populated.
         */
        void setLoaded(bool newLoaded);

        /**
         * @brief Returns whether the mesh geometry is fully loaded.
         * @return true after all vertex data has been populated.
         */
        bool getLoaded();

        /**
         * @brief Sets the source asset file path.
         * @param newFileName Path to the mesh asset on disk.
         */
        void setFileName(kString newFileName);

        /**
         * @brief Returns the source asset file path.
         * @return File path kString.
         */
        kString getFileName();

        /**
         * @brief Sets the reference name used to identify shared mesh data.
         * @param newRefName Reference identifier kString.
         */
        void setRefName(kString newRefName);

        /**
         * @brief Returns the reference name.
         * @return Reference identifier kString.
         */
        kString getRefName();

        /**
         * @brief Marks this mesh as one of the built-in procedural primitives
         *        ("cube", "sphere", "cylinder", "capsule", "plane"). Set by
         *        kMeshGenerator so save/load and duplicate can rebuild the
         *        geometry from the marker instead of needing a file on disk.
         * @param type Primitive marker name (e.g. "cube", "sphere"), or empty
         *        for non-primitive meshes.
         */
        void setPrimitiveType(kString type);

        /**
         * @brief Returns the procedural-primitive marker, if any.
         * @return Primitive type name, or an empty kString for file-loaded meshes.
         */
        kString getPrimitiveType() const;

        /**
         * @brief Overrides the serialized type from "mesh" to a custom value
         *        (e.g. "terrain"). Used by the terrain system so tiles are
         *        saved/loaded as a distinct type in .world files.
         */
        void setSerializeType(const kString &type);

        /** @brief Returns the serialized type override, or empty if default. */
        const kString &getSerializeType() const;

        /**
         * @brief Sets terrain metadata fields on the mesh so they are
         *        serialized alongside the mesh data in .world files.
         */
        void setTerrainData(int gridX, int gridZ, float worldSize, int heightRes,
                            const kString &heightFile, const kString &splatFile);

        /** @brief Propagates a local position change to this mesh and its children. */
        void setPosition(kVec3 newPosition) override;
        /** @brief Propagates a local rotation change to this mesh and its children. */
        void setRotation(kQuat newRotation) override;
        /** @brief Propagates a local scale change to this mesh and its children. */
        void setScale(kVec3 newScale) override;

        /** @brief Force-sets the local position, bypassing the static guard (editor/deserialization). */
        void setPositionForced(kVec3 newPosition) override;
        /** @brief Force-sets the local rotation, bypassing the static guard (editor/deserialization). */
        void setRotationForced(kQuat newRotation) override;
        /** @brief Force-sets the local scale, bypassing the static guard (editor/deserialization). */
        void setScaleForced(kVec3 newScale) override;

        /**
         * @brief Pre-allocates bone ID and weight arrays for a given vertex count.
         * @param vertexCount Number of vertices to reserve space for.
         */
        void reserveBoneData(size_t vertexCount);

        /**
         * @brief Pre-allocates all per-vertex attribute vectors.
         * @param vertexCount Number of vertices to reserve space for.
         */
        void reserveSpace(size_t vertexCount);

        /**
         * @brief Replaces the bone-name-to-info map.
         * @param newBoneInfoMap Map from bone name to kBoneInfo.
         */
        void setBoneInfoMap(std::map<kString, kBoneInfo> newBoneInfoMap);

        /**
         * @brief Returns a reference to the bone-name-to-info map.
         * @return Mutable reference to the internal map.
         */
        std::map<kString, kBoneInfo> &getBoneInfoMap();

        /**
         * @brief Returns a reference to the bone counter used during loading.
         * @return Mutable reference to the bone count integer.
         */
        int &getBoneCount();

        /**
         * @brief Sets the total number of bones.
         * @param newBoneCount Bone count.
         */
        void setBoneCount(int newBoneCount);

        /**
         * @brief Appends an index to the index buffer.
         * @param index Triangle vertex index.
         */
        void addIndex(uint32_t index);

        /**
         * @brief Returns a copy of the index buffer.
         * @return Vector of triangle indices.
         */
        std::vector<uint32_t> getIndices();

        /**
         * @brief Appends a vertex position.
         * @param vertex XYZ position in object space.
         */
        void addVertex(kVec3 vertex);

        /**
         * @brief Returns a copy of the vertex position buffer.
         * @return Vector of positions.
         */
        std::vector<kVec3> getVertices();

        /**
         * @brief Returns a mutable reference to the vertex position buffer.
         * @return Mutable reference to the internal vertex vector.
         */
        std::vector<kVec3> &getVerticesRef();

        /**
         * @brief Appends a UV coordinate.
         * @param uv Texture coordinate (U, V).
         */
        void addUV(kVec2 uv);

        /**
         * @brief Returns a copy of the UV coordinate buffer.
         * @return Vector of UV coordinates.
         */
        std::vector<kVec2> getUVs();

        /**
         * @brief Appends a per-vertex colour.
         * @param color RGB colour value (0..1 per channel).
         */
        void addVertexColor(kVec3 color);

        /**
         * @brief Returns a copy of the per-vertex colour buffer.
         * @return Vector of RGB colours.
         */
        std::vector<kVec3> getVertexColors();

        /**
         * @brief Appends a vertex normal.
         * @param normal Normalised surface normal in object space.
         */
        void addNormal(kVec3 normal);

        /**
         * @brief Returns a copy of the normal buffer.
         * @return Vector of normals.
         */
        std::vector<kVec3> getNormals();

        /**
         * @brief Returns a mutable reference to the normal buffer.
         * @return Mutable reference to the internal normals vector.
         */
        std::vector<kVec3> &getNormalsRef();

        /**
         * @brief Appends a vertex tangent.
         * @param tangent Normalised tangent vector in object space.
         */
        void addTangent(kVec3 tangent);

        /**
         * @brief Returns a copy of the tangent buffer.
         * @return Vector of tangents.
         */
        std::vector<kVec3> getTangents();

        /**
         * @brief Appends a vertex bitangent.
         * @param bitangent Normalised bitangent vector in object space.
         */
        void addBitangent(kVec3 bitangent);

        /**
         * @brief Returns a copy of the bitangent buffer.
         * @return Vector of bitangents.
         */
        std::vector<kVec3> getBitangents();

        /**
         * @brief Appends a bone-ID tuple for the next vertex.
         * @param boneID Up to four bone indices influencing the vertex.
         */
        void addBoneID(const kIvec4 &boneID);

        /**
         * @brief Overwrites the bone-ID tuple for a specific vertex.
         * @param vertexIndex Zero-based vertex index.
         * @param boneID      New bone-ID tuple.
         */
        void setBoneID(size_t vertexIndex, const kIvec4 &boneID);

        /**
         * @brief Returns the bone-ID tuple for a vertex.
         * @param vertexIndex Zero-based vertex index.
         * @return Four bone indices.
         */
        kIvec4 getBoneID(size_t vertexIndex);

        /**
         * @brief Returns a copy of the bone-ID buffer.
         * @return Vector of kIvec4 bone indices, one per vertex.
         */
        std::vector<kIvec4> getBoneIDs();

        /**
         * @brief Replaces the entire bone-ID buffer.
         * @param newBoneIDs New buffer.
         */
        void setBoneIDs(std::vector<kIvec4> newBoneIDs);

        /**
         * @brief Appends a bone-weight tuple for the next vertex.
         * @param weight Four blend weights (must sum to 1.0).
         */
        void addWeight(const kVec4 &weight);

        /**
         * @brief Overwrites the bone-weight tuple for a specific vertex.
         * @param vertexIndex Zero-based vertex index.
         * @param weight      New blend-weight tuple.
         */
        void setWeight(size_t vertexIndex, const kVec4 &weight);

        /**
         * @brief Returns the bone-weight tuple for a vertex.
         * @param vertexIndex Zero-based vertex index.
         * @return Four blend weights.
         */
        kVec4 getWeight(size_t vertexIndex);

        /**
         * @brief Returns a copy of the bone-weight buffer.
         * @return Vector of kVec4 weights, one per vertex.
         */
        std::vector<kVec4> getWeights();

        /**
         * @brief Replaces the entire bone-weight buffer.
         * @param newWeights New buffer.
         */
        void setWeights(std::vector<kVec4> newWeights);

        /**
         * @brief Returns the number of vertices in this mesh.
         * @return Vertex count.
         */
        int getVertexCount();

        /**
         * @brief Returns the GPU handle for the Vertex Array Object.
         * @return VAO handle (0 if not yet uploaded).
         */
        uint32_t getVertexArrayObject();

        /**
         * @brief Returns the GPU handle for the position VBO.
         * @return VBO handle (0 if not yet uploaded).
         */
        uint32_t getVertexBuffer();

        /**
         * @brief Returns the GPU handle for the vertex-colour VBO.
         * @return VBO handle (0 if not yet uploaded).
         */
        uint32_t getVertexColorBuffer();

        /**
         * @brief Stores a precomputed normal matrix.
         * @param newNormalMatrix Inverse-transpose of the model matrix (upper 3x3).
         */
        void setNormalMatrix(kMat4 newNormalMatrix);

        /**
         * @brief Returns the stored normal matrix.
         * @return 4x4 matrix whose upper-left 3x3 is the normal matrix.
         */
        kMat4 getNormalMatrix();

        /**
         * @brief Computes the local-space AABB from the vertex position buffer.
         *
         * Called automatically by generateVbo(); safe to call manually if vertices
         * are modified after VBO generation.
         */
        void computeLocalAABB();

        /**
         * @brief Returns the axis-aligned bounding box in local (object) space.
         */
        kAABB getLocalAABB() const;

        /**
         * @brief Returns the AABB transformed into world space.
         *
         * Transforms all 8 corners of the local AABB by the current world matrix
         * and returns the enclosing axis-aligned box.  Call calculateModelMatrix()
         * first to ensure the world transform is up to date.
         */
        kAABB getWorldAABB();

        /**
         * @brief Uploads all vertex attribute data to the GPU.
         *
         * Creates a VAO, one VBO per attribute, and an EBO, then describes
         * the attribute layout to the driver.  Safe to call multiple times;
         * subsequent calls re-upload the data.
         */
        void generateVbo();

        /**
         * @brief Computes per-vertex tangents and bitangents from positions, UVs, and indices.
         * Must be called after all vertices, UVs, normals, and indices have been added and
         * before generateVbo().
         */
        void generateTangents();

        /**
         * @brief Overwrites GPU position VBO with current CPU vertex data via glBufferSubData.
         *
         * The vertex buffer must already have been uploaded via generateVbo().
         * Faster than a full rebuild — skips VAO/IBO/UV/material reconstruction.
         */
        void updatePositions();

        /**
         * @brief Overwrites GPU normal VBO with current CPU normal data via glBufferSubData.
         *
         * The normal buffer must already have been uploaded via generateVbo().
         */
        void updateNormals();

        /**
         * @brief Recomputes tangents/bitangents and overwrites both GPU VBOs via glBufferSubData.
         *
         * Calls generateTangents() internally, then sub-updates both tangent
         * and bitangent VBOs. The buffers must already exist from generateVbo().
         */
        void updateTangents();

        /**
         * @brief Recomputes the normal matrix from the current world transform.
         */
        void calculateNormalMatrix();

        /**
         * @brief Issues a draw call for this mesh via the current kDriver.
         */
        void draw();

        /**
         * @brief Serialises the mesh to JSON.
         * @return JSON object with geometry, transform, and material references.
         */
        json serialize();

        /**
         * @brief Restores the mesh from a JSON object.
         * @param data JSON produced by serialize().
         */
        void deserialize(json data);

        /**
         * @brief Controls the mesh's render visibility.
         * @param newVisible false to skip this mesh during rendering.
         */
        void setVisible(bool newVisible);

        /**
         * @brief Returns whether the mesh is visible.
         * @return true if the mesh will be rendered.
         */
        bool getVisible();

        /**
         * @brief Controls whether the mesh casts shadows.
         * @param newCastShadow true to include in the shadow depth pass.
         */
        void setCastShadow(bool newCastShadow);

        /**
         * @brief Returns whether the mesh casts shadows.
         * @return true if included in the shadow pass.
         */
        bool getCastShadow();

        /**
         * @brief Controls whether the mesh receives shadows.
         * @param newReceiveShadow true to apply shadow on this mesh.
         */
        void setReceiveShadow(bool newReceiveShadow);

        /**
         * @brief Returns whether the mesh receives shadows.
         * @return true if shadow is applied on this mesh.
         */
        bool getReceiveShadow();

        /**
         * @brief Assigns a single bone influence to a vertex (used during loading).
         * @param vertexID Zero-based vertex index.
         * @param boneID   Index into the bone palette.
         * @param weight   Blend weight for this bone.
         */
        void setVertexBoneData(size_t vertexID, int boneID, float weight);

        /**
         * @brief Attaches a skeletal animator to this mesh.
         * @param newAnimator Animator driving the bone transforms.
         */
        void setAnimator(kAnimator *newAnimator);

        /**
         * @brief Returns the attached animator.
         * @return Pointer to the animator, or nullptr if none.
         */
        kAnimator *getAnimator();

        /**
         * @brief Sets whether this mesh is driven by skeletal animation.
         * @param newSkinned true to enable skinning.
         */
        void setSkinned(bool newSkinned);

        /**
         * @brief Returns whether skeletal skinning is enabled.
         * @return true if the mesh is skinned.
         */
        bool getSkinned();

        // --- Morph targets (blend shapes) ------------------------------------

        /**
         * @brief Appends a morph target, if the MAX_MORPH_TARGETS budget allows.
         * @param target Target to add (its deltas must be sized to the vertex count).
         * @return Slot index the target was stored in, or -1 when at capacity.
         */
        int addMorphTarget(const kMorphTarget &target);

        /**
         * @brief Replaces the full morph-target set and resets all weights to 0.
         * @param targets Targets to store (capped at MAX_MORPH_TARGETS).
         */
        void setMorphTargets(const std::vector<kMorphTarget> &targets);

        /** @brief Returns the stored morph targets. */
        const std::vector<kMorphTarget> &getMorphTargets() const { return morphTargets; }

        /** @brief Number of stored morph targets. */
        int getMorphTargetCount() const { return (int)morphTargets.size(); }

        /** @brief True when this mesh carries at least one morph target. */
        bool hasMorphTargets() const { return !morphTargets.empty(); }

        /**
         * @brief Finds a morph target slot by name.
         * @param name Blend-shape name.
         * @return Slot index, or -1 when no target has that name.
         */
        int findMorphTarget(const kString &name) const;

        /**
         * @brief Sets a morph weight by slot.
         * @param slot Target index (out-of-range slots are ignored).
         * @param w    Blend weight (clamped to >= 0).
         */
        void setMorphWeight(int slot, float w);

        /**
         * @brief Sets a morph weight by name (no-op when the name is unknown).
         * @param name Blend-shape name.
         * @param w    Blend weight (clamped to >= 0).
         */
        void setMorphWeight(const kString &name, float w);

        /** @brief Weight of a slot (0 when out of range). */
        float getMorphWeight(int slot) const;

        /** @brief Weight of a named target (0 when unknown). */
        float getMorphWeight(const kString &name) const;

        /**
         * @brief Replaces all weights at once.
         * @param weights Per-target weights; resized to the target count.
         */
        void setMorphWeights(const std::vector<float> &weights);

        /**
         * @brief Returns the per-target weight array uploaded as morphWeights[N].
         */
        const std::vector<float> &getMorphWeights() const { return morphWeights; }

        /** @brief Resets every morph weight to 0 (rest shape). */
        void resetMorphWeights();

    protected:
    private:
        bool loaded = false;

        kString fileName;
        kString refName;
        kString primitiveType;   ///< Empty unless created by kMeshGenerator.
        kString m_serializeType; ///< Override for serialized type (e.g. "terrain").
        int m_terrainGridX = 0;
        int m_terrainGridZ = 0;
        float m_terrainWorldSize = 0.0f;
        int m_terrainHeightRes = 0;
        kString m_terrainHeightFile;
        kString m_terrainSplatFile;

        std::vector<kVec3> vertices;
        std::vector<uint32_t> indices;
        std::vector<kVec2> uvs;
        std::vector<kVec3> vertexColors;
        std::vector<kVec3> normals;
        std::vector<kVec3> tangents;
        std::vector<kVec3> bitangents;
        std::vector<kIvec4> boneIDs;
        std::vector<kVec4> weights;

        std::vector<kMorphTarget> morphTargets; ///< Blend shapes (deltas, object space).
        std::vector<float>         morphWeights; ///< One weight per morph target.

        uint32_t vao = 0;        ///< Vertex Array Object handle.
        uint32_t indicesEbo = 0; ///< Element Buffer Object handle.

        uint32_t vertexBuffer = 0;      ///< Position VBO.
        uint32_t vertexColorBuffer = 0; ///< Vertex-colour VBO.
        uint32_t uvBuffer = 0;          ///< UV coordinate VBO.
        uint32_t normalBuffer = 0;      ///< Normal VBO.
        uint32_t tangentBuffer = 0;     ///< Tangent VBO.
        uint32_t bitangentBuffer = 0;   ///< Bitangent VBO.
        uint32_t boneIDBuffer = 0;      ///< Bone-index VBO.
        uint32_t weightBuffer = 0;      ///< Bone-weight VBO.

        std::vector<uint32_t> morphPositionBuffers; ///< Per-target position-delta VBOs.
        std::vector<uint32_t> morphNormalBuffers;   ///< Per-target normal-delta VBOs (may be empty).

        kMat3 normalMatrix; ///< Inverse-transpose of the model matrix (upper 3x3).

        kAABB localAABB; ///< Bounding box in object (local) space.

        bool isVisible = true;       ///< Render visibility flag.
        bool isCastShadow = true;    ///< Shadow-cast flag.
        bool isReceiveShadow = true; ///< Shadow-receive flag.

        std::map<kString, kBoneInfo> boneInfoMap; ///< Bone name → info lookup.
        int boneCount = 0;                        ///< Total number of bones.

        kAnimator *animator = nullptr; ///< Optional skeletal animator.
        bool isSkinned = false;        ///< Skeletal skinning enabled flag.
    };
}

#endif // KMESH_H
