#pragma once
#include "../../../graphics/render/gpudriven/scene/GPUObjectStreamTypes.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <entt/entt.hpp>

// VK-1594: taken by const reference only; the adapter includes the definition.
namespace world { struct HLODFileData; }

namespace services
{
    class IObjectStreamingProvider
    {
    public:
        virtual ~IObjectStreamingProvider() = default;

        virtual void setObjectStreamingEnabled(bool enabled) = 0;
        virtual void setObjectStreamingConfig(const render::gpudriven::ObjectStreamConfig& config) = 0;
        virtual render::gpudriven::ObjectStreamConfig getObjectStreamingConfig() const = 0;
        virtual render::gpudriven::ObjectStreamingStats getObjectStreamingStats() const = 0;
        virtual void registerSectorObjects(uint32_t sectorId,
                                           const std::vector<std::pair<uint64_t, entt::entity>>& entities) = 0;
        virtual void unregisterSectorObjects(uint32_t sectorId) = 0;

        // VK-1594: upload a baked HLOD proxy's geometry under a synthetic mesh key so it draws
        // through the ordinary MeshComponent path. Returns false if the GPU-driven renderer is
        // not available, in which case the proxy simply stays invisible.
        virtual bool registerHLODMesh(const std::string& meshKey, const ::world::HLODFileData& data) = 0;
        virtual void releaseHLODMesh(const std::string& meshKey) = 0;
    };
}
