#pragma once

#include "WorldExport.hpp"
#include "HLODTypes.hpp"
#include "HLODSerialization.hpp"
#include "WorldSector.hpp"
#include "../resource/Types.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace world
{
#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_WORLD_API HLODGenerator
    {
    public:
        // Generate HLOD proxy for a single sector (tier 0)
        bool generateForSector(const SectorCoord& coord,
                               const std::string& sectorFilePath,
                               const std::string& workingDirectory,
                               const HLODTierConfig& tierConfig,
                               const std::string& outputFilePath,
                               HLODProgressCallback progressCallback = nullptr);

        // Generate HLOD proxy for a group of sectors (tier 1+)
        bool generateForCell(const HLODCellCoord& cellCoord,
                             const std::vector<std::string>& sectorFilePaths,
                             const std::string& workingDirectory,
                             const HLODTierConfig& tierConfig,
                             const SectorConfig& sectorConfig,
                             const std::string& outputFilePath,
                             HLODProgressCallback progressCallback = nullptr);

    private:
        struct MergedSubmesh
        {
            std::string materialPath;
            std::vector<resource::Vertex> vertices;
            std::vector<uint32_t> indices;
        };

        // Collect mesh geometry from a sector file, transformed to world space
        bool collectSectorGeometry(const std::string& sectorFilePath,
                                   const std::string& workingDirectory,
                                   std::unordered_map<std::string, MergedSubmesh>& materialGroups);

        // Simplify a merged submesh to target ratio
        void simplifySubmesh(std::vector<resource::Vertex>& vertices,
                             std::vector<uint32_t>& indices,
                             float targetRatio);

        // Build the final HLOD file data from material groups
        bool buildHLODFileData(const HLODCellCoord& cellCoord,
                               const HLODTierConfig& tierConfig,
                               std::unordered_map<std::string, MergedSubmesh>& materialGroups,
                               HLODFileData& outData);
    };
#pragma warning(pop)

} // namespace world
