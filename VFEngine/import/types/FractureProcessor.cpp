#include "FractureProcessor.hpp"
#include <destruction/VoronoiFracture.hpp>
#include <destruction/ConvexHullGenerator.hpp>
#include <destruction/DestructionTypes.hpp>
#include <spdlog/spdlog.h>

namespace types
{
    FractureProcessor::Result FractureProcessor::process(
        const resource::MeshData& inputMesh,
        const importConfig::FractureImportConfig& config,
        FractureProgressCallback progressCallback,
        std::atomic<bool>* cancelFlag)
    {
        Result result;

        // Convert import config to fracture config
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

        // Run Voronoi fracture
        auto fractureCallback = [&](float progress, std::string_view stage)
        {
            if (progressCallback)
            {
                progressCallback(progress * 0.7f, stage);
            }
        };

        auto fractureResult = destruction::VoronoiFracture::fracture(
            inputMesh, fractureConfig, fractureCallback, cancelFlag);

        if (!fractureResult.success)
        {
            result.errorMessage = fractureResult.errorMessage;
            spdlog::warn("FractureProcessor: fracture failed: {}", result.errorMessage);
            return result;
        }

        if (cancelFlag && cancelFlag->load())
        {
            result.errorMessage = "Cancelled";
            return result;
        }

        // Generate convex hulls per fragment
        if (config.generateConvexHulls)
        {
            if (progressCallback)
            {
                progressCallback(0.7f, "Generating convex hulls");
            }

            destruction::FragmentHullConfig hullConfig;
            hullConfig.maxVerticesPerHull = 32;
            hullConfig.resolution = 50000;
            destruction::ConvexHullGenerator::generateBatch(fractureResult.fragments, hullConfig);
        }

        if (progressCallback)
        {
            progressCallback(0.85f, "Building metadata");
        }

        // Build fracture metadata
        result.metadata.fragmentCount = static_cast<uint32_t>(fractureResult.fragments.size());
        result.metadata.seedDistribution = static_cast<uint32_t>(config.seedDistribution);
        result.metadata.randomSeed = config.randomSeed;
        result.metadata.innerUVScale = config.innerUVScale;

        for (const auto& frag : fractureResult.fragments)
        {
            asset::FragmentPhysicsInfo info;
            info.centerOfMass = frag.centerOfMass;
            info.volume = frag.volume;

            // Compute bounding box
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

            result.metadata.fragments.push_back(info);
        }

        // Build connectivity from fragment neighbor data
        for (uint32_t i = 0; i < static_cast<uint32_t>(fractureResult.fragments.size()); ++i)
        {
            for (const auto& neighbor : fractureResult.fragments[i].neighbors)
            {
                if (neighbor.neighborIndex > i)
                {
                    result.metadata.connectivity.emplace_back(i, neighbor.neighborIndex, neighbor.sharedArea);
                }
            }
        }

        // Convert to MeshesData for serialization
        result.fragmentMeshes = destruction::VoronoiFracture::toMeshesData(fractureResult);
        result.success = true;

        if (progressCallback)
        {
            progressCallback(1.0f, "Complete");
        }

        spdlog::info("FractureProcessor: generated {} fragments with {} connectivity edges",
                    result.metadata.fragmentCount, result.metadata.connectivity.size());

        return result;
    }
}
