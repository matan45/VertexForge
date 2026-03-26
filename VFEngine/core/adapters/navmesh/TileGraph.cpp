#include "TileGraph.hpp"
#include <DetourNavMesh.h>
#include <cstring>

namespace core
{
    void TileGraph::rebuild(const dtNavMesh* navMesh, float tileWorldSize)
    {
        clear();
        if (!navMesh || tileWorldSize <= 0.0f)
            return;

        for (int i = 0; i < navMesh->getMaxTiles(); ++i)
        {
            const dtMeshTile* tile = navMesh->getTile(i);
            if (!tile || !tile->header || !tile->dataSize)
                continue;

            addOrUpdateTile(navMesh, tile->header->x, tile->header->y, tileWorldSize);
        }
    }

    void TileGraph::addOrUpdateTile(const dtNavMesh* navMesh, int tx, int tz, float tileWorldSize)
    {
        if (!navMesh || tileWorldSize <= 0.0f)
            return;

        navigation::NavmeshTileCoord coord{tx, tz};

        // Remove old edges pointing TO this tile from neighbors
        removeReverseEdges(coord);

        // Create/overwrite node
        navigation::TileGraphNode& node = nodes[coord];
        node.coord = coord;
        node.center = glm::vec3((tx + 0.5f) * tileWorldSize, 0.0f, (tz + 0.5f) * tileWorldSize);
        node.edges.clear();

        // Find the tile in dtNavMesh
        dtTileRef tileRef = navMesh->getTileRefAt(tx, tz, 0);
        if (!tileRef)
            return;

        const dtMeshTile* tile = navMesh->getTileByRef(tileRef);
        if (!tile || !tile->header)
            return;

        // Group portals by neighbor tile coordinate
        std::unordered_map<navigation::NavmeshTileCoord,
                           std::vector<navigation::TileBoundaryPortal>,
                           navigation::NavmeshTileCoordHash> neighborPortals;

        for (int i = 0; i < tile->header->polyCount; ++i)
        {
            const dtPoly* poly = &tile->polys[i];
            if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                continue;

            for (unsigned int link = poly->firstLink; link != DT_NULL_LINK; link = tile->links[link].next)
            {
                const dtLink& lnk = tile->links[link];
                if (!lnk.ref)
                    continue;

                // Decode target tile
                unsigned int targetSalt, targetTileIdx, targetPolyIdx;
                navMesh->decodePolyId(lnk.ref, targetSalt, targetTileIdx, targetPolyIdx);

                const dtMeshTile* targetTile = navMesh->getTile(targetTileIdx);
                if (!targetTile || !targetTile->header)
                    continue;

                int ntx = targetTile->header->x;
                int ntz = targetTile->header->y;

                // Skip same-tile links
                if (ntx == tx && ntz == tz)
                    continue;

                navigation::NavmeshTileCoord neighborCoord{ntx, ntz};

                // Compute boundary portal point (midpoint of the shared edge)
                // Use the polygon vertex positions on the linking edge
                unsigned char edge = lnk.edge;
                const float* va = &tile->verts[poly->verts[edge] * 3];
                const float* vb = &tile->verts[poly->verts[(edge + 1) % poly->vertCount] * 3];

                navigation::TileBoundaryPortal portal;
                portal.point = glm::vec3(
                    (va[0] + vb[0]) * 0.5f,
                    (va[1] + vb[1]) * 0.5f,
                    (va[2] + vb[2]) * 0.5f
                );
                portal.polyRefA = navMesh->getPolyRefBase(tile) | static_cast<uint64_t>(i);
                portal.polyRefB = lnk.ref;

                neighborPortals[neighborCoord].push_back(portal);
            }
        }

        // Create edges from grouped portals
        for (auto& [neighborCoord, portals] : neighborPortals)
        {
            navigation::TileGraphEdge edge;
            edge.neighbor = neighborCoord;
            edge.portals = std::move(portals);

            // Cost = distance between tile centers
            auto neighborIt = nodes.find(neighborCoord);
            if (neighborIt != nodes.end())
            {
                edge.cost = heuristic(node, neighborIt->second);

                // Add reverse edge from neighbor back to this tile
                navigation::TileGraphEdge reverseEdge;
                reverseEdge.neighbor = coord;
                reverseEdge.cost = edge.cost;
                // Reverse portals (swap A/B refs)
                for (const auto& p : edge.portals)
                {
                    navigation::TileBoundaryPortal rp;
                    rp.point = p.point;
                    rp.polyRefA = p.polyRefB;
                    rp.polyRefB = p.polyRefA;
                    reverseEdge.portals.push_back(rp);
                }
                neighborIt->second.edges.push_back(std::move(reverseEdge));
            }
            else
            {
                edge.cost = tileWorldSize; // Approximate if neighbor not yet in graph
            }

            node.edges.push_back(std::move(edge));
        }
    }

