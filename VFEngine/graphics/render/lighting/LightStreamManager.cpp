#include "LightStreamManager.hpp"
#include "components/Components.hpp"
#include "scene/EntityRegistry.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <glm/glm.hpp>

namespace render::lighting
{
    LightStreamManager::LightStreamManager() = default;

    void LightStreamManager::init(const LightStreamingConfig& cfg)
    {
        config = cfg;
        pointAllocator.reset(config.maxPointLights);
        spotAllocator.reset(config.maxSpotLights);
        registeredLights.clear();
        activeLightIds.clear();
        sectorLights.clear();

        vfLogInfo("LightStreamManager: Initialized with {} point, {} spot max slots",
                  config.maxPointLights, config.maxSpotLights);
    }

    void LightStreamManager::cleanup()
    {
        std::lock_guard<std::mutex> lock(mtx);
        registeredLights.clear();
        activeLightIds.clear();
        sectorLights.clear();
        pointAllocator.reset(0);
        spotAllocator.reset(0);
    }

    void LightStreamManager::registerSectorLights(uint32_t sectorId, const std::vector<uint32_t>& lightEntityIds)
    {
        // Query ECS component data before acquiring the lock to avoid holding
        // the mutex during potentially slow registry lookups
        auto& registry = scene::EntityRegistry::getRegistry();

        std::vector<LightStreamEntry> entries;
        entries.reserve(lightEntityIds.size());

        for (uint32_t entityId : lightEntityIds)
        {
            auto entity = static_cast<entt::entity>(entityId);

            LightStreamEntry entry;
            entry.entityId = entityId;
            entry.sectorId = sectorId;

            if (registry.all_of<components::PointLightComponent>(entity))
            {
                entry.type = LightStreamEntry::LightType::Point;
                const auto& light = registry.get<components::PointLightComponent>(entity);
                entry.intensity = light.intensity;
                entry.radius = light.radius;
                entry.castsShadow = false;
            }
            else if (registry.all_of<components::SpotLightComponent>(entity))
            {
                entry.type = LightStreamEntry::LightType::Spot;
                const auto& light = registry.get<components::SpotLightComponent>(entity);
                entry.intensity = light.intensity;
                entry.radius = light.range;
                entry.castsShadow = false;
            }
            else
            {
                continue;
            }

            // Check static flag from TransformComponent
            if (registry.all_of<components::TransformComponent>(entity))
            {
                entry.isStatic = registry.get<components::TransformComponent>(entity).isStatic;
            }

            entries.push_back(entry);
        }

        std::lock_guard<std::mutex> lock(mtx);
        auto& sectorSet = sectorLights[sectorId];

        for (auto& entry : entries)
        {
            if (registeredLights.contains(entry.entityId))
            {
                continue;
            }

            if (allocateSlot(entry))
            {
                entry.active = true;
                activeLightIds.insert(entry.entityId);
            }
            else
            {
                entry.active = false;
            }

            registeredLights[entry.entityId] = entry;
            sectorSet.insert(entry.entityId);
        }

        vfLogInfo("LightStreamManager: Registered {} lights for sector {}",
                  lightEntityIds.size(), sectorId);
    }

    void LightStreamManager::unregisterSectorLights(uint32_t sectorId)
    {
        std::lock_guard<std::mutex> lock(mtx);

        auto it = sectorLights.find(sectorId);
        if (it == sectorLights.end())
        {
            return;
        }

        for (uint32_t entityId : it->second)
        {
            auto lightIt = registeredLights.find(entityId);
            if (lightIt != registeredLights.end())
            {
                if (lightIt->second.active)
                {
                    freeSlot(lightIt->second);
                    activeLightIds.erase(entityId);
                }
                registeredLights.erase(lightIt);
            }
        }

        vfLogInfo("LightStreamManager: Unregistered {} lights from sector {}",
                  it->second.size(), sectorId);
        sectorLights.erase(it);
    }

