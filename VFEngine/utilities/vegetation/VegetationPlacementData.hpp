#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace vegetation
{
    struct VegetationInstance
    {
        glm::vec3 position{0.0f};
        float rotation = 0.0f;        // Y-axis rotation in radians
        float scale = 1.0f;
        uint32_t speciesId = 0;
    };

    struct VegetationPlacementData
    {
        std::vector<VegetationInstance> instances;

        [[nodiscard]] bool isEmpty() const { return instances.empty(); }
        [[nodiscard]] size_t getInstanceCount() const { return instances.size(); }
        [[nodiscard]] const std::vector<VegetationInstance>& getInstances() const { return instances; }

        void addInstance(const VegetationInstance& instance);
        void removeInstancesInRadius(const glm::vec3& center, float radius);

        // Remove all instances of a given species
        void removeInstancesBySpecies(uint32_t speciesId);

        void clear();
    };
}
