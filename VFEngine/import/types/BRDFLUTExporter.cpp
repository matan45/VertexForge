#include "print/Log.hpp"
#include "BRDFLUTExporter.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/Types.hpp"
#include "config/Config.hpp"
#include "threading/JobSystem.hpp"
#include "asset/AssetMetadata.hpp"
#include "asset/AssetMetadataSerializer.hpp"

#include <glm/glm.hpp>
#include <cmath>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <future>

namespace
{
    constexpr uint32_t LUT_SIZE = 512;
    constexpr uint32_t SAMPLE_COUNT = 1024u;
    constexpr float PI = 3.14159265359f;

    float radicalInverse_VdC(uint32_t bits)
    {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return static_cast<float>(bits) * 2.3283064365386963e-10f;
    }

    glm::vec2 hammersley(uint32_t i, uint32_t N)
    {
        return {static_cast<float>(i) / static_cast<float>(N), radicalInverse_VdC(i)};
    }

    glm::vec3 importanceSampleGGX(glm::vec2 Xi, glm::vec3 N, float roughness)
    {
        float a = roughness * roughness;
        float phi = 2.0f * PI * Xi.x;
        float cosTheta = std::sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
        float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

        glm::vec3 H;
        H.x = std::cos(phi) * sinTheta;
        H.y = std::sin(phi) * sinTheta;
        H.z = cosTheta;

        glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 tangent = glm::normalize(glm::cross(up, N));
        glm::vec3 bitangent = glm::cross(N, tangent);

        return glm::normalize(tangent * H.x + bitangent * H.y + N * H.z);
    }

    float geometrySchlickGGX(float NdotV, float roughness)
    {
        float k = (roughness * roughness) / 2.0f;
        return NdotV / (NdotV * (1.0f - k) + k);
    }

    float geometrySmith(glm::vec3 N, glm::vec3 V, glm::vec3 L, float roughness)
    {
        float NdotV = std::max(glm::dot(N, V), 0.0f);
        float NdotL = std::max(glm::dot(N, L), 0.0f);
        return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
    }

    std::pair<float, float> integrateBRDF(float NdotV, float roughness)
    {
        glm::vec3 V(std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);
        float A = 0.0f, B = 0.0f;
        glm::vec3 N(0.0f, 0.0f, 1.0f);

        for (uint32_t i = 0u; i < SAMPLE_COUNT; ++i)
        {
            glm::vec2 Xi = hammersley(i, SAMPLE_COUNT);
            glm::vec3 H = importanceSampleGGX(Xi, N, roughness);
            glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

            float NdotL = std::max(L.z, 0.0f);
            float NdotH = std::max(H.z, 0.0f);
            float VdotH = std::max(glm::dot(V, H), 0.0f);

            if (NdotL > 0.0f)
            {
                float G = geometrySmith(N, V, L, roughness);
                float G_Vis = (G * VdotH) / (NdotH * NdotV);
                float Fc = std::pow(1.0f - VdotH, 5.0f);
                A += (1.0f - Fc) * G_Vis;
                B += Fc * G_Vis;
            }
        }
        return {A / static_cast<float>(SAMPLE_COUNT), B / static_cast<float>(SAMPLE_COUNT)};
    }

    void computeRowRange(std::vector<unsigned char>& pixelData, uint32_t startRow, uint32_t endRow)
    {
        for (uint32_t y = startRow; y < endRow; ++y)
        {
            for (uint32_t x = 0; x < LUT_SIZE; ++x)
            {
                float NdotV = (static_cast<float>(x) + 0.5f) / static_cast<float>(LUT_SIZE);
                float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(LUT_SIZE);

                auto [scaleA, biasB] = integrateBRDF(NdotV, roughness);

                size_t idx = (static_cast<size_t>(y) * LUT_SIZE + x) * 4;
                pixelData[idx + 0] = 0;
                pixelData[idx + 1] = static_cast<unsigned char>(std::clamp(biasB, 0.0f, 1.0f) * 255.0f + 0.5f);
                pixelData[idx + 2] = static_cast<unsigned char>(std::clamp(scaleA, 0.0f, 1.0f) * 255.0f + 0.5f);
                pixelData[idx + 3] = 255;
            }
        }
    }
}

