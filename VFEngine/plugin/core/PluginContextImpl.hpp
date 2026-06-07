#pragma once
#include "../api/PluginContext.hpp"
#include "events/EventTypes.hpp"
#include <unordered_set>
#include <vector>
#include <memory>
#include <string>

namespace controllers::imguiHandler {
    class ImguiWindow;
}

namespace pipeline {
    class PipelineStage;
}

namespace plugin {

    class PluginContextImpl : public PluginContext
    {
    public:
        // VK-1290: Callback type for Core to register mType script bindings (avoids mType dep in Plugin)
        using ScriptBindingRegistrar = std::function<void(const std::vector<MetaComponentBridge>&)>;

    private:
        std::string pluginName;
        std::unordered_set<std::string> capabilities;
        std::vector<events::SubscriptionToken> managedSubscriptions;
        std::vector<std::shared_ptr<controllers::imguiHandler::ImguiWindow>> registeredWindows;
        std::vector<std::unique_ptr<pipeline::PipelineStage>> registeredImportStages;
        std::vector<plugin::RenderHookHandle> registeredRenderHooks;
        std::vector<std::string> registeredScriptFunctions;
        std::vector<plugin::CustomPipelineHandle> managedCustomPipelines;
        std::vector<plugin::CustomMeshHandle> managedCustomMeshes;
        std::vector<plugin::PluginTextureHandle> managedTextures;
        plugin::PluginTextureHandle boundWorldMaskTexture;
        struct HeightFieldBodyKey { entt::entity entity; int32_t tileX; int32_t tileZ; };
        std::vector<HeightFieldBodyKey> managedHeightFieldBodies;
        std::vector<events::SubscriptionToken> pluginEventSubscriptions;
        std::vector<services::AudioHandle> managedAudioHandles;
        std::vector<services::VFXInstanceId> managedVFXInstances;
        static std::vector<MetaComponentBridge> allBridges;
        static ScriptBindingRegistrar scriptBindingRegistrar;

    public:
        explicit PluginContextImpl(const std::string& pluginName,
                          const std::unordered_set<std::string>& capabilities);
        ~PluginContextImpl() override;

        events::EventDispatcher& getEventDispatcher() override;
        events::SubscriptionToken managedSubscribe(events::SubscriptionToken token) override;
        void registerEditorWindow(std::shared_ptr<controllers::imguiHandler::ImguiWindow> window,
                                  const std::string& title) override;
        void registerImportStage(std::unique_ptr<pipeline::PipelineStage> stage) override;
        void publishEvent(const std::string& eventName, const nlohmann::json& data) override;
        events::SubscriptionToken subscribeEvent(const std::string& eventName,
                                                  std::function<void(const nlohmann::json&)> handler) override;
        void registerScriptFunction(const std::string& name, std::any function) override;
        plugin::RenderHookHandle registerRenderPassHook(
            plugin::RenderPassHookPoint hookPoint,
            plugin::RenderHookCallback callback) override;
        void unregisterRenderPassHook(plugin::RenderHookHandle handle) override;
        plugin::CustomPipelineHandle createCustomPipeline(const plugin::CustomPipelineDesc& desc) override;
        plugin::CustomMeshHandle uploadCustomMesh(plugin::CustomMeshData data) override;
        void drawCustomMesh(plugin::CustomPipelineHandle pipeline, plugin::CustomMeshHandle mesh,
                            const glm::mat4& model,
                            const std::vector<std::byte>& pushConstants) override;
        void destroyCustomPipeline(plugin::CustomPipelineHandle handle) override;
        void destroyCustomMesh(plugin::CustomMeshHandle handle) override;
        plugin::PluginTextureHandle createTexture2D(uint32_t width, uint32_t height,
                                                    plugin::TextureFormat format) override;
        void updateTexture2D(plugin::PluginTextureHandle handle, const void* data, size_t size) override;
        void destroyTexture2D(plugin::PluginTextureHandle handle) override;
        void bindWorldMask(plugin::PluginTextureHandle handle,
                           const glm::vec3& worldMin, const glm::vec3& worldMax,
                           const plugin::WorldMaskParams& params) override;
        void unbindWorldMask() override;
        void setWorldMaskParams(const plugin::WorldMaskParams& params) override;
        entt::registry& getRegistry() override;
        bool hasCapability(const std::string& capability) const override;
        ImGuiContext* getImGuiContext() override;
        std::string getPluginDataPath() const override;
        void saveConfig(const nlohmann::json& config) override;
        nlohmann::json loadConfig() override;
        void logInfo(const std::string& message) override;
        void logWarning(const std::string& message) override;
        void logError(const std::string& message) override;

