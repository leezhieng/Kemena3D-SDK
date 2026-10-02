/**
 * @file kanimator_blend.cpp
 * @brief Implementation of kAnimator::calculateBlendedBoneTransform().
 *
 * The animator-controller blend tree needs to interpolate an arbitrary number
 * of skeletal clips at once. kAnimator only supported a two-clip cross-fade
 * (beginBlend), so this weighted multi-clip pose pass lives alongside the rest
 * of the animator implementation in the SDK. It was previously compiled in the
 * editor project (as an out-of-line member definition) to avoid rebuilding the
 * SDK; it is now a normal part of the Kemena3DSDK library so the animator's
 * blend path ships with the engine.
 *
 * The pass evaluates each node in two layers:
 *
 *   1. **Base layer** — every non-additive sample whose mask covers the bone is
 *      blended by weight (partial animation). A bone no base sample covers keeps
 *      the skeleton's rest pose.
 *   2. **Additive layer** — every additive sample layers (pose - reference)
 *      * weight on top of the base result.
 */

#include "kemena.h"

#include "kanimationmask.h"
#include "kadditiveanimation.h"

#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace kemena
{
    namespace
    {
        /** @brief Decomposes a 4x4 transform into translation / rotation / scale. */
        void blendDecomposeTRS(const kMat4 &m, kVec3 &translation, kQuat &rotation, kVec3 &scale)
        {
            translation = kVec3(m[3][0], m[3][1], m[3][2]);
            scale = kVec3(glm::length(kVec3(m[0])),
                          glm::length(kVec3(m[1])),
                          glm::length(kVec3(m[2])));

            // Rebuild the pure rotation columns (divide the scale out) so
            // non-unit scale does not leak into the quaternion extraction.
            kMat3 rot(1.0f);
            rot[0] = (scale.x > 1e-6f) ? kVec3(m[0]) / scale.x : kVec3(m[0]);
            rot[1] = (scale.y > 1e-6f) ? kVec3(m[1]) / scale.y : kVec3(m[1]);
            rot[2] = (scale.z > 1e-6f) ? kVec3(m[2]) / scale.z : kVec3(m[2]);
            rotation = kQuat(rot);
        }

        /**
         * @brief Strips Assimp's FBX pivot-wrapper suffix from a node name.
         *
         * Assimp's FBX importer inserts helper nodes around every bone
         * ("<bone>_$AssimpFbx$_Translation" / "_PreRotation" / "_Rotation" /
         * "_PostRotation" / "_Scaling") and the animation channels target those
         * wrappers, while bone masks and the mesh's bone palette use the clean
         * bone name ("mixamorig:Spine"). Matching the raw node name against a
         * mask would never hit "Spine_$AssimpFbx$_Rotation", so its bones would
         * fall back to rest even though a mask lists them.
         */
        kString baseBoneName(const kString &name)
        {
            static const kString marker = "_$AssimpFbx$_";
            const size_t pos = name.find(marker);
            return (pos == kString::npos) ? name : name.substr(0, pos);
        }

        /**
         * @brief Whether a sample is allowed to drive the bone behind @p nodeName.
         *
         * A null mask (or an empty/identity mask) covers the whole skeleton, so
         * callers that don't use partial animation see no behaviour change. The
         * node name is reduced to its base bone name first so masks (authored
         * with clean joint names) match the Assimp pivot wrappers that actually
         * carry the animation.
         */
        bool maskAllowsBone(const kAnimationMask *mask, const kString &nodeName)
        {
            return mask == nullptr || mask->isBoneActive(baseBoneName(nodeName));
        }

        // Per-(animator, clip) root-motion tracking used by the blend-tree pose
        // pass below. A blend tree mixes several motions that each carry their
        // own root displacement, so every clip's frame-to-frame root delta is
        // tracked separately and then combined by blend weight.
        struct RootTrackKey
        {
            const void *animator = nullptr;
            const void *clip     = nullptr;
            bool operator==(const RootTrackKey &o) const
            {
                return animator == o.animator && clip == o.clip;
            }
        };
        struct RootTrackKeyHash
        {
            size_t operator()(const RootTrackKey &k) const
            {
                return std::hash<const void *>()(k.animator) ^
                       (std::hash<const void *>()(k.clip) << 1);
            }
        };
        struct RootTrackData
        {
            std::string boneName;                        ///< This clip's own root-motion bone.
            kVec3 pos   = kVec3(0.0f);                   ///< Root bone local position last sampled.
            kQuat rot   = kQuat(1.0f, 0.0f, 0.0f, 0.0f); ///< Root bone local rotation last sampled.
            float time  = 0.0f;                          ///< Clip time of the last sample.
            unsigned long long lastTick = 0;             ///< Root-node frame tick when last sampled.
            bool  valid = false;                         ///< False until the first sample.
        };
        /** @brief Per-animator blend-tree root state (frame counter + session flag). */
        struct RootSessionState
        {
            unsigned long long tick = 0;   ///< Incremented once per root-node frame.
            bool rootActive = false;       ///< True once the weighted-root path has been taken.
        };
        std::unordered_map<RootTrackKey, RootTrackData, RootTrackKeyHash> g_rootTracks;
        std::unordered_map<const void *, RootSessionState> g_rootSessions;

        /// Resolves the bone that carries a clip's root translation: the topmost
        /// node whose animation is not constant. Mirrors
        /// kAnimator::findRootBoneRecursive so the blend path and the single-clip
        /// path agree on which bone is "the root" — but resolved PER CLIP, since
        /// every motion is imported as its own skeleton and the root-bearing node
        /// can differ between them.
        std::string resolveClipRootBone(kSkeletalAnimation *anim)
        {
            if (anim == nullptr)
                return std::string();
            std::string found;
            std::function<void(const kNodeData &)> walk = [&](const kNodeData &n)
            {
                if (!found.empty())
                    return;
                kBone *bone = anim->findBone(n.name);
                if (bone != nullptr && !bone->isStatic())
                {
                    found = n.name;
                    return;
                }
                for (int i = 0; i < n.childrenCount; ++i)
                    walk(n.children[i]);
            };
            walk(anim->getRootNode());
            return found;
        }
    }

    void kAnimator::calculateBlendedBoneTransform(const std::vector<kPoseSample> &samples,
                                                  const kNodeData *node, kMat4 parentTransform)
    {
        if (node == nullptr || samples.empty())
            return;

        kMat4 nodeTransform = node->transformation;

        // -----------------------------------------------------------------
        // Root motion across the blended motions.
        //
        // Single-clip playback extracts root motion in handleRootMotion() from
        // one clip. A blend tree mixes several motions that may each carry their
        // own root displacement (e.g. run-forward + run-right), so the delta is
        // built here by tracking every contributing clip's frame-to-frame root
        // displacement and combining them by blend weight. This makes both axes
        // drive the character (diagonal movement) and makes the displacement
        // fall to zero as a moving motion is blended out toward a non-moving
        // one (idle) — the character then stops together with the animation
        // instead of sliding until the last root-bearing clip's weight hits 0.
        //
        // Only the *base* (non-additive) samples define the pinned root pose;
        // additive samples still contribute their own root displacement to the
        // accumulated delta. Samples whose mask excludes the root bone are
        // skipped entirely.
        // -----------------------------------------------------------------
        bool rootMotionApplied = false;
        if (!rootBoneName.empty() && node->name == rootBoneName)
        {
            RootSessionState &session = g_rootSessions[this];
            const unsigned long long frameTick = ++session.tick;

            kVec3  accumDeltaPos(0.0f);
            kVec3  accumDeltaRot(0.0f);
            float  posSpeedSum = 0.0f; ///< Sum weight*|clip delta| over the enabled position channels.
            kVec3  posAccum(0.0f);
            kVec3  scaleAccum(0.0f);
            kQuat  rotAccum(0.0f, 0.0f, 0.0f, 0.0f);
            kQuat  rotRef(1.0f, 0.0f, 0.0f, 0.0f);
            bool   haveRef   = false;
            float  weightSum = 0.0f;
            bool   chXZ = false, chY = false, chRot = false;

            for (const kPoseSample &s : samples)
            {
                if (s.animation == nullptr || s.weight <= 0.0f)
                    continue;
                // Partial animation: a sample whose mask excludes the root bone
                // must not contribute to the root pose or its displacement.
                if (!maskAllowsBone(s.mask, node->name))
                    continue;

                kSkeletalAnimation *anim = s.animation;
                const bool additiveSample = (s.additive != nullptr);
                const bool aXZ = anim->getRootMotionPositionXZ();
                const bool aY  = anim->getRootMotionPositionY();
                const bool aR  = anim->getRootMotionRotation();
                if (aXZ) chXZ = true;
                if (aY)  chY  = true;
                if (aR)  chRot = true;

                // ---- Weighted absolute root pose (drives the pinned pose) ------
                // Uses the node the animator resolved as "the root" so the baked
                // channel matches what setBlendRootSource()/handleRootMotion()
                // pinhole. Kept separate from the motion delta below, because the
                // pin bone and the per-clip translation bone are not always the
                // same node. Additive samples are deltas, not base poses, so they
                // are excluded here.
                if (!additiveSample)
                {
                    if (kBone *poseBone = anim->findBone(node->name))
                    {
                        poseBone->update(s.time);
                        kVec3 pt, psc;
                        kQuat pr;
                        blendDecomposeTRS(poseBone->getLocalTransform(), pt, pr, psc);
                        if (!haveRef) { rotRef = pr; haveRef = true; }
                        else if (glm::dot(pr, rotRef) < 0.0f) pr = -pr;
                        posAccum   += pt  * s.weight;
                        scaleAccum += psc * s.weight;
                        rotAccum   += pr  * s.weight;
                        weightSum  += s.weight;
                    }
                }

                // ---- Per-clip root-motion delta -------------------------------
                // Every motion contributes ITS OWN authored displacement, read
                // from that clip's own root bone. Looking the same shared bone
                // name up in every clip silently drops motions whose root lives
                // on a different node (e.g. an idle clip whose root resolves to a
                // rotation-only wrapper with zero translation) and collapses the
                // blend to a single direction.
                RootTrackKey   key{ this, anim };
                RootTrackData &tr = g_rootTracks[key];
                if (tr.boneName.empty())
                    tr.boneName = resolveClipRootBone(anim);

                kBone *bone = tr.boneName.empty() ? nullptr : anim->findBone(tr.boneName);
                if (bone == nullptr)
                    continue;
                bone->update(s.time);

                kVec3 t, sc;
                kQuat r;
                blendDecomposeTRS(bone->getLocalTransform(), t, r, sc);

                // Per-clip frame-to-frame delta. A clip that was not sampled on
                // the previous frame (weight 0, e.g. while an opposite motion
                // plays) is treated as freshly started, and a backwards clip
                // time means the clip looped — both re-seed rather than emitting
                // a bogus multi-frame delta that would pop the character.
                kVec3 dPos(0.0f);
                kQuat dRotQ(1.0f, 0.0f, 0.0f, 0.0f);
                if (tr.valid && tr.lastTick == frameTick - 1 && s.time >= tr.time)
                {
                    dPos  = t - tr.pos;
                    dRotQ = r * glm::conjugate(tr.rot);
                }
                tr.pos = t; tr.rot = r; tr.time = s.time;
                tr.lastTick = frameTick; tr.valid = true;

                if ((aXZ || aY) && s.weight > 0.0f)
                {
                    // Mask to the channels this clip extracts so its magnitude
                    // reflects exactly what it contributes to the blend.
                    const kVec3 dEnabled(aXZ ? dPos.x : 0.0f,
                                         aY  ? dPos.y : 0.0f,
                                         aXZ ? dPos.z : 0.0f);
                    accumDeltaPos += dEnabled * s.weight;
                    posSpeedSum   += glm::length(dEnabled) * s.weight;
                }
                if (aR)
                    accumDeltaRot += glm::degrees(glm::eulerAngles(dRotQ)) * s.weight;
            }

            // Once this animator has extracted root motion in a blend tree, keep
            // taking the weighted-root path even on frames whose motions carry no
            // root channel (e.g. idle). Otherwise the baked root would toggle
            // between pinned and un-pinned and pop the mesh.
            if (haveRef && weightSum > 1e-6f && (chXZ || chY || chRot))
                session.rootActive = true;

            if (haveRef && weightSum > 1e-6f && session.rootActive)
            {
                const kVec3 weightedPos   = posAccum / weightSum;
                const kVec3 weightedScale = scaleAccum / weightSum;
                const float rotLen        = glm::length(rotAccum);
                const kQuat weightedRot   = (rotLen > 1e-6f) ? (rotAccum / rotLen) : rotRef;

                // Bake reference = the root bone's rest (bind) local pose. It is
                // identical every time, so re-seeding after a root-source flip
                // can never shift the pin and pop the mesh — seeding from the
                // *current* pose did exactly that.
                if (!rootMotionInitialized)
                {
                    kVec3 restScale;
                    blendDecomposeTRS(node->transformation, bakeRootPos, bakeRootRot, restScale);
                    rootMotionInitialized = true;
                }

                // Keep the travelled speed constant no matter how many motions
                // are mixed. Blending two orthogonal runs 50/50 vector-averages
                // down to ~0.707 of a single run, so diagonal movement felt
                // slower. Rescaling the delta back to the weighted-average
                // motion speed fixes that; same-direction blends already match
                // (posSpeedSum == |delta|) and are left untouched.
                const float deltaLen = glm::length(accumDeltaPos);
                if (deltaLen > 1e-6f && posSpeedSum > deltaLen)
                {
                    float scale = posSpeedSum / deltaLen;
                    if (scale > 2.0f) scale = 2.0f; // guard near-cancelling inputs
                    accumDeltaPos *= scale;
                }

                // Report the weighted per-frame delta to gameplay (consumed by
                // getRootMotionDeltaPosition() / getRootMotionDeltaRotation()).
                rootMotionAccumPos      += accumDeltaPos;
                rootMotionAccumRotEuler += accumDeltaRot;

                // Compose T * R * S from the weighted root pose, pinning the
                // enabled channels to the start-of-play reference so the
                // character does not slide inside its own pose.
                kVec3 pinnedPos = weightedPos;
                if (chXZ) { pinnedPos.x = bakeRootPos.x; pinnedPos.z = bakeRootPos.z; }
                if (chY)  { pinnedPos.y = bakeRootPos.y; }
                const kQuat pinnedRot = chRot ? bakeRootRot : weightedRot;

                kMat4 rootPose(1.0f);
                rootPose[0] = kVec4(pinnedRot * kVec3(weightedScale.x, 0.0f, 0.0f), 0.0f);
                rootPose[1] = kVec4(pinnedRot * kVec3(0.0f, weightedScale.y, 0.0f), 0.0f);
                rootPose[2] = kVec4(pinnedRot * kVec3(0.0f, 0.0f, weightedScale.z), 0.0f);
                rootPose[3] = kVec4(pinnedPos, 1.0f);

                nodeTransform     = rootPose;
                rootMotionApplied = true;
            }
        }

        if (!rootMotionApplied)
        {
            // -----------------------------------------------------------------
            // Layer 1 — BASE: weighted average of every non-additive sample that
            // its mask allows to drive this bone (partial animation).
            //
            // A bone excluded by all base masks (or absent from every clip) keeps
            // the skeleton's rest pose instead of collapsing toward identity, so
            // a masked blend cleanly layers one region over another.
            // -----------------------------------------------------------------
            kVec3 posAccum(0.0f);
            kVec3 scaleAccum(0.0f);
            kQuat rotAccum(0.0f, 0.0f, 0.0f, 0.0f);
            kQuat rotRef(1.0f, 0.0f, 0.0f, 0.0f);
            float totalWeight = 0.0f;
            bool  haveRef     = false;

            for (const kPoseSample &s : samples)
            {
                if (s.weight <= 0.0f || s.additive != nullptr)
                    continue;
                if (!maskAllowsBone(s.mask, node->name))
                    continue;

                kVec3 t, sc;
                kQuat r;
                if (s.restPose)
                {
                    // Weighted masking: contribute the skeleton's rest (bind)
                    // pose for this bone. Paired with a clip sample that carries
                    // mask weight w and rest weight (1 - w) on the same mask this
                    // makes w an absolute "how strongly does this state drive
                    // these bones" control — no clip lookup / time advance here.
                    blendDecomposeTRS(node->transformation, t, r, sc);
                }
                else
                {
                    if (s.animation == nullptr)
                        continue;

                    kBone *bone = s.animation->findBone(node->name);
                    if (bone == nullptr)
                        continue;

                    bone->update(s.time);

                    blendDecomposeTRS(bone->getLocalTransform(), t, r, sc);
                }

                // Keep every contribution on the same quaternion hemisphere as the
                // first one, otherwise opposite-sign quaternions cancel out.
                if (!haveRef) { rotRef = r; haveRef = true; }
                else if (glm::dot(r, rotRef) < 0.0f) r = -r;

                posAccum    += t * s.weight;
                scaleAccum  += sc * s.weight;
                rotAccum    += r * s.weight;
                totalWeight += s.weight;
            }

            if (totalWeight > 1e-6f)
            {
                const kVec3 pos    = posAccum / totalWeight;
                const kVec3 scale  = scaleAccum / totalWeight;
                const float rotLen = glm::length(rotAccum);
                const kQuat rot    = (rotLen > 1e-6f) ? (rotAccum / rotLen) : rotRef;

                // Compose T * R * S without glm::translate/scale so this file does
                // not need the matrix_transform extension included.
                kMat4 composed(1.0f);
                composed[0] = kVec4(rot * kVec3(scale.x, 0.0f, 0.0f), 0.0f);
                composed[1] = kVec4(rot * kVec3(0.0f, scale.y, 0.0f), 0.0f);
                composed[2] = kVec4(rot * kVec3(0.0f, 0.0f, scale.z), 0.0f);
                composed[3] = kVec4(pos, 1.0f);
                nodeTransform = composed;
            }
            else
            {
                // No base contribution for this bone — keep the rest pose.
                nodeTransform = node->transformation;
            }

            // -----------------------------------------------------------------
            // Layer 2 — ADDITIVE: layer each additive sample's (pose - reference)
            // delta on top of the base result. Deltas are applied in sample order;
            // a bone the additive clip does not reference (or whose wrapper has no
            // baked reference) contributes an identity delta and is a no-op.
            // -----------------------------------------------------------------
            for (const kPoseSample &s : samples)
            {
                if (s.additive == nullptr || s.weight <= 0.0f)
                    continue;
                if (!maskAllowsBone(s.mask, node->name))
                    continue;
                if (!s.additive->isBuilt())
                    continue;

                const kMat4 delta = s.additive->sampleDeltaMatrix(node->name, s.time);
                nodeTransform = kAdditiveAnimation::applyDelta(nodeTransform, delta, s.weight);
            }
        }

        kMat4 globalTransformation = parentTransform * nodeTransform;

        // All clips of a blend tree share the same skinned mesh / bone palette,
        // so any sample's mesh list resolves the bone indices.
        kSkeletalAnimation *meshClip = nullptr;
        for (const kPoseSample &s : samples)
            if (s.animation != nullptr) { meshClip = s.animation; break; }

        if (meshClip != nullptr)
        {
            const auto &meshes = meshClip->getMeshes();
            for (size_t i = 0; i < meshes.size(); ++i)
            {
                if (!meshes[i] || meshes[i]->getType() != kNodeType::NODE_TYPE_MESH)
                    continue;

                kMesh *childMesh = (kMesh *)meshes[i];
                std::map<kString, kBoneInfo> &boneInfoMap = childMesh->getBoneInfoMap();
                auto it = boneInfoMap.find(node->name);
                if (it != boneInfoMap.end())
                {
                    int   index  = it->second.id;
                    kMat4 offset = it->second.offset;
                    if (index >= 0 && index < (int)finalBoneMatrices.size())
                        finalBoneMatrices[index] = globalTransformation * offset;
                }
            }
        }

        for (int i = 0; i < node->childrenCount; ++i)
            calculateBlendedBoneTransform(samples, &node->children[i], globalTransformation);
    }
}