namespace types
{
    void BRDFLUTExporter::generateAndSave(const std::string& outputPath)
    {
        vfLogInfo("BRDF LUT: starting parallel computation ({}x{}, {} samples)...", LUT_SIZE, LUT_SIZE, SAMPLE_COUNT);

        std::vector<unsigned char> pixelData(static_cast<size_t>(LUT_SIZE) * LUT_SIZE * 4);

        // Split rows across parallel jobs
        constexpr uint32_t NUM_CHUNKS = 16;
        constexpr uint32_t ROWS_PER_CHUNK = LUT_SIZE / NUM_CHUNKS;

        std::vector<std::future<void>> futures;
        futures.reserve(NUM_CHUNKS);

        for (uint32_t chunk = 0; chunk < NUM_CHUNKS; ++chunk)
        {
            uint32_t startRow = chunk * ROWS_PER_CHUNK;
            uint32_t endRow = (chunk == NUM_CHUNKS - 1) ? LUT_SIZE : startRow + ROWS_PER_CHUNK;

            futures.push_back(threading::JobSystem::instance().submit(
                [&pixelData, startRow, endRow]()
                {
                    computeRowRange(pixelData, startRow, endRow);
                },
                threading::JobPriority::HIGH
            ));
        }

        for (auto& f : futures)
        {
            f.get();
        }

        vfLogInfo("BRDF LUT: computation complete, writing to file...");

        // Write vfImage binary format
        std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path());
        std::ofstream outFile(outputPath, std::ios::binary);

        if (!outFile)
        {
            vfLogError("Failed to open file for writing BRDF LUT: {}", outputPath);
            return;
        }

        // Header
        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(resource::FileType::TEXTURE));
        resource::endian::writeLE<uint32_t>(outFile, Version::major);
        resource::endian::writeLE<uint32_t>(outFile, Version::minor);
        resource::endian::writeLE<uint32_t>(outFile, Version::patch);
        resource::endian::writeLE<uint32_t>(outFile, LUT_SIZE);
        resource::endian::writeLE<uint32_t>(outFile, LUT_SIZE);
        resource::endian::writeLE<uint32_t>(outFile, 4u);
        resource::endian::writeLE<uint32_t>(outFile, 1u);

        // Compression format: Uncompressed
        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(resource::TextureCompressionFormat::Uncompressed));

        // Mip 0: width, height, dataSize, pixels
        resource::endian::writeLE<uint32_t>(outFile, LUT_SIZE);
        resource::endian::writeLE<uint32_t>(outFile, LUT_SIZE);
        resource::endian::writeLE<uint32_t>(outFile, static_cast<uint32_t>(pixelData.size()));

        outFile.write(reinterpret_cast<const char*>(pixelData.data()), pixelData.size());

        outFile.close();
        vfLogInfo("BRDF LUT saved to: {}", outputPath);

        // Create .vfmeta sidecar so the content browser can identify this asset
        auto metaPath = asset::AssetMetadataSerializer::getMetaPath(outputPath);
        if (!std::filesystem::exists(metaPath))
        {
            asset::AssetMetadata metadata;
            metadata.guid = asset::AssetGUID::generate();
            metadata.type = resource::AssetType::Texture;
            metadata.importSourcePath = "generated:brdf_lut";

            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &time);
#else
            localtime_r(&time, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
            metadata.importTimestamp = buf;

            asset::AssetMetadataSerializer::save(metadata, metaPath);
            vfLogInfo("BRDF LUT metadata saved to: {}", metaPath.string());
        }
    }
}
