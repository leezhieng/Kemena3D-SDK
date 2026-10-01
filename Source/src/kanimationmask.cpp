/**
 * @file kanimationmask.cpp
 * @brief Implementation of kAnimationMask (partial-animation bone mask).
 */

#include "kanimationmask.h"
#include "kmesh.h"

#include <algorithm>
#include <cctype>

namespace kemena
{
    namespace
    {
        /** @brief Lowercases an ASCII string for case-insensitive name matching. */
        kString toLowerAscii(const kString &s)
        {
            kString out;
            out.reserve(s.size());
            for (char c : s)
                out.push_back((char)std::tolower((unsigned char)c));
            return out;
        }

        /** @brief True when @p haystack contains @p needle (both already lowercase). */
        bool containsToken(const kString &haystack, const kString &needle)
        {
            return haystack.find(needle) != kString::npos;
        }

        /**
         * @brief Guesses whether a bone belongs to the left or right side.
         *
         * Recognises the common Unity / Mixamo / Blender spelling variants:
         * "Left"/"Right", "_L"/"_R", "LeftArm"/"RightArm", ".L"/".R". Bones with
         * no side marker default to the left side (callers only use the result
         * for the left/right variants of a classified group).
         */
        bool isLeftSide(const kString &lower)
        {
            /** @brief True when @p s ends with the marker @p suffix. */
            auto endsWith = [](const kString &s, const kString &suffix)
            {
                return s.size() >= suffix.size() &&
                       s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
            };

            // Explicit words first ("LeftArm" / "RightUpLeg").
            if (containsToken(lower, "left"))
                return true;
            if (containsToken(lower, "right"))
                return false;

            // Side suffix tokens used by DCC exports ("Arm_L", "Foot.R").
            if (endsWith(lower, "_l") || endsWith(lower, ".l") || endsWith(lower, " l"))
                return true;
            if (endsWith(lower, "_r") || endsWith(lower, ".r") || endsWith(lower, " r"))
                return false;

            // Side infix tokens ("Upper_L_Arm").
            if (containsToken(lower, "_l_") || containsToken(lower, ".l."))
                return true;
            if (containsToken(lower, "_r_") || containsToken(lower, ".r."))
                return false;

            return true; // default to the left side when the name carries no marker
        }
    }

    kAnimationMask::kAnimationMask(const kString &maskName)
        : name(maskName)
    {
    }

    void kAnimationMask::addBone(const kString &boneName)
    {
        if (boneName.empty())
            return;
        auto it = bones.find(boneName);
        if (it != bones.end())
            return;
        bones.emplace(boneName, true);
        boneNames.push_back(boneName);
    }

    bool kAnimationMask::removeBone(const kString &boneName)
    {
        auto it = bones.find(boneName);
        if (it == bones.end())
            return false;

        bones.erase(it);
        boneNames.erase(std::remove(boneNames.begin(), boneNames.end(), boneName),
                        boneNames.end());
        return true;
    }

    void kAnimationMask::setBoneEnabled(const kString &boneName, bool enabled)
    {
        if (boneName.empty())
            return;
        auto it = bones.find(boneName);
        if (it == bones.end())
        {
            bones.emplace(boneName, enabled);
            boneNames.push_back(boneName);
            return;
        }
        it->second = enabled;
    }

    int kAnimationMask::buildFromSkeleton(const kNodeData &root, bool enabledByDefault)
    {
        int added = 0;

        // Iterative walk so deeply nested rigs cannot blow the call stack and so
        // the traversal order stays the hierarchy order (top-down, left-right).
        std::vector<const kNodeData *> stack;
        stack.push_back(&root);
        while (!stack.empty())
        {
            const kNodeData *node = stack.back();
            stack.pop_back();

            if (node != nullptr && !node->name.empty() && bones.find(node->name) == bones.end())
            {
                bones.emplace(node->name, enabledByDefault);
                boneNames.push_back(node->name);
                ++added;
            }

            // Push children in reverse so the leftmost child is visited first.
            if (node != nullptr)
                for (size_t i = node->children.size(); i-- > 0;)
                    stack.push_back(&node->children[i]);
        }
        return added;
    }

