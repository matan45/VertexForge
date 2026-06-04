#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../data/OceanData.hpp"
#include <optional>

namespace services
{
    class IOceanService
    {
    public:
        virtual ~IOceanService() = default;

        virtual void registerEventHandlers() = 0;

        virtual EntityHandle createOcean(const OceanCreationData& config) = 0;
        virtual bool deleteOcean(EntityHandle oceanEntity) = 0;
        virtual std::optional<OceanData> getOceanData(EntityHandle entity) const = 0;
        virtual bool hasOceanComponent(EntityHandle entity) const = 0;
    };
}
