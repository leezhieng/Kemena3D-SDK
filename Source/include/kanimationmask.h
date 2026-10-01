/**
 * @file kanimationmask.h
 * @brief Bone (avatar) mask used for partial animation blending.
 *
 * A kAnimationMask names a subset of a skeleton's bones that a clip is allowed
 * to drive. It is the "partial animation" half of the animation system:
 *
 *   * **Building** — populate the mask from a skeleton hierarchy
 *     (buildFromSkeleton()), from a list of bone names (buildFromBoneNames()),
 *     or from a skinned mesh's bone palette (buildFromMesh()). Bones can then
 *     be toggled individually (setBoneEnabled) or by humanoid body part
 *     (setBodyPartEnabled).
 *   * **Sampling / blending** — kAnimator's blended-pose pass
 *     (calculateBlendedBoneTransform) consults isBoneActive() per bone and
 *     ignores samples whose mask disables that bone. Bones excluded by every
 *     contributing sample fall back to the skeleton's rest pose, so a mask
 *     cleanly layers (for example) an upper-body clip over a full-body clip.
 *
 * A mask with no entries is an *identity* mask: isBoneActive() returns true for
 * every bone, so it behaves exactly like passing no mask at all.
 */

#ifndef KANIMATIONMASK_H
#define KANIMATIONMASK_H

#include "kexport.h"
#include "kdatatype.h"

#include <unordered_map>
#include <vector>

namespace kemena
{
    class kMesh;

    /**
     * @brief Humanoid body-part groups used to toggle whole regions of a mask.
     *
     * The engine does not require a canonical humanoid rig; the body parts are
     * derived from common bone-name conventions (Unity / Mixamo / Blender
     * exports). setBodyPartEnabled() toggles every bone classified into the
     * requested group, and isBodyPartEnabled() reports whether *any* bone of
     * that group is currently active.
     */
    enum class kAvatarBodyPart
    {
        Root = 0,       ///< Hips / root bone.
        Spine,          ///< Spine / chest / torso chain.
        Head,           ///< Neck / head chain.
        LeftArm,        ///< Left shoulder / upper arm chain.
        RightArm,       ///< Right shoulder / upper arm chain.
        LeftHand,       ///< Left forearm / hand / finger chain.
        RightHand,      ///< Right forearm / hand / finger chain.
        LeftLeg,        ///< Left thigh / shin chain.
        RightLeg,       ///< Right thigh / shin chain.
        LeftFoot,       ///< Left foot / toe chain.
        RightFoot,      ///< Right foot / toe chain.
        Unknown         ///< Unclassified bone.
    };

    /**
     * @brief Named set of enabled bones used for partial animation blending.
     *
     * @code
     * kAnimationMask upperBody("UpperBody");
     * upperBody.buildFromSkeleton(clip->getRootNode());
     * upperBody.setBodyPartEnabled(kAvatarBodyPart::Spine,   true);
     * upperBody.setBodyPartEnabled(kAvatarBodyPart::Head,    true);
     * upperBody.setBodyPartEnabled(kAvatarBodyPart::LeftArm, true);
     * upperBody.setBodyPartEnabled(kAvatarBodyPart::RightArm,true);
     * // every other group stays disabled -> lower body follows the base clip
     * @endcode
     */
    class KEMENA3D_API kAnimationMask
    {
    public:
        /** @brief Constructs an empty (identity) mask. */
        kAnimationMask() = default;

        /**
         * @brief Constructs an empty mask with a display name.
         * @param maskName Human-readable mask name.
         */
        explicit kAnimationMask(const kString &maskName);

        // -------------------------------------------------------------------
        // Name
        // -------------------------------------------------------------------

        /** @brief Sets the mask display name. */
        void setName(const kString &maskName) { name = maskName; }
        /** @brief Mask display name. */
        const kString &getName() const { return name; }

        // -------------------------------------------------------------------
        // Building
        // -------------------------------------------------------------------

        /**
         * @brief Adds a bone to the mask (enabled) if it is not present yet.
         * @param boneName Bone name to enable.
         */
        void addBone(const kString &boneName);

