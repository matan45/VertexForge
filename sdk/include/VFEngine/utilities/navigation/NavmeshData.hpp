#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include "../types/NavmeshTypes.hpp"

namespace navigation
{
    struct NavmeshTileCoord
    {
        int32_t x = 0;
        int32_t z = 0;

        bool operator==(const NavmeshTileCoord& other) const
        {
            return x == other.x && z == other.z;
        }

        bool operator!=(const NavmeshTileCoord& other) const
        {
            return !(*this == other);
        }
    };

    struct NavmeshTileCoordHash
    {
        size_t operator()(const NavmeshTileCoord& c) const
        {
            size_t h1 = std::hash<int32_t>{}(c.x);
            size_t h2 = std::hash<int32_t>{}(c.z);
            return h1 ^ (h2 * 2654435761u);
        }
    };

    struct NavmeshInputGeometry
    {
        std::vector<float> vertices;     // Flat: x,y,z,x,y,z,...
        std::vector<int> triangles;      // Index triplets
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};

        void addVertex(const glm::vec3& v)
        {
            if (vertices.empty())
            {
                boundsMin = v;
                boundsMax = v;
            }
            else
            {
                boundsMin = glm::min(boundsMin, v);
                boundsMax = glm::max(boundsMax, v);
            }
            vertices.push_back(v.x);
            vertices.push_back(v.y);
            vertices.push_back(v.z);
        }

        void addTriangle(int a, int b, int c)
        {
            triangles.push_back(a);
            triangles.push_back(b);
            triangles.push_back(c);
        }

