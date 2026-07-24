#pragma once
// VK-1575 (Phase 4): CQRS events driving the foliage brush + its mode toggle. Mirrors
// vegetation/VegetationBrushEvents.hpp and meshbrush/MeshBrushEvents.hpp. The per-tile
// instance store + FoliageType palette live in the separate events::foliage namespace
// (foliage/FoliageEvents.hpp); this file is the authoring/brush-driver surface.
#include "../EventTypes.hpp"
#include "foliage/FoliageBrushTypes.hpp"
#include <glm/glm.hpp>
#include <cstdint>

namespace events::foliageBrush
{
    // ---- Commands ----

    struct ApplyFoliageBrushCommand : ICommand<>
    {
        glm::vec3 worldPosition{0.0f};
        glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};
        float deltaTime = 0.0f;
        bool isFirstApplication = false;

        std::string_view getName() const override { return "ApplyFoliageBrush"; }
    };

    struct SetFoliageBrushParamsCommand : ICommand<>
    {
        ::foliage::FoliageBrushParams params;

        std::string_view getName() const override { return "SetFoliageBrushParams"; }
    };

    struct SetFoliageBrushModeCommand : ICommand<>
    {
        ::foliage::FoliageBrushMode mode = ::foliage::FoliageBrushMode::Paint;

        std::string_view getName() const override { return "SetFoliageBrushMode"; }
    };

    // -1 = all paint-enabled entries (weighted random). >=0 restricts Single-mode
    // placement (and erase-selected-type) to that palette index.
    struct SetFoliageBrushSelectedEntryCommand : ICommand<>
    {
        int32_t index = -1;

        std::string_view getName() const override { return "SetFoliageBrushSelectedEntry"; }
    };

    // Ends the current paint/erase stroke and records a single undo entry for it.
    struct FinalizeFoliageBrushCommand : ICommand<>
    {
        std::string_view getName() const override { return "FinalizeFoliageBrush"; }
    };

    // ---- Mode Commands ----

    struct SetFoliageBrushModeActiveCommand : ICommand<>
    {
        bool active = false;

        std::string_view getName() const override { return "SetFoliageBrushModeActive"; }
    };

    // ---- Queries ----

    struct GetFoliageBrushParamsQuery : IQuery<::foliage::FoliageBrushParams>
    {
        std::string_view getName() const override { return "GetFoliageBrushParams"; }
    };

    struct GetFoliageBrushModeQuery : IQuery<::foliage::FoliageBrushMode>
    {
        std::string_view getName() const override { return "GetFoliageBrushMode"; }
    };

    struct IsFoliageBrushModeActiveQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsFoliageBrushModeActive"; }
    };

    // ---- Notifications ----

    struct FoliageBrushModeChangedNotification : INotification
    {
        bool isActive = false;

        std::string_view getName() const override { return "FoliageBrushModeChanged"; }
    };

    struct FoliageBrushParamsChangedNotification : INotification
    {
        ::foliage::FoliageBrushParams params;

        std::string_view getName() const override { return "FoliageBrushParamsChanged"; }
    };

    struct FoliageBrushAppliedNotification : INotification
    {
        glm::vec3 position{0.0f};
        uint32_t placedCount = 0;
        uint32_t erasedCount = 0;

        std::string_view getName() const override { return "FoliageBrushApplied"; }
    };
}
