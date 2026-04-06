#pragma once

#include "../EventTypes.hpp"
#include "../../utilities/terrain/SplineTypes.hpp"
#include <glm/glm.hpp>

namespace events::splineTerrain
{
    struct SetSplineModeActiveCommand : ICommand<>
    {
        bool active = false;

        std::string_view getName() const override { return "SetSplineModeActive"; }
    };

    struct AddSplinePointCommand : ICommand<>
    {
        glm::vec3 worldPosition{0.0f};

        std::string_view getName() const override { return "AddSplinePoint"; }
    };

    struct RemoveLastSplinePointCommand : ICommand<>
    {
        std::string_view getName() const override { return "RemoveLastSplinePoint"; }
    };

    struct ClearActiveSplineCommand : ICommand<>
    {
        std::string_view getName() const override { return "ClearActiveSpline"; }
    };

    struct FinalizeSplineCommand : ICommand<>
    {
        std::string_view getName() const override { return "FinalizeSpline"; }
    };

    struct DeleteSplineCommand : ICommand<>
    {
        uint64_t splineId = 0;

        std::string_view getName() const override { return "DeleteSpline"; }
    };

    struct SetSplineParamsCommand : ICommand<>
    {
        ::terrain::SplineParams params;

        std::string_view getName() const override { return "SetSplineParams"; }
    };

    struct GetSplineParamsQuery : IQuery<::terrain::SplineParams>
    {
        std::string_view getName() const override { return "GetSplineParams"; }
    };

    struct IsSplineModeActiveQuery : IQuery<bool>
    {
        std::string_view getName() const override { return "IsSplineModeActive"; }
    };

    struct GetActiveSplinePointCountQuery : IQuery<uint32_t>
    {
        std::string_view getName() const override { return "GetActiveSplinePointCount"; }
    };

    struct GetActiveSplinePreviewQuery : IQuery<std::vector<glm::vec3>>
    {
        float sampleStep = 0.5f;

        std::string_view getName() const override { return "GetActiveSplinePreview"; }
    };

    struct SplineModeChangedNotification : INotification
    {
        bool isActive = false;

        std::string_view getName() const override { return "SplineModeChanged"; }
    };

    struct SplinePointAddedNotification : INotification
    {
        uint32_t pointCount = 0;

        std::string_view getName() const override { return "SplinePointAdded"; }
    };

    struct SplineAppliedNotification : INotification
    {
        uint64_t splineId = 0;

        std::string_view getName() const override { return "SplineApplied"; }
    };

    struct SplineParamsChangedNotification : INotification
    {
        ::terrain::SplineParams params;

        std::string_view getName() const override { return "SplineParamsChanged"; }
    };

    // Internal: sent by SplineService to TerrainService for terrain modification
    struct ApplySplinePaintCommand : ICommand<bool>
    {
        std::vector<glm::vec3> splineSamples;
        ::terrain::SplineParams params;

        std::string_view getName() const override { return "ApplySplinePaint"; }
    };

    struct ApplySplineDeformCommand : ICommand<bool>
    {
        std::vector<glm::vec3> splineSamples; // sampled curve points
        ::terrain::SplineParams params;
        uint64_t splineId = 0;

        std::string_view getName() const override { return "ApplySplineDeform"; }
    };

    struct RestoreSplineHeightsCommand : ICommand<>
    {
        uint64_t splineId = 0;
        std::unordered_map<::terrain::TileCoord, std::vector<float>, ::terrain::TileCoordHash> originalHeights;

        std::string_view getName() const override { return "RestoreSplineHeights"; }
    };

    // Query to get captured original heights after deform
    struct GetSplineOriginalHeightsQuery : IQuery<std::unordered_map<::terrain::TileCoord, std::vector<float>, ::terrain::TileCoordHash>>
    {
        std::vector<glm::vec3> splineSamples;
        float totalHalfWidth = 0.0f;

        std::string_view getName() const override { return "GetSplineOriginalHeights"; }
    };
}
