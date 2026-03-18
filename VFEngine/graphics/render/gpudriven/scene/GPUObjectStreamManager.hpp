#pragma once

#include "../GPUDrivenTypes.hpp"
#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace render::gpudriven
{
    class MergedMeshBuffer;

    enum class ObjectStreamState : uint8_t
    {
        NotLoaded,
        Queued,
        Active
    };

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

    struct ObjectStreamConfig
    {
        uint32_t maxUploadsPerFrame = 64;
        uint32_t maxEvictionsPerFrame = 32;
        float evictionThreshold = 0.9f;
        float evictionTarget = 0.8f;
        float hysteresisMargin = 0.05f;
    };

    struct ObjectStreamingStats
    {
        uint32_t totalRegistered = 0;
        uint32_t activeOnGPU = 0;
        uint32_t queuedForUpload = 0;
        uint32_t uploadsThisFrame = 0;
        uint32_t evictionsThisFrame = 0;
        float slotUtilization = 0.0f;
        float fragmentation = 0.0f;
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
