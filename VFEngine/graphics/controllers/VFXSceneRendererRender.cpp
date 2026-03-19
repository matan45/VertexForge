#include "VFXSceneRenderer.hpp"
#include "../render/vfx/compute/GPUVFXBufferManager.hpp"
#include "../render/vfx/compute/GPUVFXComputePipeline.hpp"
#include "../render/vfx/scene/VFXSceneGPUPipeline.hpp"
#include "../render/vfx/mesh/VFXMeshGPUPipeline.hpp"
#include "../render/vfx/ribbon/VFXRibbonGPUPipeline.hpp"
#include "../render/vfx/particle/VFXParticleSystem.hpp"
#include "../../services/events/vfx/VFXEventNotifications.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace controllers
{
    void VFXSceneRenderer::recordComputeCommands(vk::CommandBuffer cmd)
    {
        if (!initialized || !gpuDrivenEnabled)
            return;

        if (!gpuComputePipeline || !gpuComputePipeline->isInitialized() || !gpuBufferManager)
            return;

        uint32_t activeGPUEmitters = 0;
        for (const auto& [id, instance] : instances)
        {
            if (instance.gpuDriven && instance.active)
                activeGPUEmitters++;
        }

        if (activeGPUEmitters == 0)
            return;

        gpuComputePipeline->insertBarriersBeforeTransfer(
            cmd,
            gpuBufferManager->getStateBuffer(),
            gpuBufferManager->getDrawCommandBuffer(),
            gpuBufferManager->getParticleBuffer()
        );

        gpuBufferManager->clearParticleBufferIfNeeded(cmd);
        gpuBufferManager->uploadStateBuffer(cmd);
        gpuBufferManager->clearDrawCommands(cmd);

        gpuComputePipeline->insertTransferToTransferBarrier(
            cmd, gpuBufferManager->getStateBuffer()
        );

        gpuBufferManager->resetAllActiveCounts(cmd);
        gpuBufferManager->clearEventBuffer(cmd);

        gpuComputePipeline->insertBarriersBeforeCompute(
            cmd,
            gpuBufferManager->getStateBuffer(),
            gpuBufferManager->getDrawCommandBuffer(),
            gpuBufferManager->getParticleBuffer(),
            gpuBufferManager->getEventBuffer()
        );

        for (const auto& [id, instance] : instances)
        {
            if (!instance.gpuDriven || !instance.active)
                continue;

            if (!isEmitterInFrustum(instance))
                continue;

            gpuComputePipeline->dispatch(
                cmd,
                instance.gpuEmitterIndex,
                instance.gpuParticleCount,
                frameNumber,
                gpuBufferManager->getMaxEmitters()
            );
        }

        gpuComputePipeline->insertBarriersAfterCompute(cmd, gpuBufferManager->getBufferSet());
        gpuBufferManager->copyEventBufferToReadback(cmd);
    }

    void VFXSceneRenderer::recordGPUDrawCommands(vk::CommandBuffer cmd)
    {
        if (!gpuRenderPipeline || !gpuRenderPipeline->isInitialized() || !gpuBufferManager)
            return;

        uint32_t activeGPUEmitters = 0;
        for (const auto& [id, instance] : instances)
        {
            if (instance.gpuDriven && instance.active)
            {
                if (distanceCullingEnabled && maxVFXDistSq > 0.0f)
                {
                    glm::vec3 emitterPos = glm::vec3(instance.worldTransform[3]);
                    glm::vec3 diff = emitterPos - currentCameraPos;
                    if (glm::dot(diff, diff) > maxVFXDistSq)
                        continue;
                }
                activeGPUEmitters++;
            }
        }

        if (activeGPUEmitters == 0)
            return;

        gpuRenderPipeline->recordCommandsInline(
            cmd,
            gpuBufferManager->getDrawCommandBuffer(),
            gpuBufferManager->getMaxEmitters()
        );

        if (gpuMeshPipeline && gpuMeshPipeline->isInitialized())
        {
            gpuMeshPipeline->recordCommandsInline(
                cmd,
                gpuBufferManager->getDrawCommandBuffer(),
                gpuBufferManager->getMaxEmitters()
            );
        }

        if (gpuRibbonPipeline && gpuRibbonPipeline->isInitialized())
        {
            gpuRibbonPipeline->recordCommandsInline(
                cmd,
                gpuBufferManager->getDrawCommandBuffer(),
                gpuBufferManager->getMaxEmitters()
            );
        }
    }

    void VFXSceneRenderer::processEvents()
    {
        if (lastFrameEventCount == 0)
            return;

        for (uint32_t i = 0; i < lastFrameEventCount; ++i)
        {
            const auto& event = lastFrameEvents[i];

            auto mapIt = emitterIndexToInstanceId.find(event.emitterIndex);
            if (mapIt == emitterIndexToInstanceId.end())
                continue;

            VFXInstanceId parentId = mapIt->second;
            auto instIt = instances.find(parentId);
            if (instIt == instances.end())
                continue;

            const VFXRuntimeInstance* parentInstance = &instIt->second;

            bool isSubEmitter = false;
            for (const auto& sub : activeSubEmitters)
            {
                if (sub.subId == parentId) { isSubEmitter = true; break; }
            }
            if (isSubEmitter)
                continue;

            std::string vfxPath;
            const auto& eventConfig = parentInstance->config.events;

            switch (event.eventType)
            {
            case 0: if (eventConfig.onSpawnEnabled) vfxPath = eventConfig.onSpawnVFXPath; break;
            case 1: if (eventConfig.onDeathEnabled) vfxPath = eventConfig.onDeathVFXPath; break;
            case 2: if (eventConfig.onCollisionEnabled) vfxPath = eventConfig.onCollisionVFXPath; break;
            case 3: if (eventConfig.onLifetimeThresholdEnabled) vfxPath = eventConfig.onLifetimeThresholdVFXPath; break;
            }

            if (vfxPath.empty())
                continue;

            uint32_t parentSubCount = 0;
            for (const auto& sub : activeSubEmitters)
            {
                if (sub.parentId == parentId && !sub.finished)
                    parentSubCount++;
            }

            if (parentSubCount >= MAX_SUB_EMITTERS_PER_PARENT)
                continue;

            VFXRuntimeParams subParams;
            subParams.vfxAssetPath = vfxPath;
            subParams.worldTransform = glm::translate(glm::mat4(1.0f),
                glm::vec3(event.position.x, event.position.y, event.position.z));
            subParams.loop = false;

            VFXInstanceId subId = createInstance(subParams);
            if (subId != 0)
            {
                playInstance(subId);

                SubEmitterInstance subEmitter;
                subEmitter.parentId = parentId;
                subEmitter.subId = subId;
                subEmitter.lifetime = 0.0f;

                auto subIt = instances.find(subId);
                if (subIt != instances.end())
                    subEmitter.maxLifetime = subIt->second.config.lifetime * 2.0f;

                activeSubEmitters.push_back(subEmitter);
            }

            services::events::vfxruntime::VFXParticleEventNotification notification;
            notification.eventType = event.eventType;
            notification.position = glm::vec3(event.position.x, event.position.y, event.position.z);
            notification.velocity = glm::vec3(event.velocity.x, event.velocity.y, event.velocity.z);
            notification.emitterIndex = event.emitterIndex;
            notification.parentInstanceId = parentId;
            notification.entityId = parentInstance->entityId;
            notification.vfxAssetPath = vfxPath;
            events::EventDispatcher::instance().publish(notification);
        }
    }

    void VFXSceneRenderer::extractFrustumPlanes(const glm::mat4& viewProj)
    {
        const auto& m = viewProj;
        frustumPlanes[0] = glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]);
        frustumPlanes[1] = glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]);
        frustumPlanes[2] = glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]);
        frustumPlanes[3] = glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]);
        frustumPlanes[4] = glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2]);
        frustumPlanes[5] = glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]);

        for (int i = 0; i < 6; ++i)
        {
            float len = glm::length(glm::vec3(frustumPlanes[i]));
            if (len > 0.0f)
                frustumPlanes[i] /= len;
        }
        frustumPlanesValid = true;
    }

    bool VFXSceneRenderer::isEmitterInFrustum(const VFXRuntimeInstance& instance) const
    {
        if (!frustumPlanesValid)
            return true;

        if (instance.cameraRelative)
            return true;

        glm::vec3 emitterPos = glm::vec3(instance.worldTransform[3]);

        float maxDim = glm::max(glm::max(
            std::abs(instance.config.shape.dimensions.x),
            std::abs(instance.config.shape.dimensions.y)),
            std::abs(instance.config.shape.dimensions.z));
        float radius = std::max(
            instance.config.lifetime * instance.config.startSpeed,
            maxDim) + 5.0f;

        for (int i = 0; i < 6; ++i)
        {
            float dist = glm::dot(glm::vec3(frustumPlanes[i]), emitterPos) + frustumPlanes[i].w;
            if (dist < -radius)
                return false;
        }
        return true;
    }

    void VFXSceneRenderer::updateInstanceLOD(VFXRuntimeInstance& instance) const
    {
        if (instance.cameraRelative)
        {
            instance.currentLOD = 0;
            instance.lodSpawnMultiplier = 1.0f;
            return;
        }

        glm::vec3 emitterPos = glm::vec3(instance.worldTransform[3]);
        float dist = glm::distance(emitterPos, currentCameraPos);
        dist -= instance.lodBias;
        dist = std::max(dist, 0.0f);

        float multiplier = 1.0f;
        uint8_t lod = 0;

        if (dist < LOD0_DIST)
        {
            lod = 0;
            multiplier = 1.0f;
        }
        else if (dist < LOD1_DIST)
        {
            lod = 1;
            float zoneEnd = std::min(LOD0_DIST + LOD_TRANSITION_ZONE, LOD1_DIST);
            float t = std::clamp((dist - LOD0_DIST) / (zoneEnd - LOD0_DIST), 0.0f, 1.0f);
            multiplier = glm::mix(1.0f, 0.5f, t);
        }
        else if (dist < LOD2_DIST)
        {
            lod = 2;
            float zoneEnd = std::min(LOD1_DIST + LOD_TRANSITION_ZONE, LOD2_DIST);
            float t = std::clamp((dist - LOD1_DIST) / (zoneEnd - LOD1_DIST), 0.0f, 1.0f);
            multiplier = glm::mix(0.5f, 0.25f, t);
        }
        else
        {
            lod = 3;
            multiplier = 0.0f;
        }

        instance.currentLOD = lod;
        instance.lodSpawnMultiplier = multiplier;
    }

    VFXInstanceId VFXSceneRenderer::findLowestPriorityInstance(services::VFXEmitterPriority belowPriority) const
    {
        VFXInstanceId worstId = 0;
        services::VFXEmitterPriority worstPriority = services::VFXEmitterPriority::Critical;

        for (const auto& [id, instance] : instances)
        {
            if (!instance.gpuDriven || !instance.active)
                continue;
            if (instance.priority == services::VFXEmitterPriority::Critical)
                continue;
            if (static_cast<uint8_t>(instance.priority) > static_cast<uint8_t>(worstPriority))
            {
                worstPriority = instance.priority;
                worstId = id;
            }
        }

        if (worstId != 0 && static_cast<uint8_t>(worstPriority) > static_cast<uint8_t>(belowPriority))
            return worstId;
        return 0;
    }

    void VFXSceneRenderer::cleanupFinishedSubEmitters(float deltaTime)
    {
        for (auto& sub : activeSubEmitters)
        {
            if (sub.finished)
                continue;

            sub.lifetime += deltaTime;
            if (sub.lifetime >= sub.maxLifetime)
            {
                sub.finished = true;
                destroyInstance(sub.subId);
            }
        }

        activeSubEmitters.erase(
            std::remove_if(activeSubEmitters.begin(), activeSubEmitters.end(),
                [](const SubEmitterInstance& s) { return s.finished; }),
            activeSubEmitters.end());
    }
}
