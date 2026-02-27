#include "LightmapAtlas.hpp"
#include "../resource/EndianUtils.hpp"
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <cmath>

namespace lightbake
{
    bool LightmapAtlas::build(const math::RayBVH& bvh, const LightmapConfig& config)
    {
        clear();

        if (!bvh.isBuilt())
        {
            return false;
        }

        // Group triangles by entity
        std::unordered_map<uint32_t, EntityChart> chartMap;
        const auto& triangles = bvh.getTriangles();

        for (uint32_t i = 0; i < static_cast<uint32_t>(triangles.size()); ++i)
        {
            const auto& tri = triangles[i];
            auto& chart = chartMap[tri.entityId];
            chart.entityId = tri.entityId;
            chart.surfaceArea += tri.computeArea();
            chart.triangleIndices.push_back(i);
        }

        // Compute per-entity chart resolutions
        std::vector<EntityChart> charts;
        charts.reserve(chartMap.size());
        for (auto& [id, chart] : chartMap)
        {
            uint32_t res = computeChartResolution(chart.surfaceArea, config);
            chart.width = res;
            chart.height = res;
            charts.push_back(std::move(chart));
        }

        // Sort charts by height (descending) for better packing
        std::sort(charts.begin(), charts.end(), [](const EntityChart& a, const EntityChart& b)
        {
            return a.height > b.height;
        });

        // Determine atlas size via skyline bin packing
        // Start with a reasonable initial size and grow if needed
        uint32_t atlasW = 256;
        uint32_t atlasH = 256;

        // Estimate minimum atlas area
        uint32_t totalArea = 0;
        for (const auto& chart : charts)
        {
            uint32_t paddedW = chart.width + config.padding * 2;
            uint32_t paddedH = chart.height + config.padding * 2;
            totalArea += paddedW * paddedH;
        }

        // Start from an atlas that can at least fit the total area
        while (atlasW * atlasH < totalArea)
        {
            if (atlasW <= atlasH)
                atlasW *= 2;
            else
                atlasH *= 2;
        }

        atlasW = std::min(atlasW, config.maxAtlasSize);
        atlasH = std::min(atlasH, config.maxAtlasSize);

        // Try to pack all charts
        bool packed = false;
        while (!packed && atlasW <= config.maxAtlasSize && atlasH <= config.maxAtlasSize)
        {
            std::vector<SkylineNode> skyline;
            skyline.push_back({0, 0, atlasW});

            packed = true;
            std::vector<std::pair<uint32_t, uint32_t>> positions(charts.size());

            for (size_t i = 0; i < charts.size(); ++i)
            {
                uint32_t paddedW = charts[i].width + config.padding * 2;
                uint32_t paddedH = charts[i].height + config.padding * 2;
                uint32_t outX, outY;

                if (!skylineInsert(skyline, paddedW, paddedH, atlasW, atlasH, outX, outY))
                {
                    packed = false;
                    break;
                }

                positions[i] = {outX + config.padding, outY + config.padding};
            }

            if (packed)
            {
                // Initialize lightmap data
                lightmapData_.width = atlasW;
                lightmapData_.height = atlasH;
                lightmapData_.channels = 3;
                lightmapData_.texels.resize(atlasW * atlasH * 3, 0.0f);
                texelSamples_.resize(atlasW * atlasH);

                // Fill entity regions and rasterize
                lightmapData_.entityRegions.reserve(charts.size());

                for (size_t i = 0; i < charts.size(); ++i)
                {
                    auto [px, py] = positions[i];

                    resource::LightmapEntityRegion region;
                    region.entityId = charts[i].entityId;
                    region.x = px;
                    region.y = py;
                    region.width = charts[i].width;
                    region.height = charts[i].height;
                    region.scaleOffset = glm::vec4(
                        static_cast<float>(charts[i].width) / static_cast<float>(atlasW),
                        static_cast<float>(charts[i].height) / static_cast<float>(atlasH),
                        static_cast<float>(px) / static_cast<float>(atlasW),
                        static_cast<float>(py) / static_cast<float>(atlasH)
                    );
                    lightmapData_.entityRegions.push_back(region);

                    rasterizeChart(charts[i], bvh, px, py, charts[i].width, charts[i].height);
                }
            }
            else
            {
                // Grow atlas
                if (atlasW <= atlasH)
                    atlasW *= 2;
                else
                    atlasH *= 2;
            }
        }

        if (!packed)
        {
            spdlog::error("[LightBake] Failed to pack all charts into atlas (max {}x{})", config.maxAtlasSize, config.maxAtlasSize);
            clear();
            return false;
        }

        spdlog::info("[LightBake] Atlas built: {}x{}, {} entity charts", atlasW, atlasH, charts.size());
        return true;
    }