        /**
         * @brief Removes a bone entry entirely (it becomes "not covered").
         * @param boneName Bone name to remove.
         * @return True when the bone was present.
         */
        bool removeBone(const kString &boneName);

        /**
         * @brief Enables or disables a single bone.
         *
         * Adds the bone (with the requested state) when it is not present yet,
         * so a mask can be authored incrementally without building it first.
         *
         * @param boneName Bone name.
         * @param enabled  True to let the clip drive the bone.
         */
        void setBoneEnabled(const kString &boneName, bool enabled);

        /**
         * @brief Populates the mask from a skeleton hierarchy.
         *
         * Every node of the hierarchy becomes an entry. When @p enabledByDefault
         * is false the mask starts fully disabled, which is convenient when the
         * caller then enables only the body parts it cares about.
         *
         * @param root             Root node of the skeleton (kMesh hierarchy).
         * @param enabledByDefault Initial state of each discovered bone.
         * @return Number of bone entries added.
         */
        int buildFromSkeleton(const kNodeData &root, bool enabledByDefault = true);

        /**
         * @brief Populates the mask from an explicit list of bone names.
         * @param bones            Bone names to add.
         * @param enabledByDefault Initial state of each bone.
         * @return Number of bone entries added.
         */
        int buildFromBoneNames(const std::vector<kString> &bones, bool enabledByDefault = true);

        /**
         * @brief Populates the mask from a skinned mesh's bone palette.
         *
         * Convenience for authoring: the mesh already knows every bone that can
         * skin a vertex, so building from it guarantees the mask names match the
         * skeleton the clip is bound to.
         *
         * @param mesh             Skinned mesh (may be nullptr — no-op).
         * @param enabledByDefault Initial state of each bone.
         * @return Number of bone entries added.
         */
        int buildFromMesh(kMesh *mesh, bool enabledByDefault = true);

        /** @brief Enables or disables every bone currently in the mask. */
        void setAllEnabled(bool enabled);

        /**
         * @brief Enables or disables every bone classified into a body part.
         *
         * Only bones already present in the mask are affected; build the mask
         * from the skeleton first.
         *
         * @param part    Body-part group.
         * @param enabled True to enable the group.
         */
        void setBodyPartEnabled(kAvatarBodyPart part, bool enabled);

        /** @brief Removes every bone entry (the mask becomes an identity mask). */
        void clear();

        // -------------------------------------------------------------------
        // Queries
        // -------------------------------------------------------------------

        /** @brief True when the mask holds no bone entries (identity mask). */
        bool empty() const { return bones.empty(); }

        /** @brief Number of bone entries in the mask. */
        size_t getBoneCount() const { return bones.size(); }

        /** @brief True when the mask has an entry for @p boneName. */
        bool contains(const kString &boneName) const;

        /** @brief State of @p boneName's entry (false when absent). */
        bool isBoneEnabled(const kString &boneName) const;

        /**
         * @brief Whether a contributing clip may drive @p boneName.
         *
         * An identity mask (no entries) allows everything, which makes "no
         * mask" and "empty mask" behave identically.
         */
        bool isBoneActive(const kString &boneName) const;

        /** @brief Bone names in insertion order. */
        const std::vector<kString> &getBoneNames() const { return boneNames; }

        /** @brief True when any bone of @p part is present and enabled. */
        bool isBodyPartEnabled(kAvatarBodyPart part) const;

        /**
         * @brief Classifies a bone name into a humanoid body part.
         *
         * Purely name-based (case-insensitive, side-aware) and intended for
         * convenience UI: rigs that follow the common naming conventions get
         * whole-group toggles for free, everything else falls back to Unknown.
         *
         * @param boneName Bone name to classify.
         * @return The matching body part, or kAvatarBodyPart::Unknown.
         */
        static kAvatarBodyPart classifyBone(const kString &boneName);

    private:
        kString                          name;      ///< Display name.
        std::vector<kString>             boneNames; ///< Insertion-ordered bone names.
        std::unordered_map<kString, bool> bones;    ///< Bone name -> enabled flag.
    };
}

#endif // KANIMATIONMASK_H
