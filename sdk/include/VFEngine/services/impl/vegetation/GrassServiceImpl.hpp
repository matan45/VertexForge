#pragma once
#include "../../interfaces/vegetation/IGrassService.hpp"
#include "../../data/EntityHandle.hpp"
#include "vegetation/GrassConfig.hpp"
#include "vegetation/VegetationScatterTypes.hpp"

namespace services
{
    class GrassServiceImpl : public IGrassService
    {
    public:
        GrassServiceImpl() = default;
        ~GrassServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void setGrassConfig(EntityHandle entityId, const vegetation::GrassRenderConfig& config);
        vegetation::GrassRenderConfig getGrassConfig(EntityHandle entityId) const;
        void setGlobalGrassConfig(const vegetation::GrassRenderConfig& config);
        vegetation::GrassRenderConfig getGlobalGrassConfig() const;

        // VK-1581 scatter profile on the (find-or-created) global GrassComponent.
        void setGlobalScatterProfile(const vegetation::ScatterProfile& profile);
        vegetation::ScatterProfile getGlobalScatterProfile() const;
    };
}
