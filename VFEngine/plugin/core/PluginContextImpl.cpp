#include "print/Log.hpp"
#include "PluginContextImpl.hpp"
#include "PluginManager.hpp"
#include "PluginEventBus.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "events/render/RenderHookEvents.hpp"
#include "events/render/CustomPipelineEvents.hpp"
#include "events/render/PluginTextureEvents.hpp"
#include "events/vfx/VFXRuntimeEvents.hpp"
#include "events/audio/AudioEvents.hpp"
#include "events/audio/AudioBusEvents.hpp"
#include "events/physics/PhysicsEvents.hpp"
#include "events/physics/ControllerEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/terrain/TerrainRaycastEvents.hpp"
#include "events/input/InputEvents.hpp"
#include "events/input/ActionMappingEvents.hpp"
#include "events/input/RuntimePickerEvents.hpp"
#include "events/navmesh/NavmeshEvents.hpp"
#include "events/vfx/VFXRuntimeEvents.hpp"
#include "data/EntityConversion.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "imguiHandler/PluginWindowRegistry.hpp"
#include "scene/EntityRegistry.hpp"
#include "Pipeline.hpp"
#include <imgui.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>

namespace plugin {

    // Static member definitions
    std::vector<MetaComponentBridge> PluginContextImpl::allBridges{};
    PluginContextImpl::ScriptBindingRegistrar PluginContextImpl::scriptBindingRegistrar{};
    const MTypePluginHost* PluginContextImpl::scriptHostVTable = nullptr;


    PluginContextImpl::PluginContextImpl(const std::string& pluginName,
                                         const std::unordered_set<std::string>& capabilities)
        : pluginName(pluginName)
        , capabilities(capabilities)
    {
    }

    PluginContextImpl::~PluginContextImpl()
    {
        cleanupAll();
    }

    events::EventDispatcher& PluginContextImpl::getEventDispatcher()
    {
        return events::EventDispatcher::instance();
    }

    events::SubscriptionToken PluginContextImpl::managedSubscribe(events::SubscriptionToken token)
    {
        managedSubscriptions.push_back(token);
        return token;
    }

    void PluginContextImpl::registerEditorWindow(std::shared_ptr<controllers::imguiHandler::ImguiWindow> window,
                                                 const std::string& title)
    {
        if (!hasCapability(std::string(capability::editor))) {
            vfLogWarning("[Plugin:{}] Cannot register editor window - editor capability not available", pluginName);
            return;
        }

        if (title.empty()) {
            controllers::imguiHandler::ImguiWindowHandler::add(window);
        } else {
            controllers::imguiHandler::PluginWindowRegistry::add(pluginName, title, window);
        }
        registeredWindows.push_back(std::move(window));
    }

    void PluginContextImpl::registerImportStage(std::unique_ptr<pipeline::PipelineStage> stage)
    {
        if (!hasCapability(std::string(capability::import_))) {
            vfLogWarning("[Plugin:{}] Cannot register import stage - import capability not available", pluginName);
            return;
        }

        if (!stage) {
            vfLogWarning("[Plugin:{}] Cannot register null import stage", pluginName);
            return;
        }

        vfLogInfo("[Plugin:{}] Registered import stage: {}", pluginName, stage->getName());
        registeredImportStages.push_back(std::move(stage));
    }

    std::vector<std::unique_ptr<pipeline::PipelineStage>> PluginContextImpl::takeImportStages()
    {
        return std::move(registeredImportStages);
    }

    void PluginContextImpl::publishEvent(const std::string& eventName, const nlohmann::json& data)
    {
        PluginEventBus::instance().publish(eventName, data);
    }

    events::SubscriptionToken PluginContextImpl::subscribeEvent(const std::string& eventName,
                                                                 std::function<void(const nlohmann::json&)> handler)
    {
        auto token = PluginEventBus::instance().subscribe(eventName, std::move(handler));
        pluginEventSubscriptions.push_back(token);
        return token;
    }

    void PluginContextImpl::registerScriptFunction(const std::string& name, MTypeNativeFn fn, void* userData)
    {
        if (!hasCapability(std::string(capability::scripting))) {
            vfLogWarning("[Plugin:{}] Cannot register script function '{}' - scripting capability not available", pluginName, name);
            return;
        }
        if (!fn) {
            vfLogWarning("[Plugin:{}] Cannot register script function '{}' - null function", pluginName, name);
            return;
        }

        events::scripting::RegisterNativeScriptFunctionCommand cmd;
        cmd.functionName = name;
        cmd.function = std::any(std::pair<MTypeNativeFn, void*>(fn, userData));
        events::EventDispatcher::instance().execute(cmd);

        registeredScriptFunctions.push_back(name);
        vfLogInfo("[Plugin:{}] Registered native script function: {}", pluginName, name);
    }

    const MTypePluginHost* PluginContextImpl::getScriptHost()
    {
        if (!hasCapability(std::string(capability::scripting))) {
            vfLogWarning("[Plugin:{}] getScriptHost unavailable - scripting capability not available", pluginName);
            return nullptr;
        }
        return scriptHostVTable;
    }

