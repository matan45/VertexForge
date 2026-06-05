#pragma once
#include "../../data/EntityHandle.hpp"
#include <optional>

namespace services
{
    class IVegetationBrushModeService
    {
    public:
        virtual ~IVegetationBrushModeService() = default;

        virtual void registerEventHandlers() = 0;

        virtual bool activate() = 0;
        virtual void deactivate() = 0;
        virtual bool isActive() const = 0;

        virtual std::optional<EntityHandle> getTargetEntity() const = 0;
    };
}
