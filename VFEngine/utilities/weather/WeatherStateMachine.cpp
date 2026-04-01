#include "WeatherStateMachine.hpp"
#include <math/EasingFunctions.hpp>
#include <algorithm>

namespace weather
{
    void WeatherStateMachine::setImmediate(const WeatherState& state)
    {
        currentState = state;
        transitioning = false;
        transitionElapsed = 0.0f;
        transitionDuration = 0.0f;
        queuedTransition.reset();
    }

    void WeatherStateMachine::transitionTo(const WeatherState& target, float duration, WeatherEasing easing)
    {
        WeatherTransition transition;
        transition.targetState = target;
        transition.duration = duration;
        transition.easing = easing;
        startTransition(transition);
    }

    void WeatherStateMachine::queueTransition(const WeatherState& target, float duration, WeatherEasing easing)
    {
        if (!transitioning)
        {
            transitionTo(target, duration, easing);
            return;
        }

        WeatherTransition transition;
        transition.targetState = target;
        transition.duration = duration;
        transition.easing = easing;
        queuedTransition = transition;
    }

    void WeatherStateMachine::update(float deltaTime)
    {
        if (!transitioning)
            return;

        transitionElapsed += deltaTime;
        float rawT = std::clamp(transitionElapsed / transitionDuration, 0.0f, 1.0f);

        // Map WeatherEasing to UIEasingFunction for the shared evaluateEasing utility
        components::UIEasingFunction easingFunc = components::UIEasingFunction::Linear;
        if (easingType == WeatherEasing::EaseInOut)
            easingFunc = components::UIEasingFunction::EaseInOut;

        float t = math::evaluateEasing(easingFunc, rawT);

        currentState = lerpWeatherState(sourceState, targetState, t);

        if (rawT >= 1.0f)
        {
            currentState = targetState;
            transitioning = false;

            if (queuedTransition.has_value())
            {
                WeatherTransition next = queuedTransition.value();
                queuedTransition.reset();
                startTransition(next);
            }
        }
    }

    float WeatherStateMachine::getTransitionProgress() const
    {
        if (!transitioning || transitionDuration <= 0.0f)
            return transitioning ? 0.0f : 1.0f;
        return std::clamp(transitionElapsed / transitionDuration, 0.0f, 1.0f);
    }

    void WeatherStateMachine::startTransition(const WeatherTransition& transition)
    {
        sourceState = currentState;
        targetState = transition.targetState;
        transitionDuration = std::max(transition.duration, 0.01f);
        transitionElapsed = 0.0f;
        easingType = transition.easing;
        transitioning = true;
    }
}
