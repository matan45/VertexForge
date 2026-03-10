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

        // Global instance ID counter
        uint64_t nextInstanceId = 1;

        // Per-palette-entry + per-sector batch entities
        // Key: (paletteIdx, sectorX, sectorZ)
        struct BatchKey
        {
            uint32_t paletteIdx;
            int32_t sectorX;
            int32_t sectorZ;
            bool operator==(const BatchKey& o) const
            {
                return paletteIdx == o.paletteIdx && sectorX == o.sectorX && sectorZ == o.sectorZ;
            }
        };
        struct BatchKeyHash
        {
            size_t operator()(const BatchKey& k) const
            {
                size_t h = std::hash<uint32_t>{}(k.paletteIdx);
                h ^= std::hash<int32_t>{}(k.sectorX) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<int32_t>{}(k.sectorZ) + 0x9e3779b9 + (h << 6) + (h >> 2);
                return h;
            }
        };
        static constexpr float sectorSize = 128.0f;
        std::unordered_map<BatchKey, EntityHandle, BatchKeyHash> batchEntities;
        EntityHandle ensureBatchEntity(uint32_t paletteIdx, const glm::vec3& worldPos);

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
