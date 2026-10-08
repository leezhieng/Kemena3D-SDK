#include "kanimator.h"
#include "kanimationmask.h"

// Root-motion math: translate/scale matrix helpers and quaternion
// constructors (mat3→quat, mat4←quat) used by handleRootMotion().
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace kemena
{
    // -----------------------------------------------------------------------
    // Local-transform blending helpers used by calculateBoneTransform() during
    // cross-faded state transitions. Both clips belong to the same skeleton, so
    // per-bone local-space blending (position lerp / rotation nlerp / scale
    // lerp) produces smooth, stable transitions without global-space "stretch".
    // -----------------------------------------------------------------------

    static void decomposeTRS(const kMat4 &m, kVec3 &translation, kQuat &rotation, kVec3 &scale)
    {
        translation = kVec3(m[3][0], m[3][1], m[3][2]);
        scale = kVec3(glm::length(kVec3(m[0])),
                      glm::length(kVec3(m[1])),
                      glm::length(kVec3(m[2])));

        // Rebuild the pure rotation columns (divide the scale out) so non-unit
        // scale does not leak into the quaternion extraction.
        kMat3 rot(1.0f);
        rot[0] = (scale.x > 1e-6f) ? kVec3(m[0]) / scale.x : kVec3(m[0]);
        rot[1] = (scale.y > 1e-6f) ? kVec3(m[1]) / scale.y : kVec3(m[1]);
        rot[2] = (scale.z > 1e-6f) ? kVec3(m[2]) / scale.z : kVec3(m[2]);
        rotation = kQuat(rot);
    }

    static kMat4 composeTRS(const kVec3 &translation, const kQuat &rotation, const kVec3 &scale)
    {
        return glm::translate(kMat4(1.0f), translation) *
               kMat4(rotation) *
               glm::scale(kMat4(1.0f), scale);
    }

    static kMat4 blendLocalTransforms(const kMat4 &from, const kMat4 &to, float t)
    {
        kVec3 fromPos, toPos;
        kQuat fromRot, toRot;
        kVec3 fromScl, toScl;
        decomposeTRS(from, fromPos, fromRot, fromScl);
        decomposeTRS(to,   toPos,   toRot,   toScl);

        // Take the shortest rotation path (quaternions q and -q are identical).
        if (glm::dot(fromRot, toRot) < 0.0f)
            toRot = -toRot;

        // Interpolate each component independently: position/scale by lerp and
        // rotation by SLERP (constant angular velocity), then reconstruct the
        // TRS matrix. Never lerp raw 4x4 matrices directly — that skews and
        // shears the blend.
        const kVec3 pos = glm::mix(fromPos, toPos, t);
        const kQuat rot = glm::slerp(fromRot, toRot, t);
        const kVec3 scl = glm::mix(fromScl, toScl, t);

        return composeTRS(pos, rot, scl);
    }

    kAnimator::kAnimator(kSkeletalAnimation *newAnimation)
    {
        currentTime = 0.0f;

        if (newAnimation != nullptr)
        {
            currentAnimation = newAnimation;
            animations.push_back(newAnimation);
        }

        finalBoneMatrices.reserve(MAX_BONES);
        for (int i = 0; i < MAX_BONES; i++)
            finalBoneMatrices.push_back(kMat4(1.0f));
    }

    void kAnimator::addAnimation(kSkeletalAnimation *newAnimation)
    {
        animations.push_back(newAnimation);
    }

    void kAnimator::updateAnimation(float newDeltaTime, int frameId)
    {
        deltaTime = newDeltaTime;
        if (currentAnimation != nullptr && currentFrameId != frameId)
        {
            // When no time has elapsed, the caller is only re-reading the pose
            // (the editor's Manager::stepAnimators drives the clip time and
            // computes the pose directly). Recomputing it here is redundant and
            // roughly doubles the per-frame pose cost during cross-fades, so
            // skip it — the renderer just uses the pose stepAnimators produced.
            if (newDeltaTime != 0.0f)
            {
                const float prevTime = currentTime;
                currentTime += currentAnimation->getTicksPerSecond() * newDeltaTime;
                currentTime = fmod(currentTime, currentAnimation->getDuration());

                // When the clip loops (time wrapped past the end), re-seed the
                // root-motion tracker. Otherwise the end-of-clip → start-of-clip
                // pose jump is accumulated as a huge delta that cancels out all
                // the motion gathered over the previous loop, snapping the object
                // back to its starting position.
                if (rootMotionActive() && currentTime < prevTime)
                    resetRootMotion();

                const kNodeData &rootNode = currentAnimation->getRootNode();
                calculateBoneTransform(&rootNode, kMat4(1.0f));
            }
        }
        // Only update once per frame.
        currentFrameId = frameId;

        // Future: sample objectAnimation tracks here and write per-target
        // transforms back to kObject — left as a hook for the cinematic
        // editor pass.
    }

    void kAnimator::playAnimation(kSkeletalAnimation *animation)
    {
        if (animation != nullptr)
        {
            // A direct play is an instant switch — drop any active cross-fade.
            blending = false;
            blendFromAnimation = nullptr;
            blendMask = nullptr;
            currentAnimation = animation;
            currentTime = 0.0f;
            resetRootMotion();
        }
    }

    void kAnimator::playAnimation(int index)
    {
        kSkeletalAnimation *clip = getAnimation(index);
        if (clip != nullptr)
        {
            blending = false;
            blendFromAnimation = nullptr;
            blendMask = nullptr;
            currentAnimation = clip;
            currentTime = 0.0f;
            resetRootMotion();
        }
    }

    kSkeletalAnimation *kAnimator::getCurrentAnimation()
    {
        return currentAnimation;
    }

    void kAnimator::calculateBoneTransform(const kNodeData *node, kMat4 parentTransform)
    {
        if (node == nullptr) return;
        if (currentAnimation == nullptr) return;

        kString nodeName      = node->name;
        kMat4   nodeTransform = node->transformation;

        // Cross-fade: while a state transition is blending, sample the source
        // clip at blendFromTime and the destination clip (the active clip) at
        // currentTime, then blend their local transforms per bone. The skeleton
        // hierarchy is traversed once with the blended transforms (local-space
        // blending), which keeps the transition smooth and artifact-free.
        if (blending && blendFromAnimation != nullptr)
        {
            kBone *fromBone = blendFromAnimation->findBone(nodeName);
            kBone *toBone   = currentAnimation->findBone(nodeName);

            // Partial (masked) cross-fade: bones the mask excludes snap straight
            // to the destination pose instead of blending, so a transition can
            // be limited to a body region (e.g. upper body only).
            const bool maskAllowsBlend = (blendMask == nullptr) ||
                                         blendMask->isBoneActive(nodeName);

            // Root-motion is NEVER blended during a cross-fade. Whether the
            // source or the destination clip carries root motion, its root
            // displacement would be mixed into the pose and make the character
            // slide / glitch back and forth while the fade runs. Instead the
            // root is always taken from the destination clip: baked through
            // handleRootMotion() when the destination has root motion, or its
            // natural rest pose otherwise. The root bone is resolved from the
            // clip that actually carries root motion so this exclusion also
            // applies when only the source clip has it enabled.
            bool isRootMotionBone = false;
            const bool fromHasRootMotion = blendFromAnimation != nullptr &&
                (blendFromAnimation->getRootMotionRotation() ||
                 blendFromAnimation->getRootMotionPositionY() ||
                 blendFromAnimation->getRootMotionPositionXZ());
            if (fromHasRootMotion || rootMotionActive())
            {
                if (rootMotionActive())
                    resolveRootBone();
                else
                    resolveRootBoneFor(blendFromAnimation);
                isRootMotionBone = (nodeName == rootBoneName);
            }

            if (isRootMotionBone && toBone != nullptr)
            {
                toBone->update(currentTime);
                nodeTransform = toBone->getLocalTransform();
                if (rootMotionActive())
                    handleRootMotion(toBone, nodeTransform);
            }
            else if (fromBone != nullptr && toBone != nullptr && maskAllowsBlend)
            {
                fromBone->update(blendFromTime);
                toBone->update(currentTime);
                nodeTransform = blendLocalTransforms(fromBone->getLocalTransform(),
                                                     toBone->getLocalTransform(),
                                                     blendFactor());
            }
            else if (toBone != nullptr)
            {
                // Bone absent from the source clip — fall back to the destination.
                toBone->update(currentTime);
                nodeTransform = toBone->getLocalTransform();
            }
            else if (fromBone != nullptr)
            {
                // Bone absent from the destination clip — keep the source pose.
                fromBone->update(blendFromTime);
                nodeTransform = fromBone->getLocalTransform();
            }
        }
        else
        {
            kBone *bone = currentAnimation->findBone(nodeName);
            if (bone != nullptr)
            {
                bone->update(currentTime);
                nodeTransform = bone->getLocalTransform();

                // Root-motion extraction: the topmost animated bone is the root
                // bone. When any channel is enabled, resolve it once and then
                // accumulate the per-frame delta + bake the enabled channels out
                // of the pose so the character stays in place.
                if (rootMotionActive())
                {
                    resolveRootBone();
                    if (nodeName == rootBoneName)
                        handleRootMotion(bone, nodeTransform);
                }
            }
        }

        kMat4 globalTransformation = parentTransform * nodeTransform;

        const auto &meshes = currentAnimation->getMeshes();
        for (size_t i = 0; i < meshes.size(); ++i)
        {
            if (!meshes[i] || meshes[i]->getType() != kNodeType::NODE_TYPE_MESH)
                continue;

            kMesh *childMesh = (kMesh *)meshes[i];
            std::map<kString, kBoneInfo> &boneInfoMap = childMesh->getBoneInfoMap();
            auto it = boneInfoMap.find(nodeName);
            if (it != boneInfoMap.end())
            {
                int   index   = it->second.id;
                kMat4 offset  = it->second.offset;
                // Guard against skeletons larger than MAX_BONES: the renderer
                // can only upload MAX_BONES matrices, so clamp the write to the
                // allocated palette instead of indexing out of bounds.
                if (index >= 0 && index < (int)finalBoneMatrices.size())
                    finalBoneMatrices[index] = globalTransformation * offset;
            }
        }

        for (int i = 0; i < node->childrenCount; ++i)
            calculateBoneTransform(&node->children[i], globalTransformation);
    }

    const std::vector<kMat4> kAnimator::getFinalBoneMatrices() const
    {
        return finalBoneMatrices;
    }

    void kAnimator::setCurrentTime(float newTime)
    {
        currentTime = newTime;
    }

    void kAnimator::setSpeed(float newSpeed)
    {
        speed = newSpeed;
    }

    float kAnimator::getSpeed()
    {
        return speed;
    }

    // -----------------------------------------------------------------------
    // Cross-fade (smooth state transitions).
    // -----------------------------------------------------------------------

    void kAnimator::beginBlend(kSkeletalAnimation *from, float fromTicks,
                               kSkeletalAnimation *to, float toTicks, float duration)
    {
        beginBlend(from, fromTicks, to, toTicks, duration, nullptr);
    }

    void kAnimator::beginBlend(kSkeletalAnimation *from, float fromTicks,
                               kSkeletalAnimation *to, float toTicks, float duration,
                               const kAnimationMask *mask)
    {
        // Drop any previous cross-fade so we always start clean.
        blending = false;
        blendFromAnimation = nullptr;
        blendMask = mask; // borrowed; nullptr means a full-body fade

        if (to == nullptr)
            return;

        currentAnimation = to;
        currentTime = toTicks;
        resetRootMotion();

        // Instant switch when there is no source clip or no blend window.
        if (from == nullptr || from == to || duration <= 0.0f)
            return;

        blendFromAnimation = from;
        blendFromTime     = fromTicks;
        blendFromTps      = from->getTicksPerSecond();
        blendFromDuration = from->getDuration();
        blendDuration     = duration;
        blendElapsed      = 0.0f;
        blending          = true;
    }

    bool kAnimator::updateBlend(float dt)
    {
        if (!blending)
            return false;

        blendElapsed += dt;
        if (blendElapsed >= blendDuration)
        {
            blending = false;
            blendFromAnimation = nullptr;
            return false;
        }

        // Keep the source clip playing through the fade so the character's
        // motion stays continuous — no stall at the start and no abrupt stop.
        // The pose is still blended, but the character keeps moving, which
        // removes the "laggy" feel of a frozen or damped transition.
        if (blendFromAnimation != nullptr)
        {
            blendFromTime += blendFromTps * dt;
            if (blendFromDuration > 0.0f)
                blendFromTime = fmod(blendFromTime, blendFromDuration);
        }

        return true;
    }

    void kAnimator::endBlend()
    {
        blending = false;
        blendFromAnimation = nullptr;
        blendMask = nullptr;
    }

    float kAnimator::blendFactor() const
    {
        if (!blending || blendDuration <= 0.0f)
            return 1.0f;
        const float t = glm::clamp(blendElapsed / blendDuration, 0.0f, 1.0f);
        // Smoothstep: eases out of the source pose gently (no pop) and into
        // the destination smoothly.
        return t * t * (3.0f - 2.0f * t);
    }

    // -----------------------------------------------------------------------
    // Object-transform animation — placeholder for future cinematic editor.
    // The setters wire the clip in but updateAnimation() doesn't sample
    // tracks yet.
    // -----------------------------------------------------------------------

    void kAnimator::setObjectAnimation(kAnimation *clip)
    {
        objectAnimation = clip;
    }

    kAnimation *kAnimator::getObjectAnimation() const
    {
        return objectAnimation;
    }

    // -----------------------------------------------------------------------
    // Blend shapes (morph targets). Weights are forwarded to the driven mesh,
    // which is the single storage location the renderer uploads, so animator
    // and script writes never fight over separate copies.
    // -----------------------------------------------------------------------

    void kAnimator::setMorphWeight(const kString &name, float weight)
    {
        if (morphMesh != nullptr)
            morphMesh->setMorphWeight(name, weight);
    }

    void kAnimator::setMorphWeight(int slot, float weight)
    {
        if (morphMesh != nullptr)
            morphMesh->setMorphWeight(slot, weight);
    }

    float kAnimator::getMorphWeight(const kString &name) const
    {
        return morphMesh != nullptr ? morphMesh->getMorphWeight(name) : 0.0f;
    }

    void kAnimator::setMorphWeights(const std::vector<float> &weights)
    {
        if (morphMesh != nullptr)
            morphMesh->setMorphWeights(weights);
    }

    std::vector<float> kAnimator::getMorphWeights() const
    {
        return morphMesh != nullptr ? morphMesh->getMorphWeights()
                                    : std::vector<float>();
    }

    // -----------------------------------------------------------------------
    // Root-motion extraction.
    // -----------------------------------------------------------------------

    bool kAnimator::rootMotionActive() const
    {
        return currentAnimation != nullptr &&
               (currentAnimation->getRootMotionRotation() ||
                currentAnimation->getRootMotionPositionY() ||
                currentAnimation->getRootMotionPositionXZ());
    }

    bool kAnimator::isRootMotionActive() const
    {
        return rootMotionActive();
    }

    void kAnimator::resolveRootBone()
    {
        // Already resolved for this clip — nothing to do.
        if (rootBoneResolved && rootBoneForAnim == currentAnimation)
            return;

        resolveRootBoneFor(currentAnimation);
    }

    void kAnimator::resolveRootBoneFor(kSkeletalAnimation *anim)
    {
        rootBoneName.clear();
        rootBoneForAnim = anim;
        rootBoneResolved = true;
        rootMotionInitialized = false;

        if (!anim)
            return;

        // The root-motion bone is the topmost animated node in the hierarchy.
        const kNodeData &root = anim->getRootNode();
        findRootBoneRecursive(&root, anim);
    }

    void kAnimator::findRootBoneRecursive(const kNodeData *node, kSkeletalAnimation *anim)
    {
        if (!node || !rootBoneName.empty())
            return;
        if (anim)
        {
            kBone *bone = anim->findBone(node->name);
            if (bone)
            {
                // Skip static container nodes. Many FBX exporters emit
                // constant (identity) animation channels on the armature's
                // parent node (e.g. "Skeleton"); the real root-motion bone is
                // the topmost node that actually moves (e.g. "Root" / "Hips").
                if (!bone->isStatic())
                {
                    rootBoneName = node->name;
                    return;
                }
            }
        }
        for (int i = 0; i < node->childrenCount; ++i)
            findRootBoneRecursive(&node->children[i], anim);
    }

    void kAnimator::handleRootMotion(kBone *bone, kMat4 &nodeTransform)
    {
        if (!currentAnimation || !bone)
            return;

        // Re-sample the root bone at the current time (the caller already
        // updated it, but resolveRootBone may have left it at a different time).
        bone->update(currentTime);
        const kMat4 currentLocal = bone->getLocalTransform();
        const kVec3  currentPos(currentLocal[3]);
        const kQuat  currentRot = kQuat(kMat3(currentLocal));

        // Loop / seek detection: when the clip time moved backwards since the
        // previous sample the clip wrapped to its start. Re-seed the tracker so
        // the end→start pose jump is never emitted as a huge delta that cancels
        // all the motion gathered over the previous loop (which snapped the
        // object back to its starting position).
        if (currentTime < lastRootTime)
            rootMotionInitialized = false;
        lastRootTime = currentTime;

        if (rootMotionInitialized)
        {
            // Per-frame delta accumulated for each enabled channel.
            if (currentAnimation->getRootMotionPositionXZ())
            {
                rootMotionAccumPos.x += currentPos.x - lastRootPos.x;
                rootMotionAccumPos.z += currentPos.z - lastRootPos.z;
            }
            if (currentAnimation->getRootMotionPositionY())
                rootMotionAccumPos.y += currentPos.y - lastRootPos.y;
            if (currentAnimation->getRootMotionRotation())
            {
                const kQuat dRot = currentRot * glm::conjugate(lastRootRot);
                rootMotionAccumRotEuler += glm::degrees(glm::eulerAngles(dRot));
            }
        }
        else
        {
            // First pose sampled: remember it as the baked reference so the
            // character stays exactly where it started, and seed the delta
            // tracker without producing a giant first-frame jump.
            bakeRootPos = currentPos;
            bakeRootRot = currentRot;
        }

        lastRootPos = currentPos;
        lastRootRot = currentRot;
        rootMotionInitialized = true;

        // Bake the enabled root-motion channels out of the pose, pinning them
        // to the start-of-play reference. Un-enabled channels keep animating.
        if (currentAnimation->getRootMotionPositionXZ())
        {
            nodeTransform[3][0] = bakeRootPos.x;
            nodeTransform[3][2] = bakeRootPos.z;
        }
        if (currentAnimation->getRootMotionPositionY())
            nodeTransform[3][1] = bakeRootPos.y;
        if (currentAnimation->getRootMotionRotation())
        {
            // Replace the rotation with the start rotation, preserving the
            // current translation and scale (root bones are usually rigid, but
            // keeping scale avoids degenerate matrices).
            const kVec3 scale(glm::length(kVec3(nodeTransform[0])),
                              glm::length(kVec3(nodeTransform[1])),
                              glm::length(kVec3(nodeTransform[2])));
            nodeTransform = glm::translate(kMat4(1.0f), kVec3(nodeTransform[3])) *
                            kMat4(bakeRootRot) *
                            glm::scale(kMat4(1.0f), scale);
        }
    }

    kVec3 kAnimator::getRootMotionDeltaPosition()
    {
        kVec3 delta = rootMotionAccumPos;
        rootMotionAccumPos = kVec3(0.0f);
        return delta;
    }

    kVec3 kAnimator::getRootMotionDeltaRotation()
    {
        kVec3 delta = rootMotionAccumRotEuler;
        rootMotionAccumRotEuler = kVec3(0.0f);
        return delta;
    }

    void kAnimator::resetRootMotion()
    {
        rootMotionAccumPos      = kVec3(0.0f);
        rootMotionAccumRotEuler = kVec3(0.0f);
        lastRootPos             = kVec3(0.0f);
        lastRootRot             = kQuat(1.0f, 0, 0, 0);
        lastRootTime            = 0.0f;
        rootMotionInitialized   = false;
        rootBoneResolved        = false;
        rootBoneForAnim         = nullptr;
        rootBoneName.clear();
    }

    void kAnimator::setBlendRootSource(kSkeletalAnimation *clip, float clipTime)
    {
        if (clip == nullptr)
            return;

        const bool sourceChanged = (clip != rootBoneForAnim);

        // Point at the source clip WITHOUT resetting time or root motion.
        // playAnimation() would clear the tracker, which stalls the accumulated
        // delta and makes the pose pop whenever the dominant clip flips.
        currentAnimation = clip;
        currentTime      = clipTime;

        if (!sourceChanged)
            return;

        const bool firstEver = !rootBoneResolved;
        resolveRootBoneFor(clip); // sets rootBoneName; clears rootMotionInitialized

        if (firstEver)
            return; // let the first handleRootMotion() seed the baked reference

        // Re-seed the frame-to-frame tracker to the new clip's current root pose
        // so the switch contributes zero delta; the baked reference is kept, so
        // the pose stays where it is instead of jumping.
        kBone *rootBone = clip->findBone(rootBoneName);
        if (rootBone != nullptr)
        {
            rootBone->update(currentTime);
            const kMat4 local = rootBone->getLocalTransform();
            lastRootPos           = kVec3(local[3][0], local[3][1], local[3][2]);
            lastRootRot           = kQuat(kMat3(local));
            lastRootTime          = currentTime;
            rootMotionInitialized = true;
        }
    }

    bool kAnimator::getRootMotionPositionXZ() const
    {
        return currentAnimation ? currentAnimation->getRootMotionPositionXZ() : false;
    }

    bool kAnimator::getRootMotionPositionY() const
    {
        return currentAnimation ? currentAnimation->getRootMotionPositionY() : false;
    }

    bool kAnimator::getRootMotionRotation() const
    {
        return currentAnimation ? currentAnimation->getRootMotionRotation() : false;
    }
}
