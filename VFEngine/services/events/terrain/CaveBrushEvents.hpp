#pragma once
#include "../EventTypes.hpp"
#include "../../utilities/terrain/CaveBrushTypes.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace events::caveBrush
{
    // Full SDF + hole-mask state of one tile, used to restore a cave stroke for
    // undo/redo. Empty sdf == "no cave on this tile".
    struct CaveTileState
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        std::vector<float> sdf;
        std::vector<uint8_t> holeMask;
    };

    // Restore cave SDF + hole mask for a set of tiles, then remesh (apron-stitched)
    // and rebuild colliders. Published by the cave-stroke undo command.
    struct RestoreCaveStateCommand : ICommand<>
    {
        uint64_t entityId = 0;
        std::vector<CaveTileState> tiles;

        std::string_view getName() const override { return "RestoreCaveState"; }
    };

    struct SetCaveBrushParamsCommand : ICommand<>
    {
        ::terrain::CaveBrushParams params;

        std::string_view getName() const override { return "SetCaveBrushParams"; }
    };

    struct SetCaveBrushRadiusCommand : ICommand<>
    {
        float radius;

        std::string_view getName() const override { return "SetCaveBrushRadius"; }
    };

    struct SetCaveBrushStrengthCommand : ICommand<>
    {
        float strength;

        std::string_view getName() const override { return "SetCaveBrushStrength"; }
    };

    struct SetCaveBrushFalloffCommand : ICommand<>
    {
        ::terrain::BrushFalloff falloff;

        std::string_view getName() const override { return "SetCaveBrushFalloff"; }
    };

    struct SetCaveBrushShapeCommand : ICommand<>
    {
        ::terrain::BrushShape shape;

        std::string_view getName() const override { return "SetCaveBrushShape"; }
    };

    struct SetCaveBrushTypeCommand : ICommand<>
    {
        ::terrain::CaveBrushType type;

        std::string_view getName() const override { return "SetCaveBrushType"; }
    };

    struct GetCaveBrushParamsQuery : IQuery<::terrain::CaveBrushParams>
    {
        std::string_view getName() const override { return "GetCaveBrushParams"; }
    };

    struct GetCaveBrushTypeQuery : IQuery<::terrain::CaveBrushType>
    {
        std::string_view getName() const override { return "GetCaveBrushType"; }
    };

    struct ApplyCaveBrushCommand : ICommand<>
    {
        glm::vec3 worldPosition{0.0f};
        float deltaTime = 0.0f;
        bool invert = false;
        bool isFirstApplication = false;

        std::string_view getName() const override { return "ApplyCaveBrush"; }
    };

    struct FinalizeCaveBrushCommand : ICommand<>
    {
        std::string_view getName() const override { return "FinalizeCaveBrush"; }
    };

    struct CaveBrushParamsChangedNotification : INotification
    {
        ::terrain::CaveBrushParams params;

        std::string_view getName() const override { return "CaveBrushParamsChanged"; }
    };

    struct CaveBrushTypeChangedNotification : INotification
    {
        ::terrain::CaveBrushType type;

        std::string_view getName() const override { return "CaveBrushTypeChanged"; }
    };

    struct CaveBrushAppliedNotification : INotification
    {
        glm::vec3 position{0.0f};
        ::terrain::CaveBrushType type;

        std::string_view getName() const override { return "CaveBrushApplied"; }
    };
}