    plugin::RenderHookHandle PluginContextImpl::registerRenderPassHook(
        plugin::RenderPassHookPoint hookPoint,
        plugin::RenderHookCallback callback)
    {
        if (!hasCapability(std::string(capability::graphics))) {
            vfLogWarning("[Plugin:{}] Cannot register render hook - graphics capability not available", pluginName);
            return {};
        }

        events::renderhook::RegisterRenderPassHookCommand cmd;
        cmd.hookPoint = hookPoint;
        // VK-1365: wrap the plugin callback so hooks of a soft-disabled plugin are
        // skipped at dispatch. The flag is atomic (hooks execute on the render thread);
        // `this` outlives the hook — cleanupAll unregisters before the context dies.
        cmd.callback = [this, cb = std::move(callback)](const plugin::RenderHookContext& ctx) {
            if (!activeState.load(std::memory_order_relaxed)) return;
            cb(ctx);
        };
        auto handle = events::EventDispatcher::instance().execute(cmd);

        if (handle.isValid()) {
            registeredRenderHooks.push_back(handle);
            vfLogInfo("[Plugin:{}] Registered render pass hook at point {}", pluginName, static_cast<uint32_t>(hookPoint));
        }

        return handle;
    }

    void PluginContextImpl::unregisterRenderPassHook(plugin::RenderHookHandle handle)
    {
        if (!handle.isValid()) return;

        events::renderhook::UnregisterRenderPassHookCommand cmd;
        cmd.handle = handle;
        events::EventDispatcher::instance().execute(cmd);

        std::erase_if(registeredRenderHooks,
            [&](const plugin::RenderHookHandle& h) { return h.id == handle.id; });
    }

    plugin::CustomPipelineHandle PluginContextImpl::createCustomPipeline(const plugin::CustomPipelineDesc& desc)
    {
        if (!hasCapability(std::string(capability::graphics))) {
            vfLogWarning("[Plugin:{}] Cannot create custom pipeline - graphics capability not available", pluginName);
            return {};
        }

        events::custompipeline::CreateCustomPipelineCommand cmd;
        cmd.desc = desc;
        auto handle = events::EventDispatcher::instance().execute(cmd);

        if (handle.isValid()) {
            managedCustomPipelines.push_back(handle);
            vfLogInfo("[Plugin:{}] Created custom pipeline {}", pluginName, handle.id);
        } else {
            vfLogError("[Plugin:{}] Custom pipeline creation failed (see engine log)", pluginName);
        }

        return handle;
    }

    plugin::CustomMeshHandle PluginContextImpl::uploadCustomMesh(plugin::CustomMeshData data)
    {
        if (!hasCapability(std::string(capability::graphics))) {
            vfLogWarning("[Plugin:{}] Cannot upload custom mesh - graphics capability not available", pluginName);
            return {};
        }

        events::custompipeline::UploadCustomMeshCommand cmd;
        cmd.data = std::move(data);
        auto handle = events::EventDispatcher::instance().execute(cmd);

        if (handle.isValid()) {
            managedCustomMeshes.push_back(handle);
        } else {
            vfLogError("[Plugin:{}] Custom mesh upload failed (see engine log)", pluginName);
        }

        return handle;
    }