    bool LightStreamManager::registerLight(uint32_t entityId, LightStreamEntry::LightType type,
                                           uint32_t sectorId)
    {
        std::lock_guard<std::mutex> lock(mtx);

        if (registeredLights.contains(entityId))
        {
            return true;
        }

        LightStreamEntry entry;
        entry.entityId = entityId;
        entry.sectorId = sectorId;
        entry.type = type;

        if (allocateSlot(entry))
        {
            entry.active = true;
            activeLightIds.insert(entityId);
        }
        else
        {
            entry.active = false;
        }

        registeredLights[entityId] = entry;
        sectorLights[sectorId].insert(entityId);
        return entry.active;
    }

    void LightStreamManager::unregisterLight(uint32_t entityId)
    {
        std::lock_guard<std::mutex> lock(mtx);

        auto it = registeredLights.find(entityId);
        if (it == registeredLights.end())
        {
            return;
        }

        if (it->second.active)
        {
            freeSlot(it->second);
            activeLightIds.erase(entityId);
        }

        sectorLights[it->second.sectorId].erase(entityId);
        registeredLights.erase(it);
    }

    void LightStreamManager::updatePriorities(const glm::vec3& cameraPos)
    {
        cameraPosition = cameraPos;

        auto& registry = scene::EntityRegistry::getRegistry();

        for (auto& [entityId, entry] : registeredLights)
        {
            auto entity = static_cast<entt::entity>(entityId);
            if (registry.valid(entity) && registry.all_of<components::WorldTransformComponent>(entity))
            {
                const auto& transform = registry.get<components::WorldTransformComponent>(entity);
                glm::vec3 lightPos = glm::vec3(transform.worldMatrix[3]);
                entry.distance = glm::distance(cameraPos, lightPos);
            }

            entry.priority = computePriority(entry);
        }
    }

