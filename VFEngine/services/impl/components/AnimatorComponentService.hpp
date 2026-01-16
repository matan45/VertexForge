#pragma once

#include "../../data/EntityHandle.hpp"
#include <string>

namespace events { class EventDispatcher; }

namespace services
{
    /**
     * AnimatorComponentService handles runtime animator operations for entities.
     * Provides CQRS event handlers for animator parameter control and state queries.
     */
    class AnimatorComponentService
    {
    public:
        AnimatorComponentService() = default;

        // Parameter setters
        void setFloat(EntityHandle entity, const std::string& paramName, float value);
        void setInt(EntityHandle entity, const std::string& paramName, int32_t value);
        void setBool(EntityHandle entity, const std::string& paramName, bool value);
        void setTrigger(EntityHandle entity, const std::string& paramName);

        // Parameter getters
        float getFloat(EntityHandle entity, const std::string& paramName) const;
        int32_t getInt(EntityHandle entity, const std::string& paramName) const;
        bool getBool(EntityHandle entity, const std::string& paramName) const;

        // Playback control
        void play(EntityHandle entity);
        void pause(EntityHandle entity);
        void stop(EntityHandle entity);
        void reset(EntityHandle entity);

        // State queries
        bool isPlaying(EntityHandle entity) const;
        bool isBlending(EntityHandle entity) const;
        std::string getCurrentState(EntityHandle entity) const;
        float getNormalizedTime(EntityHandle entity) const;
        bool hasAnimator(EntityHandle entity) const;

        // Transition control
        bool forceTransitionTo(EntityHandle entity, const std::string& stateName, float blendDuration);

        // Event handler registration
        void registerEventHandlers(::events::EventDispatcher& dispatcher);
    };
}
