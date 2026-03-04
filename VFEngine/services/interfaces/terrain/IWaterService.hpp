#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../data/WaterData.hpp"
#include <optional>

namespace services
{
    class IWaterService
    {
    public:
        virtual ~IWaterService() = default;

        virtual void registerEventHandlers() = 0;

        virtual EntityHandle createWater(const WaterCreationData& config) = 0;
        virtual bool deleteWater(EntityHandle waterEntity) = 0;
        virtual std::optional<WaterData> getWaterData(EntityHandle entity) const = 0;
        virtual bool hasWaterComponent(EntityHandle entity) const = 0;
    };
}
