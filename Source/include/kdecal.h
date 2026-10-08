/**
 * @file kdecal.h
 * @brief Projected scene node (decal) that wraps its material/texture onto
 *        the surfaces it intersects.
 *
 * Unlike the original implementation — a pre-made flat quad floating above a
 * surface — a decal is an *object-shaped projection volume*. The node's
 * transform defines the projection entirely: the pivot is the projector origin,
 * the object's rotation orients the volume, the local -Y axis is the projection
 * direction, the object's world XZ footprint is the cross-section, and the
 * volume extends one object-height (world Y scale) along -Y.
 *
 * During rendering `updateProjectedGeometry()` walks the scene meshes, clips
 * every triangle that intersects the projection box, and rebuilds a small mesh
 * from the clipped polygons.  Texture coordinates are generated automatically
 * from the fragment's position within the box, so whatever material (and its
 * albedo/alpha texture) is assigned to the decal is mapped onto the surface.
 *
 * Because the generated geometry is stored in world space the renderer draws
 * it with an identity model matrix; moving, rotating or scaling the node is the
 * only thing that reshapes the projection.
 *
 * Properties:
 *  - surface offset: pulls the generated fragments back toward the projector so
 *    they do not z-fight with the surface underneath.
 *  - projection layer mask: which object layers the volume projects onto.
 *  - shader type ("flat", "pbr" or "phong"): built-in material applied to a
 *    freshly created decal. A separately assigned .mat asset (material UUID
 *    inherited from kObject) overrides this at render time.
 */
#ifndef KDECAL_H
#define KDECAL_H

#include "kexport.h"
#include "kdriver.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "kobject.h"

namespace kemena
{
    class kScene;

    /**
     * @brief Scene-graph node that projects its material onto the geometry it
     *        covers.
     *
     * The projected polygon mesh is rebuilt lazily (see
     * updateProjectedGeometry()) whenever the projection parameters, the node's
     * world transform, or the scene mesh count changes.  Only decal-specific
     * properties are serialised — the geometry itself is always regenerated.
     */
    class KEMENA3D_API kDecal : public kObject
    {
    public:
        /**
         * @brief Constructs a decal node and optionally attaches it to a parent.
         * @param parentNode Parent scene-graph node, or nullptr for a root node.
         */
        kDecal(kObject *parentNode = nullptr);

        /** @brief Destroys the decal and releases its GPU buffers. */
        ~kDecal();

        /**
         * @brief Rebuilds the projected polygon geometry if needed.
         *
         * For a dynamic (non-static) decal this compares the current world
         * transform and a hash of every scene mesh's transform against the
         * values from the last rebuild, so it also reacts when the surface it
         * projects onto is moved.  For a static decal the geometry is baked
         * once and then frozen until markGeometryDirty() is called explicitly.
         * In both cases an up-to-date decal is a cheap no-op, so this is safe
         * to call every frame from the renderer before draw().
         *
         * @param scene Scene whose meshes are projected against.
         */
        void updateProjectedGeometry(kScene *scene);

        /** @brief Forces the next updateProjectedGeometry() call to rebuild. */
        void markGeometryDirty();

        /** @brief Returns true when no surface was hit (nothing to draw). */
        bool isGeometryEmpty() const;

        /**
         * @brief Appends the 12 wireframe edges of the projection volume to
         *        @p out as line-list vertex pairs (world space).
         *
         * Used by the editor to visualize the projection direction, distance
         * and size. The node's world transform must be up to date.
         * @param out Receives N/N+1 line-segment endpoint pairs (6 floats each).
         */
        void appendProjectionDebugLines(std::vector<kVec3> &out);

        /**
         * @brief Returns the editor billboard icon material, or nullptr.
         *
         * This is a separate slot from the projection material (which carries
         * the decal artwork): the icon material is used purely for the scene
         * gizmo billboard so the decal stays visible/selectable in the editor.
         */
        kMaterial *getIconMaterial() const;

        /**
         * @brief Sets the editor billboard icon material.
         *
         * Not serialised — the editor rebuilds it on load, mirroring how the
         * light/camera/audio gizmo materials are handled.
         */
        void setIconMaterial(kMaterial *material);

        /** @brief Returns the number of triangles in the generated decal mesh. */
        int getTriangleCount() const;

