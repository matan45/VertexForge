#include "VegetationAdapter.hpp"

namespace core::adapters
{
    VegetationAdapter::VegetationAdapter() = default;

    uint32_t VegetationAdapter::addSpecies(const vegetation::VegetationSpeciesConfig& config)
    {
        return registry.addSpecies(config);
    }

    void VegetationAdapter::removeSpecies(uint32_t speciesId)
    {
        registry.removeSpecies(speciesId);
    }

    void VegetationAdapter::updateSpecies(uint32_t speciesId, const vegetation::VegetationSpeciesConfig& config)
    {
        registry.updateSpecies(speciesId, config);
    }

    const vegetation::VegetationSpeciesConfig* VegetationAdapter::getSpecies(uint32_t speciesId) const
    {
        return registry.getSpecies(speciesId);
    }

    bool VegetationAdapter::hasSpecies(uint32_t speciesId) const
    {
        return registry.hasSpecies(speciesId);
    }

    const std::unordered_map<uint32_t, vegetation::VegetationSpeciesConfig>& VegetationAdapter::getAllSpecies() const
    {
        return registry.getAllSpecies();
    }

    uint32_t VegetationAdapter::getSpeciesCount() const
    {
        return registry.getSpeciesCount();
    }
}
