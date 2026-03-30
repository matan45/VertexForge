#pragma once
#include <entt/entt.hpp>
#include "../../services/data/AnimatorDebugTypes.hpp"
#include <string>
#include <cstdint>

namespace controllers
{
    class AnimatorSystemController
    {
    public:
        void init();
        void cleanUp();

        void setFloat(entt::entity entity, const std::string& paramName, float value);
        void setInt(entt::entity entity, const std::string& paramName, int32_t value);
        void setBool(entt::entity entity, const std::string& paramName, bool value);
        void setTrigger(entt::entity entity, const std::string& paramName);

        [[nodiscard]] float getFloat(entt::entity entity, const std::string& paramName) const;
        [[nodiscard]] int32_t getInt(entt::entity entity, const std::string& paramName) const;
        [[nodiscard]] bool getBool(entt::entity entity, const std::string& paramName) const;

        void play(entt::entity entity);
        void pause(entt::entity entity);
        void stop(entt::entity entity);
        void reset(entt::entity entity);

        [[nodiscard]] bool isPlaying(entt::entity entity) const;
        [[nodiscard]] bool isBlending(entt::entity entity) const;
        [[nodiscard]] std::string getCurrentState(entt::entity entity) const;
        [[nodiscard]] float getNormalizedTime(entt::entity entity) const;
        [[nodiscard]] bool hasAnimator(entt::entity entity) const;

        bool forceTransitionTo(entt::entity entity, const std::string& stateName, float blendDuration);

        void setRootMotion(entt::entity entity, bool enabled);
        [[nodiscard]] bool getRootMotion(entt::entity entity) const;

        // Debug
        [[nodiscard]] services::AnimatorRuntimeDebugData getDebugData(entt::entity entity, uint32_t layerIndex) const;

        // Layer management
        void setLayerWeight(entt::entity entity, uint32_t layerIndex, float weight);
        [[nodiscard]] float getLayerWeight(entt::entity entity, uint32_t layerIndex) const;
        [[nodiscard]] uint32_t getLayerCount(entt::entity entity) const;
        [[nodiscard]] std::string getLayerName(entt::entity entity, uint32_t layerIndex) const;
    };
}
