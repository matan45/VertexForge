#pragma once

#include "vegetation/VegetationSpecies.hpp"
#include <cstdint>
#include <unordered_map>

namespace services
{
    class IVegetationProvider
    {
    public:
        virtual ~IVegetationProvider() = default;

        virtual uint32_t addSpecies(const vegetation::VegetationSpeciesConfig& config) = 0;
        virtual void removeSpecies(uint32_t speciesId) = 0;
        virtual void updateSpecies(uint32_t speciesId, const vegetation::VegetationSpeciesConfig& config) = 0;

        virtual const vegetation::VegetationSpeciesConfig* getSpecies(uint32_t speciesId) const = 0;
        virtual bool hasSpecies(uint32_t speciesId) const = 0;
        virtual const std::unordered_map<uint32_t, vegetation::VegetationSpeciesConfig>& getAllSpecies() const = 0;
        virtual uint32_t getSpeciesCount() const = 0;
    };
}
