#include "FractureProcessor.hpp"
#include <destruction/VoronoiFracture.hpp>
#include <destruction/ConvexHullGenerator.hpp>
#include <destruction/DestructionTypes.hpp>
#include <print/Log.hpp>

namespace types
{
    namespace
    {
        destruction::FractureResult runVoronoiFracture(
            const resource::MeshData& inputMesh,
            const importConfig::FractureImportConfig& config,
            FractureProgressCallback progressCallback,
            std::atomic<bool>* cancelFlag)
        {
            destruction::FractureConfig fractureConfig;
            fractureConfig.cellCount = config.fragmentCount;
            fractureConfig.randomSeed = config.randomSeed;
            fractureConfig.innerUVScale = config.innerUVScale;
            fractureConfig.generateConvexHulls = config.generateConvexHulls;

            switch (config.seedDistribution)
            {
            case importConfig::FractureSeedDistribution::Uniform:
                fractureConfig.seedDistribution = destruction::SeedDistribution::Uniform;
                break;
            case importConfig::FractureSeedDistribution::Clustered:
                fractureConfig.seedDistribution = destruction::SeedDistribution::Clustered;
                fractureConfig.clusterParams.clusterCount = config.clusterCount;
                fractureConfig.clusterParams.clusterRadius = config.clusterRadius;
                break;
            }

            auto fractureCallback = [&](float progress, std::string_view stage)
            {
                if (progressCallback)
                {
                    progressCallback(progress * 0.7f, stage);
                }
            };

            return destruction::VoronoiFracture::fracture(
                inputMesh, fractureConfig, nullptr, cancelFlag);
        }

        asset::FractureMetadata buildFractureMetadata(
            const destruction::FractureResult& fractureResult,
            const importConfig::FractureImportConfig& config)
        {
            asset::FractureMetadata metadata;
            metadata.fragmentCount = static_cast<uint32_t>(fractureResult.fragments.size());
            metadata.seedDistribution = static_cast<uint32_t>(config.seedDistribution);
            metadata.randomSeed = config.randomSeed;
            metadata.innerUVScale = config.innerUVScale;

            for (const auto& frag : fractureResult.fragments)
            {
                asset::FragmentPhysicsInfo info;
                info.centerOfMass = frag.centerOfMass;
                info.volume = frag.volume;

                glm::vec3 bmin(std::numeric_limits<float>::max());
                glm::vec3 bmax(std::numeric_limits<float>::lowest());
                if (!frag.mesh.lodLevels.empty())
                {
                    for (const auto& v : frag.mesh.lodLevels[0].vertices)
                    {
                        bmin = glm::min(bmin, v.position);
                        bmax = glm::max(bmax, v.position);
                    }
                }
                info.bboxMin = bmin;
                info.bboxMax = bmax;

                metadata.fragments.push_back(info);
            }

            for (uint32_t i = 0; i < static_cast<uint32_t>(fractureResult.fragments.size()); ++i)
            {
                for (const auto& neighbor : fractureResult.fragments[i].neighbors)
                {
                    if (neighbor.neighborIndex > i)
                    {
                        metadata.connectivity.emplace_back(i, neighbor.neighborIndex, neighbor.sharedArea);
                    }
                }
            }

            return metadata;
        }
    }

    FractureProcessor::Result FractureProcessor::process(
        const resource::MeshData& inputMesh,
        const importConfig::FractureImportConfig& config,
        FractureProgressCallback progressCallback,
        std::atomic<bool>* cancelFlag)
    {
        Result result;

        auto fractureResult = runVoronoiFracture(inputMesh, config, progressCallback, cancelFlag);

        if (!fractureResult.success)
        {
            result.errorMessage = fractureResult.errorMessage;
            vfLogWarning("FractureProcessor: fracture failed: {}", result.errorMessage);
            return result;
        }

        if (cancelFlag && cancelFlag->load())
        {
            result.errorMessage = "Cancelled";
            return result;
        }

        if (config.generateConvexHulls)
        {
            if (progressCallback)
                progressCallback(0.7f, "Generating convex hulls");

            destruction::FragmentHullConfig hullConfig;
            hullConfig.maxVerticesPerHull = 32;
            hullConfig.resolution = 10000;
            destruction::ConvexHullGenerator::generateBatch(fractureResult.fragments, hullConfig);
        }

        if (progressCallback)
            progressCallback(0.85f, "Building metadata");

        result.metadata = buildFractureMetadata(fractureResult, config);
        result.fragmentMeshes = destruction::VoronoiFracture::toMeshesData(fractureResult);
        result.success = true;

        if (progressCallback)
            progressCallback(1.0f, "Complete");

        vfLogInfo("FractureProcessor: generated {} fragments with {} connectivity edges",
                    result.metadata.fragmentCount, result.metadata.connectivity.size());

        return result;
    }
}
