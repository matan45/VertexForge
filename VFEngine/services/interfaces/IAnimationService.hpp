#pragma once
#include "../data/EntityHandle.hpp"
#include <string>
#include <vector>
#include <optional>

namespace services {
    
    struct AnimationClipInfo {
        std::string name;
        float duration = 0.0f;      // Duration in seconds
        bool isLooping = false;
        float ticksPerSecond = 30.0f;
    };
    
    struct AnimatorData {
        std::string skeletonPath;           
        std::vector<std::string> clipPaths;
        std::string defaultClip;            // Name of default animation
        bool playOnStart = true;
        float speed = 1.0f;
    };

   
    struct AnimationState {
        std::string currentClip;
        float progress = 0.0f;      // 0.0 - 1.0
        float time = 0.0f;          // Current time in seconds
        bool isPlaying = false;
        bool isPaused = false;
        float speed = 1.0f;
    };
    
    struct BlendParams {
        float blendTime = 0.2f;     // Duration of blend in seconds
        bool crossFade = true;      // Cross-fade vs. snap
    };
    
    class IAnimationService {
    public:
        virtual ~IAnimationService() = default;

        // === Animator Component ===
        
        virtual void addAnimator(EntityHandle entity, const AnimatorData& data) = 0;
        
        virtual void removeAnimator(EntityHandle entity) = 0;
        
        virtual bool hasAnimator(EntityHandle entity) const = 0;
        
        virtual std::optional<AnimationState> getAnimationState(EntityHandle entity) const = 0;

        // === Animation Playback ===
        
        virtual bool playAnimation(EntityHandle entity, const std::string& clipName,
                                   bool loop = false) = 0;

        virtual void stopAnimation(EntityHandle entity) = 0;
        
        virtual void pauseAnimation(EntityHandle entity) = 0;

        virtual void resumeAnimation(EntityHandle entity) = 0;

        // === Animation Control ===
        
        virtual void setAnimationSpeed(EntityHandle entity, float speed) = 0;
        
        virtual void seekAnimation(EntityHandle entity, float time) = 0;
        
        virtual float getAnimationProgress(EntityHandle entity) const = 0;

        // === Blending ===
        
        virtual void blendTo(EntityHandle entity, const std::string& clipName,
                             const BlendParams& params = {}) = 0;
        
        virtual void crossFade(EntityHandle entity, const std::string& clipName,
                               float blendTime = 0.2f) = 0;

        // === Animation Queries ===
        
        virtual std::vector<AnimationClipInfo> getAvailableClips(EntityHandle entity) const = 0;
        
        virtual bool hasAnimation(EntityHandle entity, const std::string& clipName) const = 0;
        
        virtual float getAnimationDuration(EntityHandle entity, const std::string& clipName) const = 0;

        // === System Update ===
        
        virtual void updateAnimations(float deltaTime) = 0;
    };

}
