#pragma once
#include <string>
#include <cstdint>

namespace types
{
    struct NavmeshBakeSettings
    {
        // Rasterization
        float cellSize = 0.25f;
        float cellHeight = 0.2f;

        // Agent
        float agentRadius = 0.25f;
        float agentHeight = 2.0f;
        float agentMaxClimb = 0.4f;
        float agentMaxSlope = 45.0f;

        // Region
        int regionMinSize = 8;
        int regionMergeSize = 20;

        // Polygonization
        float edgeMaxLen = 12.0f;
        float edgeMaxError = 1.3f;
        int vertsPerPoly = 6;

        // Detail mesh
        float detailSampleDist = 6.0f;
        float detailSampleMaxError = 1.0f;

        // Tile size (in cells)
        int tileSize = 128;

        // Geometry collection
        bool includeStaticMeshes = true;
        bool includeTerrain = true;
        bool includeColliders = true;
    };

    enum class NavmeshBakeStatus : uint8_t
    {
        Idle = 0,
        Collecting,
        Voxelizing,
        Building,
        Complete,
        Failed
    };

    struct NavmeshBakeProgress
    {
        NavmeshBakeStatus status = NavmeshBakeStatus::Idle;
        float progress = 0.0f;
        std::string currentStage;
    };
}