        int getVertexCount() const { return static_cast<int>(vertices.size() / 3); }
        int getTriangleCount() const { return static_cast<int>(triangles.size() / 3); }
        bool isEmpty() const { return vertices.empty() || triangles.empty(); }
    };

    struct NavPath
    {
        std::vector<glm::vec3> waypoints;
        bool isPartial = false;
        bool isValid = false;
    };

    struct NavmeshRaycastResult
    {
        bool hit = false;
        glm::vec3 hitPoint{0.0f};
        float hitDistance = 0.0f;
    };

    constexpr uint32_t NAVMESH_FILE_MAGIC = 0x564E4D53; // "VNMS"
    constexpr uint32_t NAVMESH_FILE_VERSION = 1;
    // Version 3: added off-mesh link + obstacle fields to NavmeshBakeSettings
    // Version 4: added areaCosts[64] to NavmeshBakeSettings
    // Version 5: added LOD support (lod field in NavmeshTileData, NavmeshLodConfig in settings)
    // Version 6: added streaming settings block (enabled flag + radii/budgets/LOD distances)
    constexpr uint32_t NAVMESH_TILE_FILE_VERSION = 6;
    constexpr uint32_t NAVMESH_TILE_MIN_SUPPORTED_VERSION = 5;

    // dtPolyRef is 32 bits laid out as [salt | tile | poly]. Detour requires
    // saltBits >= 10, so tileBits + polyBits <= 22. maxTiles passed to
    // dtNavMesh::init caps CONCURRENTLY RESIDENT tiles (hash-addressed), not
    // the world grid — streaming swaps tiles in and out of that budget, so an
    // open world of any size works as long as residency stays under the cap.
    struct NavmeshRefBudget
    {
        int maxTiles = 0;
        int tileBits = 0;
        int polyBits = 0;
        int saltBits = 0;
        bool clamped = false;
    };

    inline int navmeshBitsFor(int value)
    {
        int bits = 1;
        while ((1 << bits) < value && bits < 31)
            ++bits;
        return bits;
    }

    inline NavmeshRefBudget computeNavmeshRefBudget(int requestedTiles, int maxPolysPerTile)
    {
        constexpr int MIN_SALT_BITS = 10;        // enforced by dtNavMesh::init
        constexpr int MAX_RESIDENT_TILES = 1024; // generous streaming residency cap

        NavmeshRefBudget budget;
        budget.polyBits = navmeshBitsFor(maxPolysPerTile > 1 ? maxPolysPerTile : 2);

        int tileBitsCap = 32 - MIN_SALT_BITS - budget.polyBits;
        int hardCap = tileBitsCap >= 1 ? (1 << tileBitsCap) : 1;
        if (hardCap > MAX_RESIDENT_TILES)
            hardCap = MAX_RESIDENT_TILES;

        budget.maxTiles = requestedTiles < 1 ? 1 : requestedTiles;
        if (budget.maxTiles > hardCap)
        {
            budget.maxTiles = hardCap;
            budget.clamped = true;
        }

        budget.tileBits = navmeshBitsFor(budget.maxTiles > 1 ? budget.maxTiles : 2);
        budget.saltBits = 32 - budget.tileBits - budget.polyBits;
        return budget;
    }

    struct NavmeshFileHeader
    {
        uint32_t magic = NAVMESH_FILE_MAGIC;
        uint32_t version = NAVMESH_FILE_VERSION;
        types::NavmeshBakeSettings settings;
        uint32_t tileCount = 0;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
    };

    struct NavmeshTileData
    {
        int32_t x = 0;
        int32_t y = 0;
        uint8_t lod = 0;
        uint32_t dataSize = 0;
        std::vector<uint8_t> data;
    };

    struct NavmeshTileBounds
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    inline NavmeshTileBounds computeTileBounds(const NavmeshTileCoord& coord,
                                                const types::NavmeshBakeSettings& settings,
                                                float yMin, float yMax)
    {
        float tileWorldSize = settings.tileSize * settings.cellSize;
        NavmeshTileBounds bounds;
        bounds.min = glm::vec3(coord.x * tileWorldSize, yMin, coord.z * tileWorldSize);
        bounds.max = glm::vec3((coord.x + 1) * tileWorldSize, yMax, (coord.z + 1) * tileWorldSize);
        return bounds;
    }

    inline NavmeshTileBounds expandBoundsForOverlap(const NavmeshTileBounds& bounds,
                                                     const types::NavmeshBakeSettings& settings)
    {
        float borderExpand = settings.agentRadius + settings.cellSize * 3.0f;
        NavmeshTileBounds expanded;
        expanded.min = bounds.min - glm::vec3(borderExpand, 0.0f, borderExpand);
        expanded.max = bounds.max + glm::vec3(borderExpand, 0.0f, borderExpand);
        return expanded;
    }

    struct NavmeshOffMeshConnection
    {
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
        float radius = 0.25f;
        uint8_t direction = 0;  // 0=one-way, 1=bidirectional
        uint8_t areaType = 0;
        uint16_t flags = 1;
        uint32_t userID = 0;
    };

    struct NavmeshOffMeshConnections
    {
        std::vector<NavmeshOffMeshConnection> connections;
        bool empty() const { return connections.empty(); }
    };

    using OffMeshConnectionsMap = std::unordered_map<NavmeshTileCoord, NavmeshOffMeshConnections, NavmeshTileCoordHash>;

    struct NavmeshAreaModifier
    {
        uint8_t shape = 0;           // 0=Box, 1=Cylinder
        glm::vec3 position{0.0f};    // World-space center
        glm::vec3 halfSize{2.0f};    // Box: half-extents; Cylinder: x=radius, y=half-height
        uint8_t areaType = 0;
    };

    using AreaModifiersMap = std::unordered_map<NavmeshTileCoord, std::vector<NavmeshAreaModifier>, NavmeshTileCoordHash>;

    // Hierarchical pathfinding tile graph types
    struct TileBoundaryPortal
    {
        glm::vec3 point{0.0f};
        uint64_t polyRefA = 0;  // dtPolyRef on tile A side
        uint64_t polyRefB = 0;  // dtPolyRef on tile B side
    };

    struct TileGraphEdge
    {
        NavmeshTileCoord neighbor;
        float cost = 0.0f;
        std::vector<TileBoundaryPortal> portals;
    };

    struct TileGraphNode
    {
        NavmeshTileCoord coord;
        glm::vec3 center{0.0f};
        std::vector<TileGraphEdge> edges;
    };

    struct NavmeshTileLodKey
    {
        int32_t x = 0;
        int32_t z = 0;
        uint8_t lod = 0;

        bool operator==(const NavmeshTileLodKey& other) const
        {
            return x == other.x && z == other.z && lod == other.lod;
        }

        bool operator!=(const NavmeshTileLodKey& other) const
        {
            return !(*this == other);
        }
    };

    struct NavmeshTileLodKeyHash
    {
        size_t operator()(const NavmeshTileLodKey& k) const
        {
            size_t h1 = std::hash<int32_t>{}(k.x);
            size_t h2 = std::hash<int32_t>{}(k.z);
            size_t h3 = std::hash<uint8_t>{}(k.lod);
            return h1 ^ (h2 * 2654435761u) ^ (h3 * 40503u);
        }
    };
}
