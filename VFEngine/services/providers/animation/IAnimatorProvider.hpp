#pragma once
#include "../../data/EntityHandle.hpp"
#include <string>
#include <cstdint>

namespace services
{
    class IAnimatorProvider
    {
    public:
        virtual ~IAnimatorProvider() = default;

        virtual void setFloat(EntityHandle entity, const std::string& paramName, float value) = 0;
        virtual void setInt(EntityHandle entity, const std::string& paramName, int32_t value) = 0;
        virtual void setBool(EntityHandle entity, const std::string& paramName, bool value) = 0;
        virtual void setTrigger(EntityHandle entity, const std::string& paramName) = 0;

        [[nodiscard]] virtual float getFloat(EntityHandle entity, const std::string& paramName) const = 0;
        [[nodiscard]] virtual int32_t getInt(EntityHandle entity, const std::string& paramName) const = 0;
        [[nodiscard]] virtual bool getBool(EntityHandle entity, const std::string& paramName) const = 0;

        virtual void play(EntityHandle entity) = 0;
        virtual void pause(EntityHandle entity) = 0;
        virtual void stop(EntityHandle entity) = 0;
        virtual void reset(EntityHandle entity) = 0;

        [[nodiscard]] virtual bool isPlaying(EntityHandle entity) const = 0;
        [[nodiscard]] virtual bool isBlending(EntityHandle entity) const = 0;
        [[nodiscard]] virtual std::string getCurrentState(EntityHandle entity) const = 0;
        [[nodiscard]] virtual float getNormalizedTime(EntityHandle entity) const = 0;
        [[nodiscard]] virtual bool hasAnimator(EntityHandle entity) const = 0;

        virtual bool forceTransitionTo(EntityHandle entity, const std::string& stateName, float blendDuration) = 0;

        virtual void setRootMotion(EntityHandle entity, bool enabled) = 0;
        [[nodiscard]] virtual bool getRootMotion(EntityHandle entity) const = 0;
    };
}
