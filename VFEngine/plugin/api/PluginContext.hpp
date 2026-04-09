#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <any>
#include <utility>
#include <vector>
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include "../../services/data/RenderHookTypes.hpp"
#include "../../services/events/EventTypes.hpp"
#include "../../services/interfaces/audio/IAudioService.hpp"
#include "../../services/interfaces/physics/IPhysicsService.hpp"
#include "../../utilities/terrain/TerrainHitResult.hpp"
#include "../../utilities/terrain/TerrainHeightAtResult.hpp"

struct ImGuiContext;

namespace events {
    class EventDispatcher;
}

namespace controllers::imguiHandler {
    class ImguiWindow;
}

namespace pipeline {
    class PipelineStage;
}

namespace plugin {

    class ComponentBuilder;
    class PluginComponentData;

    namespace capability {
        constexpr std::string_view editor   = "editor";
        constexpr std::string_view audio    = "audio";
        constexpr std::string_view physics  = "physics";
        constexpr std::string_view import_  = "import";
        constexpr std::string_view scripting = "scripting";
        constexpr std::string_view graphics = "graphics";
        constexpr std::string_view terrain  = "terrain";
        constexpr std::string_view input    = "input";
    }

    class PluginContext
    {
    public:
        virtual ~PluginContext() = default;

        // === Event System Access ===

        // Returns the global EventDispatcher singleton.
        virtual events::EventDispatcher& getEventDispatcher() = 0;

        // Track a subscription for automatic cleanup when the plugin is unloaded.
        // Returns the same token passed in.
        virtual events::SubscriptionToken managedSubscribe(events::SubscriptionToken token) = 0;

        // === Editor Window Registration ===
        // Only available when hasCapability(capability::editor) is true.
        virtual void registerEditorWindow(std::shared_ptr<controllers::imguiHandler::ImguiWindow> window) = 0;

        // === Import Pipeline Extension ===
        // Only available when hasCapability(capability::import_) is true.
        virtual void registerImportStage(std::unique_ptr<pipeline::PipelineStage> stage) = 0;

        // === Script Native Function Registration ===
        // Register a native function callable from mType scripts.
        // The function signature must be: value::Value(const std::vector<value::Value>&)
        // Wrap it in std::any before passing. Only available when hasCapability(capability::scripting) is true.
        virtual void registerScriptFunction(const std::string& name, std::any function) = 0;

        // === Graphics Render Hooks ===
        // Only available when hasCapability(capability::graphics) is true.
        // Register a callback at a specific render pass injection point.
        virtual RenderHookHandle registerRenderPassHook(
            RenderPassHookPoint hookPoint,
            RenderHookCallback callback) = 0;

        // Unregister a previously registered render hook. Also cleaned up automatically on unload.
        virtual void unregisterRenderPassHook(RenderHookHandle handle) = 0;

        // === Custom Component Registration ===
        // Register a custom component type using the property descriptor builder.
        // Declare typed properties (int, float, bool, string, vec2/3/4, color) with defaults and min/max hints.
        // The engine auto-generates serialization, deserialization, and inspector UI.
        // Optionally call setInspector() on the builder for a custom ImGui inspector.
        // Call .build() to finalize registration.
        virtual ComponentBuilder& registerComponent(const std::string& componentName) = 0;

        // === Plugin Component ECS Helpers ===
        // Safe across DLL boundary — all EnTT operations execute in the exe's address space.
        // Use these instead of directly accessing the registry for plugin component data.
        virtual bool addPluginComponent(entt::entity entity, const std::string& componentName) = 0;
        virtual bool removePluginComponent(entt::entity entity, const std::string& componentName) = 0;
        // WARNING: The returned pointer is valid only until the next ECS mutation
        // (scene clear, entity destroy, component remove, plugin unload).
        // Do NOT cache this pointer across frames — re-query each frame.
        virtual PluginComponentData* getPluginComponent(entt::entity entity, const std::string& componentName) = 0;
        virtual bool hasPluginComponent(entt::entity entity, const std::string& componentName) = 0;

        // Iterate all entities that have a specific plugin component.
        // The callback receives the entity handle and a typed data accessor.
        // Iteration happens exe-side (safe across DLL boundary).
        virtual void forEachWithComponent(const std::string& componentName,
                                           const std::function<void(entt::entity, PluginComponentData&)>& callback) = 0;

        // === Plugin Events ===
        // Dynamic event system for plugin-to-plugin and plugin-to-engine communication.
        // Uses string event names + JSON payloads (safe across DLL boundaries).

        // Publish a fire-and-forget notification. All subscribers receive it.
        virtual void publishEvent(const std::string& eventName, const nlohmann::json& data = {}) = 0;

