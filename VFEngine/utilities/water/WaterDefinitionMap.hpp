#pragma once

#include "WaterTypes.hpp"
#include <unordered_map>
#include <vector>

namespace water
{
    struct WaterTileDefinition
    {
        float waterHeight = 0.0f;
        float waveIntensity = 1.0f;
        bool physicsEnabled = true;
    };

    class WaterDefinitionMap
    {
    public:
        void addDefinition(const TileCoord& coord, const WaterTileDefinition& def)
        {
            definitions[coord] = def;
        }

        void removeDefinition(const TileCoord& coord)
        {
            definitions.erase(coord);
        }

        [[nodiscard]] bool hasDefinition(const TileCoord& coord) const
        {
            return definitions.count(coord) > 0;
        }

        [[nodiscard]] const WaterTileDefinition* getDefinition(const TileCoord& coord) const
        {
            auto it = definitions.find(coord);
            return it != definitions.end() ? &it->second : nullptr;
        }

        template <typename Func>
        void forEachDefinition(Func&& func) const
        {
            for (const auto& [coord, def] : definitions)
            {
                func(coord, def);
            }
        }

        void clear() { definitions.clear(); }
        [[nodiscard]] size_t size() const { return definitions.size(); }

    private:
        std::unordered_map<TileCoord, WaterTileDefinition, TileCoordHash> definitions;
    };
}
