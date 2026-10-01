/**
 * @file kadditiveanimation.cpp
 * @brief Implementation of kAdditiveAnimation (reference baking + delta sampling).
 */

#include "kadditiveanimation.h"
#include "kskelanimation.h"
#include "kbone.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <vector>

namespace kemena
{
    kAdditiveAnimation::kAdditiveAnimation(kSkeletalAnimation *additiveClip, float time)
    {
        clip = additiveClip;
        if (additiveClip != nullptr)
            buildFromClip(additiveClip, time);
    }

    void kAdditiveAnimation::setClip(kSkeletalAnimation *newClip)
    {
        // Re-binding the same clip must not throw away a baked reference.
        if (clip == newClip)
            return;
        clip = newClip;
        clearReference();
    }

    int kAdditiveAnimation::buildFromBindPose(kSkeletalAnimation *referenceClip)
    {
        reference.clear();
        built = false;
        referenceTime = 0.0f;

        if (referenceClip == nullptr)
            return 0;

        int count = 0;
        std::vector<const kNodeData *> stack;
        stack.push_back(&referenceClip->getRootNode());
        while (!stack.empty())
        {
            const kNodeData *node = stack.back();
            stack.pop_back();
            if (node == nullptr)
                continue;

            if (!node->name.empty() && reference.find(node->name) == reference.end())
            {
                // The hierarchy transform is the rig's rest/bind local pose.
                reference.emplace(node->name, node->transformation);
                ++count;
            }

            for (size_t i = node->children.size(); i-- > 0;)
                stack.push_back(&node->children[i]);
        }

        built = !reference.empty();
        return count;
    }

    int kAdditiveAnimation::buildFromClip(kSkeletalAnimation *referenceClip, float time)
    {
        reference.clear();
        built = false;
        referenceTime = time;

        if (referenceClip == nullptr)
            return 0;

        int count = 0;
        std::vector<const kNodeData *> stack;
        stack.push_back(&referenceClip->getRootNode());
        while (!stack.empty())
        {
            const kNodeData *node = stack.back();
            stack.pop_back();
            if (node == nullptr)
                continue;

            if (!node->name.empty() && reference.find(node->name) == reference.end())
            {
                kBone *bone = referenceClip->findBone(node->name);
                if (bone != nullptr)
                {
                    bone->update(time);
                    reference.emplace(node->name, bone->getLocalTransform());
                    ++count;
                }
            }

            for (size_t i = node->children.size(); i-- > 0;)
                stack.push_back(&node->children[i]);
        }

        built = !reference.empty();
        return count;
    }

    void kAdditiveAnimation::setReferenceBone(const kString &boneName, const kMat4 &referenceLocal)
    {
        if (boneName.empty())
            return;
        reference[boneName] = referenceLocal;
        built = !reference.empty();
    }

    void kAdditiveAnimation::clearReference()
    {
        reference.clear();
        built = false;
    }

    bool kAdditiveAnimation::sampleDelta(const kString &boneName, float clipTime,
                                         kVec3 &outPos, kQuat &outRot, kVec3 &outScale)
    {
        if (clip == nullptr || !built)
            return false;

        auto it = reference.find(boneName);
        if (it == reference.end())
            return false;

        kBone *bone = clip->findBone(boneName);
        if (bone == nullptr)
            return false;

        bone->update(clipTime);

        kVec3 posePos, poseScale, refPos, refScale;
        kQuat poseRot, refRot;
        decomposeTRS(bone->getLocalTransform(), posePos, poseRot, poseScale);
        decomposeTRS(it->second,              refPos,  refRot,  refScale);

        outPos = posePos - refPos;

        // Relative rotation: reference^-1 * pose, so that applying the delta to
        // a base pose equal to the reference reproduces the sampled pose.
        outRot = glm::normalize(glm::inverse(refRot) * poseRot);

        // Component-wise scale ratio (guarded against a zero reference scale).
        outScale = kVec3((std::fabs(refScale.x) > 1e-6f) ? poseScale.x / refScale.x : 1.0f,
                         (std::fabs(refScale.y) > 1e-6f) ? poseScale.y / refScale.y : 1.0f,
                         (std::fabs(refScale.z) > 1e-6f) ? poseScale.z / refScale.z : 1.0f);
        return true;
    }

    kMat4 kAdditiveAnimation::sampleDeltaMatrix(const kString &boneName, float clipTime)
    {
        kVec3 pos, scale;
        kQuat rot;
        if (!sampleDelta(boneName, clipTime, pos, rot, scale))
            return kMat4(1.0f);
        return composeTRS(pos, rot, scale);
    }

    kMat4 kAdditiveAnimation::applyDelta(const kMat4 &base, const kMat4 &delta, float weight)
    {
        if (weight == 0.0f)
            return base;

        kVec3 basePos, baseScale, deltaPos, deltaScale;
        kQuat baseRot, deltaRot;
        decomposeTRS(base,  basePos,  baseRot,  baseScale);
        decomposeTRS(delta, deltaPos, deltaRot, deltaScale);

        // Rotation is layered multiplicatively: slerp from identity so a weight
        // of 0 leaves the base rotation untouched and 1 applies the full delta.
        const kQuat partialRot = glm::slerp(kQuat(1.0f, 0.0f, 0.0f, 0.0f),
                                            glm::normalize(deltaRot), weight);

        // Position/scale are layered additively (scale as a ratio toward the
        // delta scale so a neutral delta of 1.0 keeps the base scale).
        const kVec3 pos   = basePos + deltaPos * weight;
        const kVec3 scale = baseScale * glm::mix(kVec3(1.0f), deltaScale, weight);

        return composeTRS(pos, baseRot * partialRot, scale);
    }

    void kAdditiveAnimation::decomposeTRS(const kMat4 &m, kVec3 &outPos, kQuat &outRot, kVec3 &outScale)
    {
        outPos = kVec3(m[3][0], m[3][1], m[3][2]);
        outScale = kVec3(glm::length(kVec3(m[0])),
                         glm::length(kVec3(m[1])),
                         glm::length(kVec3(m[2])));

        // Strip the scale out of the basis columns before extracting the
        // rotation, otherwise non-unit scale leaks into the quaternion.
        kMat3 rot(1.0f);
        rot[0] = (outScale.x > 1e-6f) ? kVec3(m[0]) / outScale.x : kVec3(m[0]);
        rot[1] = (outScale.y > 1e-6f) ? kVec3(m[1]) / outScale.y : kVec3(m[1]);
        rot[2] = (outScale.z > 1e-6f) ? kVec3(m[2]) / outScale.z : kVec3(m[2]);
        outRot = kQuat(rot);
    }

    kMat4 kAdditiveAnimation::composeTRS(const kVec3 &pos, const kQuat &rot, const kVec3 &scale)
    {
        // Compose T * R * S directly (no matrix_transform dependency, and no
        // re-normalisation surprises from glm::translate/scale).
        const kQuat r = glm::normalize(rot);
        kMat4 m(1.0f);
        m[0] = kVec4(r * kVec3(scale.x, 0.0f, 0.0f), 0.0f);
        m[1] = kVec4(r * kVec3(0.0f, scale.y, 0.0f), 0.0f);
        m[2] = kVec4(r * kVec3(0.0f, 0.0f, scale.z), 0.0f);
        m[3] = kVec4(pos, 1.0f);
        return m;
    }
}
