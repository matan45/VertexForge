#pragma once
#include "../math/RayBVH.hpp"
#include "../resource/Types.hpp"
#include <vector>
#include <cstdint>
#include <functional>

namespace lightbake
{
    struct TexelSample
    {
        glm::vec3 worldPosition{0.0f};
        glm::vec3 worldNormal{0.0f};
        uint32_t triangleIdx = UINT32_MAX; // Invalid = texel not mapped
        bool valid = false;
    };

    struct LightmapConfig
    {
        float texelsPerUnit = 16.0f;
        uint32_t padding = 2; // Texel padding between charts
        uint32_t minResolution = 4; // Min resolution per entity
        uint32_t maxResolution = 2048; // Max resolution per entity
        uint32_t maxAtlasSize = 4096; // Max atlas dimension
    };

    class LightmapAtlas
    {
    private:
        resource::LightmapData lightmapData;
        std::vector<TexelSample> texelSamples;

        struct EntityChart
        {
            uint32_t entityId;
            uint32_t width;
            uint32_t height;
            float surfaceArea;
            std::vector<uint32_t> triangleIndices; // Indices into RayBVH triangles
        };

        struct SkylineNode
        {
            uint32_t x, y, width;
        };

    public:
        LightmapAtlas() = default;

        bool build(const math::RayBVH& bvh, const LightmapConfig& config = {});

        const resource::LightmapData& getLightmapData() const { return lightmapData; }
        resource::LightmapData& getLightmapData() { return lightmapData; }

        const std::vector<TexelSample>& getTexelSamples() const { return texelSamples; }

        uint32_t getWidth() const { return lightmapData.width; }
        uint32_t getHeight() const { return lightmapData.height; }

        bool isBuilt() const { return lightmapData.width > 0 && lightmapData.height > 0; }

        void clear();

        static bool save(const resource::LightmapData& data, const std::string& path);
        static resource::LightmapData load(const std::string& path);

    private:
        uint32_t computeChartResolution(float surfaceArea, const LightmapConfig& config, bool isTerrain = false) const;

        static uint32_t nextPowerOf2(uint32_t v);

        bool skylineInsert(std::vector<SkylineNode>& skyline, uint32_t rectW, uint32_t rectH,
                           uint32_t atlasW, uint32_t atlasH, uint32_t& outX, uint32_t& outY);

        std::vector<EntityChart> buildEntityCharts(const math::RayBVH& bvh, const LightmapConfig& config);

        bool packCharts(std::vector<EntityChart>& charts, const math::RayBVH& bvh, const LightmapConfig& config);

        void rasterizeChart(const EntityChart& chart, const math::RayBVH& bvh,
                            uint32_t atlasX, uint32_t atlasY,
                            uint32_t chartW, uint32_t chartH);
    };
}
