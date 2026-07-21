#include "ReflectionProbeBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include <probe/ReflectionProbeMath.hpp>

#include <algorithm>
#include <cstring>

namespace render::probe
{
    ReflectionProbeBufferManager::ReflectionProbeBufferManager(core::Device& device)
        : device(device)
    {
        cpuProbes.reserve(MAX_REFLECTION_PROBES);
        resolved.reserve(MAX_REFLECTION_PROBES);
        slotOwner.fill(entt::null); // entity 0 is a valid handle — never leave these value-initialized
    }

    ReflectionProbeBufferManager::~ReflectionProbeBufferManager()
    {
        if (initialized)
        {
            cleanup();
        }
    }

    void ReflectionProbeBufferManager::init()
    {
        if (initialized) return;

        createBuffer();

        // Start with an explicit zero count so the buffer is a valid "no probes" state from the
        // very first frame, before updateFromScene() has ever run.
        if (mappedMemory)
        {
            std::memset(mappedMemory, 0, PROBE_BUFFER_SIZE);
        }

        initialized = true;
    }

    void ReflectionProbeBufferManager::cleanup()
    {
        if (!initialized) return;
        destroyBuffer();
        cpuProbes.clear();
        resolved.clear();
        slotReady = {};
        initialized = false;
    }

    void ReflectionProbeBufferManager::createBuffer()
    {
        core::BufferInfoRequest request(device.getLogicalDevice(), device.getPhysicalDevice());
        request.size = PROBE_BUFFER_SIZE;
        request.usage = vk::BufferUsageFlagBits::eStorageBuffer;
        request.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(request, probeBuffer, probeAllocation, device.getMemoryManager());

        mappedMemory = probeAllocation.mappedPtr;
    }

    void ReflectionProbeBufferManager::destroyBuffer()
    {
        mappedMemory = nullptr;
        core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), probeBuffer, probeAllocation,
                                             device.getMemoryManager());
    }

    void ReflectionProbeBufferManager::updateFromScene()
    {
        cpuProbes.clear();
        resolved.clear();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::ReflectionProbeComponent, components::WorldTransformComponent>();

        // Gather first, then sort, then cap. Capping before the sort would keep an arbitrary subset
        // rather than the highest-priority one.
        struct Candidate
        {
            ProbeSortKey key;
            entt::entity entity;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(MAX_REFLECTION_PROBES * 2);

        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;

            const auto& p = view.get<components::ReflectionProbeComponent>(entity);
            const bool isSphere = p.shape == components::ReflectionProbeShape::Sphere;

            // The UUID is what makes the order reproducible: EnTT pool order shifts on add/remove
            // and is not preserved across save/load, so an index-based tiebreak would let two
            // equal-priority probes swap places and flicker. Entities without a UUID (should not
            // happen for authored content) fall back to the raw handle so the order stays total.
            const uint64_t stableId =
                registry.all_of<components::UUIDComponent>(entity)
                    ? registry.get<components::UUIDComponent>(entity).id.getValue()
                    : static_cast<uint64_t>(entt::to_integral(entity));

            candidates.push_back(Candidate{
                ProbeSortKey{
                    .priority = p.priority,
                    .volume = probeVolume(p.halfExtents, isSphere),
                    .stableId = stableId
                },
                entity
            });
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return probeSortLess(a.key, b.key); });

        const size_t kept = std::min<size_t>(candidates.size(), MAX_REFLECTION_PROBES);
        for (size_t i = 0; i < kept; ++i)
        {
            const entt::entity entity = candidates[i].entity;
            const auto& p = registry.get<components::ReflectionProbeComponent>(entity);
            const auto& world = registry.get<components::WorldTransformComponent>(entity);

            const bool isSphere = p.shape == components::ReflectionProbeShape::Sphere;
            const glm::vec3 capturePos = glm::vec3(world.worldMatrix[3]);
            const auto slot = static_cast<uint32_t>(i);

            // The slot is a pure function of this frame's sort position, so removing or inserting a
            // probe silently hands this cube to a different probe. The cube still holds the previous
            // owner's capture, so readiness has to be dropped until the bake scheduler refills it —
            // otherwise the probe renders someone else's environment indefinitely.
            if (slotOwner[slot] != entity)
            {
                slotOwner[slot] = entity;
                slotReady[slot] = false;
            }

            GPUReflectionProbe gpu{};
            gpu.worldToLocal = buildWorldToLocal(world.worldMatrix, p.halfExtents);
            gpu.localToWorld = buildLocalToWorld(world.worldMatrix, p.halfExtents);
            gpu.positionRadius = glm::vec4(capturePos, p.halfExtents.x);
            gpu.blendNormIntensity = glm::vec4(blendNormalized(p.halfExtents, p.blendDistance), p.intensity);

            const WorldBounds bounds = worldBoundsOfBox(world.worldMatrix, p.halfExtents);
            gpu.boundsMin = glm::vec4(bounds.min, 0.0f);
            gpu.boundsMax = glm::vec4(bounds.max, 0.0f);

            gpu.params = glm::uvec4(slot,
                                    static_cast<uint32_t>(isSphere ? GPUProbeShape::Sphere : GPUProbeShape::Box),
                                    0u, 0u);

            cpuProbes.push_back(gpu);
            resolved.push_back(ResolvedProbe{
                .entity = entity,
                .slot = slot,
                .capturePos = capturePos,
                .nearPlane = p.nearPlane,
                .farPlane = p.farPlane,
                .captureShadows = p.captureShadows,
                .dirty = p.dirty
            });
        }
    }

    uint32_t ReflectionProbeBufferManager::getUploadedCount() const
    {
        // Ready probes must form a PREFIX of the array for the shader's simple loop to work, so a
        // gap (slot 0 not baked yet, slot 1 baked) stops the count rather than skipping. Probes bake
        // in slot order, so this converges; it just means a scene mid-bake shows fewer probes for a
        // few frames rather than the wrong ones.
        uint32_t count = 0;
        for (uint32_t i = 0; i < cpuProbes.size(); ++i)
        {
            if (!slotReady[i]) break;
            ++count;
        }
        return count;
    }

    void ReflectionProbeBufferManager::uploadToGPU()
    {
        if (!initialized || !mappedMemory) return;

        const uint32_t count = getUploadedCount();

        const uint32_t header[4] = {count, 0, 0, 0};
        std::memcpy(mappedMemory, header, sizeof(header));

        if (count > 0)
        {
            auto* dst = static_cast<uint8_t*>(mappedMemory) + PROBE_BUFFER_HEADER_SIZE;
            std::memcpy(dst, cpuProbes.data(), count * sizeof(GPUReflectionProbe));
        }
    }
}
