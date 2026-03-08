#include "VegetationPhysicsIntegration.hpp"

namespace services
{
    void VegetationPhysicsIntegration::onTileLoaded(
        int32_t coordX, int32_t coordZ,
        const vegetation::VegetationPlacementData& placement,
        const vegetation::VegetationSpeciesRegistry& registry)
    {
        TileKey key{coordX, coordZ};
        auto& bodyIds = tileBodyIds[key];
        bodyIds.clear();

        for (const auto& inst : placement.instances)
        {
            const auto* species = registry.getSpecies(inst.speciesId);
            if (!species || !species->hasCollision) continue;

            // Stub: actual physics body creation via IPhysicsProvider
            // Will create capsule colliders at inst.position with
            // species->collisionRadius and species->collisionHeight
            bodyIds.push_back(0); // Placeholder body ID
        }
    }

    void VegetationPhysicsIntegration::onTileUnloaded(int32_t coordX, int32_t coordZ)
    {
        TileKey key{coordX, coordZ};
        auto it = tileBodyIds.find(key);
        if (it != tileBodyIds.end())
        {
            // Stub: remove physics bodies via IPhysicsProvider
            tileBodyIds.erase(it);
        }
    }

    void VegetationPhysicsIntegration::clear()
    {
        // Stub: remove all physics bodies
        tileBodyIds.clear();
    }
}