        // Subscribe to a named event. Returns a subscription token (auto-cleaned on plugin unload).
        virtual events::SubscriptionToken subscribeEvent(const std::string& eventName,
                                                          std::function<void(const nlohmann::json&)> handler) = 0;

        // === ECS Registry Access ===
        // Returns the global EnTT entity registry.
        // Use this for direct component manipulation (add, get, view, etc.).
        // NOTE: For plugin-defined custom components, use the plugin component helpers above
        // instead of the registry directly, to avoid EnTT DLL type-ID issues.
        virtual entt::registry& getRegistry() = 0;

        // === Capability Queries ===
        // Check if an engine capability is available. Use plugin::capability constants.
        virtual bool hasCapability(const std::string& capability) const = 0;

        // === ImGui Context ===
        // Plugins must call ImGui::SetCurrentContext() with this in onInitialize.
        virtual ImGuiContext* getImGuiContext() = 0;

        // === Plugin Data Directory ===
        // Returns a persistent directory path for this plugin's data storage.
        virtual std::string getPluginDataPath() const = 0;

        // === Logging ===
        virtual void logInfo(const std::string& message) = 0;
        virtual void logWarning(const std::string& message) = 0;
        virtual void logError(const std::string& message) = 0;

        // === Audio API ===
        // Only available when hasCapability(capability::audio) is true.

        // Play a spatialized 3D sound at a world position. Returns a handle for controlling playback.
        virtual services::AudioHandle playSound3D(const std::string& path, glm::vec3 position,
                                                  const services::AudioParams& params = {}) = 0;
        // Play a streaming (non-3D) sound. Returns a handle for controlling playback.
        virtual services::AudioHandle playStreamingSound(const std::string& path,
                                                         const services::AudioParams& params = {}) = 0;
        virtual void stopSound(services::AudioHandle handle) = 0;
        virtual void pauseSound(services::AudioHandle handle) = 0;
        virtual void resumeSound(services::AudioHandle handle) = 0;
        virtual void setSoundVolume(services::AudioHandle handle, float volume) = 0;
        virtual void setSoundPitch(services::AudioHandle handle, float pitch) = 0;
        virtual bool isSoundPlaying(services::AudioHandle handle) = 0;
        virtual void setBusVolume(const std::string& busName, float volume) = 0;
        virtual float getBusVolume(const std::string& busName) = 0;

        // === Physics API ===
        // Only available when hasCapability(capability::physics) is true.

        // Cast a ray and return the first hit. Check hit.hit to see if anything was hit.
        virtual services::RaycastHit raycast(glm::vec3 origin, glm::vec3 direction,
                                             float maxDistance, uint16_t layerMask = 0xFFFF) = 0;
        // Cast a ray and return all hits along the ray.
        virtual std::vector<services::RaycastHit> raycastAll(glm::vec3 origin, glm::vec3 direction,
                                                              float maxDistance, uint16_t layerMask = 0xFFFF) = 0;
        virtual void applyForce(entt::entity entity, glm::vec3 force) = 0;
        virtual void applyImpulse(entt::entity entity, glm::vec3 impulse) = 0;
        virtual void setLinearVelocity(entt::entity entity, glm::vec3 velocity) = 0;
        virtual glm::vec3 getLinearVelocity(entt::entity entity) = 0;
        virtual glm::vec3 getAngularVelocity(entt::entity entity) = 0;
        virtual bool isGrounded(entt::entity entity) = 0;
        virtual bool hasRigidBody(entt::entity entity) = 0;
        virtual glm::vec3 getPhysicsPosition(entt::entity entity) = 0;

        // === Terrain API ===
        // Only available when hasCapability(capability::terrain) is true.

        // Query terrain height at a world position. Check result.valid before using result.height.
        virtual terrain::TerrainHeightAtResult getTerrainHeightAt(float worldX, float worldZ) = 0;
        // Get the current terrain cursor raycast hit (from editor viewport).
        virtual terrain::TerrainHitResult getTerrainHit() = 0;
        virtual bool hasTerrainComponent(entt::entity entity) = 0;

        // === Input API ===
        // Only available when hasCapability(capability::input) is true.

        virtual bool isKeyDown(int keyCode) = 0;
        virtual bool isKeyPressed(int keyCode) = 0;
        virtual bool isMouseButtonDown(int button) = 0;
        virtual glm::vec2 getMousePosition() = 0;
        virtual glm::vec2 getMouseDelta() = 0;
        // Action-based input (uses registered action mappings).
        virtual bool isActionDown(const std::string& actionName) = 0;
        virtual bool isActionPressed(const std::string& actionName) = 0;
        virtual float getAxis1DValue(const std::string& axisName) = 0;
        virtual glm::vec2 getAxis2DValue(const std::string& axisName) = 0;

    };

}
