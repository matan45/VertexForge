#include "EditorHandler.hpp"
#include "editor/EditorBootstrap.hpp"
#include "impl/physics/PhysicsPlayModeHandler.hpp"
#include "impl/vfx/VFXPlayModeHandler.hpp"
#include "impl/vfx/VFXSequencePlayModeHandler.hpp"
#include "impl/ai/BehaviorTreePlayModeHandler.hpp"
#include "impl/threading/FrameTaskGraph.hpp"
#include "core/PluginManager.hpp"
#include "../../core/audio/AudioSceneUpdater.hpp"
#include "time/Timer.hpp"
#include "print/Log.hpp"
#include "threading/EditorTaskStats.hpp"
#include "events/EventDispatcher.hpp"
#include "events/weather/WeatherEvents.hpp"
#include "events/terrain/OceanEvents.hpp"

#include <chrono>

namespace handlers
{
    void EditorHandler::buildFrameTaskGraph()
    {
        frameTaskGraph = std::make_unique<services::FrameTaskGraph>();

        frameTaskGraph->addTask("Scene", [this]() {
            if (sceneService) sceneService->update();
        });

        frameTaskGraph->addTask("Input", [this]() {
            if (inputService) inputService->update();
        });

        frameTaskGraph->addTask("WindowState", [this]() {
            if (windowStateService) windowStateService->update();
        });

        // Gameplay tasks gate on isGameTimeActive(): false when paused without a
        // pending step or when time-scale is 0, so a single predicate unifies
        // pause + step + scale==0. They also use getGameDeltaTime() so slow-mo /
        // fast-forward scales gameplay without touching editor camera/UI/render pacing.
        frameTaskGraph->addTask("PhysicsKick", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && engineTime::Timer::isGameTimeActive() && physicsPlayModeHandler) {
                float dt = static_cast<float>(engineTime::Timer::getGameDeltaTime());
                physicsPlayModeHandler->kickUpdate(dt);
            }
        });

        frameTaskGraph->addTask("PhysicsSync", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && engineTime::Timer::isGameTimeActive() && physicsPlayModeHandler) {
                float dt = static_cast<float>(engineTime::Timer::getGameDeltaTime());
                physicsPlayModeHandler->syncUpdate(dt);
            }
        });

        frameTaskGraph->addTask("Destruction", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && engineTime::Timer::isGameTimeActive() && destructionService) {
                float dt = static_cast<float>(engineTime::Timer::getGameDeltaTime());
                destructionService->update(dt);
            }
        });

        frameTaskGraph->addTask("Scripts", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && engineTime::Timer::isGameTimeActive() && scriptingService) {
                float dt = static_cast<float>(engineTime::Timer::getGameDeltaTime());
                // Safety net: a catchable mType (interpreter) error in any script
                // is logged and play mode continues, instead of killing the editor.
                // Native JIT faults still go to the crash handler (see CrashHandler).
                try { scriptingService->updateScripts(dt); }
                catch (const std::exception& e) { vfLogError("[Script] updateScripts threw: {}", e.what()); }
            }
        });

        frameTaskGraph->addTask("Controllers", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && engineTime::Timer::isGameTimeActive() && controllerService) {
                float dt = static_cast<float>(engineTime::Timer::getGameDeltaTime());
                controllerService->applyControllerMovement(dt);
            }
        });

        frameTaskGraph->addTask("BehaviorTrees", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && engineTime::Timer::isGameTimeActive() && behaviorTreeService) {
                float dt = static_cast<float>(engineTime::Timer::getGameDeltaTime());
                behaviorTreeService->updateAll(dt);
            }
        });

        frameTaskGraph->addTask("VFX", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && engineTime::Timer::isGameTimeActive() && vfxPlayModeHandler) {
                float dt = static_cast<float>(engineTime::Timer::getGameDeltaTime());
                vfxPlayModeHandler->update(dt);
                // VK-1425: step running combos and drain queued animation-event triggers (after the
                // per-instance handler so child Create/Play/SetTransform run before UpdateVFXRuntime).
                if (vfxSequencePlayModeHandler) {
                    vfxSequencePlayModeHandler->update(dt);
                }
            }
        });

        // Streaming/bake portions run in edit mode too (world baker, tile preview);
        // only the crowd agent simulation is play-mode gated.
        frameTaskGraph->addTask("Navmesh", [this]() {
            if (navmeshService) {
                // Streaming/bake run in edit mode on raw dt; only agent simulation is
                // gameplay-gated, so it follows isGameTimeActive() (pause/step/scale).
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                bool simulateAgents = editorModeService && editorModeService->isPlayMode() && engineTime::Timer::isGameTimeActive();
                navmeshService->update(dt, simulateAgents);
            }
        });

        // VK-1330 / VK-1333: an "AudioListener" task used to live here that
        // called audioSceneUpdater->updateListenerFromPrimaryCamera() every
        // frame in Play mode. Multi-task layers in the FrameTaskGraph dispatch
        // tasks onto enkiTS workers, and the audio backend
        // (SetListenerPositionCommand -> AudioServiceImpl -> AudioAdapter ->
        // AudioController::commandQueue) is not safe to invoke from a worker
        // thread - doing so aborts under MSVC's CRT debug runtime as soon as
        // a scene camera becomes primary.
        //
        // In the editor, ViewPort::updateRendererCameras already dispatches
        // SetListenerPositionCommand every frame from the ImGuiDraw task,
        // which sits in a single-task layer and therefore runs on the main
        // thread. The AudioListener task was strictly redundant here, so the
        // fix is simply to remove it. Runtime keeps its own AudioListener
        // task because it has no ViewPort to do the work.

        frameTaskGraph->addTask("Weather", [this]() {
            float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
            events::weather::UpdateWeatherCommand cmd;
            cmd.deltaTime = dt;
            events::EventDispatcher::instance().execute(cmd);
        });

        frameTaskGraph->addTask("Ocean", [this]() {
            float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
            events::ocean::UpdateOceanCommand cmd;
            cmd.deltaTime = dt;
            events::EventDispatcher::instance().execute(cmd);
        });

        frameTaskGraph->addTask("WorldSector", [this]() {
            if (worldSectorService) worldSectorService->update();
        });

        frameTaskGraph->addTask("AssetLifecycle", [this]() {
            if (assetLifecycleService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                assetLifecycleService->update(dt);
            }
        });

        frameTaskGraph->addTask("Plugins", [this]() {
            if (pluginManager) {
                // VK-816: dt is the scaled GAME delta DURING PLAY (matches the
                // Runtime path) and 0 in edit / paused / time-scale==0. Gated on
                // isPlayMode() like the other gameplay tasks because
                // getGameDeltaTime() alone is nonzero in edit mode whenever the
                // editor clock runs. dt-driven plugins (GAS effects/cooldowns/
                // input) therefore run only while gameplay advances; dt-agnostic
                // plugins (e.g. RTSGameplay fog) still tick every frame and
                // ignore dt.
                const bool gameplayActive = editorModeService &&
                    editorModeService->isPlayMode() && engineTime::Timer::isGameTimeActive();
                float dt = gameplayActive
                    ? static_cast<float>(engineTime::Timer::getGameDeltaTime())
                    : 0.0f;
                pluginManager->updateAll(dt);
            }
        });

        frameTaskGraph->addDependency("Weather", "Scene");
        frameTaskGraph->addDependency("Ocean", "Weather");
        frameTaskGraph->addDependency("PhysicsKick", "Scene");
        frameTaskGraph->addDependency("PhysicsKick", "Input");
        frameTaskGraph->addDependency("PhysicsKick", "WindowState");
        frameTaskGraph->addDependency("PhysicsSync", "PhysicsKick");
        frameTaskGraph->addDependency("Scripts", "PhysicsSync");
        frameTaskGraph->addDependency("Controllers", "Scripts");
        frameTaskGraph->addDependency("BehaviorTrees", "Controllers");
        frameTaskGraph->addDependency("Navmesh", "BehaviorTrees");
        frameTaskGraph->addDependency("VFX", "Scripts");

        auto sceneGraphFn = bootstrap->getSceneGraphUpdateFn();
        frameTaskGraph->addTask("Transforms", [sceneGraphFn]() {
            if (sceneGraphFn) sceneGraphFn();
        });

        frameTaskGraph->addTask("LateScripts", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && engineTime::Timer::isGameTimeActive() && scriptingService) {
                float dt = static_cast<float>(engineTime::Timer::getGameDeltaTime());
                try { scriptingService->lateUpdateScripts(dt); }
                catch (const std::exception& e) { vfLogError("[Script] lateUpdateScripts threw: {}", e.what()); }
            }
        });

        auto imguiDrawFn = bootstrap->getImguiDrawFn();
        // MUST run on the main thread. imguiDrawFn() calls MainLoop::newFrame() ->
        // ImGui_ImplGlfw_NewFrame() -> GLFW window functions (e.g.
        // _glfwSetWindowMousePassthroughWin32), which Windows marshals via SendMessage
        // to the thread that owns the window (the main thread). It also dispatches the
        // audio listener command (ViewPort::updateRendererCameras), unsafe off-thread.
        // Pre-VK-1385 this ran on the main thread only because it was the lone task in
        // its barrier layer; with native dependencies (no layers) a worker would pick it
        // up and deadlock against the main thread parked in TaskGraph::execute(). Pinning
        // to thread 0 makes the main thread run it itself while it waits on the terminal.
        frameTaskGraph->addTask("ImGuiDraw", [imguiDrawFn]() {
            if (!imguiDrawFn) {
                threading::EditorTaskStats::imguiDrawDurationNs.store(0, std::memory_order_relaxed);
                return;
            }
            const auto start = std::chrono::steady_clock::now();
            imguiDrawFn();
            const auto end = std::chrono::steady_clock::now();
            threading::EditorTaskStats::imguiDrawDurationNs.store(
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()),
                std::memory_order_relaxed);
        }, threading::JobPriority::NORMAL, /*mainThread=*/true);

        auto renderFn = bootstrap->getRenderFn();
        frameTaskGraph->addTask("Render", [renderFn]() {
            if (renderFn) renderFn();
        });

        frameTaskGraph->addDependency("Transforms", "Weather");
        frameTaskGraph->addDependency("Transforms", "BehaviorTrees");
        frameTaskGraph->addDependency("Transforms", "Navmesh");
        frameTaskGraph->addDependency("Transforms", "VFX");
        frameTaskGraph->addDependency("Transforms", "WorldSector");
        frameTaskGraph->addDependency("Transforms", "AssetLifecycle");
        frameTaskGraph->addDependency("Transforms", "Plugins");
        frameTaskGraph->addDependency("LateScripts", "Transforms");
        frameTaskGraph->addDependency("ImGuiDraw", "LateScripts");
        frameTaskGraph->addDependency("Render", "ImGuiDraw");

        if (frameTaskGraph->compile()) {
            frameTaskGraph->registerEventHandlers();
        }
    }
}
