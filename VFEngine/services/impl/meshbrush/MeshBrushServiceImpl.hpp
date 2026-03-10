#pragma once
#include "../../interfaces/meshbrush/IMeshBrushService.hpp"
#include "../../events/EventTypes.hpp"
#include "../../../utilities/meshbrush/MeshBrushTypes.hpp"
#include "../../../utilities/meshbrush/MeshBrushSpatialGrid.hpp"
#include "../../data/EntityHandle.hpp"
#include <vector>
#include <random>
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

        std::mt19937 rng{std::random_device{}()};

        ::events::SubscriptionToken modeChangedToken;
        ::events::SubscriptionToken sceneClearedToken;

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
