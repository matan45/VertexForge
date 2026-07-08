#pragma once

#include "GPUObjectStreamTypes.hpp"
#include "../GPUDrivenTypes.hpp"
#include "../FreeListAllocator.hpp"
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace render::gpudriven
{
    class MergedMeshBuffer;
    struct ObjectResolvers;

    struct ObjectStreamEntry
    {
        uint64_t entityUUID = 0;
        entt::entity entity = entt::null;
        uint32_t gpuSlot = FreeListAllocator::ALLOCATION_FAILED;
        uint32_t sectorId = 0;
        ObjectStreamState state = ObjectStreamState::NotLoaded;
        float distanceToCamera = 0.0f;
        float priority = 0.0f;
        uint64_t lastAccessFrame = 0;
        bool isStatic = false;
        glm::vec3 position{0.0f};
    };

    class GPUObjectStreamManager
    {
    public:
        explicit GPUObjectStreamManager(MergedMeshBuffer& buffer);
        ~GPUObjectStreamManager() = default;

        GPUObjectStreamManager(const GPUObjectStreamManager&) = delete;
        GPUObjectStreamManager& operator=(const GPUObjectStreamManager&) = delete;

        void init(const ObjectStreamConfig& config = {});
        void cleanup();

        void registerSectorObjects(uint32_t sectorId,
                                   const std::vector<std::pair<uint64_t, entt::entity>>& entities,
                                   entt::registry& registry);
        void unregisterSectorObjects(uint32_t sectorId);

        void update(const glm::vec3& cameraPosition,
                    const ObjectResolvers& resolvers,
                    entt::registry& registry);

        // Force every resident (Active) object back to Queued so the next update() re-runs the texture
        // resolver and re-uploads its GPU slot(s). Used on an SVT enable/disable (finding #4): resident
        // objects still hold the texture indices resolved at stream-in, and the recompiled mesh shader no
        // longer accepts the old (SVT-tagged vs plain) values — re-resolving reconciles them without a
        // full scene reload. Reuses the eviction path, so it also handles multi-submesh objects.
        void requeueActiveObjects();

        const ObjectStreamingStats& getStats() const { return stats; }
        const ObjectStreamConfig& getConfig() const { return config; }
        void setConfig(const ObjectStreamConfig& cfg) { config = cfg; }

    private:
        MergedMeshBuffer& buffer;
        ObjectStreamConfig config;
        ObjectStreamingStats stats;

        std::unordered_map<uint64_t, ObjectStreamEntry> entries;
        std::unordered_map<uint32_t, std::unordered_set<uint64_t>> sectorObjects;

        uint64_t currentFrame = 0;

        void updatePriorities(const glm::vec3& cameraPosition);
        void processEvictions();
        void processUploads(const ObjectResolvers& resolvers, entt::registry& registry);
        void updateStats();

        float calculatePriority(const ObjectStreamEntry& entry, const glm::vec3& cameraPos) const;
    };
}
