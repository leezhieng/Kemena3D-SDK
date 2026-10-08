/**
 * @file kanimator.h
 * @brief Drives playback of skeletal (bone) animation, and reserves a hook
 *        for future non-skeletal (object-transform) animation.
 */

#ifndef KANIMATOR_H
#define KANIMATOR_H

#include "kdatatype.h"
#include "kskelanimation.h"
#include "kbone.h"
#include "kmesh.h"

#include <glm/gtx/string_cast.hpp>
#include <unordered_map>

namespace kemena
{
    class kSkeletalAnimation;
    class kAnimation;
    class kMesh;
    class kAnimationMask;
    class kAdditiveAnimation;

    /**
     * @brief One weighted clip sample used by kAnimator::calculateBlendedBoneTransform().
     *
     * Blend-tree playback feeds an arbitrary set of these (all belonging to the
     * same skeleton) so the animator can blend every one of their poses in a
     * single hierarchy traversal.
     *
     * A sample supports three optional modifiers:
     *
     *   * @ref mask — **partial animation**. When set, only bones flagged active
     *     in the mask contribute this sample's pose; every other bone is left to
     *     the remaining samples (or the rest pose). This is how an upper-body
     *     clip is layered over a full-body one.
     *   * @ref additive — **additive animation**. When set, the sample no longer
     *     replaces the base pose: it contributes @c (pose - reference) * weight
     *     layered on top of the base pose (position/scale additive, rotation
     *     multiplicative). @ref animation must still name the clip to sample;
     *     @ref additive supplies the baked reference pose.
     *   * @ref restPose — **weighted masking**. When set, the sample contributes
     *     the skeleton's *rest* (bind) pose for the bones its @ref mask allows,
     *     instead of sampling a clip. @ref animation is ignored. Pairing a clip
     *     sample with mask weight @c w and a rest-pose sample with mask weight
     *     @c (1 - w) over the same mask makes @c w behave as an absolute "how
     *     strongly does this state drive these bones" control (w = 0 → the state
     *     leaves the region at rest, w = 1 → the clip fully drives it).
     */
    struct kPoseSample
    {
        kSkeletalAnimation *animation = nullptr;  ///< Clip to sample (ignored when @ref restPose).
        float               time      = 0.0f;     ///< Clip time in ticks.
        float               weight    = 0.0f;     ///< Blend weight (relative; normalized internally).
        const kAnimationMask   *mask     = nullptr; ///< Partial-blend mask (nullptr = whole skeleton).
        kAdditiveAnimation     *additive = nullptr; ///< When set, sample is blended additively.
        bool                    restPose = false;   ///< When set, contributes the rest pose (weighted masking).
    };

    /**
     * @brief Controls playback of animation clips.
     *
     * Today the animator only plays skeletal clips (kSkeletalAnimation): it
     * produces a flat array of final bone matrices that the renderer uploads
     * to the shader each frame via @ref getFinalBoneMatrices().
     *
     * Non-skeletal (object-transform) clips of type kAnimation are accepted
     * through @ref setObjectAnimation() / @ref playObjectAnimation() but are
     * not yet executed — the hooks are in place for the future cinematic
     * editor; per-object track sampling will land alongside it.
     */
    class kAnimator
    {
    public:
        /**
         * @brief Constructs an animator and sets the initial skeletal clip.
         * @param newAnimation Skeletal animation to start playing.
         */
        kAnimator(kSkeletalAnimation *newAnimation);

        /** @brief Registers an additional skeletal clip. */
        void addAnimation(kSkeletalAnimation *newAnimation);

        /**
         * @brief Advances the active animation by one step.
         * @param newDeltaTime Elapsed time since the last update in seconds.
         * @param frameId      Frame identifier — skips duplicate updates.
         */
        void updateAnimation(float newDeltaTime, int frameId);

        /** @brief Switches to a different skeletal clip and resets time. */
        void playAnimation(kSkeletalAnimation *animation);

        /** @brief Currently active skeletal clip. */
        kSkeletalAnimation *getCurrentAnimation();

        /** @brief Number of registered skeletal clips. */
        int getAnimationCount() const { return (int)animations.size(); }

        /** @brief Registered skeletal clip by index, or nullptr. */
        kSkeletalAnimation *getAnimation(int index) const
        {
            return (index >= 0 && index < (int)animations.size()) ? animations[index] : nullptr;
        }

        /** @brief Plays a registered clip by index and resets time. */
        void playAnimation(int index);

        /** @brief Current playback position in ticks. */
        float getCurrentTime() const { return currentTime; }

