#include "ObjectStreamingAdapter.hpp"
#include "../../controllers/OffScreen.hpp"

namespace core::adapters
{
    void ObjectStreamingAdapter::setObjectStreamingConfig(const render::gpudriven::ObjectStreamConfig& config)
    {
        if (offScreen)
        {
            offScreen->setObjectStreamingConfig(config);
        }
    }

    render::gpudriven::ObjectStreamConfig ObjectStreamingAdapter::getObjectStreamingConfig() const
    {
        if (offScreen)
        {
            return offScreen->getObjectStreamingConfig();
        }
        return {};
    }

    render::gpudriven::ObjectStreamingStats ObjectStreamingAdapter::getObjectStreamingStats() const
    {
        if (offScreen)
        {
            return offScreen->getObjectStreamingStats();
        }
        return {};
    }

    void ObjectStreamingAdapter::registerSectorObjects(
        uint32_t sectorId,
        const std::vector<std::pair<uint64_t, entt::entity>>& entities)
    {
        if (offScreen)
        {
            offScreen->registerSectorObjects(sectorId, entities);
        }
    }

    void ObjectStreamingAdapter::unregisterSectorObjects(uint32_t sectorId)
    {
        if (offScreen)
        {
            offScreen->unregisterSectorObjects(sectorId);
        }
    }
}
