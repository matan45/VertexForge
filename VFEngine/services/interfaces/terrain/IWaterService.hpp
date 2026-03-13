#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../data/WaterData.hpp"
#include <optional>
#include <string>

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

        virtual bool addTile(EntityHandle waterEntity, int32_t tileX, int32_t tileZ) = 0;
        virtual bool removeTile(EntityHandle waterEntity, int32_t tileX, int32_t tileZ) = 0;

        virtual bool saveWater(EntityHandle waterEntity, const std::string& path) = 0;
        virtual EntityHandle loadWater(const std::string& path) = 0;
    };
}