        /**
         * @brief Points the animator at the clip that feeds blend-tree root motion.
         *
         * A blend tree changes its root-motion source as the blend parameter moves
         * between clips. Unlike playAnimation(), this does NOT reset the clip time
         * or the root-motion tracker, so the accumulated delta keeps flowing and the
         * character never stalls. When the source clip changes, the per-frame delta
         * tracker is re-seeded to the new clip's root pose (so the switch itself
         * contributes no motion) while the baked start-of-play reference is
         * preserved (so the pose does not pop). The very first call seeds the
         * reference normally.
         *
         * @param clip     Clip whose root channels drive this frame (no-op if null).
         * @param clipTime Clip time in ticks at which to sample the root pose.
         */
        void setBlendRootSource(kSkeletalAnimation *clip, float clipTime);

        // --- Named controller variables -------------------------------------
        // The editor animator controller drives state transitions from named
        // variables (bool / float / int / trigger). Scripts can set them through
        // these methods; the editor samples getVariables() each step.

        /** @brief Raw variable access used by the editor controller. */
        void  setVariable(const kString &name, float value) { variables[name] = value; }

        /** @brief Reads a variable value (0.0f when absent). */
        float getVariable(const kString &name) const
        {
            auto it = variables.find(name);
            return it != variables.end() ? it->second : 0.0f;
        }

        /** @brief All named controller variables (editor transition evaluation). */
        const std::unordered_map<kString, float> &getVariables() const { return variables; }

        /** @brief Sets a bool variable (stored as 0/1). */
        void setBool(const kString &name, bool value)     { variables[name] = value ? 1.0f : 0.0f; }

        /** @brief Sets a float variable. */
        void setFloat(const kString &name, float value)   { variables[name] = value; }

        /** @brief Sets an integer variable (stored as a float). */
        void setInt(const kString &name, int value)       { variables[name] = (float)value; }

        /** @brief Fires a trigger variable (sets it to 1.0f). */
        void setTrigger(const kString &name)              { variables[name] = 1.0f; }

        /**
         * @brief Recursively computes bone transforms for the entire skeleton.
         * @param node            Current hierarchy node being processed.
         * @param parentTransform Accumulated world transform of the parent bone.
         */
        void calculateBoneTransform(const kNodeData *node, kMat4 parentTransform);

        /**
         * @brief Computes a weighted blend of several clips' poses in one pass.
         *
         * Recursively traverses @p node; for each joint it finds the matching
         * bone in every sample clip, samples it at that sample's time, then
         * blends the per-bone local transforms (position/scale lerp, rotation
         * hemisphere-aligned weighted accumulation + normalize). The blended
         * skeleton is written to finalBoneMatrices exactly like
         * calculateBoneTransform(). The caller supplies the samples every frame,
         * so no blend state is stored on the animator (ABI stays unchanged).
         *
         * @param samples Weighted clip samples (same skeleton, non-empty).
         * @param node    Current hierarchy node being processed.
         * @param parentTransform Accumulated world transform of the parent bone.
         */
        void calculateBlendedBoneTransform(const std::vector<kPoseSample> &samples,
                                           const kNodeData *node, kMat4 parentTransform);

        /** @brief Per-bone world matrices ready for shader upload. */
        const std::vector<kMat4> getFinalBoneMatrices() const;

        /** @brief Seeks to a specific time in the active clip (ticks). */
        void setCurrentTime(float newTime);

        /** @brief Sets the global playback speed multiplier. */
        void setSpeed(float newSpeed);

        /** @brief Current global playback speed multiplier. */
        float getSpeed();

        // -------------------------------------------------------------------
        // Cross-fade (smooth state transitions).
        //
        // Instead of instantly snapping to a new clip, the controller can
        // beginBlend() between two clips of the same skeleton. During the blend
        // calculateBoneTransform() samples both clips and interpolates the
        // per-bone local transforms (local-space blending), giving smooth,
        // stable transitions between animation states.
        // -------------------------------------------------------------------

        /**
         * @brief Starts a cross-fade from one clip to another.
         * @param from      Source clip (already playing). May be nullptr to just
         *                  switch instantly.
         * @param fromTicks Source clip time in ticks at the start of the blend.
         * @param to        Destination clip — becomes the active clip.
         * @param toTicks   Destination clip time in ticks at the start of the blend.
         * @param duration  Cross-fade duration in seconds (> 0 to blend).
         */
        void beginBlend(kSkeletalAnimation *from, float fromTicks,
                        kSkeletalAnimation *to, float toTicks, float duration);

