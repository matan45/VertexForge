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

        struct BudgetStats
        {
            uint32_t totalAnimators = 0;
            uint32_t culledEntities = 0;
            uint32_t lodCounts[4] = {0, 0, 0, 0};
            uint32_t pendingStreamingInits = 0;
        };
        [[nodiscard]] virtual BudgetStats getBudgetStats() const = 0;

        struct LODConfig
        {
            float lod0Distance = 25.0f;
            float lod1Distance = 75.0f;
            float lod2Distance = 150.0f;
            float lod3Distance = 300.0f;
            uint32_t lod0Interval = 1;
            uint32_t lod1Interval = 2;
            uint32_t lod2Interval = 6;
            uint32_t maxStreamingInitPerFrame = 4;
        };
        [[nodiscard]] virtual LODConfig getLODConfig() const = 0;
        virtual void setLODConfig(const LODConfig& config) = 0;

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

        // Layer management
        virtual void setLayerWeight(EntityHandle entity, uint32_t layerIndex, float weight) = 0;
        [[nodiscard]] virtual float getLayerWeight(EntityHandle entity, uint32_t layerIndex) const = 0;
        [[nodiscard]] virtual uint32_t getLayerCount(EntityHandle entity) const = 0;
        [[nodiscard]] virtual std::string getLayerName(EntityHandle entity, uint32_t layerIndex) const = 0;
    };
}