        /**
         * @brief Draws the generated projected mesh.
         *
         * Assumes the caller (kRenderer) has already bound a shader with an
         * identity model matrix and set the material uniforms, mirroring how
         * kMesh nodes are drawn.
         */
        void draw() override;

        /**
         * @brief Returns the decal shader type marker.
         * @return "flat", "pbr" or "phong" (used to rebuild the default material).
         */
        kString getShaderType() const;

        /**
         * @brief Sets the decal shader type marker.
         *
         * This only records the choice that should drive the built-in default
         * material; it does not rebuild the runtime material here (the editor
         * owns material construction).
         * @param type "flat", "pbr" or "phong".
         */
        void setShaderType(const kString &type);

        /**
         * @brief Returns the layer mask this decal projects onto.
         *
         * Only scene meshes whose layer mask intersects this value are
         * considered during projection. The default (all bits set) projects
         * onto every layer.
         */
        uint32_t getProjectionLayerMask() const;

        /**
         * @brief Sets the layer mask this decal projects onto.
         * @param mask Bitmask of layers (bit 0 = "Default").
         */
        void setProjectionLayerMask(uint32_t mask);

        /**
         * @brief Returns how far generated fragments are pulled back toward the
         *        projector to avoid z-fighting.
         * @return Surface offset in world units.
         */
        float getSurfaceOffset() const;

        /**
         * @brief Sets how far generated fragments are pulled back.
         * @param offset Surface offset in world units (0 = lie exactly on the surface).
         */
        void setSurfaceOffset(float offset);

        /**
         * @brief Serialises this decal node to JSON.
         *
         * Delegates to kObject::serialize() (transform, children, material UUID,
         * components), stamps the node type as "decal", and stores the shader
         * type and layer mask. The projection volume is derived from the node's
         * transform, so it needs no parameters of its own.
         * @return JSON object describing the decal.
         */
        json serialize() override;

    private:
        /**
         * @brief Computes the world-space projection frame from the node's
         *        rotation and scale (the projection volume is the object box).
         * @return false if the transform is degenerate (zero scale).
         */
        bool computeProjectionFrame(kVec3 &origin, kVec3 &axisXW, kVec3 &axisYW,
                                    kVec3 &dirWorld, float &halfW, float &halfH,
                                    float &worldWidth, float &worldHeight,
                                    float &worldDistance);

        /// Rebuilds the CPU geometry buffers by clipping scene triangles.
        void rebuildGeometry(kScene *scene);
        /// (Re)uploads the CPU geometry buffers to the GPU (idempotent).
        void uploadGeometry();
        /// Releases the GPU geometry buffers.
        void releaseGeometry();

        // --- GPU geometry -----------------------------------------------------
        uint32_t vao = 0; ///< Vertex array for the generated decal mesh.
        uint32_t vbo = 0; ///< Vertex position buffer (world space).
        uint32_t uvbo = 0; ///< Vertex UV buffer.
        uint32_t nbo = 0;  ///< Vertex normal buffer (world space).
        uint32_t ebo = 0;  ///< Index buffer.
        size_t indexCount = 0; ///< Number of indices in the generated mesh.

        // --- CPU geometry (world space) --------------------------------------
        std::vector<kVec3> positions;
        std::vector<kVec3> normals;
        std::vector<kVec2> uvs;
        std::vector<uint32_t> indices;

        // --- Projection parameters -------------------------------------------
        // The projection volume is derived entirely from this node's transform
        // (see computeProjectionFrame), so only the layer mask and the
        // surface-offset nudge remain as explicit settings.
        uint32_t projLayerMask = 0xFFFFFFFFu;          ///< Layers this decal projects onto (all by default).
        float surfaceOffset = 0.01f;                   ///< Pull-back toward the projector.
        kString decalShaderType = "flat";              ///< Built-in shader choice.
        kMaterial *iconMaterial = nullptr;             ///< Editor gizmo billboard material (not owned/serialised).

        // --- Rebuild tracking -------------------------------------------------
        bool     geometryDirty      = true; ///< Force a rebuild on next update.
        kMat4    lastWorldMatrix    = kMat4(1.0f); ///< World transform at last rebuild.
        uint64_t lastSceneSignature = 0;    ///< Hash of scene mesh transforms at last rebuild.
        bool     hasLastSignature   = false; ///< False until the first rebuild completed.
    };
}

#endif // KDECAL_H