    uint32_t LightmapAtlas::computeChartResolution(float surfaceArea, const LightmapConfig& config) const
    {
        // Resolution based on surface area: side = sqrt(area) * texelsPerUnit
        float side = std::sqrt(surfaceArea) * config.texelsPerUnit;
        uint32_t res = nextPowerOf2(static_cast<uint32_t>(std::ceil(side)));
        res = std::max(res, config.minResolution);
        res = std::min(res, config.maxResolution);
        return res;
    }

    uint32_t LightmapAtlas::nextPowerOf2(uint32_t v)
    {
        if (v == 0) return 1;
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
    }

    bool LightmapAtlas::skylineInsert(std::vector<SkylineNode>& skyline,
                                       uint32_t rectW, uint32_t rectH,
                                       uint32_t atlasW, uint32_t atlasH,
                                       uint32_t& outX, uint32_t& outY)
    {
        // Find the best position (lowest y that fits)
        int bestIdx = -1;
        uint32_t bestY = UINT32_MAX;
        uint32_t bestWidth = UINT32_MAX;

        for (size_t i = 0; i < skyline.size(); ++i)
        {
            // Check if rect fits starting at this node
            if (skyline[i].x + rectW > atlasW)
            {
                continue;
            }

            // Find the max y across the span this rect would occupy
            uint32_t maxY = skyline[i].y;
            uint32_t remainingWidth = rectW;
            size_t j = i;

            bool fits = true;
            while (remainingWidth > 0 && j < skyline.size())
            {
                maxY = std::max(maxY, skyline[j].y);
                if (maxY + rectH > atlasH)
                {
                    fits = false;
                    break;
                }

                uint32_t segWidth = std::min(skyline[j].width - (j == i ? 0 : 0), remainingWidth);
                if (j == i)
                {
                    segWidth = std::min(skyline[j].width, remainingWidth);
                }
                else
                {
                    segWidth = std::min(skyline[j].width, remainingWidth);
                }
                remainingWidth -= segWidth;
                ++j;
            }

            if (!fits || remainingWidth > 0)
            {
                continue;
            }

            if (maxY < bestY || (maxY == bestY && skyline[i].width < bestWidth))
            {
                bestIdx = static_cast<int>(i);
                bestY = maxY;
                bestWidth = skyline[i].width;
            }
        }

        if (bestIdx < 0)
        {
            return false;
        }

        outX = skyline[bestIdx].x;
        outY = bestY;

        // Insert new skyline node
        SkylineNode newNode{outX, outY + rectH, rectW};

        // Remove overlapping nodes and insert new one
        uint32_t rightEdge = outX + rectW;
        size_t i = static_cast<size_t>(bestIdx);

        // Trim/remove nodes covered by the new rect
        while (i < skyline.size() && skyline[i].x < rightEdge)
        {
            uint32_t nodeRight = skyline[i].x + skyline[i].width;

            if (nodeRight > rightEdge)
            {
                // Trim this node
                skyline[i].width = nodeRight - rightEdge;
                skyline[i].x = rightEdge;
                break;
            }
            else
            {
                // Remove this node entirely
                skyline.erase(skyline.begin() + i);
            }
        }

        skyline.insert(skyline.begin() + bestIdx, newNode);

        // Merge adjacent nodes with same height
        for (size_t k = 0; k + 1 < skyline.size();)
        {
            if (skyline[k].y == skyline[k + 1].y)
            {
                skyline[k].width += skyline[k + 1].width;
                skyline.erase(skyline.begin() + k + 1);
            }
            else
            {
                ++k;
            }
        }

        return true;
    }

