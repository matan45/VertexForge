#pragma once
#include "../data/EntityHandle.hpp"
#include <string>
#include <vector>
#include <optional>

namespace services {

    /**
     * @brief Information about an animation clip.
     */
    struct AnimationClipInfo {
        std::string name;
        float duration = 0.0f;      // Duration in seconds
        bool isLooping = false;
        float ticksPerSecond = 30.0f;
    };

    /**
     * @brief Data for animator component.
     */
    struct AnimatorData {
        std::string skeletonPath;           // Path to skeleton/rig
        std::vector<std::string> clipPaths; // Paths to animation clips
        std::string defaultClip;            // Name of default animation
        bool playOnStart = true;
        float speed = 1.0f;
    };

    /**
     * @brief Current state of an animation.
     */
    struct AnimationState {
        std::string currentClip;
        float progress = 0.0f;      // 0.0 - 1.0
        float time = 0.0f;          // Current time in seconds
        bool isPlaying = false;
        bool isPaused = false;
        float speed = 1.0f;
    };

    /**
     * @brief Blend parameters for animation transitions.
     */
    struct BlendParams {
        float blendTime = 0.2f;     // Duration of blend in seconds
        bool crossFade = true;      // Cross-fade vs. snap
    };

    /**
     * @brief Service interface for animation playback operations.
     *
     * This service provides high-level APIs for skeletal animation
     * playback and blending.
     *
     * NOTE: This is an interface stub. Implementations will be added
     * when the animation system is fully integrated.
     */
    class IAnimationService {
    public:
        virtual ~IAnimationService() = default;

        // === Animator Component ===

        /**
         * @brief Add an animator component to an entity.
         */
        virtual void addAnimator(EntityHandle entity, const AnimatorData& data) = 0;

        /**
         * @brief Remove animator from an entity.
         */
        virtual void removeAnimator(EntityHandle entity) = 0;

        /**
         * @brief Check if entity has an animator.
         */
        virtual bool hasAnimator(EntityHandle entity) const = 0;

        /**
         * @brief Get current animation state.
         */
        virtual std::optional<AnimationState> getAnimationState(EntityHandle entity) const = 0;

        // === Animation Playback ===

        /**
         * @brief Play an animation by name.
         * @param entity Target entity
         * @param clipName Name of the animation clip
         * @param loop Whether to loop the animation
         * @return true if animation started successfully
         */
        virtual bool playAnimation(EntityHandle entity, const std::string& clipName,
                                   bool loop = false) = 0;

        /**
         * @brief Stop the current animation.
         */
        virtual void stopAnimation(EntityHandle entity) = 0;

        /**
         * @brief Pause the current animation.
         */
        virtual void pauseAnimation(EntityHandle entity) = 0;

        /**
         * @brief Resume a paused animation.
         */
        virtual void resumeAnimation(EntityHandle entity) = 0;

        // === Animation Control ===

        /**
         * @brief Set animation playback speed.
         * @param speed Multiplier (1.0 = normal, 2.0 = double speed, 0.5 = half speed)
         */
        virtual void setAnimationSpeed(EntityHandle entity, float speed) = 0;

        /**
         * @brief Seek to a specific time in the animation.
         * @param time Time in seconds
         */
        virtual void seekAnimation(EntityHandle entity, float time) = 0;

        /**
         * @brief Get current animation progress (0.0 - 1.0).
         */
        virtual float getAnimationProgress(EntityHandle entity) const = 0;

        // === Blending ===

        /**
         * @brief Blend to a new animation.
         * @param entity Target entity
         * @param clipName Name of the target animation
         * @param params Blend parameters
         */
        virtual void blendTo(EntityHandle entity, const std::string& clipName,
                             const BlendParams& params = {}) = 0;

        /**
         * @brief Cross-fade between current and target animation.
         * @param blendTime Duration of the cross-fade
         */
        virtual void crossFade(EntityHandle entity, const std::string& clipName,
                               float blendTime = 0.2f) = 0;

        // === Animation Queries ===

        /**
         * @brief Get list of available animation clips for an entity.
         */
        virtual std::vector<AnimationClipInfo> getAvailableClips(EntityHandle entity) const = 0;

        /**
         * @brief Check if a specific animation is available.
         */
        virtual bool hasAnimation(EntityHandle entity, const std::string& clipName) const = 0;

        /**
         * @brief Get duration of an animation clip.
         */
        virtual float getAnimationDuration(EntityHandle entity, const std::string& clipName) const = 0;

        // === System Update ===

        /**
         * @brief Update all animations (called once per frame).
         */
        virtual void updateAnimations(float deltaTime) = 0;
    };

}
