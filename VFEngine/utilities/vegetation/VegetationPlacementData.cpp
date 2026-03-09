#include "VegetationPlacementData.hpp"
#include <algorithm>

namespace vegetation
{
    void VegetationPlacementData::addInstance(const VegetationInstance& instance)
    {
        instances.push_back(instance);
    }

    void VegetationPlacementData::removeInstancesInRadius(const glm::vec3& center, float radius)
    {
        float radiusSq = radius * radius;
        std::erase_if(instances, [&](const VegetationInstance& inst)
        {
            // Compare in XZ only - instances are placed with Y=0
            float dx = inst.position.x - center.x;
            float dz = inst.position.z - center.z;
            return (dx * dx + dz * dz) <= radiusSq;
        });
    }

    void VegetationPlacementData::removeInstancesBySpecies(uint32_t speciesId)
    {
        std::erase_if(instances, [speciesId](const VegetationInstance& inst)
        {
            return inst.speciesId == speciesId;
        });
    }

    void VegetationPlacementData::clear()
    {
        instances.clear();
    }
}
