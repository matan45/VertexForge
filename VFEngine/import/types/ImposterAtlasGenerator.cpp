#include "ImposterAtlasGenerator.hpp"
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace importTypes
{
    glm::uvec2 ImposterAtlasGenerator::calculateAtlasDimensions(const ImposterAtlasConfig& config)
    {
        uint32_t totalViews = config.horizontalAngles * config.verticalAngles;
        uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(totalViews))));
        uint32_t rows = static_cast<uint32_t>(std::ceil(static_cast<double>(totalViews) / cols));

        uint32_t width = cols * config.viewResolution;
        uint32_t height = rows * config.viewResolution;

        return { width, height };
    }

    ImposterAtlasData ImposterAtlasGenerator::generateAtlasLayout(const ImposterAtlasConfig& config)
    {
        ImposterAtlasData atlas;
        atlas.config = config;

        glm::uvec2 dims = calculateAtlasDimensions(config);
        atlas.atlasWidth = dims.x;
        atlas.atlasHeight = dims.y;

        uint32_t totalViews = config.horizontalAngles * config.verticalAngles;
        uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(totalViews))));

        float invAtlasW = 1.0f / static_cast<float>(atlas.atlasWidth);
        float invAtlasH = 1.0f / static_cast<float>(atlas.atlasHeight);
        float viewUvW = static_cast<float>(config.viewResolution) * invAtlasW;
        float viewUvH = static_cast<float>(config.viewResolution) * invAtlasH;

        // Compute vertical angle step
        // For verticalAngles=1 use 0 elevation; otherwise spread from 0 to pi/3 (60 degrees)
        float vertStep = (config.verticalAngles > 1)
            ? (glm::pi<float>() / 3.0f) / static_cast<float>(config.verticalAngles - 1)
            : 0.0f;

        float horizStep = glm::two_pi<float>() / static_cast<float>(config.horizontalAngles);

        atlas.views.reserve(totalViews);

        for (uint32_t v = 0; v < config.verticalAngles; ++v)
        {
            float vertAngle = static_cast<float>(v) * vertStep;

            for (uint32_t h = 0; h < config.horizontalAngles; ++h)
            {
                float horizAngle = static_cast<float>(h) * horizStep;

                uint32_t viewIndex = v * config.horizontalAngles + h;
                uint32_t col = viewIndex % cols;
                uint32_t row = viewIndex / cols;

                float uvX = static_cast<float>(col * config.viewResolution) * invAtlasW;
                float uvY = static_cast<float>(row * config.viewResolution) * invAtlasH;

                ImposterViewInfo view;
                view.horizontalAngle = horizAngle;
                view.verticalAngle = vertAngle;
                view.uvRect = glm::vec4(uvX, uvY, viewUvW, viewUvH);

                atlas.views.push_back(view);
            }
        }

        // Allocate placeholder color data (transparent black)
        size_t pixelCount = static_cast<size_t>(atlas.atlasWidth) * atlas.atlasHeight;
        atlas.colorData.resize(pixelCount * 4, 0);

        // Allocate placeholder normal data if requested
        if (config.generateNormalMap)
        {
            atlas.normalData.resize(pixelCount * 4, 0);

            // Fill normals with default facing-camera normal (0, 0, 1) encoded as (128, 128, 255, 255)
            for (size_t i = 0; i < pixelCount; ++i)
            {
                size_t offset = i * 4;
                atlas.normalData[offset + 0] = 128; // X
                atlas.normalData[offset + 1] = 128; // Y
                atlas.normalData[offset + 2] = 255; // Z
                atlas.normalData[offset + 3] = 255; // A
            }
        }

        return atlas;
    }
}
