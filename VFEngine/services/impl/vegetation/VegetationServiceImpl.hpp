#pragma once

#include "../../interfaces/vegetation/IVegetationService.hpp"
#include "../../providers/vegetation/IVegetationProvider.hpp"

namespace services
{
    class VegetationServiceImpl : public IVegetationService
    {
    public:
        explicit VegetationServiceImpl(IVegetationProvider* vegetationProvider);
        ~VegetationServiceImpl() = default;

        void registerEventHandlers() override;

    private:
        uint32_t addSpecies(const vegetation::VegetationSpeciesConfig& config);
        void removeSpecies(uint32_t speciesId);
        void updateSpecies(uint32_t speciesId, const vegetation::VegetationSpeciesConfig& config);

        IVegetationProvider* provider = nullptr;
    };
}
