#pragma once
#include "../../../graphics/render/gpudriven/scene/GPUObjectStreamTypes.hpp"
#include <cstdint>
#include <vector>
#include <utility>
#include <entt/entt.hpp>

namespace services
{
    class IObjectStreamingProvider
    {
    public:
        virtual ~IObjectStreamingProvider() = default;

        virtual void setObjectStreamingConfig(const render::gpudriven::ObjectStreamConfig& config) = 0;
        virtual render::gpudriven::ObjectStreamConfig getObjectStreamingConfig() const = 0;
        virtual render::gpudriven::ObjectStreamingStats getObjectStreamingStats() const = 0;
        virtual void registerSectorObjects(uint32_t sectorId,
                                           const std::vector<std::pair<uint64_t, entt::entity>>& entities) = 0;
        virtual void unregisterSectorObjects(uint32_t sectorId) = 0;
    };
}
