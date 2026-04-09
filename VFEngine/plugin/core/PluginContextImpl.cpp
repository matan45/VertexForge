#include "print/Log.hpp"
#include "PluginContextImpl.hpp"
#include "PluginComponentRegistry.hpp"
#include "ComponentBuilderImpl.hpp"
#include "PluginEventBus.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "events/render/RenderHookEvents.hpp"
#include "events/audio/AudioEvents.hpp"
#include "events/audio/AudioBusEvents.hpp"
#include "events/physics/PhysicsEvents.hpp"
#include "events/physics/ControllerEvents.hpp"
#include "events/terrain/TerrainEvents.hpp"
#include "events/terrain/TerrainRaycastEvents.hpp"
#include "events/input/InputEvents.hpp"
#include "events/input/ActionMappingEvents.hpp"
#include "events/navmesh/NavmeshEvents.hpp"
#include "events/vfx/VFXRuntimeEvents.hpp"
#include "data/EntityConversion.hpp"
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/PluginComponents.hpp"
#include "Pipeline.hpp"
#include <imgui.h>
#include <filesystem>
#include <algorithm>

namespace plugin {

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

    void PluginContextImpl::registerEditorWindow(std::shared_ptr<controllers::imguiHandler::ImguiWindow> window)
    {
        if (!hasCapability(std::string(capability::editor))) {
            vfLogWarning("[Plugin:{}] Cannot register editor window - editor capability not available", pluginName);
            return;
        }

        controllers::imguiHandler::ImguiWindowHandler::add(window);
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

    ComponentBuilder& PluginContextImpl::registerComponent(const std::string& componentName)
    {
        activeBuilder = std::make_unique<ComponentBuilderImpl>(this, componentName);
        return *activeBuilder;
    }

    void PluginContextImpl::finalizeComponentRegistration(ComponentBuilderImpl& builder)
    {
        std::string qualifiedName = pluginName + "::" + builder.getComponentName();

        PluginComponentInfo info;
        info.pluginName = pluginName;
        info.componentName = builder.getComponentName();
        info.qualifiedName = qualifiedName;
        info.properties = builder.getProperties();
        info.inspector = builder.getInspector();

        // Build default data from property descriptors
        info.defaultData = nlohmann::json::object();
        for (const auto& prop : info.properties)
        {
            info.defaultData[prop.name] = prop.defaultValue;
        }

        PluginComponentRegistry::instance().registerComponent(info);
        registeredComponentNames.push_back(qualifiedName);

        vfLogInfo("[Plugin:{}] Registered component: {} ({} properties)",
                  pluginName, builder.getComponentName(), info.properties.size());
    }

    bool PluginContextImpl::addPluginComponent(entt::entity entity, const std::string& componentName)
    {
        std::string qualifiedName = pluginName + "::" + componentName;
        auto& reg = scene::EntityRegistry::getRegistry();

        if (!reg.valid(entity))
            return false;

        const auto* info = PluginComponentRegistry::instance().findComponent(qualifiedName);
        if (!info)
        {
            vfLogWarning("[Plugin:{}] Component '{}' not registered", pluginName, componentName);
            return false;
        }

        auto& pluginComp = reg.get_or_emplace<components::PluginComponentsComponent>(entity);
        if (pluginComp.components.contains(qualifiedName))
            return false;

        pluginComp.components[qualifiedName] = info->defaultData;
        return true;
    }

    bool PluginContextImpl::removePluginComponent(entt::entity entity, const std::string& componentName)
    {
        std::string qualifiedName = pluginName + "::" + componentName;
        auto& reg = scene::EntityRegistry::getRegistry();

        if (!reg.valid(entity) || !reg.all_of<components::PluginComponentsComponent>(entity))
            return false;

        auto& pluginComp = reg.get<components::PluginComponentsComponent>(entity);
        bool erased = pluginComp.components.erase(qualifiedName) > 0;

        if (pluginComp.components.empty())
            reg.remove<components::PluginComponentsComponent>(entity);

        componentDataWrappers.erase(qualifiedName);
        return erased;
    }

    PluginComponentData* PluginContextImpl::getPluginComponent(entt::entity entity, const std::string& componentName)
    {
        std::string qualifiedName = pluginName + "::" + componentName;
        auto& reg = scene::EntityRegistry::getRegistry();

        if (!reg.valid(entity) || !reg.all_of<components::PluginComponentsComponent>(entity))
            return nullptr;

        auto& pluginComp = reg.get<components::PluginComponentsComponent>(entity);
        auto it = pluginComp.components.find(qualifiedName);
        if (it == pluginComp.components.end())
            return nullptr;

        std::string key = std::to_string(static_cast<uint32_t>(entity)) + ":" + qualifiedName;
        componentDataWrappers.insert_or_assign(key, PluginComponentData(&it->second));
        return &componentDataWrappers.at(key);
    }

    bool PluginContextImpl::hasPluginComponent(entt::entity entity, const std::string& componentName)
    {
        std::string qualifiedName = pluginName + "::" + componentName;
        auto& reg = scene::EntityRegistry::getRegistry();

        if (!reg.valid(entity) || !reg.all_of<components::PluginComponentsComponent>(entity))
            return false;

        auto& pluginComp = reg.get<components::PluginComponentsComponent>(entity);
        return pluginComp.components.contains(qualifiedName);
    }

    void PluginContextImpl::forEachWithComponent(const std::string& componentName,
                                                  const std::function<void(entt::entity, PluginComponentData&)>& callback)
    {
        std::string qualifiedName = pluginName + "::" + componentName;
        auto& reg = scene::EntityRegistry::getRegistry();
        auto view = reg.view<components::PluginComponentsComponent>();

        for (auto entity : view)
        {
            auto& pluginComp = view.get<components::PluginComponentsComponent>(entity);
            auto it = pluginComp.components.find(qualifiedName);
            if (it != pluginComp.components.end())
            {
                PluginComponentData data(&it->second);
                callback(entity, data);
            }
        }
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

    void PluginContextImpl::registerScriptFunction(const std::string& name, std::any function)
    {
        if (!hasCapability(std::string(capability::scripting))) {
            vfLogWarning("[Plugin:{}] Cannot register script function '{}' - scripting capability not available", pluginName, name);
            return;
        }

        events::scripting::RegisterNativeScriptFunctionCommand cmd;
        cmd.functionName = name;
        cmd.function = std::move(function);
        events::EventDispatcher::instance().execute(cmd);

        vfLogInfo("[Plugin:{}] Registered native script function: {}", pluginName, name);
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
        cmd.callback = std::move(callback);
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

        auto path = std::filesystem::current_path() / "plugins" / "data" / safeName;
        return path.string();
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

        for (const auto& window : registeredWindows) {
            controllers::imguiHandler::ImguiWindowHandler::remove(window);
        }
        registeredWindows.clear();

        for (const auto& token : pluginEventSubscriptions) {
            PluginEventBus::instance().unsubscribe(token);
        }
        pluginEventSubscriptions.clear();

        // Clean up plugin components from all entities
        if (!registeredComponentNames.empty())
        {
            auto& reg = scene::EntityRegistry::getRegistry();
            auto view = reg.view<components::PluginComponentsComponent>();

            // Collect entities to remove component from (can't modify during iteration)
            std::vector<entt::entity> toRemove;

            for (auto entity : view)
            {
                auto& pluginComp = view.get<components::PluginComponentsComponent>(entity);
                for (const auto& name : registeredComponentNames)
                {
                    pluginComp.components.erase(name);
                }
                if (pluginComp.components.empty())
                {
                    toRemove.push_back(entity);
                }
            }

            for (auto entity : toRemove)
            {
                reg.remove<components::PluginComponentsComponent>(entity);
            }

            PluginComponentRegistry::instance().unregisterPlugin(pluginName);
            registeredComponentNames.clear();
        }

        componentDataWrappers.clear();
    }

}
