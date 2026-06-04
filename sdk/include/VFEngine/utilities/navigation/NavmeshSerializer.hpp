#pragma once
#include "NavmeshData.hpp"
#include <string>
#include <vector>

namespace navigation
{
    class NavmeshSerializer
    {
    public:
        static bool save(const std::string& filePath,
                         const NavmeshFileHeader& header,
                         const std::vector<NavmeshTileData>& tiles);

        static bool load(const std::string& filePath,
                         NavmeshFileHeader& outHeader,
                         std::vector<NavmeshTileData>& outTiles);
    };
}
