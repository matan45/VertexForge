#pragma once
#include "EventTypes.hpp"
#include "../../utilities/terrain/BrushTypes.hpp"

namespace events::brush
{
    // ============================================
    // COMMANDS - Operations that modify brush state
    // ============================================

    struct SetBrushParamsCommand : ICommand<>
    {
        terrain::BrushParams params;

        std::string_view getName() const override { return "SetBrushParams"; }
    };

    struct SetBrushRadiusCommand : ICommand<>
    {
        float radius;

        std::string_view getName() const override { return "SetBrushRadius"; }
    };

    struct SetBrushStrengthCommand : ICommand<>
    {
        float strength;

        std::string_view getName() const override { return "SetBrushStrength"; }
    };

    struct SetBrushFalloffCommand : ICommand<>
    {
        terrain::BrushFalloff falloff;

        std::string_view getName() const override { return "SetBrushFalloff"; }
    };

    struct SetBrushShapeCommand : ICommand<>
    {
        terrain::BrushShape shape;

        std::string_view getName() const override { return "SetBrushShape"; }
    };

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

    struct GetBrushParamsQuery : IQuery<terrain::BrushParams>
    {
        std::string_view getName() const override { return "GetBrushParams"; }
    };

    // ============================================
    // NOTIFICATIONS - State change broadcasts
    // ============================================

    struct BrushParamsChangedNotification : INotification
    {
        terrain::BrushParams params;

        std::string_view getName() const override { return "BrushParamsChanged"; }
    };
}
