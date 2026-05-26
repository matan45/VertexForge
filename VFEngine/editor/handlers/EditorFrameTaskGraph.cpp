#include "EditorHandler.hpp"
#include "editor/EditorBootstrap.hpp"
#include "impl/physics/PhysicsPlayModeHandler.hpp"
#include "impl/vfx/VFXPlayModeHandler.hpp"
#include "impl/render/RenderTexturePlayModeHandler.hpp"
#include "impl/ai/BehaviorTreePlayModeHandler.hpp"
#include "impl/threading/FrameTaskGraph.hpp"
#include "core/PluginManager.hpp"
#include "../../../core/audio/AudioSceneUpdater.hpp"
#include "time/Timer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/weather/WeatherEvents.hpp"
#include "print/Log.hpp"
#include "events/editor/EditorModeEvents.hpp"

namespace
{
    // Diagnostic: log first N play-mode invocations of each task so the last
    // "begin" without a matching "end" identifies the task an abort fires in.
    // Resets counter on Edit->Play transition so the rate limit isn't burned
    // through during edit mode.
    template <typename Fn>
    auto withTaskLog(const char* name, Fn fn)
    {
        return [name, fn = std::move(fn)]() mutable {
            constexpr int LOG_FIRST_N = 3;
            static bool wasPlay = false;
            static int playInvocations = LOG_FIRST_N;
            bool isPlay = events::EventDispatcher::instance().query(events::editor::IsPlayModeQuery{});
            if (isPlay && !wasPlay) playInvocations = 0;
            wasPlay = isPlay;
            const bool log = isPlay && playInvocations < LOG_FIRST_N;
            const int tag = playInvocations;
            if (log) vfLogInfo("[FrameTask {} f{}] begin", name, tag);
            fn();
            if (log) { vfLogInfo("[FrameTask {} f{}] end", name, tag); playInvocations++; }
        };
    }
}

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

        frameTaskGraph->addTask("PhysicsKick", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && physicsPlayModeHandler) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                physicsPlayModeHandler->kickUpdate(dt);
            }
        });

        frameTaskGraph->addTask("PhysicsSync", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && physicsPlayModeHandler) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                physicsPlayModeHandler->syncUpdate(dt);
            }
        });

        frameTaskGraph->addTask("Destruction", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && destructionService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                destructionService->update(dt);
            }
        });

        frameTaskGraph->addTask("Scripts", withTaskLog("Scripts", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && scriptingService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                scriptingService->updateScripts(dt);
            }
        }));

        frameTaskGraph->addTask("Controllers", withTaskLog("Controllers", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && controllerService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                controllerService->applyControllerMovement(dt);
            }
        }));

        frameTaskGraph->addTask("BehaviorTrees", withTaskLog("BehaviorTrees", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && behaviorTreeService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                behaviorTreeService->updateAll(dt);
            }
        }));

        frameTaskGraph->addTask("VFX", withTaskLog("VFX", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && vfxPlayModeHandler) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                vfxPlayModeHandler->update(dt);
            }
        }));

        // VK-1330/1333: RenderTexturePlayModeHandler::update() does Vulkan
        // command-buffer work via provider->renderAll(); that must not race
        // with the main render thread. Pin it to the main thread (the engine
        // task graph now honors the pinned flag in multi-task layers).
        frameTaskGraph->addTask("RenderTexture", withTaskLog("RenderTexture", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && renderTexturePlayModeHandler) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                renderTexturePlayModeHandler->update(dt);
            }
        }), threading::JobPriority::NORMAL, /*mainThread=*/true);

        // VK-1330/1333: the audio listener update used to be a parallel task,
        // but TaskGraph::execute() does not honor the pinned flag — any task
        // in a multi-task layer runs on an enkiTS worker. The audio backend
        // (SetListenerPositionCommand -> AudioServiceImpl -> AudioAdapter ->
        // AudioController::commandQueue) is not safe to invoke from a worker
        // thread; doing so aborts under MSVC's CRT debug runtime.
        //
        // In the editor, ViewPort::updateRendererCameras already dispatches
        // SetListenerPositionCommand every frame from the ImGuiDraw task,
        // which sits in a single-task layer and therefore runs on the main
        // thread. That makes the AudioListener task strictly redundant here,
        // and removing it eliminates the worker-thread dispatch path that
        // caused the abort. Runtime keeps its own AudioListener task because
        // it has no ViewPort to do the work; that path is a separate fix.

        frameTaskGraph->addTask("Weather", withTaskLog("Weather", [this]() {
            float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
            events::weather::UpdateWeatherCommand cmd;
            cmd.deltaTime = dt;
            events::EventDispatcher::instance().execute(cmd);
        }));

        frameTaskGraph->addTask("WorldSector", withTaskLog("WorldSector", [this]() {
            if (worldSectorService) worldSectorService->update();
        }));

        frameTaskGraph->addTask("AssetLifecycle", withTaskLog("AssetLifecycle", [this]() {
            if (assetLifecycleService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                assetLifecycleService->update(dt);
            }
        }));

        frameTaskGraph->addTask("Plugins", withTaskLog("Plugins", [this]() {
            if (pluginManager) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                pluginManager->updateAll(dt);
            }
        }));

        frameTaskGraph->addDependency("Weather", "Scene");
        frameTaskGraph->addDependency("PhysicsKick", "Scene");
        frameTaskGraph->addDependency("PhysicsKick", "Input");
        frameTaskGraph->addDependency("PhysicsKick", "WindowState");
        frameTaskGraph->addDependency("PhysicsSync", "PhysicsKick");
        frameTaskGraph->addDependency("Scripts", "PhysicsSync");
        frameTaskGraph->addDependency("Controllers", "Scripts");
        frameTaskGraph->addDependency("BehaviorTrees", "Controllers");
        frameTaskGraph->addDependency("VFX", "Scripts");
        frameTaskGraph->addDependency("RenderTexture", "Scripts");

        auto sceneGraphFn = bootstrap->getSceneGraphUpdateFn();
        frameTaskGraph->addTask("Transforms", withTaskLog("Transforms", [sceneGraphFn]() {
            if (sceneGraphFn) sceneGraphFn();
        }));

        frameTaskGraph->addTask("LateScripts", withTaskLog("LateScripts", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && scriptingService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                scriptingService->lateUpdateScripts(dt);
            }
        }));

        auto imguiDrawFn = bootstrap->getImguiDrawFn();
        frameTaskGraph->addTask("ImGuiDraw", withTaskLog("ImGuiDraw", [imguiDrawFn]() {
            if (imguiDrawFn) imguiDrawFn();
        }));

        auto renderFn = bootstrap->getRenderFn();
        frameTaskGraph->addTask("Render", withTaskLog("Render", [renderFn]() {
            if (renderFn) renderFn();
        }));

        frameTaskGraph->addDependency("Transforms", "Weather");
        frameTaskGraph->addDependency("Transforms", "BehaviorTrees");
        frameTaskGraph->addDependency("Transforms", "VFX");
        frameTaskGraph->addDependency("Transforms", "RenderTexture");
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
