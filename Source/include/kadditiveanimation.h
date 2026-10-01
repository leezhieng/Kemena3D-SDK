/**
 * @file kadditiveanimation.h
 * @brief Additive animation — bake a reference pose, sample deltas, blend them.
 *
 * An additive clip is an animation whose pose is interpreted as a *delta* on top
 * of whatever pose the character is already in, instead of replacing it. That is
 * how a "hit react", "lean", "breathing" or "aim offset" layer can play over a
 * locomotion state without authoring every combination.
 *
 * The class deliberately separates the three steps the animation system needs:
 *
 *   * **Building** — @ref buildFromBindPose() bakes the skeleton's rest pose as
 *     the reference, while @ref buildFromClip() bakes the reference by sampling
 *     a clip at a chosen time (the usual "frame 0 of the base clip" workflow).
 *     @ref setReferenceBone() lets a tool override individual entries.
 *   * **Sampling** — @ref sampleDeltaMatrix() samples the clip at an arbitrary
 *     time and returns the *relative* pose (pose * reference^-1) as a TRS delta.
 *   * **Blending** — @ref applyDelta() layers that delta onto a base local
 *     transform with a weight (position/scale additive, rotation multiplicative),
 *     which is exactly what kAnimator's blended-pose pass uses for additive
 *     kPoseSample entries.
 *
 * Additive blends are order-independent enough for real use because each delta is
 * applied to the running result: applying weight*delta on top of a base pose yields
 * the same pose as authoring the base plus the delta by hand when the base equals
 * the reference.
 */

#ifndef KADDITIVEANIMATION_H
#define KADDITIVEANIMATION_H

#include "kexport.h"
#include "kdatatype.h"

#include <unordered_map>
#include <vector>

namespace kemena
{
    class kSkeletalAnimation;

    /**
     * @brief Additive animation wrapper: reference pose + delta sampling.
     *
     * @code
     * // Build once (editor / load time):
     * kAdditiveAnimation lean;
     * lean.setClip(leanClip);          // a short additive "lean" clip
     * lean.buildFromClip(baseClip, 0); // bake the base clip's frame 0 as reference
     *
     * // Per frame: add it to a blend tree as an additive sample.
     * kPoseSample s;
     * s.animation = locomotionClip;
     * s.time      = locomotionTime;
     * s.weight    = 0.5f;
     * kPoseSample add;
     * add.animation = lean.getClip();
     * add.time      = leanTime;
     * add.weight    = 0.5f;
     * add.additive  = &lean;
     * animator.calculateBlendedBoneTransform({ s, add }, &root, kMat4(1.0f));
     * @endcode
     */
    class KEMENA3D_API kAdditiveAnimation
    {
    public:
        /** @brief Constructs an empty additive wrapper (no clip, no reference). */
        kAdditiveAnimation() = default;

        /**
         * @brief Constructs a wrapper, binds the additive clip and builds the
         *        reference from that same clip at @p referenceTime.
         *
         * Convenient for clips that were authored relative to their own frame 0.
         * When the reference must come from a different (base) clip, construct
         * empty and call setClip() + buildFromClip() separately.
         *
         * @param clip          Clip that supplies the additive pose.
         * @param referenceTime Time (ticks) sampled for the reference pose.
         */
        kAdditiveAnimation(kSkeletalAnimation *clip, float referenceTime = 0.0f);

        // -------------------------------------------------------------------
        // Clip binding
        // -------------------------------------------------------------------

        /**
         * @brief Binds the clip that supplies the additive pose.
         *
         * Binding a new clip clears the baked reference so a stale reference
         * from a different skeleton can never be applied.
         *
         * @param clip Additive clip (may be nullptr).
         */
        void setClip(kSkeletalAnimation *clip);

        /** @brief The bound additive clip, or nullptr. */
        kSkeletalAnimation *getClip() const { return clip; }

        // -------------------------------------------------------------------
        // Building
        // -------------------------------------------------------------------

        /**
         * @brief Bakes the skeleton's rest (bind) local pose as the reference.
         *
         * Uses the clip's node hierarchy transforms, so the reference is the
         * pose the rig is authored in. This is the right choice for "pose
         * offset" clips that were authored relative to the bind pose.
         *
         * The reference clip is NOT bound to this wrapper — buildFromBindPose()
         * only reads its skeleton, so the additive source (setClip()) can be a
         * different clip on the same rig.
         *
         * @param referenceClip Clip whose skeleton rest pose is baked.
         * @return Number of bones baked.
         */
        int buildFromBindPose(kSkeletalAnimation *referenceClip);