    void LightmapAtlas::rasterizeChart(const EntityChart& chart, const math::RayBVH& bvh,
                                        uint32_t atlasX, uint32_t atlasY,
                                        uint32_t chartW, uint32_t chartH)
    {
        if (chartW == 0 || chartH == 0)
        {
            return;
        }

        // Compute AABB of all triangles in this chart (in their UV space)
        // Use a simple planar projection for UV mapping:
        // Map the entity's triangles into the chart using their existing UV0 coordinates
        const auto& triangles = bvh.getTriangles();

        // Find UV bounds for this entity's triangles
        glm::vec2 uvMin(std::numeric_limits<float>::max());
        glm::vec2 uvMax(std::numeric_limits<float>::lowest());

        for (uint32_t triIdx : chart.triangleIndices)
        {
            const auto& tri = triangles[triIdx];
            uvMin = glm::min(uvMin, glm::min(tri.uv0, glm::min(tri.uv1, tri.uv2)));
            uvMax = glm::max(uvMax, glm::max(tri.uv0, glm::max(tri.uv1, tri.uv2)));
        }

        glm::vec2 uvRange = uvMax - uvMin;
        if (uvRange.x < 1e-6f) uvRange.x = 1.0f;
        if (uvRange.y < 1e-6f) uvRange.y = 1.0f;

        // For each texel in the chart, find which triangle it maps to
        // and compute the world-space position and normal
        float invW = 1.0f / static_cast<float>(chartW);
        float invH = 1.0f / static_cast<float>(chartH);

        for (uint32_t ly = 0; ly < chartH; ++ly)
        {
            for (uint32_t lx = 0; lx < chartW; ++lx)
            {
                // Map texel center to UV space
                float u = uvMin.x + (static_cast<float>(lx) + 0.5f) * invW * uvRange.x;
                float v = uvMin.y + (static_cast<float>(ly) + 0.5f) * invH * uvRange.y;
                glm::vec2 sampleUV(u, v);

                // Find which triangle contains this UV point
                for (uint32_t triIdx : chart.triangleIndices)
                {
                    const auto& tri = triangles[triIdx];

                    // Compute barycentric coords in UV space
                    glm::vec2 v0 = tri.uv1 - tri.uv0;
                    glm::vec2 v1 = tri.uv2 - tri.uv0;
                    glm::vec2 v2 = sampleUV - tri.uv0;

                    float dot00 = glm::dot(v0, v0);
                    float dot01 = glm::dot(v0, v1);
                    float dot02 = glm::dot(v0, v2);
                    float dot11 = glm::dot(v1, v1);
                    float dot12 = glm::dot(v1, v2);

                    float denom = dot00 * dot11 - dot01 * dot01;
                    if (std::abs(denom) < 1e-10f) continue;

                    float invDenom = 1.0f / denom;
                    float baryU = (dot11 * dot02 - dot01 * dot12) * invDenom;
                    float baryV = (dot00 * dot12 - dot01 * dot02) * invDenom;

                    // Check if point is inside triangle (with small epsilon for edge cases)
                    constexpr float eps = -1e-4f;
                    if (baryU >= eps && baryV >= eps && (baryU + baryV) <= (1.0f - eps))
                    {
                        uint32_t atlasIdx = (atlasY + ly) * lightmapData_.width + (atlasX + lx);

                        texelSamples_[atlasIdx].worldPosition = tri.interpolatePosition(baryU, baryV);
                        texelSamples_[atlasIdx].worldNormal = tri.interpolateNormal(baryU, baryV);
                        texelSamples_[atlasIdx].triangleIdx = triIdx;
                        texelSamples_[atlasIdx].valid = true;
                        break; // Found the triangle for this texel
                    }
                }
            }
        }
    }

