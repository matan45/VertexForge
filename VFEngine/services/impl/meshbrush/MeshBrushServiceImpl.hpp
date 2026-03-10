#pragma once
#include "../../interfaces/meshbrush/IMeshBrushService.hpp"
#include "../../events/EventTypes.hpp"
#include "../../../utilities/meshbrush/MeshBrushTypes.hpp"
#include "../../../utilities/meshbrush/MeshBrushSpatialGrid.hpp"
#include "../../data/EntityHandle.hpp"
#include <vector>
#include <random>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

namespace services
{
    class MeshBrushServiceImpl : public IMeshBrushService
    {
    private:
        meshbrush::MeshBrushParams currentParams;
        meshbrush::MeshBrushMode currentMode = meshbrush::MeshBrushMode::Paint;
        std::vector<meshbrush::MeshPaletteEntry> palette;
        meshbrush::MeshBrushSpatialGrid spatialGrid;
        bool meshBrushModeActive = false;
        int selectedPaletteIndex = -1; // -1 = all (weighted random)

        std::mt19937 rng{std::random_device{}()};

        ::events::SubscriptionToken modeChangedToken;
        ::events::SubscriptionToken sceneClearedToken;

        // Rate limiting for continuous painting
        glm::vec3 lastPlacementPos{0.0f};
        bool hasLastPlacement = false;

        // Global entity counter for unique names
        uint32_t entityCounter = 0;

        // Per-palette-entry group parent entities
        std::unordered_map<uint32_t, EntityHandle> groupEntities;
        EntityHandle ensureGroupEntity(uint32_t paletteIdx);

        // AABB Y-offset cache (meshPath -> -aabb.min.y)
        std::unordered_map<std::string, float> aabbYOffsetCache;
        float getAABBYOffset(const std::string& meshPath);

    public:
        MeshBrushServiceImpl() = default;
        ~MeshBrushServiceImpl() override;

        void registerEventHandlers() override;

    private:
        void applyBrush(const glm::vec3& worldPos, const glm::vec3& normal, float deltaTime, bool isFirst);
        void placeMeshes(const glm::vec3& worldPos, const glm::vec3& normal);
        void eraseInstances(const glm::vec3& worldPos);

        void publishParamsChanged();
    };
}
