#pragma once
#include "EventTypes.hpp"
#include <glm/glm.hpp>
#include "terrain/TerrainHitResult.hpp"

namespace events::terrainRaycast
{
    struct SetCursorPositionCommand : ICommand<>
    {
        glm::vec2 cursorUV;

        std::string_view getName() const override { return "SetTerrainRaycastCursorPosition"; }
    };

    struct ClearCursorCommand : ICommand<>
    {
        std::string_view getName() const override { return "ClearTerrainRaycastCursor"; }
    };

    struct GetTerrainHitQuery : IQuery<terrain::TerrainHitResult>
    {
        std::string_view getName() const override { return "GetTerrainHit"; }
    };
}