    void LightmapAtlas::clear()
    {
        lightmapData_ = {};
        texelSamples_.clear();
    }

    bool LightmapAtlas::save(const resource::LightmapData& data, const std::string& path)
    {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            spdlog::error("[LightBake] Failed to open file for writing: {}", path);
            return false;
        }

        // Header
        resource::endian::writeLE<uint8_t>(file, static_cast<uint8_t>(data.headerFileType));
        resource::endian::writeLE<uint32_t>(file, data.version.major);
        resource::endian::writeLE<uint32_t>(file, data.version.minor);
        resource::endian::writeLE<uint32_t>(file, data.version.patch);

        // Dimensions
        resource::endian::writeLE<uint32_t>(file, data.width);
        resource::endian::writeLE<uint32_t>(file, data.height);
        resource::endian::writeLE<uint32_t>(file, data.channels);

        // Texel data
        uint32_t texelCount = static_cast<uint32_t>(data.texels.size());
        resource::endian::writeLE<uint32_t>(file, texelCount);
        for (float val : data.texels)
        {
            resource::endian::writeLE<float>(file, val);
        }

        // Entity regions
        uint32_t regionCount = static_cast<uint32_t>(data.entityRegions.size());
        resource::endian::writeLE<uint32_t>(file, regionCount);
        for (const auto& region : data.entityRegions)
        {
            resource::endian::writeLE<uint32_t>(file, region.entityId);
            resource::endian::writeLE<uint32_t>(file, region.x);
            resource::endian::writeLE<uint32_t>(file, region.y);
            resource::endian::writeLE<uint32_t>(file, region.width);
            resource::endian::writeLE<uint32_t>(file, region.height);
            resource::endian::writeLE<float>(file, region.scaleOffset.x);
            resource::endian::writeLE<float>(file, region.scaleOffset.y);
            resource::endian::writeLE<float>(file, region.scaleOffset.z);
            resource::endian::writeLE<float>(file, region.scaleOffset.w);
        }

        return file.good();
    }

    resource::LightmapData LightmapAtlas::load(const std::string& path)
    {
        resource::LightmapData data;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            spdlog::error("[LightBake] Failed to open lightmap file: {}", path);
            return data;
        }

        // Header
        data.headerFileType = static_cast<resource::FileType>(resource::endian::readLE<uint8_t>(file));
        data.version.major = resource::endian::readLE<uint32_t>(file);
        data.version.minor = resource::endian::readLE<uint32_t>(file);
        data.version.patch = resource::endian::readLE<uint32_t>(file);

        // Dimensions
        data.width = resource::endian::readLE<uint32_t>(file);
        data.height = resource::endian::readLE<uint32_t>(file);
        data.channels = resource::endian::readLE<uint32_t>(file);

        // Texel data
        uint32_t texelCount = resource::endian::readLE<uint32_t>(file);
        data.texels.resize(texelCount);
        for (uint32_t i = 0; i < texelCount; ++i)
        {
            data.texels[i] = resource::endian::readLE<float>(file);
        }

        // Entity regions
        uint32_t regionCount = resource::endian::readLE<uint32_t>(file);
        data.entityRegions.resize(regionCount);
        for (uint32_t i = 0; i < regionCount; ++i)
        {
            auto& region = data.entityRegions[i];
            region.entityId = resource::endian::readLE<uint32_t>(file);
            region.x = resource::endian::readLE<uint32_t>(file);
            region.y = resource::endian::readLE<uint32_t>(file);
            region.width = resource::endian::readLE<uint32_t>(file);
            region.height = resource::endian::readLE<uint32_t>(file);
            region.scaleOffset.x = resource::endian::readLE<float>(file);
            region.scaleOffset.y = resource::endian::readLE<float>(file);
            region.scaleOffset.z = resource::endian::readLE<float>(file);
            region.scaleOffset.w = resource::endian::readLE<float>(file);
        }

        return data;
    }
}