    void TileGraph::removeReverseEdges(const navigation::NavmeshTileCoord& from)
    {
        auto nodeIt = nodes.find(from);
        if (nodeIt == nodes.end())
            return;

        for (const auto& edge : nodeIt->second.edges)
        {
            auto neighborIt = nodes.find(edge.neighbor);
            if (neighborIt == nodes.end())
                continue;

            auto& neighborEdges = neighborIt->second.edges;
            neighborEdges.erase(
                std::remove_if(neighborEdges.begin(), neighborEdges.end(),
                    [&from](const navigation::TileGraphEdge& e) { return e.neighbor == from; }),
                neighborEdges.end());
        }
    }

    void TileGraph::removeTile(int tx, int tz)
    {
        navigation::NavmeshTileCoord coord{tx, tz};
        removeReverseEdges(coord);
        nodes.erase(coord);
    }

    void TileGraph::clear()
    {
        nodes.clear();
    }

    std::vector<navigation::NavmeshTileCoord> TileGraph::findTilePath(
        const navigation::NavmeshTileCoord& start,
        const navigation::NavmeshTileCoord& end) const
    {
        if (!hasTile(start) || !hasTile(end))
            return {};

        if (start == end)
            return {start};

        struct AStarNode
        {
            navigation::NavmeshTileCoord coord;
            float gCost;
            float fCost;

            bool operator>(const AStarNode& other) const { return fCost > other.fCost; }
        };

        std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openList;
        std::unordered_map<navigation::NavmeshTileCoord, float, navigation::NavmeshTileCoordHash> gCosts;
        std::unordered_map<navigation::NavmeshTileCoord, navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> cameFrom;
        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> closedSet;

        const auto& endNode = nodes.at(end);

        gCosts[start] = 0.0f;
        float h = heuristic(nodes.at(start), endNode);
        openList.push({start, 0.0f, h});

        while (!openList.empty())
        {
            AStarNode current = openList.top();
            openList.pop();

            if (current.coord == end)
            {
                // Reconstruct path
                std::vector<navigation::NavmeshTileCoord> path;
                navigation::NavmeshTileCoord c = end;
                while (!(c == start))
                {
                    path.push_back(c);
                    c = cameFrom.at(c);
                }
                path.push_back(start);
                std::reverse(path.begin(), path.end());
                return path;
            }

            if (closedSet.count(current.coord))
                continue;
            closedSet.insert(current.coord);

            auto nodeIt = nodes.find(current.coord);
            if (nodeIt == nodes.end())
                continue;

            for (const auto& edge : nodeIt->second.edges)
            {
                if (closedSet.count(edge.neighbor))
                    continue;

                float newG = current.gCost + edge.cost;
                auto existingG = gCosts.find(edge.neighbor);
                if (existingG != gCosts.end() && newG >= existingG->second)
                    continue;

                gCosts[edge.neighbor] = newG;
                cameFrom[edge.neighbor] = current.coord;

                auto neighborNodeIt = nodes.find(edge.neighbor);
                if (neighborNodeIt != nodes.end())
                {
                    float fCost = newG + heuristic(neighborNodeIt->second, endNode);
                    openList.push({edge.neighbor, newG, fCost});
                }
            }
        }

        return {}; // No path found
    }
}
