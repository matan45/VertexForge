#pragma once
#include "EventTypes.hpp"
#include <glm/glm.hpp>
#include "terrain/TerrainHitResult.hpp"

namespace events::terrainRaycast
{
    // ============================================
    // COMMANDS
    // ============================================

    struct SetCursorPositionCommand : ICommand<>
    {
        glm::vec2 cursorUV;

        std::string_view getName() const override { return "SetTerrainRaycastCursorPosition"; }
    };

    struct ClearCursorCommand : ICommand<>
    {
        std::string_view getName() const override { return "ClearTerrainRaycastCursor"; }
    };

    // ============================================
    // QUERIES
    // ============================================

    struct GetTerrainHitQuery : IQuery<terrain::TerrainHitResult>
    {
        std::string_view getName() const override { return "GetTerrainHit"; }
    };

    // ============================================
    // NOTIFICATIONS
    // ============================================

    struct TerrainHitUpdatedNotification : INotification
    {
        terrain::TerrainHitResult result;

        std::string_view getName() const override { return "TerrainHitUpdated"; }
    };
}
