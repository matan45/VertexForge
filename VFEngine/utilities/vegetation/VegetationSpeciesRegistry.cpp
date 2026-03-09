#include "VegetationSpeciesRegistry.hpp"

namespace vegetation
{
    uint32_t VegetationSpeciesRegistry::addSpecies(const VegetationSpeciesConfig& config)
    {
        uint32_t id = nextId++;
        species[id] = config;
        return id;
    }

    void VegetationSpeciesRegistry::removeSpecies(uint32_t speciesId)
    {
        species.erase(speciesId);
    }

    void VegetationSpeciesRegistry::updateSpecies(uint32_t speciesId, const VegetationSpeciesConfig& config)
    {
        auto it = species.find(speciesId);
        if (it != species.end()) {
            it->second = config;
        }
    }

    const VegetationSpeciesConfig* VegetationSpeciesRegistry::getSpecies(uint32_t speciesId) const
    {
        auto it = species.find(speciesId);
        if (it != species.end()) {
            return &it->second;
        }
        return nullptr;
    }

    bool VegetationSpeciesRegistry::hasSpecies(uint32_t speciesId) const
    {
        return species.contains(speciesId);
    }

    const std::unordered_map<uint32_t, VegetationSpeciesConfig>& VegetationSpeciesRegistry::getAllSpecies() const
    {
        return species;
    }

    uint32_t VegetationSpeciesRegistry::getSpeciesCount() const
    {
        return static_cast<uint32_t>(species.size());
    }

    void VegetationSpeciesRegistry::clear()
    {
        species.clear();
        nextId = 1;
    }
}
