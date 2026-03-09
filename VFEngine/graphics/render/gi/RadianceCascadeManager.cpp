#include "RadianceCascadeManager.hpp"
#include "../../core/Device.hpp"
#include "print/Log.hpp"
#include <cmath>

namespace render::gi
{
    RadianceCascadeManager::RadianceCascadeManager(core::Device& device)
        : device(device)
    {
    }

    RadianceCascadeManager::~RadianceCascadeManager()
    {
        cleanup();
    }

    void RadianceCascadeManager::init(const GISettings& giSettings)
    {
        if (initialized)
        {
            cleanup();
        }

        settings = giSettings;

        // Determine cascade count from quality
        switch (settings.quality)
        {
        case GIQuality::Off:
            config.cascadeCount = 0;
            break;
        case GIQuality::Medium:
            config.cascadeCount = 1;
            break;
        case GIQuality::High:
            config.cascadeCount = 3;
            break;
        case GIQuality::Ultra:
            config.cascadeCount = 4;
            break;
        }

        config.baseSpacing = settings.probeSpacing;
        config.cascadeMultiplier = settings.cascadeMultiplier;

        if (config.cascadeCount > 0)
        {
            buildCascades();

            probeStorage = std::make_unique<ProbeStorageBuffer>(device);
            probeStorage->init(totalProbeCount, static_cast<uint32_t>(cascades.size()));
            probeStorage->uploadCascadeInfo(cascades);
        }

        initialized = true;
        vfLogInfo("RadianceCascadeManager: Initialized with {} cascades, {} total probes",
                  config.cascadeCount, totalProbeCount);
    }

    void RadianceCascadeManager::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        if (probeStorage)
        {
            probeStorage->cleanup();
            probeStorage.reset();
        }

        cascades.clear();
        totalProbeCount = 0;
        initialized = false;
    }

    void RadianceCascadeManager::buildCascades()
    {
        cascades.clear();
        totalProbeCount = 0;

        for (uint32_t i = 0; i < config.cascadeCount; ++i)
        {
            CascadeLevel cascade;
            cascade.spacing = config.baseSpacing * std::pow(config.cascadeMultiplier, static_cast<float>(i));

            uint32_t probes = static_cast<uint32_t>(
                config.gridDimensions.x * config.gridDimensions.y * config.gridDimensions.z);

            cascade.probeCount = probes;
            cascade.probeOffset = totalProbeCount;
            cascade.updateCursor = 0;

            // Center grid on camera position
            glm::vec3 halfExtent = glm::vec3(config.gridDimensions) * cascade.spacing * 0.5f;
            cascade.gridOrigin = lastCameraPosition - halfExtent;

            totalProbeCount += probes;
            cascades.push_back(cascade);
        }
    }

    void RadianceCascadeManager::updateCameraPosition(const glm::vec3& cameraPos)
    {
        if (!initialized || cascades.empty())
        {
            lastCameraPosition = cameraPos;
            return;
        }

        for (uint32_t i = 0; i < cascades.size(); ++i)
        {
            auto& cascade = cascades[i];

            // Check if camera has moved enough to scroll the grid
            glm::vec3 gridCenter = cascade.gridOrigin +
                glm::vec3(config.gridDimensions) * cascade.spacing * 0.5f;
            glm::vec3 offset = cameraPos - gridCenter;

            // Scroll if camera moved more than one probe spacing
            if (glm::length(offset) > cascade.spacing)
            {
                glm::vec3 halfExtent = glm::vec3(config.gridDimensions) * cascade.spacing * 0.5f;
                glm::vec3 newOrigin = cameraPos - halfExtent;

                // Snap to grid
                newOrigin.x = std::floor(newOrigin.x / cascade.spacing) * cascade.spacing;
                newOrigin.y = std::floor(newOrigin.y / cascade.spacing) * cascade.spacing;
                newOrigin.z = std::floor(newOrigin.z / cascade.spacing) * cascade.spacing;

                scrollCascadeGrid(i, newOrigin);
            }
        }

        lastCameraPosition = cameraPos;

        if (probeStorage)
        {
            probeStorage->uploadCascadeInfo(cascades);
        }
    }

    void RadianceCascadeManager::scrollCascadeGrid(uint32_t cascadeIndex, const glm::vec3& newOrigin)
    {
        auto& cascade = cascades[cascadeIndex];
        cascade.gridOrigin = newOrigin;
        // Probes that fall outside the new grid will be invalidated during the next update cycle
    }

    void RadianceCascadeManager::beginFrame()
    {
        ++frameIndex;
    }

    std::vector<RadianceCascadeManager::ProbeUpdateBatch> RadianceCascadeManager::getProbeUpdateBatches()
    {
        std::vector<ProbeUpdateBatch> batches;

        if (!initialized || cascades.empty())
        {
            return batches;
        }

        for (uint32_t i = 0; i < cascades.size(); ++i)
        {
            const auto& cascade = cascades[i];
            uint32_t updateCount = static_cast<uint32_t>(
                std::ceil(cascade.probeCount * settings.probeUpdateRate));
            updateCount = std::min(updateCount, cascade.probeCount);

            if (updateCount == 0)
            {
                continue;
            }

            ProbeUpdateBatch batch;
            batch.cascadeIndex = i;
            batch.probeStartOffset = cascade.probeOffset + cascade.updateCursor;

            // Wrap around
            if (cascade.updateCursor + updateCount > cascade.probeCount)
            {
                batch.probeCount = cascade.probeCount - cascade.updateCursor;
            }
            else
            {
                batch.probeCount = updateCount;
            }

            batches.push_back(batch);
        }

        // Advance cursors
        for (auto& batch : batches)
        {
            auto& cascade = cascades[batch.cascadeIndex];
            cascade.updateCursor = (cascade.updateCursor + batch.probeCount) % cascade.probeCount;
        }

        return batches;
    }

    void RadianceCascadeManager::applySettings(const GISettings& newSettings)
    {
        bool needsRebuild = (newSettings.quality != settings.quality) ||
                            (newSettings.probeSpacing != settings.probeSpacing);

        settings = newSettings;

        if (needsRebuild)
        {
            cleanup();
            init(settings);
        }
    }

    vk::DescriptorSetLayout RadianceCascadeManager::getProbeDataLayout() const
    {
        return probeStorage ? probeStorage->getProbeDataLayout() : vk::DescriptorSetLayout{};
    }

    vk::DescriptorSet RadianceCascadeManager::getProbeDataDescSet() const
    {
        return probeStorage ? probeStorage->getProbeDataDescSet() : vk::DescriptorSet{};
    }

    vk::DescriptorSetLayout RadianceCascadeManager::getCascadeInfoLayout() const
    {
        return probeStorage ? probeStorage->getCascadeInfoLayout() : vk::DescriptorSetLayout{};
    }

    vk::DescriptorSet RadianceCascadeManager::getCascadeInfoDescSet() const
    {
        return probeStorage ? probeStorage->getCascadeInfoDescSet() : vk::DescriptorSet{};
    }

    GIDebugStats RadianceCascadeManager::getDebugStats() const
    {
        GIDebugStats stats;
        stats.totalProbes = totalProbeCount;
        stats.activeCascades = static_cast<uint32_t>(cascades.size());

        uint32_t totalUpdated = 0;
        for (const auto& cascade : cascades)
        {
            uint32_t updateCount = static_cast<uint32_t>(
                std::ceil(cascade.probeCount * settings.probeUpdateRate));
            totalUpdated += std::min(updateCount, cascade.probeCount);
        }
        stats.probesUpdatedThisFrame = totalUpdated;

        return stats;
    }
}
