#pragma once

#include <vulkan/vulkan.hpp>
#include "../data/VFXTypes.hpp"

namespace services
{

    // Provider interface for runtime VFX rendering in the scene.
    // Unlike IVFXPreviewProvider (which renders offscreen for editor preview windows),
    // this provider integrates VFX directly into the scene render pass.
    class IVFXRuntimeProvider
    {
    public:
        virtual ~IVFXRuntimeProvider() = default;

        // System lifecycle (call once)
        virtual void init(vk::RenderPass sceneRenderPass) = 0;
        virtual void cleanUp() = 0;
        virtual void recreate(vk::RenderPass sceneRenderPass) = 0;
        virtual bool isInitialized() const = 0;

        // Instance management
        virtual VFXInstanceId createInstance(const VFXRuntimeParams& params) = 0;
        virtual void destroyInstance(VFXInstanceId id) = 0;

        // Instance control
        virtual void setInstanceTransform(VFXInstanceId id, const glm::mat4& worldTransform) = 0;
        virtual void playInstance(VFXInstanceId id) = 0;
        virtual void stopInstance(VFXInstanceId id) = 0;
        virtual void resetInstance(VFXInstanceId id) = 0;
        virtual bool isInstancePlaying(VFXInstanceId id) const = 0;

        // Frame update (call each frame)
        virtual void update(float deltaTime) = 0;
        virtual void setCamera(const glm::mat4& view, const glm::mat4& projection,
                               const glm::vec3& cameraPos, float time) = 0;

        // Called before render pass to dispatch compute shaders (GPU mode)
        virtual void recordComputeCommands(const vk::CommandBuffer& cmd) = 0;

        // Called during scene render pass to record VFX draw commands
        virtual void recordDrawCommands(const vk::CommandBuffer& cmd) = 0;

        virtual size_t getInstanceCount() const = 0;
    };
}
