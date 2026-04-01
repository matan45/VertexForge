#pragma once

#include "WeatherTypes.hpp"
#include <optional>

namespace weather
{
    class WeatherStateMachine
    {
    public:
        void setImmediate(const WeatherState& state);
        void transitionTo(const WeatherState& target, float duration, WeatherEasing easing = WeatherEasing::EaseInOut);
        void queueTransition(const WeatherState& target, float duration, WeatherEasing easing = WeatherEasing::EaseInOut);
        void update(float deltaTime);

        const WeatherState& getCurrentState() const { return currentState; }
        bool isTransitioning() const { return transitioning; }
        float getTransitionProgress() const;

    private:
        void startTransition(const WeatherTransition& transition);

        WeatherState currentState;
        WeatherState sourceState;
        WeatherState targetState;
        float transitionDuration = 0.0f;
        float transitionElapsed = 0.0f;
        WeatherEasing easingType = WeatherEasing::Linear;
        bool transitioning = false;

        std::optional<WeatherTransition> queuedTransition;
    };
}
