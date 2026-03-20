#include "TerrainSVTCompositor.hpp"
#include "resource/Types.hpp"
#include "print/Log.hpp"
#include <ispc_texcomp.h>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace render::svt
{
    void TerrainSVTCompositor::init(const SVTConfig& config,
                                     const glm::vec2& terrainWorldMin,
                                     const glm::vec2& terrainWorldMax)
    {
        this->config = config;
        this->terrainWorldMin = terrainWorldMin;
        this->terrainWorldMax = terrainWorldMax;
        initialized = true;
    }

    void TerrainSVTCompositor::setLayers(std::vector<TerrainLayerSource> layers)
    {
        this->layers = std::move(layers);
    }

    glm::vec2 TerrainSVTCompositor::getSVTScale() const
    {
        glm::vec2 worldSize = terrainWorldMax - terrainWorldMin;
        if (worldSize.x <= 0.0f || worldSize.y <= 0.0f) return glm::vec2(1.0f);
        return glm::vec2(1.0f / worldSize.x, 1.0f / worldSize.y);
    }

    glm::vec2 TerrainSVTCompositor::getSVTOffset() const
    {
        glm::vec2 scale = getSVTScale();
        return -terrainWorldMin * scale;
    }

    TileBounds TerrainSVTCompositor::computeTileBounds(const VirtualTileCoord& coord) const
    {
        TileBounds bounds;
        bounds.physTileSize = SVT_PHYSICAL_TILE_SIZE;

        uint32_t tileSize = SVT_TILE_SIZE;
        uint32_t border = SVT_BORDER_SIZE;

        // Tiles per side at this mip level
        uint32_t tilesPerSide = computeTilesPerMipSide(coord.mipLevel,
            config.virtualTextureSizeLog2, config.tileSizeLog2);
        if (tilesPerSide == 0) tilesPerSide = 1;

        // Virtual UV bounds for this tile (including border)
        float tileUVSize = 1.0f / static_cast<float>(tilesPerSide);
        bounds.texelUVSize = tileUVSize / static_cast<float>(tileSize);
        float borderUV = static_cast<float>(border) * bounds.texelUVSize;

        bounds.uvMinX = static_cast<float>(coord.x) * tileUVSize - borderUV;
        bounds.uvMinY = static_cast<float>(coord.y) * tileUVSize - borderUV;
        bounds.worldSize = terrainWorldMax - terrainWorldMin;

        return bounds;
    }

    void TerrainSVTCompositor::compositeTileTexels(const TileBounds& bounds,
                                                    const TerrainWeightMapRegion& weightRegion,
                                                    std::vector<uint8_t>& albedoRGBA,
                                                    std::vector<uint8_t>& normalRGBA,
                                                    std::vector<uint8_t>& ormRGBA) const
    {
        auto clampByte = [](float v) -> uint8_t {
            return static_cast<uint8_t>(std::clamp(v * 255.0f, 0.0f, 255.0f));
        };

        for (uint32_t py = 0; py < bounds.physTileSize; ++py)
        {
            for (uint32_t px = 0; px < bounds.physTileSize; ++px)
            {
                // Virtual UV for this texel
                float u = bounds.uvMinX + (static_cast<float>(px) + 0.5f) * bounds.texelUVSize;
                float v = bounds.uvMinY + (static_cast<float>(py) + 0.5f) * bounds.texelUVSize;

                // World position
                float worldX = terrainWorldMin.x + u * bounds.worldSize.x;
                float worldZ = terrainWorldMin.y + v * bounds.worldSize.y;

                // Sample weights
                float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                if (weightRegion.data)
                {
                    sampleWeights(weightRegion, worldX, worldZ, weights);
                }
                else
                {
                    weights[0] = 1.0f;  // Default: 100% first layer
                }

                // Accumulate weighted material properties
                glm::vec3 albedo(0.0f);
                glm::vec3 normal(0.0f);
                float roughness = 0.0f, metallic = 0.0f, ao = 0.0f;
                float totalWeight = 0.0f;

                uint32_t packedLI = weightRegion.packedLayerIndices;

                for (int ch = 0; ch < 4; ++ch)
                {
                    float w = weights[ch];
                    if (w < 0.001f) continue;

                    uint32_t paletteIdx = (packedLI >> (ch * 8)) & 0xFFu;
                    if (paletteIdx >= layers.size()) continue;

                    const auto& layer = layers[paletteIdx];

                    // Sample layer textures
                    glm::vec4 layerAlbedo = sampleLayerTexture(
                        layer.albedoTexture.get(), worldX, worldZ, layer.tilingScale);
                    glm::vec4 layerNormal = sampleLayerTexture(
                        layer.normalTexture.get(), worldX, worldZ, layer.tilingScale);

                    float layerRoughness, layerMetallic, layerAO;
                    if (layer.ormTexture)
                    {
                        glm::vec4 ormSample = sampleLayerTexture(
                            layer.ormTexture.get(), worldX, worldZ, layer.tilingScale);
                        layerAO = ormSample.r;
                        layerRoughness = ormSample.g;
                        layerMetallic = ormSample.b;
                    }
                    else
                    {
                        layerAO = layer.ao;
                        layerRoughness = layer.roughness;
                        layerMetallic = layer.metallic;
                    }

                    albedo += glm::vec3(layerAlbedo) * w;
                    normal += glm::vec3(layerNormal.r * 2.0f - 1.0f,
                                         layerNormal.g * 2.0f - 1.0f,
                                         layerNormal.b * 2.0f - 1.0f) * w;
                    roughness += layerRoughness * w;
                    metallic += layerMetallic * w;
                    ao += layerAO * w;
                    totalWeight += w;
                }

                if (totalWeight > 0.001f)
                {
                    float invW = 1.0f / totalWeight;
                    albedo *= invW;
                    normal = glm::normalize(normal);
                    roughness *= invW;
                    metallic *= invW;
                    ao *= invW;
                }
                else
                {
                    albedo = glm::vec3(0.5f);
                    normal = glm::vec3(0.0f, 0.0f, 1.0f);
                    roughness = 0.5f;
                    metallic = 0.0f;
                    ao = 1.0f;
                }

                // Write to RGBA8 buffers
                size_t idx = (static_cast<size_t>(py) * bounds.physTileSize + px) * 4;

                albedoRGBA[idx + 0] = clampByte(albedo.r);
                albedoRGBA[idx + 1] = clampByte(albedo.g);
                albedoRGBA[idx + 2] = clampByte(albedo.b);
                albedoRGBA[idx + 3] = 255;

                // Normal: re-encode from [-1,1] to [0,1]
                normalRGBA[idx + 0] = clampByte(normal.x * 0.5f + 0.5f);
                normalRGBA[idx + 1] = clampByte(normal.y * 0.5f + 0.5f);
                normalRGBA[idx + 2] = clampByte(normal.z * 0.5f + 0.5f);
                normalRGBA[idx + 3] = 255;

                // ORM: R=AO, G=Roughness, B=Metallic
                ormRGBA[idx + 0] = clampByte(ao);
                ormRGBA[idx + 1] = clampByte(roughness);
                ormRGBA[idx + 2] = clampByte(metallic);
                ormRGBA[idx + 3] = 255;
            }
        }
    }

    SVTTileData TerrainSVTCompositor::generateTile(const VirtualTileCoord& coord)
    {
        SVTTileData result;
        result.coord = coord;

        if (!initialized || layers.empty())
        {
            result.valid = false;
            return result;
        }

        TileBounds bounds = computeTileBounds(coord);

        TerrainWeightMapRegion weightRegion;

        // Allocate RGBA8 buffers for compositing
        size_t pixelCount = static_cast<size_t>(bounds.physTileSize) * bounds.physTileSize;
        std::vector<uint8_t> albedoRGBA(pixelCount * 4, 128);
        std::vector<uint8_t> normalRGBA(pixelCount * 4, 128);
        std::vector<uint8_t> ormRGBA(pixelCount * 4, 128);

        compositeTileTexels(bounds, weightRegion, albedoRGBA, normalRGBA, ormRGBA);

        // BC7-compress the tiles
        result.albedoData = compressTileBC7(albedoRGBA, true);
        result.normalData = compressTileBC7(normalRGBA, false);
        result.ormData = compressTileBC7(ormRGBA, false);
        result.valid = !result.albedoData.empty() && !result.normalData.empty() && !result.ormData.empty();

        return result;
    }

    glm::vec4 TerrainSVTCompositor::sampleLayerTexture(const resource::TextureData* texture,
                                                         float worldX, float worldZ,
                                                         float tilingScale) const
    {
        if (!texture || texture->mipData.empty() || texture->width == 0)
        {
            return glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
        }

        // World-space tiling
        float u = worldX * tilingScale;
        float v = worldZ * tilingScale;

        // Wrap to [0,1)
        u = u - std::floor(u);
        v = v - std::floor(v);

        // Nearest-neighbor sample from mip 0
        const auto& mip0 = texture->mipData[0];
        uint32_t w = texture->width;
        uint32_t h = texture->height;
        uint32_t channels = texture->numbersOfChannels;
        if (channels == 0) channels = 4;

        uint32_t px = std::min(static_cast<uint32_t>(u * w), w - 1);
        uint32_t py = std::min(static_cast<uint32_t>(v * h), h - 1);

        // Only uncompressed RGBA8 data can be sampled directly on CPU
        if (texture->compressionFormat == resource::TextureCompressionFormat::Uncompressed)
        {
            size_t idx = (static_cast<size_t>(py) * w + px) * channels;
            if (idx + 2 < mip0.data.size())
            {
                return glm::vec4(
                    mip0.data[idx + 0] / 255.0f,
                    mip0.data[idx + 1] / 255.0f,
                    mip0.data[idx + 2] / 255.0f,
                    (channels >= 4 && idx + 3 < mip0.data.size()) ? mip0.data[idx + 3] / 255.0f : 1.0f
                );
            }
        }

        // Fallback for compressed textures
        return glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    }

    void TerrainSVTCompositor::sampleWeights(const TerrainWeightMapRegion& region,
                                              float worldX, float worldZ,
                                              float weights[4]) const
    {
        if (!region.data || region.resolution == 0)
        {
            weights[0] = 1.0f;
            weights[1] = weights[2] = weights[3] = 0.0f;
            return;
        }

        // Map world coords to weight map UV
        glm::vec2 regionSize = region.worldMax - region.worldMin;
        if (regionSize.x <= 0.0f || regionSize.y <= 0.0f)
        {
            weights[0] = 1.0f;
            weights[1] = weights[2] = weights[3] = 0.0f;
            return;
        }

        float u = std::clamp((worldX - region.worldMin.x) / regionSize.x, 0.0f, 1.0f);
        float v = std::clamp((worldZ - region.worldMin.y) / regionSize.y, 0.0f, 1.0f);

        uint32_t res = region.resolution;
        uint32_t px = std::min(static_cast<uint32_t>(u * (res - 1)), res - 1);
        uint32_t py = std::min(static_cast<uint32_t>(v * (res - 1)), res - 1);

        // RGBA packed: 4 bytes per texel
        size_t idx = (static_cast<size_t>(py) * res + px) * 4;
        for (int ch = 0; ch < 4; ++ch)
        {
            weights[ch] = region.data[idx + ch] / 255.0f;
        }
    }

    std::vector<uint8_t> TerrainSVTCompositor::compressTileBC7(const std::vector<uint8_t>& rgbaData,
                                                                 bool /*srgb*/) const
    {
        uint32_t w = SVT_PHYSICAL_TILE_SIZE;
        uint32_t h = SVT_PHYSICAL_TILE_SIZE;
        size_t expectedSize = static_cast<size_t>(w) * h * 4;
        if (rgbaData.size() < expectedSize)
        {
            vfLogWarning("SVT compressTileBC7: input size {} < expected {}", rgbaData.size(), expectedSize);
            return {};
        }

        // Pad to block-aligned dimensions (already 136 = 34*4, so aligned)
        uint32_t blocksX = (w + 3) / 4;
        uint32_t blocksY = (h + 3) / 4;
        uint32_t totalBlocks = blocksX * blocksY;
        uint32_t compressedSize = totalBlocks * 16; // 16 bytes per BC7 block

        rgba_surface surface{};
        surface.ptr = const_cast<uint8_t*>(rgbaData.data());
        surface.width = static_cast<int32_t>(w);
        surface.height = static_cast<int32_t>(h);
        surface.stride = static_cast<int32_t>(w * 4);

        bc7_enc_settings settings{};
        GetProfile_ultrafast(&settings); // Fast for runtime compositing

        std::vector<uint8_t> compressed(compressedSize);
        CompressBlocksBC7(&surface, compressed.data(), &settings);

        return compressed;
    }
}