    int kAnimationMask::buildFromBoneNames(const std::vector<kString> &bonesList,
                                           bool enabledByDefault)
    {
        int added = 0;
        for (const kString &boneName : bonesList)
        {
            if (boneName.empty() || bones.find(boneName) != bones.end())
                continue;
            bones.emplace(boneName, enabledByDefault);
            boneNames.push_back(boneName);
            ++added;
        }
        return added;
    }

    int kAnimationMask::buildFromMesh(kMesh *mesh, bool enabledByDefault)
    {
        if (mesh == nullptr)
            return 0;

        std::vector<kString> names;
        names.reserve(mesh->getBoneInfoMap().size());
        for (const auto &entry : mesh->getBoneInfoMap())
            names.push_back(entry.first);
        return buildFromBoneNames(names, enabledByDefault);
    }

    void kAnimationMask::setAllEnabled(bool enabled)
    {
        for (auto &entry : bones)
            entry.second = enabled;
    }

    void kAnimationMask::setBodyPartEnabled(kAvatarBodyPart part, bool enabled)
    {
        for (auto &entry : bones)
            if (classifyBone(entry.first) == part)
                entry.second = enabled;
    }

    void kAnimationMask::clear()
    {
        bones.clear();
        boneNames.clear();
    }

    bool kAnimationMask::contains(const kString &boneName) const
    {
        return bones.find(boneName) != bones.end();
    }

    bool kAnimationMask::isBoneEnabled(const kString &boneName) const
    {
        auto it = bones.find(boneName);
        return it != bones.end() ? it->second : false;
    }

    bool kAnimationMask::isBoneActive(const kString &boneName) const
    {
        // An identity mask (no entries) covers the whole skeleton so callers can
        // treat "no mask" and "empty mask" identically.
        if (bones.empty())
            return true;
        return isBoneEnabled(boneName);
    }

    bool kAnimationMask::isBodyPartEnabled(kAvatarBodyPart part) const
    {
        for (const auto &entry : bones)
            if (entry.second && classifyBone(entry.first) == part)
                return true;
        return false;
    }

    kAvatarBodyPart kAnimationMask::classifyBone(const kString &boneName)
    {
        if (boneName.empty())
            return kAvatarBodyPart::Unknown;

        const kString lower = toLowerAscii(boneName);
        const bool left = isLeftSide(lower);

        // Digits / hand: check before the generic "arm" test so "LeftHandIndex1"
        // lands in the hand group rather than the arm group.
        if (containsToken(lower, "finger") || containsToken(lower, "thumb") ||
            containsToken(lower, "index") || containsToken(lower, "middle") ||
            containsToken(lower, "ring") || containsToken(lower, "pinky") ||
            containsToken(lower, "hand") || containsToken(lower, "wrist") ||
            containsToken(lower, "forearm") || containsToken(lower, "lowerarm"))
            return left ? kAvatarBodyPart::LeftHand : kAvatarBodyPart::RightHand;

        if (containsToken(lower, "shoulder") || containsToken(lower, "clavicle") ||
            containsToken(lower, "upperarm") || containsToken(lower, "arm"))
            return left ? kAvatarBodyPart::LeftArm : kAvatarBodyPart::RightArm;

        if (containsToken(lower, "toe") || containsToken(lower, "foot") ||
            containsToken(lower, "ankle"))
            return left ? kAvatarBodyPart::LeftFoot : kAvatarBodyPart::RightFoot;

        if (containsToken(lower, "thigh") || containsToken(lower, "shin") ||
            containsToken(lower, "calf") || containsToken(lower, "knee") ||
            containsToken(lower, "upleg") || containsToken(lower, "leg"))
            return left ? kAvatarBodyPart::LeftLeg : kAvatarBodyPart::RightLeg;

        if (containsToken(lower, "head") || containsToken(lower, "neck") ||
            containsToken(lower, "jaw"))
            return kAvatarBodyPart::Head;

        if (containsToken(lower, "spine") || containsToken(lower, "chest") ||
            containsToken(lower, "torso") || containsToken(lower, "upperchest"))
            return kAvatarBodyPart::Spine;

        if (containsToken(lower, "hip") || containsToken(lower, "pelvis") ||
            containsToken(lower, "root"))
            return kAvatarBodyPart::Root;

        return kAvatarBodyPart::Unknown;
    }
}
