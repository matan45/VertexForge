#include "print/Log.hpp"
#include "crash/CrashHandler.hpp"
#include "ScriptingServiceImpl.hpp"
#include "../../events/scripting/ScriptingEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include "../../events/input/ActionMappingEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include <asset/AssetRef.hpp>
#include <cassert>
#include <filesystem>
#include <algorithm>

namespace services
{
    using internal::fromHandle;
    using internal::toHandle;

    ScriptingServiceImpl::ScriptingServiceImpl(IScriptingProvider* scriptingProvider,
                                               std::shared_ptr<scene::SceneGraphSystem> sceneGraph)
        : scriptingProvider(scriptingProvider)
          , sceneGraph(sceneGraph)
    {
        assert(scriptingProvider && "ScriptingProvider must not be null");
        assert(sceneGraph && "SceneGraphSystem must not be null");

        // Catch every mutation path that adds/removes ScriptComponents
        // (scene load/clear, entity destruction, runtime spawns) so the cached
        // update list rebuilds on demand
        auto& registry = scene::EntityRegistry::getRegistry();
        registry.on_construct<components::ScriptComponent>().connect<&ScriptingServiceImpl::onScriptComponentChanged>(this);
        registry.on_destroy<components::ScriptComponent>().connect<&ScriptingServiceImpl::onScriptComponentChanged>(this);
    }