    void LightStreamManager::applyBudget()
    {
        std::lock_guard<std::mutex> lock(mtx);

        // Sort all registered lights by priority (descending)
        sortedPriorities.clear();
        sortedPriorities.reserve(registeredLights.size());
        for (const auto& [entityId, entry] : registeredLights)
        {
            sortedPriorities.emplace_back(entityId, entry.priority);
        }
        std::sort(sortedPriorities.begin(), sortedPriorities.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        uint32_t pointBudget = config.maxPointLights;
        uint32_t spotBudget = config.maxSpotLights;
        uint32_t pointActive = 0;
        uint32_t spotActive = 0;

        // Track which lights should be active
        std::unordered_set<uint32_t> shouldBeActive;

        for (const auto& [entityId, priority] : sortedPriorities)
        {
            auto& entry = registeredLights[entityId];

            bool withinBudget = false;
            if (entry.type == LightStreamEntry::LightType::Point && pointActive < pointBudget)
            {
                withinBudget = true;
                ++pointActive;
            }
            else if (entry.type == LightStreamEntry::LightType::Spot && spotActive < spotBudget)
            {
                withinBudget = true;
                ++spotActive;
            }

            if (withinBudget)
            {
                shouldBeActive.insert(entityId);
            }
        }

        // Deactivate lights that are no longer in budget
        for (auto& [entityId, entry] : registeredLights)
        {
            if (entry.active && !shouldBeActive.contains(entityId))
            {
                // Hysteresis: only deactivate if clearly below threshold
                bool shouldDeactivate = true;
                if (config.hysteresisMargin > 0.0f && !sortedPriorities.empty())
                {
                    float cutoffPriority = 0.0f;
                    // Find the cutoff priority (lowest active priority)
                    for (auto rit = sortedPriorities.rbegin(); rit != sortedPriorities.rend(); ++rit)
                    {
                        if (shouldBeActive.contains(rit->first))
                        {
                            cutoffPriority = rit->second;
                            break;
                        }
                    }
                    // Static lights get wider hysteresis (harder to deactivate)
                    float margin = entry.isStatic
                        ? config.hysteresisMargin * config.staticHysteresisMultiplier
                        : config.hysteresisMargin;
                    if (entry.priority >= cutoffPriority - margin)
                    {
                        shouldDeactivate = false;
                    }
                }

                if (shouldDeactivate)
                {
                    freeSlot(entry);
                    entry.active = false;
                    activeLightIds.erase(entityId);
                }
            }
        }

        // Activate lights that should be active but aren't
        for (uint32_t entityId : shouldBeActive)
        {
            auto& entry = registeredLights[entityId];
            if (!entry.active)
            {
                if (allocateSlot(entry))
                {
                    entry.active = true;
                    activeLightIds.insert(entityId);
                }
            }
        }
    }

    bool LightStreamManager::isLightActive(uint32_t entityId) const
    {
        auto it = registeredLights.find(entityId);
        return it != registeredLights.end() && it->second.active;
    }

    uint32_t LightStreamManager::getSlotIndex(uint32_t entityId) const
    {
        auto it = registeredLights.find(entityId);
        if (it != registeredLights.end())
        {
            return it->second.slotIndex;
        }
        return gpudriven::FreeListAllocator::ALLOCATION_FAILED;
    }

    void LightStreamManager::setConfig(const LightStreamingConfig& cfg)
    {
        config = cfg;
    }

    LightStreamingStats LightStreamManager::getStats() const
    {
        LightStreamingStats stats;

        uint32_t pointRegistered = 0, spotRegistered = 0;
        uint32_t pointActive = 0, spotActive = 0;

        for (const auto& [entityId, entry] : registeredLights)
        {
            if (entry.type == LightStreamEntry::LightType::Point)
            {
                ++pointRegistered;
                if (entry.active) ++pointActive;
            }
            else
            {
                ++spotRegistered;
                if (entry.active) ++spotActive;
            }
        }

        stats.registeredPointLights = pointRegistered;
        stats.registeredSpotLights = spotRegistered;
        stats.activePointLights = pointActive;
        stats.activeSpotLights = spotActive;
        stats.excludedByBudget = (pointRegistered - pointActive) + (spotRegistered - spotActive);
        stats.pointPoolUtilization = config.maxPointLights > 0
            ? static_cast<float>(pointActive) / static_cast<float>(config.maxPointLights)
            : 0.0f;
        stats.spotPoolUtilization = config.maxSpotLights > 0
            ? static_cast<float>(spotActive) / static_cast<float>(config.maxSpotLights)
            : 0.0f;
        stats.pointFragmentation = pointAllocator.getFragmentationPercent();
        stats.spotFragmentation = spotAllocator.getFragmentationPercent();

        return stats;
    }

    float LightStreamManager::computePriority(const LightStreamEntry& entry) const
    {
        // Higher priority = more important light
        float distanceFactor = 1.0f / (1.0f + entry.distance * 0.01f);
        float intensityFactor = entry.intensity;
        float radiusFactor = entry.radius;
        float shadowFactor = entry.castsShadow ? 1.0f : 0.0f;
        float staticFactor = entry.isStatic ? config.staticBonus : 0.0f;

        return distanceFactor * config.distanceWeight +
               intensityFactor * config.intensityWeight +
               radiusFactor * config.radiusWeight +
               shadowFactor * config.shadowWeight +
               staticFactor;
    }

    bool LightStreamManager::allocateSlot(LightStreamEntry& entry)
    {
        auto& allocator = (entry.type == LightStreamEntry::LightType::Point)
            ? pointAllocator : spotAllocator;

        uint32_t slot = allocator.allocate(1);
        if (slot == gpudriven::FreeListAllocator::ALLOCATION_FAILED)
        {
            return false;
        }
        allocator.markUsed(1);
        entry.slotIndex = slot;
        return true;
    }

    void LightStreamManager::freeSlot(LightStreamEntry& entry)
    {
        auto& allocator = (entry.type == LightStreamEntry::LightType::Point)
            ? pointAllocator : spotAllocator;

        allocator.free(entry.slotIndex, 1);
    }
}
