#include "EditorHandler.hpp"
#include "editor/EditorBootstrap.hpp"
#include "impl/physics/PhysicsPlayModeHandler.hpp"
#include "impl/vfx/VFXPlayModeHandler.hpp"
#include "impl/ai/BehaviorTreePlayModeHandler.hpp"
#include "impl/threading/FrameTaskGraph.hpp"
#include "core/PluginManager.hpp"
#include "../../../core/audio/AudioSceneUpdater.hpp"
#include "time/Timer.hpp"
#include "events/EventDispatcher.hpp"
#include "events/weather/WeatherEvents.hpp"

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

        frameTaskGraph->addTask("Scripts", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && scriptingService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                scriptingService->updateScripts(dt);
            }
        });

        frameTaskGraph->addTask("Controllers", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && controllerService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                controllerService->applyControllerMovement(dt);
            }
        });

        frameTaskGraph->addTask("BehaviorTrees", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && behaviorTreeService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                behaviorTreeService->updateAll(dt);
            }
        });

        frameTaskGraph->addTask("VFX", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && vfxPlayModeHandler) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                vfxPlayModeHandler->update(dt);
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
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                pluginManager->updateAll(dt);
            }
        });

        frameTaskGraph->addDependency("Weather", "Scene");
        frameTaskGraph->addDependency("PhysicsKick", "Scene");
        frameTaskGraph->addDependency("PhysicsKick", "Input");
        frameTaskGraph->addDependency("PhysicsKick", "WindowState");
        frameTaskGraph->addDependency("PhysicsSync", "PhysicsKick");
        frameTaskGraph->addDependency("Scripts", "PhysicsSync");
        frameTaskGraph->addDependency("Controllers", "Scripts");
        frameTaskGraph->addDependency("BehaviorTrees", "Controllers");
        frameTaskGraph->addDependency("VFX", "Scripts");

        auto sceneGraphFn = bootstrap->getSceneGraphUpdateFn();
        frameTaskGraph->addTask("Transforms", [sceneGraphFn]() {
            if (sceneGraphFn) sceneGraphFn();
        });

        frameTaskGraph->addTask("LateScripts", [this]() {
            if (editorModeService && editorModeService->isPlayMode() && !editorModeService->isPaused() && scriptingService) {
                float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
                scriptingService->lateUpdateScripts(dt);
            }
        });

        auto imguiDrawFn = bootstrap->getImguiDrawFn();
        frameTaskGraph->addTask("ImGuiDraw", [imguiDrawFn]() {
            if (imguiDrawFn) imguiDrawFn();
        });

        auto renderFn = bootstrap->getRenderFn();
        frameTaskGraph->addTask("Render", [renderFn]() {
            if (renderFn) renderFn();
        });

        frameTaskGraph->addDependency("Transforms", "Weather");
        frameTaskGraph->addDependency("Transforms", "BehaviorTrees");
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
