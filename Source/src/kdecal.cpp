#include "kdecal.h"
#include "kdatatype.h"
#include "kscene.h"
#include "kmesh.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace kemena
{
    namespace
    {
        /// A vertex used while clipping a scene triangle against the projection box.
        struct ClipVertex
        {
            kVec3 pos; ///< World-space position.
            kVec3 nrm; ///< World-space normal.
        };

        /// Builds an orthonormal lateral basis (axisX, axisY) perpendicular to
        /// the (normalised) projection axis @p dir, biased toward the object's
        /// local +X so rotating the decal about the projection axis rolls the
        /// texture.
        void buildProjectionBasis(const kVec3 &dir, kVec3 &axisX, kVec3 &axisY)
        {
            kVec3 d = dir;
            float len = glm::length(d);
            if (len < 1e-6f)
                d = kVec3(0.0f, -1.0f, 0.0f);
            else
                d /= len;

            // Project the object's local +X onto the plane perpendicular to d.
            const kVec3 localX(1.0f, 0.0f, 0.0f);
            kVec3 rx = localX - d * glm::dot(localX, d);
            if (glm::length(rx) < 1e-5f)
            {
                // d is (anti-)parallel to local +X: fall back to local +Z.
                const kVec3 localZ(0.0f, 0.0f, 1.0f);
                rx = localZ - d * glm::dot(localZ, d);
            }
            if (glm::length(rx) < 1e-5f)
                rx = kVec3(1.0f, 0.0f, 0.0f);

            axisX = glm::normalize(rx);
            axisY = glm::normalize(glm::cross(d, axisX));
            // Re-orthogonalise so the frame is exactly orthonormal.
            axisX = glm::normalize(glm::cross(axisY, d));
        }

        /// Sutherland–Hodgman clip of a convex polygon against the half-space
        /// dot(n, p) <= d.  Positions and normals are linearly interpolated at
        /// the crossings.
        void clipPolygon(const std::vector<ClipVertex> &in,
                         const kVec3 &n, float d,
                         std::vector<ClipVertex> &out)
        {
            out.clear();
            const size_t count = in.size();
            if (count < 3)
                return;

            for (size_t i = 0; i < count; ++i)
            {
                const ClipVertex &cur = in[i];
                const ClipVertex &nxt = in[(i + 1) % count];

                const float dc = d - glm::dot(n, cur.pos);
                const float dn = d - glm::dot(n, nxt.pos);
                const bool curIn = dc >= 0.0f;
                const bool nxtIn = dn >= 0.0f;

                if (curIn)
                    out.push_back(cur);

                if (curIn != nxtIn)
                {
                    const float denom = dc - dn;
                    const float t = (std::abs(denom) > 1e-9f) ? (dc / denom) : 0.0f;
                    ClipVertex v;
                    v.pos = cur.pos + (nxt.pos - cur.pos) * t;
                    v.nrm = cur.nrm + (nxt.nrm - cur.nrm) * t;
                    out.push_back(v);
                }
            }
        }

        /// Recursively gathers loaded mesh nodes from the scene graph.
        ///
        /// kScene::getMeshes() is not populated by the editor/runtime — meshes
        /// live in the scene-graph hierarchy — so the projection walks the graph
        /// exactly like kOctree::collectMeshes does.
        void collectSceneMeshes(kObject *node, std::vector<kMesh *> &out)
        {
            if (node == nullptr || !node->getActive())
                return;

            if (node->getType() == kNodeType::NODE_TYPE_MESH)
            {
                kMesh *mesh = static_cast<kMesh *>(node);
                if (mesh->getLoaded())
                    out.push_back(mesh);
            }

            for (kObject *child : node->getChildren())
                collectSceneMeshes(child, out);
        }

        /// Mixes one float into a running FNV-style hash.
        void hashFloat(uint64_t &h, float v)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &v, sizeof(float));
            h ^= (uint64_t)bits + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        }

        /// Hashes every mesh's world transform so a dynamic decal follows the
        /// surfaces it projects onto.
        void hashSceneGeometry(kObject *node, uint64_t &h)
        {
            if (node == nullptr)
                return;

            if (node->getType() == kNodeType::NODE_TYPE_MESH)
            {
                kMesh *mesh = static_cast<kMesh *>(node);
                mesh->calculateModelMatrix();
                const kMat4 w = mesh->getModelMatrixWorld();
                const float *f = glm::value_ptr(w);
                for (int i = 0; i < 16; ++i)
                    hashFloat(h, f[i]);
            }

            for (kObject *child : node->getChildren())
                hashSceneGeometry(child, h);
        }
    } // namespace

    kDecal::kDecal(kObject *parentNode)
    {
        if (parentNode != nullptr)
            setParent(parentNode);
        setType(kNodeType::NODE_TYPE_DECAL);
    }

    kDecal::~kDecal()
    {
        releaseGeometry();
    }

    void kDecal::releaseGeometry()
    {
        if (kDriver *driver = kDriver::getCurrent())
        {
            if (ebo)  driver->deleteBuffer(ebo);
            if (nbo)  driver->deleteBuffer(nbo);
            if (uvbo) driver->deleteBuffer(uvbo);
            if (vbo)  driver->deleteBuffer(vbo);
            if (vao)  driver->deleteVertexArray(vao);
        }
        vao = vbo = uvbo = nbo = ebo = 0;
        indexCount = 0;
    }

    void kDecal::uploadGeometry()
    {
        releaseGeometry();

        if (positions.empty() || indices.empty())
            return;

        kDriver *driver = kDriver::getCurrent();
        if (driver == nullptr)
            return;

        // Vertex attribute layout mirrors kMesh::generateVbo so the material
        // shaders (and the picking shader) can render the generated mesh:
        //   loc0 = position, loc2 = uv, loc3 = normal.
        vao = driver->createVertexArray();
        driver->bindVertexArray(vao);

        vbo = driver->createBuffer();
        driver->uploadVertexBuffer(vbo, positions.data(), positions.size() * sizeof(kVec3));
        driver->setVertexAttribFloat(0, 3, sizeof(kVec3), 0);

        uvbo = driver->createBuffer();
        driver->uploadVertexBuffer(uvbo, uvs.data(), uvs.size() * sizeof(kVec2));
        driver->setVertexAttribFloat(2, 2, sizeof(kVec2), 0);

        nbo = driver->createBuffer();
        driver->uploadVertexBuffer(nbo, normals.data(), normals.size() * sizeof(kVec3));
        driver->setVertexAttribFloat(3, 3, sizeof(kVec3), 0);

        ebo = driver->createBuffer();
        driver->uploadIndexBuffer(ebo, indices.data(), indices.size() * sizeof(uint32_t));

        driver->unbindVertexArray();

        indexCount = indices.size();
    }

    bool kDecal::computeProjectionFrame(kVec3 &origin, kVec3 &axisXW, kVec3 &axisYW,
                                        kVec3 &dirWorld, float &halfW, float &halfH,
                                        float &worldWidth, float &worldHeight,
                                        float &worldDistance)
    {
        const kMat4 W = getModelMatrixWorld();
        origin = kVec3(W[3]);

        const float sizeX = (projSize.x > 1e-5f) ? projSize.x : 1.0f;
        const float sizeY = (projSize.y > 1e-5f) ? projSize.y : 1.0f;

        // Local-space basis; then transform it into world space.
        kVec3 axisXLocal, axisYLocal;
        buildProjectionBasis(projDirection, axisXLocal, axisYLocal);

        kVec3 dirLocal = projDirection;
        if (glm::length(dirLocal) < 1e-6f)
            dirLocal = kVec3(0.0f, -1.0f, 0.0f);
        else
            dirLocal = glm::normalize(dirLocal);

        // The object's linear part applies rotation + scale; normalising the
        // transformed axes gives unit directions while their lengths recover the
        // object's world scale along each axis.
        const kVec3 axW = kVec3(W * kVec4(axisXLocal, 0.0f));
        const kVec3 ayW = kVec3(W * kVec4(axisYLocal, 0.0f));
        const kVec3 dirWraw = kVec3(W * kVec4(dirLocal, 0.0f));

        const float scaleX = glm::length(axW);
        const float scaleY = glm::length(ayW);
        const float scaleD = glm::length(dirWraw);
        if (scaleX < 1e-6f || scaleY < 1e-6f || scaleD < 1e-6f)
            return false; // degenerate (zero) scale — nothing to project

        axisXW = axW / scaleX;
        axisYW = ayW / scaleY;
        dirWorld = dirWraw / scaleD;

        halfW = sizeX * 0.5f * scaleX;
        halfH = sizeY * 0.5f * scaleY;
        worldWidth = halfW * 2.0f;
        worldHeight = halfH * 2.0f;
        // Avoid std::max here: on Windows a bare `max` macro (when NOMINMAX is
        // not in effect for a translation unit) breaks the `std::max` token.
        worldDistance = ((projDistance > 0.0f) ? projDistance : 0.0f) * scaleD;
        return true;
    }

    void kDecal::rebuildGeometry(kScene *scene)
    {
        positions.clear();
        normals.clear();
        uvs.clear();
        indices.clear();

        if (scene == nullptr)
        {
            uploadGeometry();
            return;
        }

        // --- Projection frame in world space ---------------------------------
        kVec3 origin, axisXW, axisYW, dirWorld;
        float halfW = 0.0f, halfH = 0.0f, worldWidth = 0.0f, worldHeight = 0.0f, worldDistance = 0.0f;
        if (!computeProjectionFrame(origin, axisXW, axisYW, dirWorld,
                                    halfW, halfH, worldWidth, worldHeight, worldDistance))
        {
            uploadGeometry();
            return;
        }

        // Plane constants are measured against the world origin, so cache the
        // projector origin's projection onto each box axis.
        const float oAxisX = glm::dot(axisXW, origin);
        const float oAxisY = glm::dot(axisYW, origin);
        const float oDir   = glm::dot(dirWorld, origin);

        // Enclosing world AABB of the projection box, used to reject meshes.
        const kVec3 boxCenter = origin + dirWorld * (worldDistance * 0.5f);
        const kVec3 boxExtent = axisXW * halfW + axisYW * halfH + dirWorld * (worldDistance * 0.5f);
        kAABB decalBox;
        decalBox.expandBy(boxCenter - boxExtent);
        decalBox.expandBy(boxCenter + boxExtent);

        // --- Project every intersecting scene triangle -----------------------
        std::vector<kMesh *> meshes;
        collectSceneMeshes(scene->getRootNode(), meshes);

        std::vector<ClipVertex> polyIn, polyOut;
        for (kMesh *mesh : meshes)
        {
            if (mesh == nullptr)
                continue;

            // Layer filter: skip meshes that share no layer with this decal.
            if ((mesh->getLayerMask() & projLayerMask) == 0u)
                continue;

            mesh->calculateModelMatrix();
            const kAABB meshBox = mesh->getWorldAABB();
            if (meshBox.isValid() && !meshBox.overlaps(decalBox))
                continue;

            const std::vector<kVec3> verts = mesh->getVertices();
            const std::vector<uint32_t> idx = mesh->getIndices();
            if (verts.size() < 3 || idx.size() < 3)
                continue;

            const std::vector<kVec3> nrmSource = mesh->getNormals();
            const bool hasNormals = (nrmSource.size() == verts.size());

            const kMat4 meshW = mesh->getModelMatrixWorld();
            const kMat3 normalMat = glm::transpose(glm::inverse(kMat3(meshW)));

            for (size_t t = 0; t + 2 < idx.size(); t += 3)
            {
                const uint32_t i0 = idx[t];
                const uint32_t i1 = idx[t + 1];
                const uint32_t i2 = idx[t + 2];
                if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
                    continue;

                const kVec3 wp0 = kVec3(meshW * kVec4(verts[i0], 1.0f));
                const kVec3 wp1 = kVec3(meshW * kVec4(verts[i1], 1.0f));
                const kVec3 wp2 = kVec3(meshW * kVec4(verts[i2], 1.0f));

                // Face normal (fallback for meshes without per-vertex normals).
                kVec3 faceNormal = glm::cross(wp1 - wp0, wp2 - wp0);
                const float faceLen2 = glm::dot(faceNormal, faceNormal);
                if (faceLen2 < 1e-12f)
                    continue; // degenerate triangle
                faceNormal /= std::sqrt(faceLen2);

                auto vertexNormal = [&](uint32_t vi) -> kVec3
                {
                    if (hasNormals)
                    {
                        kVec3 n = glm::normalize(normalMat * nrmSource[vi]);
                        if (glm::length(n) > 1e-5f)
                            return n;
                    }
                    return faceNormal;
                };

                // Start with the full triangle, clipped against the 6 box
                // planes. The box is anchored at the projector origin, so each
                // plane constant is offset by dot(planeNormal, origin).
                polyIn.clear();
                polyIn.push_back({wp0, vertexNormal(i0)});
                polyIn.push_back({wp1, vertexNormal(i1)});
                polyIn.push_back({wp2, vertexNormal(i2)});

                clipPolygon(polyIn,  axisXW, halfW + oAxisX, polyOut); polyIn.swap(polyOut); if (polyIn.size() < 3) continue;
                clipPolygon(polyIn, -axisXW, halfW - oAxisX, polyOut); polyIn.swap(polyOut); if (polyIn.size() < 3) continue;
                clipPolygon(polyIn,  axisYW, halfH + oAxisY, polyOut); polyIn.swap(polyOut); if (polyIn.size() < 3) continue;
                clipPolygon(polyIn, -axisYW, halfH - oAxisY, polyOut); polyIn.swap(polyOut); if (polyIn.size() < 3) continue;
                clipPolygon(polyIn,  dirWorld, worldDistance + oDir, polyOut); polyIn.swap(polyOut); if (polyIn.size() < 3) continue;
                clipPolygon(polyIn, -dirWorld, -oDir, polyOut); polyIn.swap(polyOut); if (polyIn.size() < 3) continue;

                // Emit the clipped polygon as a triangle fan.
                const uint32_t base = (uint32_t)positions.size();
                for (const ClipVertex &cv : polyIn)
                {
                    // Automatic texture mapping from the fragment's lateral
                    // offset within the projection box.
                    const kVec3 rel = cv.pos - origin;
                    const float u = glm::dot(rel, axisXW) / worldWidth + 0.5f;
                    const float v = glm::dot(rel, axisYW) / worldHeight + 0.5f;

                    // Pull the fragment back toward the projector to avoid
                    // z-fighting with the surface underneath.
                    const kVec3 p = cv.pos - dirWorld * surfaceOffset;

                    kVec3 n = cv.nrm;
                    const float nlen = glm::length(n);
                    n = (nlen > 1e-5f) ? (n / nlen) : faceNormal;

                    positions.push_back(p);
                    normals.push_back(n);
                    uvs.push_back(kVec2(u, v));
                }

                for (size_t k = 1; k + 1 < polyIn.size(); ++k)
                {
                    indices.push_back(base);
                    indices.push_back(base + (uint32_t)k);
                    indices.push_back(base + (uint32_t)(k + 1));
                }
            }
        }

        uploadGeometry();
    }

    void kDecal::updateProjectedGeometry(kScene *scene)
    {
        if (scene == nullptr)
            return;

        const kMat4 W = getModelMatrixWorld();
        const bool worldChanged = (W != lastWorldMatrix);

        // A static decal is baked once and then frozen: it stops following the
        // scene so its projected geometry stays stable and costs nothing per
        // frame. Only an explicit markGeometryDirty() — the inspector's Rebuild
        // action, or toggling the object back to non-static — recomputes it.
        if (getStatic() && hasLastSignature && !geometryDirty)
            return;

        uint64_t signature = lastSceneSignature;
        if (!getStatic())
        {
            // Hash every mesh's world transform so a dynamic decal follows a
            // surface that is moved/rotated/scaled — not only its own transform
            // or the number of meshes in the scene.
            signature = 1469598103934665603ull;
            hashSceneGeometry(scene->getRootNode(), signature);

            const bool sceneChanged = (!hasLastSignature || signature != lastSceneSignature);
            if (!geometryDirty && !worldChanged && !sceneChanged)
                return;
        }

        lastWorldMatrix = W;
        lastSceneSignature = signature;
        hasLastSignature = true;
        geometryDirty = false;

        rebuildGeometry(scene);
    }

    void kDecal::appendProjectionDebugLines(std::vector<kVec3> &out)
    {
        kVec3 origin, axisXW, axisYW, dirWorld;
        float halfW = 0.0f, halfH = 0.0f, worldWidth = 0.0f, worldHeight = 0.0f, worldDistance = 0.0f;
        if (!computeProjectionFrame(origin, axisXW, axisYW, dirWorld,
                                    halfW, halfH, worldWidth, worldHeight, worldDistance))
            return;

        // 8 corners: near face at the pivot (d = 0) and far face (d = distance).
        const kVec3 p0 = origin - axisXW * halfW - axisYW * halfH;
        const kVec3 p1 = origin + axisXW * halfW - axisYW * halfH;
        const kVec3 p2 = origin + axisXW * halfW + axisYW * halfH;
        const kVec3 p3 = origin - axisXW * halfW + axisYW * halfH;
        const kVec3 q0 = p0 + dirWorld * worldDistance;
        const kVec3 q1 = p1 + dirWorld * worldDistance;
        const kVec3 q2 = p2 + dirWorld * worldDistance;
        const kVec3 q3 = p3 + dirWorld * worldDistance;

        auto edge = [&out](const kVec3 &a, const kVec3 &b)
        {
            out.push_back(a);
            out.push_back(b);
        };

        // Near face.
        edge(p0, p1); edge(p1, p2); edge(p2, p3); edge(p3, p0);
        // Far face.
        edge(q0, q1); edge(q1, q2); edge(q2, q3); edge(q3, q0);
        // Connecting edges.
        edge(p0, q0); edge(p1, q1); edge(p2, q2); edge(p3, q3);
    }

    void kDecal::markGeometryDirty()
    {
        geometryDirty = true;
    }

    kMaterial *kDecal::getIconMaterial() const
    {
        return iconMaterial;
    }

    void kDecal::setIconMaterial(kMaterial *material)
    {
        iconMaterial = material;
    }

    bool kDecal::isGeometryEmpty() const
    {
        return indices.empty();
    }

    int kDecal::getTriangleCount() const
    {
        return (int)(indices.size() / 3);
    }

    void kDecal::draw()
    {
        if (vao == 0 || indexCount == 0)
            return;

        kDriver *driver = kDriver::getCurrent();
        if (driver == nullptr)
            return;

        driver->drawIndexed(vao, (int)indexCount);
    }

    kString kDecal::getShaderType() const
    {
        return decalShaderType;
    }

    void kDecal::setShaderType(const kString &type)
    {
        decalShaderType = type;
    }

    kVec3 kDecal::getProjectionDirection() const
    {
        return projDirection;
    }

    void kDecal::setProjectionDirection(const kVec3 &direction)
    {
        projDirection = direction;
        markGeometryDirty();
    }

    float kDecal::getProjectionDistance() const
    {
        return projDistance;
    }

    void kDecal::setProjectionDistance(float distance)
    {
        projDistance = (distance < 0.0f) ? 0.0f : distance;
        markGeometryDirty();
    }

    kVec2 kDecal::getProjectionSize() const
    {
        return projSize;
    }

    void kDecal::setProjectionSize(const kVec2 &size)
    {
        projSize = size;
        markGeometryDirty();
    }

    uint32_t kDecal::getProjectionLayerMask() const
    {
        return projLayerMask;
    }

    void kDecal::setProjectionLayerMask(uint32_t mask)
    {
        projLayerMask = mask;
        markGeometryDirty();
    }

    float kDecal::getSurfaceOffset() const
    {
        return surfaceOffset;
    }

    void kDecal::setSurfaceOffset(float offset)
    {
        surfaceOffset = offset;
        markGeometryDirty();
    }

    json kDecal::serialize()
    {
        // Delegate to the base so transform, children, scripts (full format),
        // material UUID, physics, character controller and navigation
        // components are all emitted consistently, then stamp the node type and
        // the decal-specific fields. The projected geometry is always rebuilt,
        // so it is not serialised.
        json data = kObject::serialize();
        data["type"] = "decal";
        data["decal_offset"] = surfaceOffset;
        data["decal_shader"] = decalShaderType;
        data["decal_dir"] = { projDirection.x, projDirection.y, projDirection.z };
        data["decal_distance"] = projDistance;
        data["decal_size"] = { projSize.x, projSize.y };
        data["decal_layers"] = projLayerMask;
        return data;
    }
}