        /**
         * @brief Starts a *partial* cross-fade that only blends a bone subset.
         *
         * Bones flagged active in @p mask cross-fade from the source clip to the
         * destination clip; every other bone follows the destination clip
         * immediately. This lets a transition touch, for example, only the upper
         * body while the lower body keeps running the destination locomotion.
         *
         * The mask is borrowed — the caller keeps ownership and must keep it
         * alive for the duration of the blend (pass nullptr for a full-body
         * cross-fade, identical to the other overload).
         *
         * @param from      Source clip (already playing). May be nullptr.
         * @param fromTicks Source clip time in ticks at the start of the blend.
         * @param to        Destination clip — becomes the active clip.
         * @param toTicks   Destination clip time in ticks at the start of the blend.
         * @param duration  Cross-fade duration in seconds (> 0 to blend).
         * @param mask      Bones allowed to blend (nullptr = all bones).
         */
        void beginBlend(kSkeletalAnimation *from, float fromTicks,
                        kSkeletalAnimation *to, float toTicks, float duration,
                        const kAnimationMask *mask);

        /**
         * @brief Sets (or clears) the mask used by the active cross-fade.
         * @param mask Bones allowed to blend; nullptr restores a full-body fade.
         */
        void setBlendMask(const kAnimationMask *mask) { blendMask = mask; }

        /** @brief Mask currently governing the cross-fade (nullptr = full body). */
        const kAnimationMask *getBlendMask() const { return blendMask; }

        /**
         * @brief Advances the active cross-fade by @p dt seconds.
         * @return True while the cross-fade is still running.
         */
        bool updateBlend(float dt);

        /** @brief True while a cross-fade is in progress. */
        bool isBlending() const { return blending; }

        /** @brief Seconds elapsed in the active cross-fade (diagnostics). */
        float getBlendElapsed() const { return blendElapsed; }

        /** @brief Current cross-fade blend factor in [0,1] (diagnostics). */
        float getBlendFactor() const { return blendFactor(); }

        /** @brief Source clip time in ticks during the active cross-fade (diagnostics). */
        float getBlendSourceTime() const { return blendFromTime; }

        /** @brief Immediately ends any active cross-fade (snaps to the destination clip). */
        void endBlend();

        // -------------------------------------------------------------------
        // Root-motion extraction.
        //
        // When the active clip's root-motion channels (kSkeletalAnimation:
        // setRootMotionRotation / setRootMotionPositionY / setRootMotionPositionXZ)
        // are enabled, the topmost animated bone is treated as the root bone.
        // Its motion for those channels is (a) accumulated into per-query deltas
        // below and (b) baked out of the pose so the character stays in place —
        // the script then applies the deltas to the GameObject / physics object.
        //
        // The deltas are returned in the root bone's local space. Rotation is
        // exposed as euler degrees (XYZ) to match the rest of the scripting API.
        // -------------------------------------------------------------------

        /** @brief Accumulated root-bone position delta since the last query (local space). */
        kVec3 getRootMotionDeltaPosition();

        /** @brief Accumulated root-bone rotation delta since the last query, in euler degrees (XYZ). */
        kVec3 getRootMotionDeltaRotation();

        /** @brief Clears accumulated root motion and re-seeds tracking (called on clip switches). */
        void resetRootMotion();

        /** @brief True when the active clip extracts the root bone's XZ translation. */
        bool getRootMotionPositionXZ() const;
        /** @brief True when the active clip extracts the root bone's Y translation. */
        bool getRootMotionPositionY() const;
        /** @brief True when the active clip extracts the root bone's rotation. */
        bool getRootMotionRotation() const;

        /** @brief True when the active clip has at least one root-motion channel enabled. */
        bool isRootMotionActive() const;

        /** @brief Name of the resolved root-motion bone (empty until the first pose is sampled). */
        const kString &getResolvedRootBoneName() const { return rootBoneName; }

        // -------------------------------------------------------------------
        // Object-transform animation (kAnimation) — placeholder.
        //
        // Reserved for the future cinematic editor. The setters accept and
        // remember a clip but updateAnimation() doesn't drive any object
        // transforms yet; that pass will land when the editor is wired up.
        // -------------------------------------------------------------------

        /** @brief Registers the active non-skeletal clip (no-op playback for now). */
        void setObjectAnimation(kAnimation *clip);

        /** @brief Returns the registered non-skeletal clip, or nullptr. */
        kAnimation *getObjectAnimation() const;

        // --- Blend shapes (morph targets) ------------------------------------
        // The animator is the natural owner of time-varying state, so it can
        // drive a character's blend shapes (facial expressions, visemes...) by
        // forwarding weights through to the mesh it is attached to. The mesh
        // remains the single storage location, which is what the renderer
        // uploads, so animator-driven and script-driven weights never diverge.

        /** @brief Sets the mesh whose morph weights this animator drives. */
        void setMorphMesh(kMesh *newMorphMesh) { morphMesh = newMorphMesh; }

        /** @brief Mesh whose morph weights this animator drives (may be nullptr). */
        kMesh *getMorphMesh() const { return morphMesh; }