    ScriptingServiceImpl::~ScriptingServiceImpl()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        registry.on_construct<components::ScriptComponent>().disconnect<&ScriptingServiceImpl::onScriptComponentChanged>(this);
        registry.on_destroy<components::ScriptComponent>().disconnect<&ScriptingServiceImpl::onScriptComponentChanged>(this);
    }

    void ScriptingServiceImpl::onScriptComponentChanged(entt::registry&, entt::entity)
    {
        scriptListDirty = true;
    }

    void ScriptingServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // === Commands ===
        dispatcher.registerCommandHandler<events::scripting::AttachScriptCommand>(
            [this](const auto& cmd)
            {
                return attachScript(cmd.entity, cmd.data);
            });

        dispatcher.registerCommandHandler<events::scripting::DetachScriptCommand>(
            [this](const auto& cmd)
            {
                detachScript(cmd.entity, cmd.scriptPath);
                return true;
            });

        dispatcher.registerCommandHandler<events::scripting::SetScriptEnabledCommand>(
            [this](const auto& cmd)
            {
                setScriptEnabled(cmd.entity, cmd.scriptPath, cmd.enabled);
            });

        // === Queries ===
        dispatcher.registerQueryHandler<events::scripting::IsScriptEnabledQuery>(
            [this](const auto& query)
            {
                return isScriptEnabled(query.entity, query.scriptPath);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptPathsQuery>(
            [this](const auto& query)
            {
                return getScriptPaths(query.entity);
            });

        dispatcher.registerQueryHandler<events::scripting::GetScriptDataQuery>(
            [this](const auto& query) -> std::optional<ScriptData>
            {
                auto enttEntity = internal::fromHandle(query.entity);
                auto& registry = scene::EntityRegistry::getRegistry();
                if (!registry.all_of<components::ScriptComponent>(enttEntity))
                {
                    return std::nullopt;
                }
                // Return first script's data, or empty data if no scripts
                const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
                ScriptData data;
                if (!scriptComp.scripts.empty())
                {
                    data.scriptPath = scriptComp.scripts[0].scriptRef.resolve();
                    data.enabled = scriptComp.scripts[0].enabled;
                }
                return data;
            });

        // === Build Commands ===
        dispatcher.registerCommandHandler<events::scripting::BuildScriptsCommand>(
            [this](const auto& cmd)
            {
                auto result = buildScripts();
                return result.success;
            });

        dispatcher.registerCommandHandler<events::scripting::CleanScriptsCommand>(
            [this](const auto& cmd)
            {
                cleanScripts();
                return true;
            });

        dispatcher.registerQueryHandler<events::scripting::IsScriptsCompiledQuery>(
            [this](const auto& query)
            {
                return isCompiled();
            });

        // === Debugger (VK-1371) ===
        dispatcher.registerCommandHandler<events::scripting::StartScriptDebuggerCommand>(
            [this](const events::scripting::StartScriptDebuggerCommand& cmd)
            {
                if (scriptingProvider)
                {
                    scriptingProvider->startDebugServer(cmd.port);
                }
            });

        dispatcher.registerCommandHandler<events::scripting::StopScriptDebuggerCommand>(
            [this](const events::scripting::StopScriptDebuggerCommand&)
            {
                if (scriptingProvider)
                {
                    scriptingProvider->stopDebugServer();
                }
            });

        dispatcher.registerQueryHandler<events::scripting::IsScriptDebuggerActiveQuery>(
            [this](const events::scripting::IsScriptDebuggerActiveQuery&)
            {
                return scriptingProvider && scriptingProvider->isDebuggerActive();
            });

        // === Plugin Native Function Registration ===
        dispatcher.registerCommandHandler<events::scripting::RegisterNativeScriptFunctionCommand>(
            [this](const events::scripting::RegisterNativeScriptFunctionCommand& cmd)
            {
                if (scriptingProvider)
                {
                    scriptingProvider->registerPluginNativeFunction(cmd.functionName, cmd.function);
                }
            });

        dispatcher.registerCommandHandler<events::scripting::UnregisterNativeScriptFunctionCommand>(
            [this](const events::scripting::UnregisterNativeScriptFunctionCommand& cmd)
            {
                if (scriptingProvider)
                {
                    scriptingProvider->unregisterPluginNativeFunction(cmd.functionName);
                }
            });

        // === Instance Priority ===
        dispatcher.registerCommandHandler<events::scripting::SetInstancePriorityCommand>(
            [this](const events::scripting::SetInstancePriorityCommand& cmd)
            {
                if (scriptingProvider)
                {
                    scriptingProvider->setInstancePriority(cmd.instanceId, cmd.priority);
                }
                scriptListDirty = true;  // priority drives the cached update order
            });

        // === Mode Change Subscription ===
        dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& notification)
            {
                // Stop all scripts when exiting play mode
                if (notification.previousMode == EditorMode::Play &&
                    notification.currentMode == EditorMode::Edit)
                {
                    stopAllScripts();
                }
            });
    }

    // === Build Methods ===
    std::string ScriptingServiceImpl::getManifestPath() const
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});

        if (projectOpt.has_value())
        {
            std::filesystem::path workingDir(projectOpt->workingDirectory);
            return (workingDir / "scripts" / "scripts.mtproj").string();
        }

        vfLogWarning("[Script] No project loaded, cannot determine manifest path");
        return "";
    }

    ScriptBuildResult ScriptingServiceImpl::buildScripts()
    {
        vfLogInfo("[Script] Building scripts...");

        std::string manifestPath = getManifestPath();
        if (manifestPath.empty())
        {
            ScriptBuildResult result;
            result.success = false;
            result.errors.push_back("No project loaded");
            return result;
        }

        auto result = scriptingProvider->buildScripts(manifestPath);

        // Load the compiled scripts if build was successful
        if (result.success)
        {
            scriptingProvider->loadCompiledScripts(manifestPath);
        }

        return result;
    }

    void ScriptingServiceImpl::cleanScripts()
    {
        vfLogInfo("[Script] Cleaning scripts...");
        std::string manifestPath = getManifestPath();
        if (!manifestPath.empty())
        {
            scriptingProvider->cleanScripts(manifestPath);
        }
    }

    bool ScriptingServiceImpl::isCompiled() const
    {
        return scriptingProvider->isCompiled();
    }

    bool ScriptingServiceImpl::attachScript(EntityHandle entity, const ScriptData& data)
    {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        // Add ScriptComponent if not present
        if (!registry.all_of<components::ScriptComponent>(enttEntity))
        {
            registry.emplace<components::ScriptComponent>(enttEntity);
        }

        // If no script path provided, just ensure component exists (for "Add Component" UI flow)
        if (data.scriptPath.empty())
        {
            vfLogInfo("[Script] Added empty ScriptComponent to entity");
            return true;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);

        // Check if script already attached
        if (scriptComp.hasScriptPath(data.scriptPath))
        {
            vfLogWarning("[Script] Script '{}' already attached to entity", data.scriptPath);
            return false;
        }

        auto scriptRef = asset::AssetRef::fromPath(data.scriptPath);

        // Just store the script ref - actual loading happens when Play is pressed
        components::ScriptEntry entry;
        entry.scriptRef = scriptRef;
        entry.scriptPath = data.scriptPath;
        entry.enabled = data.enabled;
        // Carry the full authored set: this is the only path an entity duplicate has to
        // reconstruct its scripts (ComponentClone excludes ScriptComponent), so anything not
        // copied here is silently lost on duplicate.
        entry.inputPriority = data.inputPriority;
        entry.updateInterval = data.updateInterval;
        entry.tickSignificance = data.tickSignificance;
        entry.pinFullRate = data.pinFullRate;

        scriptComp.scripts.push_back(entry);
        scriptListDirty = true;

        vfLogInfo("[Script] Attached script '{}' to entity (total scripts: {})",
                  data.scriptPath, scriptComp.scripts.size());
        return true;
    }

    void ScriptingServiceImpl::detachScript(EntityHandle entity, const std::string& scriptPath)
    {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity))
        {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        auto* entry = scriptComp.findByPath(scriptPath);

        if (!entry)
        {
            return;
        }

        // Call onDestroy if script was started
        if (entry->started)
        {
            scriptingProvider->callOnDestroy(entry->instanceId);
        }

        // Unload from provider
        scriptingProvider->unloadScript(entry->instanceId);

        // Remove from component (shifts the remaining entries' indices)
        scriptComp.removeByPath(scriptPath);
        scriptListDirty = true;

        // Remove component entirely if no scripts left
        if (scriptComp.scripts.empty())
        {
            registry.remove<components::ScriptComponent>(enttEntity);
        }

        vfLogInfo("[Script] Detached script '{}' from entity", scriptPath);
    }

    std::vector<std::string> ScriptingServiceImpl::getScriptPaths(EntityHandle entity) const
    {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<std::string> paths;

        if (!registry.all_of<components::ScriptComponent>(enttEntity))
        {
            return paths;
        }

        const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        for (const auto& entry : scriptComp.scripts)
        {
            if (!entry.scriptPath.empty())
            {
                paths.push_back(entry.scriptPath);
            }
            else
            {
                paths.push_back(entry.scriptRef.resolve());
            }
        }
        return paths;
    }

    void ScriptingServiceImpl::setScriptEnabled(EntityHandle entity, const std::string& scriptPath, bool enabled)
    {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity))
        {
            return;
        }

        auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        auto* entry = scriptComp.findByPath(scriptPath);
        if (entry)
        {
            bool wasEnabled = entry->enabled;
            entry->enabled = enabled;

            if (entry->started && entry->playbackState == ScriptPlaybackState::Playing)
            {
                if (!wasEnabled && enabled)
                {
                    scriptingProvider->callOnEnable(entry->instanceId);
                }
                else if (wasEnabled && !enabled)
                {
                    scriptingProvider->callOnDisable(entry->instanceId);
                }
            }
        }
    }

    bool ScriptingServiceImpl::isScriptEnabled(EntityHandle entity, const std::string& scriptPath) const
    {
        auto enttEntity = fromHandle(entity);
        auto& registry = scene::EntityRegistry::getRegistry();

        if (!registry.all_of<components::ScriptComponent>(enttEntity))
        {
            return false;
        }

        const auto& scriptComp = registry.get<components::ScriptComponent>(enttEntity);
        const auto* entry = scriptComp.findByPath(scriptPath);
        return entry ? entry->enabled : false;
    }

    void ScriptingServiceImpl::rebuildScriptUpdateList(entt::registry& registry)
    {
        cachedUpdateList.clear();

        auto view = registry.view<components::ScriptComponent>();
        for (auto entity : view)
        {
            auto& scriptComp = view.get<components::ScriptComponent>(entity);
            for (size_t i = 0; i < scriptComp.scripts.size(); ++i)
            {
                cachedUpdateList.push_back({entity, i, scriptComp.scripts[i].inputPriority});
            }
        }

        // Sort by priority descending (higher priority scripts execute first)
        std::stable_sort(cachedUpdateList.begin(), cachedUpdateList.end(),
            [](const ScriptUpdateEntry& a, const ScriptUpdateEntry& b)
            {
                return a.priority > b.priority;
            });

        scriptListDirty = false;
    }

    bool ScriptingServiceImpl::queryCameraPosition(glm::vec3& outPos) const
    {
        // VK-1536. updateScripts only runs in play mode (EditorFrameTaskGraph gates on
        // isPlayMode() && isGameTimeActive(); Runtime is always play), so the primary camera is
        // always a real ECS entity here. That is why this can use the two-hop query directly and
        // does not need WorldSectorStreamingOps' isPlayMode ? primary : cachedViewportCamera
        // hybrid — the edit-mode staleness that WeatherServiceImpl::queryCameraPosition works
        // around cannot arise on this path.
        try
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            auto camOpt = dispatcher.query(events::scene::GetPrimaryCameraQuery{});
            if (!camOpt.has_value())
                return false;

            events::scene::GetWorldTransformQuery tq;
            tq.entity = camOpt.value();
            auto xformOpt = dispatcher.query(tq);
            if (!xformOpt.has_value())
                return false;

            outPos = xformOpt->position;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void ScriptingServiceImpl::updateScripts(float deltaTime)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Clear per-frame action consumption (guard against handler being unregistered during shutdown)
        try { dispatcher.execute(events::input::ClearConsumedActionsCommand{}); }
        catch (...) {}

        auto& registry = scene::EntityRegistry::getRegistry();

        if (scriptListDirty)
        {
            rebuildScriptUpdateList(registry);
        }

        // VK-1536 — resolve the camera ONCE per tick, not per script: the two-hop query is far more
        // expensive than the distance test it feeds. Only needed when distance scaling is on.
        glm::vec3 cameraPos{0.0f};
        const bool haveCamera = scriptLOD.enabled && queryCameraPosition(cameraPos);

        for (const auto& [entity, scriptIndex, priority] : cachedUpdateList)
        {
            // Entities/scripts can be destroyed mid-frame by other scripts —
            // validate the cached entry and queue a rebuild if it went stale
            if (!registry.valid(entity) || !registry.all_of<components::ScriptComponent>(entity))
            {
                scriptListDirty = true;
                continue;
            }

            // VK-1536: assign() into a reused member rather than a fresh local — this runs for every
            // script every frame, and the copy is deliberate (see the catch below).
            entityNameScratch.clear();
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive) continue;
                entityNameScratch.assign(nameComp.name);
            }

            auto& scriptComp = registry.get<components::ScriptComponent>(entity);
            if (scriptIndex >= scriptComp.scripts.size())
            {
                scriptListDirty = true;
                continue;
            }

            auto& entry = scriptComp.scripts[scriptIndex];
            if (!entry.enabled) continue;

            // Load script if needed
            if (entry.instanceId == 0 && (!entry.scriptPath.empty() || entry.scriptRef.isValid()))
            {
                // Prefer resolved asset ref (absolute path) over stored scriptPath (may be relative)
                std::string path;
                if (entry.scriptRef.isValid())
                {
                    std::string resolved = entry.scriptRef.resolve();
                    if (!resolved.empty())
                        path = resolved;
                }
                if (path.empty())
                    path = entry.scriptPath;

                // Resolve relative paths against project working directory
                if (!path.empty() && !std::filesystem::path(path).is_absolute())
                {
                    auto& disp = ::events::EventDispatcher::instance();
                    auto projectOpt = disp.query(events::project::GetCurrentProjectQuery{});
                    if (projectOpt.has_value())
                    {
                        auto absPath = std::filesystem::path(projectOpt->workingDirectory) / path;
                        if (std::filesystem::exists(absPath))
                            path = absPath.string();
                    }
                }

                auto info = scriptingProvider->loadScript(path, toHandle(entity));
                if (info.has_value())
                {
                    entry.instanceId = info->instanceId;
                    entry.playbackState = ScriptPlaybackState::Playing;
                    scriptingProvider->setInstancePriority(entry.instanceId, entry.inputPriority);
                    scriptingProvider->playVFX(entry.instanceId);
                }
                else
                {
                    continue;
                }
            }

            if (!entry.started && entry.playbackState == ScriptPlaybackState::Stopped)
            {
                entry.playbackState = ScriptPlaybackState::Playing;
                scriptingProvider->playVFX(entry.instanceId);
            }

            if (entry.playbackState == ScriptPlaybackState::Playing && !entry.started)
            {
                entry.started = true;
                if (entry.enabled)
                {
                    scriptingProvider->callOnEnable(entry.instanceId);
                }
            }

            // VK-1536 tick governor. Placed AFTER the load/start blocks above on purpose: a script
            // must still load and receive onStart/onEnable at full rate, so throttling only ever
            // affects how often onUpdate runs, never whether the script comes alive.
            //
            // Distance scaling multiplies the AUTHORED interval, and is only reached when the script
            // already opted in (updateInterval > 0). Gating on the authored value first is what
            // keeps this off by default: a plain multiply would be a no-op anyway (0 * n == 0), but
            // more importantly distance must never silently throttle a script whose author never
            // asked for it.
            float effInterval = entry.updateInterval;
            if (effInterval > 0.0f && haveCamera && !entry.pinFullRate &&
                registry.all_of<components::WorldTransformComponent>(entity))
            {
                // WorldTransformComponent, never TransformComponent: the latter is parent-local, so
                // a child of a moving root would score against the wrong position.
                const auto& wt = registry.get<components::WorldTransformComponent>(entity);
                const glm::vec3 diff = glm::vec3(wt.worldMatrix[3]) - cameraPos;
                const float distSq = glm::dot(diff, diff);
                effInterval *= scripting::distanceMultiplier(
                    scriptLOD, scripting::effectiveDistanceSq(distSq, entry.tickSignificance));
            }
            // An entity with no WorldTransformComponent (a manager/singleton script) has no position
            // to score, so it keeps its authored interval and is never distance-throttled.

            scripting::ScriptTickInputs tickIn;
            tickIn.accumulator = entry.tickAccumulator;
            tickIn.dt = deltaTime;
            tickIn.interval = effInterval;
            tickIn.firstTickDone = entry.tickFirstDone;
            tickIn.instanceId = entry.instanceId;

            const scripting::ScriptTickDecision tick = scripting::evaluateScriptTick(tickIn);
            entry.tickAccumulator = tick.accumulator;
            entry.tickFirstDone = tick.firstTickDone;
            if (!tick.shouldTick) continue;

            // Breadcrumb so that an *uncatchable* native fault (e.g. an mType JIT
            // access violation) inside this script is still attributed by name in
            // the crash report written by util::installCrashHandler().
            // VK-1536: built into a reused member instead of concatenating fresh temporaries —
            // setCrashLogContext only memcpys into a fixed buffer, so the allocations this used to
            // do were the entire cost of a diagnostic that is read only if the process faults.
            crashContextScratch.clear();
            crashContextScratch += "script onUpdate: ";
            crashContextScratch += entry.scriptPath;
            crashContextScratch += " @ '";
            crashContextScratch += entityNameScratch;
            crashContextScratch += "'";
            util::setCrashLogContext(crashContextScratch);
            try
            {
                scriptingProvider->callOnUpdate(entry.instanceId, tick.tickDt);
            }
            catch (const std::exception& e)
            {
                // Catches only mType *interpreter* errors (surfaced as C++
                // exceptions). mType *JIT* faults are SEH access violations that
                // bypass this catch entirely — those are captured by the crash
                // handler, or avoided by running with JIT off (the editor "Debug"
                // script button forces the VM into interpreter mode).
                // entityNameScratch is a copy, not a view: the script may have destroyed its own
                // entity (and its NameComponent) inside callOnUpdate above.
                vfLogScriptError("[Script] '{}' on '{}' threw in onUpdate: {}",
                                 entry.scriptPath, entityNameScratch, e.what());
                continue;
            }
        }

        // Re-point the breadcrumb away from the last script: a native fault in
        // tickCoroutines (a likely JIT-fault site) or in later frame work must not
        // be misattributed in the crash report to the last onUpdate that ran.
        util::setCrashLogContext("script coroutines tick");
        scriptingProvider->tickCoroutines(deltaTime);
        util::setCrashLogContext("");
    }

    void ScriptingServiceImpl::fixedUpdateScripts(float fixedDeltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::ScriptComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive) continue;
            }

            auto& scriptComp = view.get<components::ScriptComponent>(entity);
            for (auto& entry : scriptComp.scripts)
            {
                if (!entry.enabled || !entry.started || entry.instanceId == 0) continue;
                scriptingProvider->callOnFixedUpdate(entry.instanceId, fixedDeltaTime);
            }
        }

        scriptingProvider->tickFixedUpdateCoroutines();
    }

    void ScriptingServiceImpl::lateUpdateScripts(float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::ScriptComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive) continue;
            }

            auto& scriptComp = view.get<components::ScriptComponent>(entity);
            for (auto& entry : scriptComp.scripts)
            {
                if (!entry.enabled || !entry.started || entry.instanceId == 0) continue;
                scriptingProvider->callOnLateUpdate(entry.instanceId, deltaTime);
            }
        }
    }

    void ScriptingServiceImpl::stopAllScripts()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::ScriptComponent>();

        for (auto entity : view)
        {
            auto& scriptComp = view.get<components::ScriptComponent>(entity);
            for (auto& entry : scriptComp.scripts)
            {
                // Call onDisable then onDestroy for started scripts
                if (entry.started)
                {
                    if (entry.enabled)
                    {
                        scriptingProvider->callOnDisable(entry.instanceId);
                    }
                    scriptingProvider->callOnDestroy(entry.instanceId);
                }
                // Reset script entry state
                entry.instanceId = 0;
                entry.started = false;
                entry.playbackState = ScriptPlaybackState::Stopped;
            }
        }

        scriptingProvider->unloadAllScripts();
        scriptListDirty = true;
        vfLogInfo("[Script] All scripts stopped");
    }
}