        /**
         * @brief Bakes the reference by sampling a clip at @p time.
         *
         * The classic workflow: point this at the BASE clip (the one the
         * additive layer sits on top of) and sample its first frame, so the
         * delta is zero whenever the additive clip is at its own rest frame.
         *
         * The reference clip is NOT bound to this wrapper — it only supplies the
         * reference pose, so the additive source (setClip()) can be a different
         * clip on the same rig.
         *
         * @param referenceClip Clip sampled for the reference pose.
         * @param time          Reference time in ticks.
         * @return Number of bones baked.
         */
        int buildFromClip(kSkeletalAnimation *referenceClip, float time);

        /**
         * @brief Overrides the reference local transform of a single bone.
         * @param boneName       Bone name.
         * @param referenceLocal Reference local-space transform.
         */
        void setReferenceBone(const kString &boneName, const kMat4 &referenceLocal);

        /** @brief Removes every baked reference entry and clears the built flag. */
        void clearReference();

        /** @brief True once a reference pose has been baked. */
        bool isBuilt() const { return built; }

        /** @brief Time (ticks) the reference was sampled at, for diagnostics. */
        float getReferenceTime() const { return referenceTime; }

        /** @brief Number of baked reference bones. */
        size_t getReferenceBoneCount() const { return reference.size(); }

        /** @brief True when @p boneName has a baked reference transform. */
        bool hasReferenceBone(const kString &boneName) const
        {
            return reference.find(boneName) != reference.end();
        }

        // -------------------------------------------------------------------
        // Sampling
        // -------------------------------------------------------------------

        /**
         * @brief Samples the additive delta for one bone at @p clipTime.
         *
         * Writes position/rotation/scale *relative to the reference pose*:
         * @c pos = posePos - refPos, @c rot = refRot^-1 * poseRot,
         * @c scale = poseScale / refScale.
         *
         * @param boneName Bone to sample.
         * @param clipTime Clip time in ticks.
         * @param outPos   Receives the relative translation.
         * @param outRot   Receives the relative rotation.
         * @param outScale Receives the relative scale.
         * @return False when the clip/bone/reference is missing (nothing written).
         */
        bool sampleDelta(const kString &boneName, float clipTime,
                         kVec3 &outPos, kQuat &outRot, kVec3 &outScale);

        /**
         * @brief Convenience wrapper around sampleDelta() returning a TRS matrix.
         *
         * Returns an identity matrix when the bone has no reference or is absent
         * from the clip, which makes it safe to apply unconditionally.
         *
         * @param boneName Bone to sample.
         * @param clipTime Clip time in ticks.
         * @return Relative (additive) local transform.
         */
        kMat4 sampleDeltaMatrix(const kString &boneName, float clipTime);

        // -------------------------------------------------------------------
        // Blending helpers (also useful standalone)
        // -------------------------------------------------------------------

        /**
         * @brief Layers an additive delta onto a base local transform.
         *
         * @c pos = basePos + weight*deltaPos
         * @c rot = baseRot * slerp(identity, deltaRot, weight)
         * @c scale = baseScale * mix(1, deltaScale, weight)
         *
         * @param base   Base local transform (the pose being added to).
         * @param delta  Additive delta (from sampleDeltaMatrix()).
         * @param weight Additive strength (1.0 = the authored delta).
         * @return The resulting local transform.
         */
        static kMat4 applyDelta(const kMat4 &base, const kMat4 &delta, float weight);

        /**
         * @brief Decomposes a transform into translation / rotation / scale.
         * @param m           Matrix to decompose.
         * @param outPos      Receives the translation.
         * @param outRot      Receives the rotation.
         * @param outScale    Receives the scale.
         */
        static void decomposeTRS(const kMat4 &m, kVec3 &outPos, kQuat &outRot, kVec3 &outScale);

        /**
         * @brief Composes a translation / rotation / scale matrix.
         * @param pos    Translation.
         * @param rot    Rotation.
         * @param scale  Scale.
         * @return The composed local transform.
         */
        static kMat4 composeTRS(const kVec3 &pos, const kQuat &rot, const kVec3 &scale);

    private:
        kSkeletalAnimation               *clip = nullptr; ///< Additive pose source.
        float                             referenceTime = 0.0f; ///< Time the reference was sampled at.
        bool                              built = false;  ///< True once a reference exists.
        std::unordered_map<kString, kMat4> reference;     ///< Bone name -> baked reference local transform.
    };
}

#endif // KADDITIVEANIMATION_H
