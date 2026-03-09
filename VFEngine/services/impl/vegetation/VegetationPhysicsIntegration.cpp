#include "VegetationPhysicsIntegration.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"
#include "../../providers/vegetation/IVegetationProvider.hpp"

namespace services
{
    void VegetationPhysicsIntegration::onTileLoaded(
        int32_t coordX, int32_t coordZ,
        const vegetation::VegetationPlacementData& placement)
    {
        if (!physicsProvider || !vegetationProvider) return;

        TileKey key{coordX, coordZ};

        // Remove existing colliders for this tile if any
        if (activeTiles.contains(key))
        {
            physicsProvider->removeVegetationTileColliders(coordX, coordZ);
            activeTiles.erase(key);
        }

        std::vector<IPhysicsProvider::VegetationColliderInstance> colliderInstances;

        for (const auto& inst : placement.getInstances())
        {
            const auto* species = vegetationProvider->getSpecies(inst.speciesId);
            if (!species || !species->hasCollision) continue;

            IPhysicsProvider::VegetationColliderInstance collider;
            collider.position = inst.position;
            collider.rotation = inst.rotation;
            collider.scale = inst.scale;
            collider.radius = species->collisionRadius;
            collider.height = species->collisionHeight;
            colliderInstances.push_back(collider);
        }

        if (!colliderInstances.empty())
        {
            physicsProvider->addVegetationTileColliders(coordX, coordZ, colliderInstances);
            activeTiles[key] = true;
        }
    }

    void VegetationPhysicsIntegration::onTileUnloaded(int32_t coordX, int32_t coordZ)
    {
        if (!physicsProvider) return;

        TileKey key{coordX, coordZ};
        auto it = activeTiles.find(key);
        if (it != activeTiles.end())
        {
            physicsProvider->removeVegetationTileColliders(coordX, coordZ);
            activeTiles.erase(it);
        }
    }

    void VegetationPhysicsIntegration::clear()
    {
        if (physicsProvider)
        {
            physicsProvider->removeAllVegetationColliders();
        }
        activeTiles.clear();
    }
}
