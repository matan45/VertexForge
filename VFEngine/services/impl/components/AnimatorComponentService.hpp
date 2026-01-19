#pragma once

#include "../../data/EntityHandle.hpp"
#include <string>

namespace events { class EventDispatcher; }

namespace services
{
    class IAnimatorProvider;

    class AnimatorComponentService
    {
    private:
        IAnimatorProvider* animatorProvider;

    public:
        explicit AnimatorComponentService(IAnimatorProvider* provider);

        void setFloat(EntityHandle entity, const std::string& paramName, float value);
        void setInt(EntityHandle entity, const std::string& paramName, int32_t value);
        void setBool(EntityHandle entity, const std::string& paramName, bool value);
        void setTrigger(EntityHandle entity, const std::string& paramName);

        float getFloat(EntityHandle entity, const std::string& paramName) const;
        int32_t getInt(EntityHandle entity, const std::string& paramName) const;
        bool getBool(EntityHandle entity, const std::string& paramName) const;

        void play(EntityHandle entity);
        void pause(EntityHandle entity);
        void stop(EntityHandle entity);
        void reset(EntityHandle entity);

        bool isPlaying(EntityHandle entity) const;
        bool isBlending(EntityHandle entity) const;
        std::string getCurrentState(EntityHandle entity) const;
        float getNormalizedTime(EntityHandle entity) const;
        bool hasAnimator(EntityHandle entity) const;

        bool forceTransitionTo(EntityHandle entity, const std::string& stateName, float blendDuration);

        void registerEventHandlers(::events::EventDispatcher& dispatcher);
    };
}
