#pragma once
#include "../../services/providers/animation/IAnimatorProvider.hpp"
#include <memory>

namespace controllers
{
    class AnimatorSystemController;
}

namespace core
{
    class AnimatorAdapter : public services::IAnimatorProvider
    {
    private:
        std::unique_ptr<controllers::AnimatorSystemController> controller;

    public:
        AnimatorAdapter();
        ~AnimatorAdapter() override;

        void setFloat(services::EntityHandle entity, const std::string& paramName, float value) override;
        void setInt(services::EntityHandle entity, const std::string& paramName, int32_t value) override;
        void setBool(services::EntityHandle entity, const std::string& paramName, bool value) override;
        void setTrigger(services::EntityHandle entity, const std::string& paramName) override;

        [[nodiscard]] float getFloat(services::EntityHandle entity, const std::string& paramName) const override;
        [[nodiscard]] int32_t getInt(services::EntityHandle entity, const std::string& paramName) const override;
        [[nodiscard]] bool getBool(services::EntityHandle entity, const std::string& paramName) const override;

        void play(services::EntityHandle entity) override;
        void pause(services::EntityHandle entity) override;
        void stop(services::EntityHandle entity) override;
        void reset(services::EntityHandle entity) override;

        [[nodiscard]] bool isPlaying(services::EntityHandle entity) const override;
        [[nodiscard]] bool isBlending(services::EntityHandle entity) const override;
        [[nodiscard]] std::string getCurrentState(services::EntityHandle entity) const override;
        [[nodiscard]] float getNormalizedTime(services::EntityHandle entity) const override;
        [[nodiscard]] bool hasAnimator(services::EntityHandle entity) const override;

        bool forceTransitionTo(services::EntityHandle entity, const std::string& stateName, float blendDuration) override;

        void setRootMotion(services::EntityHandle entity, bool enabled) override;
        [[nodiscard]] bool getRootMotion(services::EntityHandle entity) const override;
    };
}
