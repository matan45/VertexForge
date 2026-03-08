#pragma once
#include "VegetationSpecies.hpp"
#include <cstdint>
#include <unordered_map>

namespace vegetation
{
    class VegetationSpeciesRegistry
    {
    public:
        uint32_t addSpecies(const VegetationSpeciesConfig& config);
        void removeSpecies(uint32_t speciesId);
        void updateSpecies(uint32_t speciesId, const VegetationSpeciesConfig& config);

        [[nodiscard]] const VegetationSpeciesConfig* getSpecies(uint32_t speciesId) const;
        [[nodiscard]] bool hasSpecies(uint32_t speciesId) const;
        [[nodiscard]] const std::unordered_map<uint32_t, VegetationSpeciesConfig>& getAllSpecies() const;
        [[nodiscard]] uint32_t getSpeciesCount() const;

        void clear();

    private:
        std::unordered_map<uint32_t, VegetationSpeciesConfig> species;
        uint32_t nextId = 1;  // 0 reserved as invalid
    };
}
