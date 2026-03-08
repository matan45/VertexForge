#pragma once

#include "../../../services/providers/vegetation/IVegetationProvider.hpp"
#include "vegetation/VegetationSpeciesRegistry.hpp"
#include <memory>

namespace core::adapters
{
    class VegetationAdapter : public services::IVegetationProvider
    {
    public:
        VegetationAdapter();
        ~VegetationAdapter() override = default;

        uint32_t addSpecies(const vegetation::VegetationSpeciesConfig& config) override;
        void removeSpecies(uint32_t speciesId) override;
        void updateSpecies(uint32_t speciesId, const vegetation::VegetationSpeciesConfig& config) override;

        const vegetation::VegetationSpeciesConfig* getSpecies(uint32_t speciesId) const override;
        bool hasSpecies(uint32_t speciesId) const override;
        const std::unordered_map<uint32_t, vegetation::VegetationSpeciesConfig>& getAllSpecies() const override;
        uint32_t getSpeciesCount() const override;

    private:
        vegetation::VegetationSpeciesRegistry registry;
    };
}