        /** @brief Sets a morph weight by blend-shape name (forwarded to the mesh). */
        void setMorphWeight(const kString &name, float weight);

        /** @brief Sets a morph weight by slot (forwarded to the mesh). */
        void setMorphWeight(int slot, float weight);

        /** @brief Weight of a named blend shape (0 when unknown / no mesh). */
        float getMorphWeight(const kString &name) const;

        /** @brief Replaces every morph weight at once (forwarded to the mesh). */
        void setMorphWeights(const std::vector<float> &weights);

        /** @brief Current per-target morph weights (empty when no mesh is set). */
        std::vector<float> getMorphWeights() const;

    private:
        // Skeletal playback state.
        std::vector<kMat4>               finalBoneMatrices;             ///< Per-bone matrices.
        kSkeletalAnimation              *currentAnimation = nullptr;    ///< Active skeletal clip.
        std::vector<kSkeletalAnimation *> animations;                   ///< Registered skeletal clips.

        // Non-skeletal placeholder.
        kAnimation                      *objectAnimation = nullptr;       ///< Registered non-skeletal clip (not yet driven).

        kMesh                           *morphMesh = nullptr;             ///< Mesh whose blend shapes this animator drives.

        // Named controller variables (bool/float/int/trigger stored as floats).
        std::unordered_map<kString, float> variables;                     ///< Controller variable values.

        // Playback time / pacing.
        float currentTime    = 0.0f;                                      ///< Current playback position in ticks.
        float deltaTime      = 0.0f;                                      ///< Last elapsed time in seconds.
        float speed          = 1.0f;                                      ///< Global playback speed multiplier.
        int   currentFrameId = -1;                                        ///< Last processed frame id (guards duplicate updates).

        // Cross-fade (state transition) state.
        kSkeletalAnimation *blendFromAnimation = nullptr;                 ///< Source clip during a cross-fade.
        const kAnimationMask *blendMask = nullptr;                        ///< Bones allowed to blend (nullptr = all).
        float blendFromTime     = 0.0f;                                   ///< Source clip time in ticks.
        float blendFromTps      = 0.0f;                                   ///< Source clip ticks-per-second (advances the fade).
        float blendFromDuration = 0.0f;                                   ///< Source clip duration in ticks (loop wrap).
        float blendDuration     = 0.0f;                                   ///< Cross-fade duration in seconds.
        float blendElapsed      = 0.0f;                                   ///< Seconds elapsed in the cross-fade.
        bool  blending          = false;                                  ///< True while a cross-fade is active.

        // Root-motion extraction state. The root bone is the topmost animated
        // node of the active clip; only tracked while a channel is enabled.
        kVec3                       rootMotionAccumPos      = kVec3(0.0f);       ///< Accumulated position delta since last query.
        kVec3                       rootMotionAccumRotEuler = kVec3(0.0f);       ///< Accumulated rotation delta (degrees) since last query.
        kVec3                       lastRootPos             = kVec3(0.0f);       ///< Root position at the previous pose sample.
        kQuat                       lastRootRot             = kQuat(1.0f, 0, 0, 0); ///< Root rotation at the previous pose sample.
        float                       lastRootTime            = 0.0f;              ///< Clip time of the previous pose sample (loop detection).
        bool                        rootMotionInitialized   = false;             ///< True once the first pose has been sampled.
        kString                     rootBoneName;                                ///< Name of the resolved root-motion bone.
        kSkeletalAnimation         *rootBoneForAnim         = nullptr;           ///< Clip the root bone was resolved for.
        bool                        rootBoneResolved        = false;             ///< True once the root bone has been resolved.
        kVec3                       bakeRootPos             = kVec3(0.0f);       ///< Root position baked into the pose (start-of-play reference).
        kQuat                       bakeRootRot             = kQuat(1.0f, 0, 0, 0); ///< Root rotation baked into the pose.

        /** @brief Resolves the topmost animated node as the root-motion bone. */
        void resolveRootBone();
        /** @brief Resolves the root-motion bone for a specific clip (used while cross-fading). */
        void resolveRootBoneFor(kSkeletalAnimation *anim);
        /** @brief Recursive helper for resolveRootBone() / resolveRootBoneFor(). */
        void findRootBoneRecursive(const kNodeData *node, kSkeletalAnimation *anim);
        /** @brief True when the active clip has at least one root-motion channel enabled. */
        bool rootMotionActive() const;
        /** @brief Accumulates the per-frame root-motion delta and bakes the enabled channels out of the pose. */
        void handleRootMotion(kBone *bone, kMat4 &nodeTransform);

        /** @brief Current cross-fade blend factor in [0,1] (smoothstepped). */
        float blendFactor() const;
    };
}

#endif // KANIMATOR_H
