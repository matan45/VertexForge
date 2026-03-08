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
            glm::vec3 diff = inst.position - center;
            return glm::dot(diff, diff) <= radiusSq;
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
