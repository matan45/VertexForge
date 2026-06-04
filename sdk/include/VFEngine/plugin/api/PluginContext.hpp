#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <any>
#include <utility>
#include <vector>
#include <functional>
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include "../../services/data/RenderHookTypes.hpp"
#include "../../services/data/CustomPipelineTypes.hpp"
#include "../../services/events/EventTypes.hpp"
#include "../../services/interfaces/audio/IAudioService.hpp"
#include "../../services/interfaces/physics/IPhysicsService.hpp"
#include "../../utilities/terrain/TerrainHitResult.hpp"
#include "../../utilities/terrain/TerrainHeightAtResult.hpp"
#include "../../utilities/navigation/NavmeshData.hpp"
#include "../../services/data/VFXTypes.hpp"

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

    // Type-erased bridge for accessing plugin-defined native components from the engine.
    // Lambdas are instantiated in the plugin DLL (correct type_index) but stored engine-side.
    struct MetaComponentBridge {
        entt::id_type typeId = 0;
        // Owning copy — was previously `const char*`, but the engine stores bridges
        // by value in a static cache that outlives the plugin DLL's string storage.
        // A raw pointer would dangle on plugin unload. std::string deep-copies.
        std::string name;
        std::string pluginName;
        entt::meta_type metaType;  // Resolved from plugin DLL's meta context
        std::function<void*(entt::registry&, entt::entity)> tryGet;
        std::function<void(entt::registry&, entt::entity)> emplace;
        std::function<void(entt::registry&, entt::entity)> remove;
        std::function<bool(entt::registry&, entt::entity)> has;
    };

    namespace capability {
        constexpr std::string_view editor   = "editor";
        constexpr std::string_view audio    = "audio";
        constexpr std::string_view physics  = "physics";
        constexpr std::string_view import_  = "import";
        constexpr std::string_view scripting = "scripting";
        constexpr std::string_view graphics = "graphics";
        constexpr std::string_view terrain  = "terrain";
        constexpr std::string_view input    = "input";
        constexpr std::string_view navmesh  = "navmesh";
        constexpr std::string_view vfx      = "vfx";
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
        // With a non-empty title, the window appears in the editor's "Plugins"
        // main-menu dropdown and can be opened/closed there (starts visible).
        // With an empty title it is always drawn (legacy behavior).
        virtual void registerEditorWindow(std::shared_ptr<controllers::imguiHandler::ImguiWindow> window,
                                          const std::string& title = "") = 0;

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

        // === Custom Render Pipelines ===
        // Only available when hasCapability(capability::graphics) is true.
        // Handle-based custom shader rendering: the engine compiles the GLSL, owns all
        // Vulkan objects, and records the draws inside the scene pass (depth-tested
        // against scene geometry). No Vulkan calls ever cross the plugin DLL boundary.
        // All handles are cleaned up automatically on plugin unload.

        // Compile a custom shader and create a pipeline against the scene formats.
        // Returns an invalid handle on compile/creation failure (error is logged).
        virtual CustomPipelineHandle createCustomPipeline(const CustomPipelineDesc& desc) = 0;

        // Upload geometry to device-local GPU memory. Call at init time, not per frame.
        virtual CustomMeshHandle uploadCustomMesh(CustomMeshData data) = 0;

        // Enqueue one draw for the current frame (call each frame from onUpdate).
        // pushConstants must match the pipeline's pushConstantSize (may be empty if 0).
        virtual void drawCustomMesh(CustomPipelineHandle pipeline, CustomMeshHandle mesh,
                                    const glm::mat4& model,
                                    const std::vector<std::byte>& pushConstants = {}) = 0;

        virtual void destroyCustomPipeline(CustomPipelineHandle handle) = 0;
        virtual void destroyCustomMesh(CustomMeshHandle handle) = 0;

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
        // Use this for direct component manipulation (add, get, view, emplace, etc.).
        // EnTT type IDs are shared across DLL boundaries via ECSRegistry DLL.
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

        // === Plugin Config Persistence ===
        // Save plugin settings to a JSON file in the plugin's data directory.
        // Creates the directory if it doesn't exist.
        virtual void saveConfig(const nlohmann::json& config) = 0;

        // Load plugin settings from the data directory.
        // Returns an empty JSON object if no config file exists or if it's corrupt.
        virtual nlohmann::json loadConfig() = 0;

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

        // === NavMesh API ===
        // Only available when hasCapability(capability::navmesh) is true.

        virtual void setAgentDestination(entt::entity entity, glm::vec3 target) = 0;
        virtual void stopAgent(entt::entity entity) = 0;
        virtual glm::vec3 getAgentVelocity(entt::entity entity) = 0;
        virtual float getAgentSpeed(entt::entity entity) = 0;
        // Find a path between two world positions. Check result.isValid before using waypoints.
        virtual navigation::NavPath findPath(glm::vec3 start, glm::vec3 end) = 0;
        virtual glm::vec3 getClosestPointOnNavmesh(glm::vec3 point, float searchRadius = 5.0f) = 0;
        virtual bool isPointOnNavmesh(glm::vec3 point, float tolerance = 0.5f) = 0;
        virtual bool hasNavmesh() = 0;

        // === VFX API ===
        // Only available when hasCapability(capability::vfx) is true.

        // Create a VFX instance from an asset path. Returns instance ID for controlling it.
        virtual services::VFXInstanceId createVFXInstance(const services::VFXRuntimeParams& params) = 0;
        virtual void destroyVFXInstance(services::VFXInstanceId instanceId) = 0;
        virtual void setVFXInstanceTransform(services::VFXInstanceId instanceId, const glm::mat4& worldTransform) = 0;
        virtual void playVFXInstance(services::VFXInstanceId instanceId) = 0;
        virtual void stopVFXInstance(services::VFXInstanceId instanceId) = 0;
        virtual bool isVFXInstancePlaying(services::VFXInstanceId instanceId) = 0;

        // === Native Component Registration ===
        // Register native C++ component types so the engine can discover, inspect, and manage them.
        // Uses type-erased bridges — no shared type IDs needed across DLL boundaries.

        // Register a type-erased component bridge for engine-side access (inspector, Add Component UI).
        virtual void registerComponentBridge(MetaComponentBridge bridge) = 0;

        // Template helper — registers a native component with a bridge + meta reflection.
        // Instantiated in the plugin DLL so type_index is correct for that DLL.
        // Returns entt::meta_factory<T> for chaining .data<>() calls (used by auto-inspector).
        //
        // Usage:
        //   ctx->registerNativeComponent<Health>("Health")
        //       .data<&Health::maxHP>("maxHP")
        //       .data<&Health::currentHP>("currentHP");
        template<typename T>
        auto registerNativeComponent(const char* name)
        {
            entt::id_type id = entt::hashed_string::value(name);

            MetaComponentBridge bridge;
            bridge.typeId = id;
            bridge.name = name;
            bridge.tryGet = [](entt::registry& r, entt::entity e) -> void* {
                return r.try_get<T>(e);
            };
            bridge.emplace = [](entt::registry& r, entt::entity e) {
                if (!r.all_of<T>(e)) r.emplace<T>(e);
            };
            bridge.remove = [](entt::registry& r, entt::entity e) {
                if (r.all_of<T>(e)) r.remove<T>(e);
            };
            bridge.has = [](entt::registry& r, entt::entity e) -> bool {
                return r.all_of<T>(e);
            };
            // Register meta type in plugin DLL's local context
            auto factory = entt::meta_factory<T>().type(id, name);
            bridge.metaType = entt::resolve<T>();
            registerComponentBridge(std::move(bridge));

            return factory;
        }

    };

}
