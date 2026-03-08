#pragma once
#include "../../interfaces/vegetation/IGrassService.hpp"
#include "../../data/EntityHandle.hpp"
#include "vegetation/GrassConfig.hpp"

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
    };
}