    void PluginContextImpl::drawCustomMesh(plugin::CustomPipelineHandle pipeline, plugin::CustomMeshHandle mesh,
                                           const glm::mat4& model,
                                           const std::vector<std::byte>& pushConstants)
    {
        if (!pipeline.isValid() || !mesh.isValid()) return;
        if (!isActive()) return; // VK-1365: no draws while soft-disabled (covers event-driven enqueues too)

        events::custompipeline::EnqueueCustomDrawCommand cmd;
        cmd.item.pipeline = pipeline;
        cmd.item.mesh = mesh;
        cmd.item.model = model;
        cmd.item.pushConstants = pushConstants;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PluginContextImpl::destroyCustomPipeline(plugin::CustomPipelineHandle handle)
    {
        if (!handle.isValid()) return;

        events::custompipeline::DestroyCustomPipelineCommand cmd;
        cmd.handle = handle;
        events::EventDispatcher::instance().execute(cmd);

        std::erase_if(managedCustomPipelines,
            [&](const plugin::CustomPipelineHandle& h) { return h.id == handle.id; });
    }

    void PluginContextImpl::destroyCustomMesh(plugin::CustomMeshHandle handle)
    {
        if (!handle.isValid()) return;

        events::custompipeline::DestroyCustomMeshCommand cmd;
        cmd.handle = handle;
        events::EventDispatcher::instance().execute(cmd);

        std::erase_if(managedCustomMeshes,
            [&](const plugin::CustomMeshHandle& h) { return h.id == handle.id; });
    }

    plugin::PluginTextureHandle PluginContextImpl::createTexture2D(uint32_t width, uint32_t height,
                                                                   plugin::TextureFormat format)
    {
        if (!hasCapability(std::string(capability::graphics))) {
            vfLogWarning("[Plugin:{}] Cannot create texture - graphics capability not available", pluginName);
            return {};
        }

        events::plugintexture::CreateTexture2DCommand cmd;
        cmd.width = width;
        cmd.height = height;
        cmd.format = format;
        auto handle = events::EventDispatcher::instance().execute(cmd);

        if (handle.isValid()) {
            managedTextures.push_back(handle);
            vfLogInfo("[Plugin:{}] Created texture {} ({}x{})", pluginName, handle.id, width, height);
        } else {
            vfLogError("[Plugin:{}] Texture creation failed (see engine log)", pluginName);
        }

        return handle;
    }

    void PluginContextImpl::updateTexture2D(plugin::PluginTextureHandle handle, const void* data, size_t size)
    {
        if (!handle.isValid() || !data || size == 0) return;

        events::plugintexture::UpdateTexture2DCommand cmd;
        cmd.handle = handle;
        cmd.data.resize(size);
        std::memcpy(cmd.data.data(), data, size);
        events::EventDispatcher::instance().execute(cmd);
    }

    void PluginContextImpl::destroyTexture2D(plugin::PluginTextureHandle handle)
    {
        if (!handle.isValid()) return;

        if (boundWorldMaskTexture.id == handle.id) {
            unbindWorldMask();
        }

        events::plugintexture::DestroyTexture2DCommand cmd;
        cmd.handle = handle;
        events::EventDispatcher::instance().execute(cmd);

        std::erase_if(managedTextures,
            [&](const plugin::PluginTextureHandle& h) { return h.id == handle.id; });
    }

    void PluginContextImpl::bindWorldMask(plugin::PluginTextureHandle handle,
                                          const glm::vec3& worldMin, const glm::vec3& worldMax,
                                          const plugin::WorldMaskParams& params)
    {
        if (!hasCapability(std::string(capability::graphics))) {
            vfLogWarning("[Plugin:{}] Cannot bind world mask - graphics capability not available", pluginName);
            return;
        }
        if (!handle.isValid()) return;

        events::plugintexture::BindWorldMaskCommand cmd;
        cmd.handle = handle;
        cmd.worldMin = worldMin;
        cmd.worldMax = worldMax;
        cmd.params = params;
        // VK-1365: cache the plugin's intended params; while soft-disabled the mask
        // stays bound but is forced inert (enabled=false => treated as 1.0).
        lastWorldMaskParams = params;
        if (!isActive()) cmd.params.enabled = false;
        events::EventDispatcher::instance().execute(cmd);

        boundWorldMaskTexture = handle;
    }

    void PluginContextImpl::unbindWorldMask()
    {
        if (!boundWorldMaskTexture.isValid()) return;

        events::plugintexture::UnbindWorldMaskCommand cmd;
        events::EventDispatcher::instance().execute(cmd);

        boundWorldMaskTexture = {};
    }

    void PluginContextImpl::setWorldMaskParams(const plugin::WorldMaskParams& params)
    {
        if (!hasCapability(std::string(capability::graphics))) return;

        events::plugintexture::SetWorldMaskParamsCommand cmd;
        cmd.params = params;
        // VK-1365: cache intent, keep the mask inert while soft-disabled.
        lastWorldMaskParams = params;
        if (!isActive()) cmd.params.enabled = false;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PluginContextImpl::setActive(bool active)
    {
        activeState.store(active, std::memory_order_relaxed);

        // Hide/show this plugin's registered editor windows. Composes with (never
        // clobbers) the user's per-window visibility toggle in the Plugins menu.
        // No-op in Runtime, where no windows are registered.
        controllers::imguiHandler::PluginWindowRegistry::setPluginActive(pluginName, active);

        // Suppress/restore the bound world mask via the existing params.enabled
        // runtime gate (UBO write only — no rebind, hitch-free). Restoring replays
        // the plugin's cached params verbatim.
        if (boundWorldMaskTexture.isValid()) {
            events::plugintexture::SetWorldMaskParamsCommand cmd;
            cmd.params = lastWorldMaskParams;
            if (!active) cmd.params.enabled = false;
            events::EventDispatcher::instance().execute(cmd);
        }
    }

    entt::registry& PluginContextImpl::getRegistry()
    {
        return scene::EntityRegistry::getRegistry();
    }

    bool PluginContextImpl::hasCapability(const std::string& capability) const
    {
        return capabilities.contains(capability);
    }

    ImGuiContext* PluginContextImpl::getImGuiContext()
    {
        return ImGui::GetCurrentContext();
    }

    std::string PluginContextImpl::getPluginDataPath() const
    {
        std::string safeName;
        safeName.reserve(pluginName.size());
        for (char c : pluginName) {
            if (c == '/' || c == '\\' || c == '\0') {
                safeName += '_';
            } else {
                safeName += c;
            }
        }
        if (safeName.find_first_not_of('.') == std::string::npos) {
            safeName = "_plugin_";
        }

        // Anchor on the resolved plugins directory, not the CWD — IDE launchers
        // run with a different working directory and would scatter config files
        // (and create stray "plugins" folders that used to hijack discovery).
        const auto* manager = PluginManager::getActive();
        auto base = manager && !manager->getPluginsDirectory().empty()
            ? manager->getPluginsDirectory()
            : PluginManager::resolvePluginsDirectory();
        auto path = base / "data" / safeName;
        return path.string();
    }

    void PluginContextImpl::saveConfig(const nlohmann::json& config)
    {
        auto dir = std::filesystem::path(getPluginDataPath());
        std::filesystem::create_directories(dir);

        auto configPath = dir / "config.json";
        std::ofstream file(configPath);
        if (!file.is_open())
        {
            vfLogError("[Plugin:{}] Failed to save config to {}", pluginName, configPath.string());
            return;
        }
        file << config.dump(4);
        vfLogInfo("[Plugin:{}] Config saved to {}", pluginName, configPath.string());
    }

    nlohmann::json PluginContextImpl::loadConfig()
    {
        auto configPath = std::filesystem::path(getPluginDataPath()) / "config.json";
        if (!std::filesystem::exists(configPath))
            return nlohmann::json::object();

        std::ifstream file(configPath);
        if (!file.is_open())
            return nlohmann::json::object();

        try
        {
            nlohmann::json config;
            file >> config;
            return config;
        }
        catch (...)
        {
            vfLogWarning("[Plugin:{}] Failed to parse config file {}", pluginName, configPath.string());
            return nlohmann::json::object();
        }
    }

    void PluginContextImpl::logInfo(const std::string& message)
    {
        vfLogInfo("[Plugin:{}] {}", pluginName, message);
    }

    void PluginContextImpl::logWarning(const std::string& message)
    {
        vfLogWarning("[Plugin:{}] {}", pluginName, message);
    }

    void PluginContextImpl::logError(const std::string& message)
    {
        vfLogError("[Plugin:{}] {}", pluginName, message);
    }

    // ========================================================================
    // Audio API
    // ========================================================================

    services::AudioHandle PluginContextImpl::playSound3D(const std::string& path, glm::vec3 position,
                                                          const services::AudioParams& params)
    {
        if (!hasCapability(std::string(capability::audio))) {
            vfLogWarning("[Plugin:{}] Cannot play sound - audio capability not available", pluginName);
            return {};
        }
        events::audio::PlaySound3DCommand cmd;
        cmd.path = path;
        cmd.position = position;
        cmd.params = params;
        auto handle = events::EventDispatcher::instance().execute(cmd);
        if (handle.isValid()) {
            managedAudioHandles.push_back(handle);
        }
        return handle;
    }

    services::AudioHandle PluginContextImpl::playStreamingSound(const std::string& path,
                                                                 const services::AudioParams& params)
    {
        if (!hasCapability(std::string(capability::audio))) {
            vfLogWarning("[Plugin:{}] Cannot play sound - audio capability not available", pluginName);
            return {};
        }
        events::audio::PlayStreamingSoundCommand cmd;
        cmd.path = path;
        cmd.params = params;
        auto handle = events::EventDispatcher::instance().execute(cmd);
        if (handle.isValid()) {
            managedAudioHandles.push_back(handle);
        }
        return handle;
    }

    void PluginContextImpl::stopSound(services::AudioHandle handle)
    {
        if (!handle.isValid()) return;
        events::audio::StopSoundCommand cmd;
        cmd.handle = handle;
        events::EventDispatcher::instance().execute(cmd);
        std::erase_if(managedAudioHandles,
            [&](const services::AudioHandle& h) { return h.id == handle.id; });
    }

    void PluginContextImpl::pauseSound(services::AudioHandle handle)
    {
        if (!handle.isValid()) return;
        events::audio::PauseSoundCommand cmd;
        cmd.handle = handle;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PluginContextImpl::resumeSound(services::AudioHandle handle)
    {
        if (!handle.isValid()) return;
        events::audio::ResumeSoundCommand cmd;
        cmd.handle = handle;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PluginContextImpl::setSoundVolume(services::AudioHandle handle, float volume)
    {
        if (!handle.isValid()) return;
        events::audio::SetSoundVolumeCommand cmd;
        cmd.handle = handle;
        cmd.volume = volume;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PluginContextImpl::setSoundPitch(services::AudioHandle handle, float pitch)
    {
        if (!handle.isValid()) return;
        events::audio::SetSoundPitchCommand cmd;
        cmd.handle = handle;
        cmd.pitch = pitch;
        events::EventDispatcher::instance().execute(cmd);
    }

    bool PluginContextImpl::isSoundPlaying(services::AudioHandle handle)
    {
        if (!handle.isValid()) return false;
        events::audio::IsSoundPlayingQuery q;
        q.handle = handle;
        return events::EventDispatcher::instance().query(q);
    }

    void PluginContextImpl::setBusVolume(const std::string& busName, float volume)
    {
        if (!hasCapability(std::string(capability::audio))) {
            vfLogWarning("[Plugin:{}] Cannot set bus volume - audio capability not available", pluginName);
            return;
        }
        events::audio::SetBusVolumeCommand cmd;
        cmd.busName = busName;
        cmd.volume = volume;
        events::EventDispatcher::instance().execute(cmd);
    }

    float PluginContextImpl::getBusVolume(const std::string& busName)
    {
        if (!hasCapability(std::string(capability::audio))) {
            return 0.0f;
        }
        events::audio::GetBusVolumeQuery q;
        q.busName = busName;
        return events::EventDispatcher::instance().query(q);
    }

    // ========================================================================
    // Physics API
    // ========================================================================

    services::RaycastHit PluginContextImpl::raycast(glm::vec3 origin, glm::vec3 direction,
                                                     float maxDistance, uint16_t layerMask)
    {
        if (!hasCapability(std::string(capability::physics))) {
            return {};
        }
        events::physics::RaycastQuery q;
        q.origin = origin;
        q.direction = direction;
        q.maxDistance = maxDistance;
        q.layerMask = layerMask;
        return events::EventDispatcher::instance().query(q);
    }

    std::vector<services::RaycastHit> PluginContextImpl::raycastAll(glm::vec3 origin, glm::vec3 direction,
                                                                      float maxDistance, uint16_t layerMask)
    {
        if (!hasCapability(std::string(capability::physics))) {
            return {};
        }
        events::physics::RaycastAllQuery q;
        q.origin = origin;
        q.direction = direction;
        q.maxDistance = maxDistance;
        q.layerMask = layerMask;
        return events::EventDispatcher::instance().query(q);
    }

    void PluginContextImpl::applyForce(entt::entity entity, glm::vec3 force)
    {
        if (!hasCapability(std::string(capability::physics))) {
            vfLogWarning("[Plugin:{}] Cannot apply force - physics capability not available", pluginName);
            return;
        }
        events::physics::ApplyForceCommand cmd;
        cmd.entity = services::internal::toHandle(entity);
        cmd.force = force;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PluginContextImpl::applyImpulse(entt::entity entity, glm::vec3 impulse)
    {
        if (!hasCapability(std::string(capability::physics))) {
            vfLogWarning("[Plugin:{}] Cannot apply impulse - physics capability not available", pluginName);
            return;
        }
        events::physics::ApplyImpulseCommand cmd;
        cmd.entity = services::internal::toHandle(entity);
        cmd.impulse = impulse;
        events::EventDispatcher::instance().execute(cmd);
    }

    bool PluginContextImpl::createPhysicsBody(entt::entity entity, bool rebuild)
    {
        if (!hasCapability(std::string(capability::physics))) {
            vfLogWarning("[Plugin:{}] Cannot create physics body - physics capability not available", pluginName);
            return false;
        }
        events::physics::CreatePhysicsBodyCommand cmd;
        cmd.entity = services::internal::toHandle(entity);
        cmd.rebuild = rebuild;
        return events::EventDispatcher::instance().execute(cmd);
    }

    bool PluginContextImpl::destroyPhysicsBody(entt::entity entity)
    {
        if (!hasCapability(std::string(capability::physics))) {
            return false;
        }
        events::physics::DestroyPhysicsBodyCommand cmd;
        cmd.entity = services::internal::toHandle(entity);
        return events::EventDispatcher::instance().execute(cmd);
    }

    bool PluginContextImpl::createHeightFieldBody(entt::entity entity, int32_t tileX, int32_t tileZ,
                                                  std::vector<float> heightSamples, uint32_t sampleCount,
                                                  glm::vec3 worldOrigin, float vertexSpacing,
                                                  float friction, float restitution)
    {
        if (!hasCapability(std::string(capability::physics))) {
            vfLogWarning("[Plugin:{}] Cannot create height-field body - physics capability not available", pluginName);
            return false;
        }
        events::physics::CreateHeightFieldBodyCommand cmd;
        cmd.entity = services::internal::toHandle(entity);
        cmd.tileX = tileX;
        cmd.tileZ = tileZ;
        cmd.heightSamples = std::move(heightSamples);
        cmd.sampleCount = sampleCount;
        cmd.worldOrigin = worldOrigin;
        cmd.vertexSpacing = vertexSpacing;
        cmd.friction = friction;
        cmd.restitution = restitution;
        const bool created = events::EventDispatcher::instance().execute(cmd);

        if (created) {
            managedHeightFieldBodies.push_back({entity, tileX, tileZ});
            vfLogInfo("[Plugin:{}] Created height-field body ({}x{} samples)", pluginName, sampleCount, sampleCount);
        }
        return created;
    }

    void PluginContextImpl::destroyHeightFieldBody(entt::entity entity, int32_t tileX, int32_t tileZ)
    {
        if (!hasCapability(std::string(capability::physics))) return;

        events::physics::DestroyHeightFieldBodyCommand cmd;
        cmd.entity = services::internal::toHandle(entity);
        cmd.tileX = tileX;
        cmd.tileZ = tileZ;
        events::EventDispatcher::instance().execute(cmd);

        std::erase_if(managedHeightFieldBodies, [&](const HeightFieldBodyKey& key) {
            return key.entity == entity && key.tileX == tileX && key.tileZ == tileZ;
        });
    }

    void PluginContextImpl::setLinearVelocity(entt::entity entity, glm::vec3 velocity)
    {
        if (!hasCapability(std::string(capability::physics))) {
            return;
        }
        events::physics::SetLinearVelocityCommand cmd;
        cmd.entity = services::internal::toHandle(entity);
        cmd.velocity = velocity;
        events::EventDispatcher::instance().execute(cmd);
    }

    glm::vec3 PluginContextImpl::getLinearVelocity(entt::entity entity)
    {
        if (!hasCapability(std::string(capability::physics))) {
            return glm::vec3(0.0f);
        }
        events::physics::GetLinearVelocityQuery q;
        q.entity = services::internal::toHandle(entity);
        return events::EventDispatcher::instance().query(q);
    }

    glm::vec3 PluginContextImpl::getAngularVelocity(entt::entity entity)
    {
        if (!hasCapability(std::string(capability::physics))) {
            return glm::vec3(0.0f);
        }
        events::physics::GetAngularVelocityQuery q;
        q.entity = services::internal::toHandle(entity);
        return events::EventDispatcher::instance().query(q);
    }

    bool PluginContextImpl::isGrounded(entt::entity entity)
    {
        if (!hasCapability(std::string(capability::physics))) {
            return false;
        }
        events::controller::IsGroundedQuery q;
        q.entity = services::internal::toHandle(entity);
        return events::EventDispatcher::instance().query(q);
    }

    bool PluginContextImpl::hasRigidBody(entt::entity entity)
    {
        if (!hasCapability(std::string(capability::physics))) {
            return false;
        }
        events::physics::HasRigidBodyQuery q;
        q.entity = services::internal::toHandle(entity);
        return events::EventDispatcher::instance().query(q);
    }

    glm::vec3 PluginContextImpl::getPhysicsPosition(entt::entity entity)
    {
        if (!hasCapability(std::string(capability::physics))) {
            return glm::vec3(0.0f);
        }
        events::physics::GetPhysicsPositionQuery q;
        q.entity = services::internal::toHandle(entity);
        return events::EventDispatcher::instance().query(q);
    }

    // ========================================================================
    // Terrain API
    // ========================================================================

    terrain::TerrainHeightAtResult PluginContextImpl::getTerrainHeightAt(float worldX, float worldZ)
    {
        if (!hasCapability(std::string(capability::terrain))) {
            return {};
        }
        events::terrain::GetTerrainHeightAtQuery q;
        q.worldX = worldX;
        q.worldZ = worldZ;
        return events::EventDispatcher::instance().query(q);
    }

    terrain::TerrainHitResult PluginContextImpl::getTerrainHit()
    {
        if (!hasCapability(std::string(capability::terrain))) {
            return {};
        }
        events::terrainRaycast::GetTerrainHitQuery q;
        return events::EventDispatcher::instance().query(q);
    }

    bool PluginContextImpl::hasTerrainComponent(entt::entity entity)
    {
        if (!hasCapability(std::string(capability::terrain))) {
            return false;
        }
        events::terrain::HasTerrainComponentQuery q;
        q.entity = services::internal::toHandle(entity);
        return events::EventDispatcher::instance().query(q);
    }

    // ========================================================================
    // Input API
    // ========================================================================

    bool PluginContextImpl::isKeyDown(int keyCode)
    {
        if (!hasCapability(std::string(capability::input))) {
            return false;
        }
        events::input::IsKeyDownQuery q;
        q.keyCode = keyCode;
        return events::EventDispatcher::instance().query(q);
    }

    bool PluginContextImpl::isKeyPressed(int keyCode)
    {
        if (!hasCapability(std::string(capability::input))) {
            return false;
        }
        events::input::IsKeyPressedQuery q;
        q.keyCode = keyCode;
        return events::EventDispatcher::instance().query(q);
    }

    bool PluginContextImpl::isMouseButtonDown(int button)
    {
        if (!hasCapability(std::string(capability::input))) {
            return false;
        }
        events::input::IsMouseButtonDownQuery q;
        q.button = button;
        return events::EventDispatcher::instance().query(q);
    }

    glm::vec2 PluginContextImpl::getMousePosition()
    {
        if (!hasCapability(std::string(capability::input))) {
            return glm::vec2(0.0f);
        }
        events::input::GetMousePositionQuery q;
        return events::EventDispatcher::instance().query(q);
    }

    glm::vec2 PluginContextImpl::getMouseDelta()
    {
        if (!hasCapability(std::string(capability::input))) {
            return glm::vec2(0.0f);
        }
        events::input::GetMouseDeltaQuery q;
        return events::EventDispatcher::instance().query(q);
    }

    bool PluginContextImpl::isActionDown(const std::string& actionName)
    {
        if (!hasCapability(std::string(capability::input))) {
            return false;
        }
        events::input::IsActionDownQuery q;
        q.actionName = actionName;
        return events::EventDispatcher::instance().query(q);
    }

    bool PluginContextImpl::isActionPressed(const std::string& actionName)
    {
        if (!hasCapability(std::string(capability::input))) {
            return false;
        }
        events::input::IsActionPressedQuery q;
        q.actionName = actionName;
        return events::EventDispatcher::instance().query(q);
    }

    float PluginContextImpl::getAxis1DValue(const std::string& axisName)
    {
        if (!hasCapability(std::string(capability::input))) {
            return 0.0f;
        }
        events::input::GetAxis1DValueQuery q;
        q.axisName = axisName;
        return events::EventDispatcher::instance().query(q);
    }

    glm::vec2 PluginContextImpl::getAxis2DValue(const std::string& axisName)
    {
        if (!hasCapability(std::string(capability::input))) {
            return glm::vec2(0.0f);
        }
        events::input::GetAxis2DValueQuery q;
        q.axisName = axisName;
        return events::EventDispatcher::instance().query(q);
    }

    glm::vec2 PluginContextImpl::getViewportMousePosition()
    {
        if (!hasCapability(std::string(capability::input))) {
            return glm::vec2(0.0f);
        }
        events::input::GetViewportMousePositionQuery q;
        return events::EventDispatcher::instance().query(q);
    }

    bool PluginContextImpl::screenToWorldRay(glm::vec2 screenPos, glm::vec3& outOrigin, glm::vec3& outDirection)
    {
        if (!hasCapability(std::string(capability::input))) {
            return false;
        }
        events::input::ScreenToWorldRayQuery q;
        q.screenPos = screenPos;
        auto ray = events::EventDispatcher::instance().query(q);
        if (!ray.has_value()) return false;

        outOrigin = ray->origin;
        outDirection = ray->direction;
        return true;
    }

    // ========================================================================
    // NavMesh API
    // ========================================================================

    void PluginContextImpl::setAgentDestination(entt::entity entity, glm::vec3 target)
    {
        if (!hasCapability(std::string(capability::navmesh))) {
            vfLogWarning("[Plugin:{}] Cannot set agent destination - navmesh capability not available", pluginName);
            return;
        }
        events::navmesh::SetAgentDestinationCommand cmd;
        cmd.entity = services::internal::toHandle(entity);
        cmd.target = target;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PluginContextImpl::stopAgent(entt::entity entity)
    {
        if (!hasCapability(std::string(capability::navmesh))) {
            return;
        }
        events::navmesh::StopAgentCommand cmd;
        cmd.entity = services::internal::toHandle(entity);
        events::EventDispatcher::instance().execute(cmd);
    }

    glm::vec3 PluginContextImpl::getAgentVelocity(entt::entity entity)
    {
        if (!hasCapability(std::string(capability::navmesh))) {
            return glm::vec3(0.0f);
        }
        events::navmesh::GetAgentVelocityQuery q;
        q.entity = services::internal::toHandle(entity);
        return events::EventDispatcher::instance().query(q);
    }

    float PluginContextImpl::getAgentSpeed(entt::entity entity)
    {
        if (!hasCapability(std::string(capability::navmesh))) {
            return 0.0f;
        }
        events::navmesh::GetAgentSpeedQuery q;
        q.entity = services::internal::toHandle(entity);
        return events::EventDispatcher::instance().query(q);
    }

    navigation::NavPath PluginContextImpl::findPath(glm::vec3 start, glm::vec3 end)
    {
        if (!hasCapability(std::string(capability::navmesh))) {
            return {};
        }
        events::navmesh::FindPathQuery q;
        q.start = start;
        q.end = end;
        return events::EventDispatcher::instance().query(q);
    }

    glm::vec3 PluginContextImpl::getClosestPointOnNavmesh(glm::vec3 point, float searchRadius)
    {
        if (!hasCapability(std::string(capability::navmesh))) {
            return point;
        }
        events::navmesh::GetClosestPointQuery q;
        q.point = point;
        q.searchRadius = searchRadius;
        return events::EventDispatcher::instance().query(q);
    }

    bool PluginContextImpl::isPointOnNavmesh(glm::vec3 point, float tolerance)
    {
        if (!hasCapability(std::string(capability::navmesh))) {
            return false;
        }
        events::navmesh::IsPointOnNavmeshQuery q;
        q.point = point;
        q.tolerance = tolerance;
        return events::EventDispatcher::instance().query(q);
    }

    bool PluginContextImpl::hasNavmesh()
    {
        if (!hasCapability(std::string(capability::navmesh))) {
            return false;
        }
        events::navmesh::HasNavmeshQuery q;
        return events::EventDispatcher::instance().query(q);
    }

    // ========================================================================
    // VFX API
    // ========================================================================

    services::VFXInstanceId PluginContextImpl::createVFXInstance(const services::VFXRuntimeParams& params)
    {
        if (!hasCapability(std::string(capability::vfx))) {
            vfLogWarning("[Plugin:{}] Cannot create VFX instance - vfx capability not available", pluginName);
            return 0;
        }
        services::events::vfxruntime::CreateVFXInstanceCommand cmd;
        cmd.params = params;
        auto id = events::EventDispatcher::instance().execute(cmd);
        if (id != 0) {
            managedVFXInstances.push_back(id);
        }
        return id;
    }

    void PluginContextImpl::destroyVFXInstance(services::VFXInstanceId instanceId)
    {
        if (instanceId == 0) return;
        services::events::vfxruntime::DestroyVFXInstanceCommand cmd;
        cmd.instanceId = instanceId;
        events::EventDispatcher::instance().execute(cmd);
        std::erase(managedVFXInstances, instanceId);
    }

    void PluginContextImpl::setVFXInstanceTransform(services::VFXInstanceId instanceId, const glm::mat4& worldTransform)
    {
        if (instanceId == 0) return;
        services::events::vfxruntime::SetVFXInstanceTransformCommand cmd;
        cmd.instanceId = instanceId;
        cmd.worldTransform = worldTransform;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PluginContextImpl::playVFXInstance(services::VFXInstanceId instanceId)
    {
        if (instanceId == 0) return;
        services::events::vfxruntime::PlayVFXInstanceCommand cmd;
        cmd.instanceId = instanceId;
        events::EventDispatcher::instance().execute(cmd);
    }

    void PluginContextImpl::stopVFXInstance(services::VFXInstanceId instanceId)
    {
        if (instanceId == 0) return;
        services::events::vfxruntime::StopVFXInstanceCommand cmd;
        cmd.instanceId = instanceId;
        events::EventDispatcher::instance().execute(cmd);
    }

    bool PluginContextImpl::isVFXInstancePlaying(services::VFXInstanceId instanceId)
    {
        if (instanceId == 0) return false;
        services::events::vfxruntime::IsVFXInstancePlayingQuery q;
        q.instanceId = instanceId;
        return events::EventDispatcher::instance().query(q);
    }

    // ========================================================================
    // Meta Component Registration
    // ========================================================================

    void PluginContextImpl::registerComponentBridge(MetaComponentBridge bridge)
    {
        bridge.pluginName = pluginName;
        vfLogInfo("[Plugin:{}] Registered component bridge: {}", pluginName, bridge.name.empty() ? "unnamed" : bridge.name);
        allBridges.push_back(std::move(bridge));
    }

    void PluginContextImpl::cleanupAll()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Stop all managed audio handles
        for (const auto& handle : managedAudioHandles) {
            if (handle.isValid()) {
                events::audio::StopSoundCommand cmd;
                cmd.handle = handle;
                try { dispatcher.execute(cmd); } catch (...) {}
            }
        }
        managedAudioHandles.clear();

        // Destroy all managed VFX instances
        for (auto id : managedVFXInstances) {
            if (id != 0) {
                services::events::vfxruntime::DestroyVFXInstanceCommand cmd;
                cmd.instanceId = id;
                try { dispatcher.execute(cmd); } catch (...) {}
            }
        }
        managedVFXInstances.clear();

        // Remove component bridges registered by this plugin
        std::erase_if(allBridges, [this](const MetaComponentBridge& b) {
            return b.pluginName == pluginName;
        });

        for (const auto& token : managedSubscriptions) {
            if (token.isValid()) {
                dispatcher.unsubscribe(token);
            }
        }
        managedSubscriptions.clear();

        for (const auto& handle : registeredRenderHooks) {
            events::renderhook::UnregisterRenderPassHookCommand cmd;
            cmd.handle = handle;
            try {
                dispatcher.execute(cmd);
            } catch (...) {
            }
        }
        registeredRenderHooks.clear();

        // Remove script natives before the plugin DLL unloads — the registered
        // NativeDelegate's function pointer lives in the plugin's code segment.
        for (const auto& name : registeredScriptFunctions) {
            events::scripting::UnregisterNativeScriptFunctionCommand cmd;
            cmd.functionName = name;
            try {
                dispatcher.execute(cmd);
            } catch (...) {
            }
        }
        registeredScriptFunctions.clear();

        for (const auto& handle : managedCustomPipelines) {
            events::custompipeline::DestroyCustomPipelineCommand cmd;
            cmd.handle = handle;
            try {
                dispatcher.execute(cmd);
            } catch (...) {
            }
        }
        managedCustomPipelines.clear();

        for (const auto& handle : managedCustomMeshes) {
            events::custompipeline::DestroyCustomMeshCommand cmd;
            cmd.handle = handle;
            try {
                dispatcher.execute(cmd);
            } catch (...) {
            }
        }
        managedCustomMeshes.clear();

        // Unbind the world mask if this plugin owns it, then destroy its textures
        if (boundWorldMaskTexture.isValid()) {
            events::plugintexture::UnbindWorldMaskCommand cmd;
            try { dispatcher.execute(cmd); } catch (...) {}
            boundWorldMaskTexture = {};
        }
        for (const auto& handle : managedTextures) {
            events::plugintexture::DestroyTexture2DCommand cmd;
            cmd.handle = handle;
            try {
                dispatcher.execute(cmd);
            } catch (...) {
            }
        }
        managedTextures.clear();

        for (const auto& key : managedHeightFieldBodies) {
            events::physics::DestroyHeightFieldBodyCommand cmd;
            cmd.entity = services::internal::toHandle(key.entity);
            cmd.tileX = key.tileX;
            cmd.tileZ = key.tileZ;
            try {
                dispatcher.execute(cmd);
            } catch (...) {
            }
        }
        managedHeightFieldBodies.clear();

        for (const auto& window : registeredWindows) {
            controllers::imguiHandler::ImguiWindowHandler::remove(window);
        }
        controllers::imguiHandler::PluginWindowRegistry::removeByPlugin(pluginName);
        registeredWindows.clear();

        for (const auto& token : pluginEventSubscriptions) {
            PluginEventBus::instance().unsubscribe(token);
        }
        pluginEventSubscriptions.clear();

    }

}
