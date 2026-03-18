#pragma once
#include "../../../services/providers/render/IObjectStreamingProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core::adapters
{
    class ObjectStreamingAdapter : public services::IObjectStreamingProvider
    {
    private:
        controllers::OffScreen* offScreen = nullptr;

    public:
        ObjectStreamingAdapter() = default;
        ~ObjectStreamingAdapter() override = default;

        void setOffScreenController(controllers::OffScreen* controller) { offScreen = controller; }

        void setObjectStreamingConfig(const render::gpudriven::ObjectStreamConfig& config) override;
        render::gpudriven::ObjectStreamConfig getObjectStreamingConfig() const override;
        render::gpudriven::ObjectStreamingStats getObjectStreamingStats() const override;
        void registerSectorObjects(uint32_t sectorId,
                                   const std::vector<std::pair<uint64_t, entt::entity>>& entities) override;
        void unregisterSectorObjects(uint32_t sectorId) override;
    };
}
