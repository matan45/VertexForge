#include "TerrainStrokeUndoCommands.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/terrain/TerrainStrokeEvents.hpp"

#include <utility>

namespace services
{
    namespace
    {
        using Kind = ::events::terrain::StrokeDataKind;

        // Weight maps are compared field by field rather than with a single == because
        // TileWeightMapData carries the per-tile palette (layerIndices) alongside the
        // channels, and a SetBaseLayer stroke changes only the palette.
        [[nodiscard]] bool weightsEqual(const ::terrain::TileWeightMapData& a,
                                        const ::terrain::TileWeightMapData& b)
        {
            if (a.resolution != b.resolution)
                return false;
            if (a.layerIndices != b.layerIndices)
                return false;
            if (a.layerWeights.size() != b.layerWeights.size())
                return false;
            for (size_t channel = 0; channel < a.layerWeights.size(); ++channel)
            {
                if (a.layerWeights[channel] != b.layerWeights[channel])
                    return false;
            }
            return true;
        }

        [[nodiscard]] size_t weightsBytes(const ::terrain::TileWeightMapData& weights)
        {
            size_t total = 0;
            for (const auto& channel : weights.layerWeights)
                total += channel.capacity() * sizeof(float);
            return total;
        }
    }

    bool TerrainStrokeUndoCommand::addTile(
        int32_t tileX, int32_t tileZ, uint8_t requestedKinds,
        std::vector<float> heightsBefore,
        ::terrain::TileWeightMapData weightsBefore,
        std::vector<uint8_t> holesBefore,
        const ::terrain::TerrainTile& after)
    {
        TileSnapshot snapshot;
        snapshot.tileX = tileX;
        snapshot.tileZ = tileZ;

        if (::events::terrain::hasStrokeKind(requestedKinds, Kind::Heights)
            && heightsBefore != after.heightData)
        {
            snapshot.heightsBefore = std::move(heightsBefore);
            snapshot.heightsAfter = after.heightData;
            snapshot.kinds |= ::events::terrain::strokeKindBit(Kind::Heights);
        }

        if (::events::terrain::hasStrokeKind(requestedKinds, Kind::Weights)
            && !weightsEqual(weightsBefore, after.weightMap))
        {
            snapshot.weightsBefore = std::move(weightsBefore);
            snapshot.weightsAfter = after.weightMap;
            snapshot.kinds |= ::events::terrain::strokeKindBit(Kind::Weights);
        }

        if (::events::terrain::hasStrokeKind(requestedKinds, Kind::Holes)
            && holesBefore != after.holeMask)
        {
            snapshot.holesBefore = std::move(holesBefore);
            snapshot.holesAfter = after.holeMask;
            snapshot.kinds |= ::events::terrain::strokeKindBit(Kind::Holes);
        }

        if (snapshot.kinds == 0)
            return false;

        tiles.push_back(std::move(snapshot));
        return true;
    }

    void TerrainStrokeUndoCommand::applyAll(bool useAfter)
    {
        ::events::terrain::RestoreStrokeStateCommand cmd;
        cmd.entityId = entityId;
        cmd.tiles.reserve(tiles.size());

        for (const auto& tile : tiles)
        {
            ::events::terrain::StrokeTileState state;
            state.tileX = tile.tileX;
            state.tileZ = tile.tileZ;
            state.kinds = tile.kinds;

            if (::events::terrain::hasStrokeKind(tile.kinds, Kind::Heights))
                state.heightData = useAfter ? tile.heightsAfter : tile.heightsBefore;
            if (::events::terrain::hasStrokeKind(tile.kinds, Kind::Weights))
                state.weightMap = useAfter ? tile.weightsAfter : tile.weightsBefore;
            if (::events::terrain::hasStrokeKind(tile.kinds, Kind::Holes))
                state.holeMask = useAfter ? tile.holesAfter : tile.holesBefore;

            cmd.tiles.push_back(std::move(state));
        }

        events::EventDispatcher::instance().execute(cmd);
    }

    void TerrainStrokeUndoCommand::execute()
    {
        applyAll(true);
    }

    void TerrainStrokeUndoCommand::undo()
    {
        applyAll(false);
    }

    size_t TerrainStrokeUndoCommand::getMemoryFootprint() const
    {
        size_t total = tiles.capacity() * sizeof(TileSnapshot);
        for (const auto& tile : tiles)
        {
            total += tile.heightsBefore.capacity() * sizeof(float);
            total += tile.heightsAfter.capacity() * sizeof(float);
            total += weightsBytes(tile.weightsBefore);
            total += weightsBytes(tile.weightsAfter);
            total += tile.holesBefore.capacity();
            total += tile.holesAfter.capacity();
        }
        return total;
    }


    void SurfaceMaskStrokeUndoCommand::apply(const std::vector<uint8_t>& texels)
    {
        ::events::terrain::RestoreSurfaceMaskRegionCommand cmd;
        cmd.channel = channel;
        cmd.maskWidth = maskWidth;
        cmd.maskHeight = maskHeight;
        cmd.minX = minX;
        cmd.minZ = minZ;
        cmd.rectWidth = rectWidth;
        cmd.rectHeight = rectHeight;
        cmd.texels = texels;
        events::EventDispatcher::instance().execute(cmd);
    }

    void SurfaceMaskStrokeUndoCommand::execute()
    {
        apply(after);
    }

    void SurfaceMaskStrokeUndoCommand::undo()
    {
        apply(before);
    }

    size_t SurfaceMaskStrokeUndoCommand::getMemoryFootprint() const
    {
        return before.capacity() + after.capacity();
    }
}