        // Audio API
        services::AudioHandle playSound3D(const std::string& path, glm::vec3 position,
                                          const services::AudioParams& params) override;
        services::AudioHandle playStreamingSound(const std::string& path,
                                                  const services::AudioParams& params) override;
        void stopSound(services::AudioHandle handle) override;
        void pauseSound(services::AudioHandle handle) override;
        void resumeSound(services::AudioHandle handle) override;
        void setSoundVolume(services::AudioHandle handle, float volume) override;
        void setSoundPitch(services::AudioHandle handle, float pitch) override;
        bool isSoundPlaying(services::AudioHandle handle) override;
        void setBusVolume(const std::string& busName, float volume) override;
        float getBusVolume(const std::string& busName) override;

        // Physics API
        services::RaycastHit raycast(glm::vec3 origin, glm::vec3 direction,
                                     float maxDistance, uint16_t layerMask) override;
        std::vector<services::RaycastHit> raycastAll(glm::vec3 origin, glm::vec3 direction,
                                                      float maxDistance, uint16_t layerMask) override;
        void applyForce(entt::entity entity, glm::vec3 force) override;
        void applyImpulse(entt::entity entity, glm::vec3 impulse) override;
        void setLinearVelocity(entt::entity entity, glm::vec3 velocity) override;
        glm::vec3 getLinearVelocity(entt::entity entity) override;
        glm::vec3 getAngularVelocity(entt::entity entity) override;
        bool isGrounded(entt::entity entity) override;
        bool hasRigidBody(entt::entity entity) override;
        glm::vec3 getPhysicsPosition(entt::entity entity) override;
        bool createPhysicsBody(entt::entity entity, bool rebuild) override;
        bool destroyPhysicsBody(entt::entity entity) override;
        bool createHeightFieldBody(entt::entity entity, int32_t tileX, int32_t tileZ,
                                   std::vector<float> heightSamples, uint32_t sampleCount,
                                   glm::vec3 worldOrigin, float vertexSpacing,
                                   float friction, float restitution) override;
        void destroyHeightFieldBody(entt::entity entity, int32_t tileX, int32_t tileZ) override;

        // Terrain API
        terrain::TerrainHeightAtResult getTerrainHeightAt(float worldX, float worldZ) override;
        terrain::TerrainHitResult getTerrainHit() override;
        bool hasTerrainComponent(entt::entity entity) override;

        // Input API
        bool isKeyDown(int keyCode) override;
        bool isKeyPressed(int keyCode) override;
        bool isMouseButtonDown(int button) override;
        glm::vec2 getMousePosition() override;
        glm::vec2 getMouseDelta() override;
        bool isActionDown(const std::string& actionName) override;
        bool isActionPressed(const std::string& actionName) override;
        float getAxis1DValue(const std::string& axisName) override;
        glm::vec2 getAxis2DValue(const std::string& axisName) override;
        glm::vec2 getViewportMousePosition() override;
        bool screenToWorldRay(glm::vec2 screenPos, glm::vec3& outOrigin, glm::vec3& outDirection) override;

        // NavMesh API
        void setAgentDestination(entt::entity entity, glm::vec3 target) override;
        void stopAgent(entt::entity entity) override;
        glm::vec3 getAgentVelocity(entt::entity entity) override;
        float getAgentSpeed(entt::entity entity) override;
        navigation::NavPath findPath(glm::vec3 start, glm::vec3 end) override;
        glm::vec3 getClosestPointOnNavmesh(glm::vec3 point, float searchRadius) override;
        bool isPointOnNavmesh(glm::vec3 point, float tolerance) override;
        bool hasNavmesh() override;

        // VFX API
        services::VFXInstanceId createVFXInstance(const services::VFXRuntimeParams& params) override;
        void destroyVFXInstance(services::VFXInstanceId instanceId) override;
        void setVFXInstanceTransform(services::VFXInstanceId instanceId, const glm::mat4& worldTransform) override;
        void playVFXInstance(services::VFXInstanceId instanceId) override;
        void stopVFXInstance(services::VFXInstanceId instanceId) override;
        bool isVFXInstancePlaying(services::VFXInstanceId instanceId) override;

        // Meta component registration
        void registerComponentBridge(MetaComponentBridge bridge) override;

        static const std::vector<MetaComponentBridge>& getAllBridges() { return allBridges; }

        static void setScriptBindingRegistrar(ScriptBindingRegistrar registrar) { scriptBindingRegistrar = std::move(registrar); }
        static ScriptBindingRegistrar getScriptBindingRegistrar() { return scriptBindingRegistrar; }

        void cleanupAll();

        std::vector<std::unique_ptr<pipeline::PipelineStage>> takeImportStages();

        const std::string& getPluginName() const { return pluginName; }
    };

}
