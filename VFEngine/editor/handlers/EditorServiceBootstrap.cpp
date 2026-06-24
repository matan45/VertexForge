#include "EditorHandler.hpp"
#include "editor/EditorBootstrap.hpp"
#include "impl/project/SceneServiceImpl.hpp"
#include "impl/render/EditorRenderServiceImpl.hpp"
#include "impl/input/InputServiceImpl.hpp"
#include "impl/input/ActionMappingServiceImpl.hpp"
#include "impl/editor/WindowStateServiceImpl.hpp"
#include "impl/render/PreviewServiceImpl.hpp"
#include "impl/editor/EditorModeServiceImpl.hpp"
#include "impl/audio/AudioServiceImpl.hpp"
#include "impl/scripting/ScriptingServiceImpl.hpp"
#include "impl/editor/UndoRedoServiceImpl.hpp"
#include "impl/project/FileOperationsServiceImpl.hpp"
#include "impl/physics/PhysicsServiceImpl.hpp"
#include "impl/physics/PhysicsAnimationServiceImpl.hpp"
#include "impl/navmesh/NavmeshServiceImpl.hpp"
#include "impl/physics/PhysicsPlayModeHandler.hpp"
#include "impl/vfx/VFXPlayModeHandler.hpp"
#include "impl/vfx/VFXRuntimeServiceImpl.hpp"
#include "impl/vfx/VFXSequencePlayModeHandler.hpp"
#include "impl/vfx/VFXSequenceRuntimeServiceImpl.hpp"
#include "impl/project/ProjectServiceImpl.hpp"
#include "impl/scene/TerrainService.hpp"
#include "impl/scene/OceanService.hpp"
#include "impl/editor/SculptModeServiceImpl.hpp"
#include "impl/terrain/BrushServiceImpl.hpp"
#include "impl/terrain/PaintModeServiceImpl.hpp"
#include "impl/terrain/PaintBrushServiceImpl.hpp"
#include "impl/terrain/HoleModeServiceImpl.hpp"
#include "impl/terrain/HoleBrushServiceImpl.hpp"
#include "impl/terrain/CaveModeServiceImpl.hpp"
#include "impl/terrain/CaveBrushServiceImpl.hpp"
#include "impl/terrain/TerrainRaycastServiceImpl.hpp"
#include "impl/render/RenderTextureServiceImpl.hpp"
#include "impl/render/RenderTexturePlayModeHandler.hpp"
#include "impl/physics/ControllerServiceImpl.hpp"
#include "impl/render/RenderHookServiceImpl.hpp"
#include "impl/render/CustomPipelineServiceImpl.hpp"
#include "impl/render/PostProcessEffectServiceImpl.hpp"
#include "impl/render/PluginTextureServiceImpl.hpp"
#include "impl/render/DebugDrawServiceImpl.hpp"
#include "impl/render/BillboardRenderServiceImpl.hpp"
#include "impl/render/DecalRenderServiceImpl.hpp"
#include "impl/render/LightStreamingServiceImpl.hpp"
#include "impl/render/ObjectStreamingServiceImpl.hpp"
#include "impl/render/GIServiceImpl.hpp"
#include "impl/ai/BehaviorTreeServiceImpl.hpp"
#include "impl/ai/BehaviorTreePlayModeHandler.hpp"
#include "impl/input/RuntimePickerServiceImpl.hpp"
#include "impl/lifecycle/AssetLifecycleServiceImpl.hpp"
#include "impl/memory/CpuMemoryServiceImpl.hpp"
#include "impl/asset/AssetDatabaseServiceImpl.hpp"
#include "impl/world/WorldSectorServiceImpl.hpp"
#include "impl/vegetation/GrassServiceImpl.hpp"
#include "impl/vegetation/VegetationBrushServiceImpl.hpp"
#include "impl/vegetation/VegetationBrushModeServiceImpl.hpp"
#include "impl/meshbrush/MeshBrushModeServiceImpl.hpp"
#include "impl/meshbrush/MeshBrushServiceImpl.hpp"
#include "impl/weather/WeatherServiceImpl.hpp"
#include "impl/destruction/DestructionServiceImpl.hpp"
#include "../adapters/terrain/TerrainRenderAdapter.hpp"
#include "../adapters/terrain/OceanRenderAdapter.hpp"
#include "../audio/AudioSceneUpdater.hpp"
#include "../audio/ReverbZoneManager.hpp"
#include "events/EventDispatcher.hpp"
#include "events/vegetation/GrassEvents.hpp"
#include "providers/vegetation/IGrassRenderProvider.hpp"
#include "events/render/RenderEvents.hpp"
#include "ExportHandler.hpp"
#include "resource/PathResolver.hpp"
#include "impl/save/SaveService.hpp"
#include "impl/save/ConfigService.hpp"
#include "impl/editor/EditorSettingsService.hpp"
#include "events/editor/EditorSettingsEvents.hpp"
#include "cpumem/CpuMemoryManager.hpp"
#include "impl/editor/EditorKeybindingServiceImpl.hpp"
#include "impl/terrain/SplineTerrainServiceImpl.hpp"

namespace handlers
{
    void EditorHandler::initializeServices()
    {
        createCoreServices();
        createMediaServices();
        createPhysicsServices();
        createVFXServices();
        createTerrainServices();
        createOceanServices();
        createVegetationServices();
        createMeshBrushServices();
        createAIServices();
        createWeatherServices();
        exportHandler = std::make_unique<handlers::ExportHandler>();
        registerAllEventHandlers();
    }

    void EditorHandler::createCoreServices()
    {
        sceneService = std::make_shared<services::SceneServiceImpl>(
            bootstrap->getSceneGraphSystem(),
            bootstrap->getAnimatorProvider(),
            bootstrap->getSocketProvider()
        );
        renderService = std::make_shared<services::EditorRenderServiceImpl>(
            bootstrap->getOffScreenProvider(),
            bootstrap->getEditorTextureProvider(),
            bootstrap->getPostProcessProvider()
        );
        inputService = std::make_shared<services::InputServiceImpl>(bootstrap->getWindow());
        actionMappingService = std::make_shared<services::ActionMappingServiceImpl>();
        windowStateService = std::make_shared<services::WindowStateServiceImpl>(bootstrap->getWindow());
        previewService = std::make_shared<services::PreviewServiceImpl>(
            bootstrap->getMaterialPreviewProvider(),
            bootstrap->getMeshPreviewProvider(),
            bootstrap->getAnimationPreviewProvider(),
            bootstrap->getVFXPreviewProvider(),
            bootstrap->getPrefabRigPreviewProvider(),
            bootstrap->getUILayerPreviewProvider()
        );
        editorModeService = std::make_shared<services::EditorModeServiceImpl>(bootstrap->getSceneGraphSystem());
        undoRedoService = std::make_shared<services::UndoRedoServiceImpl>();
        fileOperationsService = std::make_shared<services::FileOperationsServiceImpl>(undoRedoService);
        projectService = std::make_shared<services::ProjectServiceImpl>();
        assetDatabaseService = std::make_shared<services::AssetDatabaseServiceImpl>();
        renderTextureService = std::make_shared<services::RenderTextureServiceImpl>(
            bootstrap->getRenderTextureProvider()
        );

        if (auto* rttProvider = bootstrap->getRenderTextureProvider())
        {
            renderTexturePlayModeHandler = std::make_unique<services::RenderTexturePlayModeHandler>(rttProvider);
            renderTexturePlayModeHandler->subscribeToEvents();
        }

        renderHookService = std::make_shared<services::RenderHookServiceImpl>(bootstrap->getRenderHookProvider());
        customPipelineService = std::make_shared<services::CustomPipelineServiceImpl>(bootstrap->getCustomPipelineProvider());
        postProcessEffectService = std::make_shared<services::PostProcessEffectServiceImpl>(bootstrap->getPostProcessEffectProvider());
        pluginTextureService = std::make_shared<services::PluginTextureServiceImpl>(bootstrap->getPluginTextureProvider());
        debugDrawService = std::make_shared<services::DebugDrawServiceImpl>(bootstrap->getDebugDrawProvider());
        billboardRenderService = std::make_shared<services::BillboardRenderServiceImpl>(bootstrap->getBillboardRenderProvider());
        decalRenderService = std::make_shared<services::DecalRenderServiceImpl>(bootstrap->getDecalRenderProvider());
        assetLifecycleService = std::make_shared<services::AssetLifecycleServiceImpl>();
        cpuMemoryService = std::make_shared<services::CpuMemoryServiceImpl>();
        lightStreamingService = std::make_shared<services::LightStreamingServiceImpl>(bootstrap->getLightStreamingProvider());
        objectStreamingService = std::make_shared<services::ObjectStreamingServiceImpl>(bootstrap->getObjectStreamingProvider());
        giService = std::make_shared<services::GIServiceImpl>(bootstrap->getGIProvider());
        worldSectorService = std::make_shared<services::WorldSectorServiceImpl>(bootstrap->getSceneGraphSystem());
    }

    void EditorHandler::createMediaServices()
    {
        audioService = std::make_shared<services::AudioServiceImpl>(bootstrap->getAudioProvider());
        scriptingService = std::make_shared<services::ScriptingServiceImpl>(
            bootstrap->getScriptingProvider(),
            bootstrap->getSceneGraphSystem()
        );
        audioSceneUpdater = std::make_unique<core::audio::AudioSceneUpdater>();
        auto* reverbZoneMgr = static_cast<core::audio::ReverbZoneManager*>(
            bootstrap->getAudioProvider()->getReverbZoneManager());
        audioSceneUpdater->setReverbZoneManager(reverbZoneMgr);

        saveService = std::make_unique<services::SaveService>(
            bootstrap->getSceneGraphSystem(),
            bootstrap->getScriptingProvider()
        );
        configService = std::make_unique<services::ConfigService>();
        editorSettingsService = std::make_unique<services::EditorSettingsService>();
        editorKeybindingService = std::make_shared<services::EditorKeybindingServiceImpl>();
    }

    void EditorHandler::createPhysicsServices()
    {
        if (auto* physicsProvider = bootstrap->getPhysicsProvider())
        {
            physicsService = std::make_shared<services::PhysicsServiceImpl>(physicsProvider);
            physicsAnimationService = std::make_shared<services::PhysicsAnimationServiceImpl>(physicsProvider);
            physicsPlayModeHandler = std::make_unique<services::PhysicsPlayModeHandler>(physicsProvider);
            physicsPlayModeHandler->subscribeToEvents();
        }

        if (auto* navmeshProvider = bootstrap->getNavmeshProvider())
            navmeshService = std::make_shared<services::NavmeshServiceImpl>(navmeshProvider);

        controllerService = std::make_shared<services::ControllerServiceImpl>(bootstrap->getPhysicsProvider());

        if (auto* ikProvider = bootstrap->getIKProvider())
            ikComponentService = std::make_shared<services::IKComponentService>(ikProvider);

        destructionService = std::make_shared<services::DestructionServiceImpl>();
    }

    void EditorHandler::createVFXServices()
    {
        if (auto* vfxProvider = bootstrap->getVFXRuntimeProvider())
        {
            vfxRuntimeService = std::make_unique<services::VFXRuntimeServiceImpl>(vfxProvider);
            vfxRuntimeService->registerEventHandlers();
            vfxPlayModeHandler = std::make_unique<services::VFXPlayModeHandler>(vfxProvider);
            vfxPlayModeHandler->subscribeToEvents();

            // VFX combo sequences (VK-1425) — orchestrate child instances through the per-instance
            // command API above; only meaningful when the VFX runtime is available.
            vfxSequenceRuntimeService = std::make_unique<services::VFXSequenceRuntimeServiceImpl>();
            vfxSequenceRuntimeService->registerEventHandlers();
            vfxSequencePlayModeHandler = std::make_unique<services::VFXSequencePlayModeHandler>();
            vfxSequencePlayModeHandler->subscribeToEvents();
        }
    }

    void EditorHandler::createTerrainServices()
    {
        auto terrainServiceImpl = std::make_shared<services::TerrainService>(bootstrap->getSceneGraphSystem());
        terrainService = terrainServiceImpl;

        if (auto* terrainAdapter = bootstrap->getTerrainRenderAdapterInternal())
            terrainAdapter->setTerrainService(terrainServiceImpl.get());

        terrainServiceImpl->setBrushComputeProvider(bootstrap->getTerrainBrushComputeProvider());

        if (auto* physicsProvider = bootstrap->getPhysicsProvider())
            terrainServiceImpl->setPhysicsProvider(physicsProvider);

        sculptModeService = std::make_shared<services::SculptModeServiceImpl>();
        brushService = std::make_shared<services::BrushServiceImpl>();
        paintModeService = std::make_shared<services::PaintModeServiceImpl>();
        paintBrushService = std::make_shared<services::PaintBrushServiceImpl>();
        holeModeService = std::make_shared<services::HoleModeServiceImpl>();
        holeBrushService = std::make_shared<services::HoleBrushServiceImpl>();
        caveModeService = std::make_shared<services::CaveModeServiceImpl>();
        caveBrushService = std::make_shared<services::CaveBrushServiceImpl>();
        terrainRaycastService = std::make_shared<services::TerrainRaycastServiceImpl>(
            bootstrap->getTerrainRaycastProvider());
        splineTerrainService = std::make_shared<services::SplineTerrainServiceImpl>();
    }

    void EditorHandler::createOceanServices()
    {
        auto oceanServiceImpl = std::make_shared<services::OceanService>(bootstrap->getSceneGraphSystem());
        oceanService = oceanServiceImpl;
        oceanServiceImpl->setPhysicsProvider(bootstrap->getPhysicsProvider());

        if (auto* oceanAdapter = bootstrap->getOceanRenderAdapterInternal())
            oceanAdapter->setOceanService(oceanServiceImpl.get());

        if (physicsPlayModeHandler)
            physicsPlayModeHandler->setOceanService(oceanServiceImpl.get());
    }

    void EditorHandler::createVegetationServices()
    {
        grassService = std::make_shared<services::GrassServiceImpl>();

        auto brushServiceImpl = std::make_shared<services::VegetationBrushServiceImpl>();
        vegetationBrushService = brushServiceImpl;
        vegetationBrushModeService = std::make_shared<services::VegetationBrushModeServiceImpl>();

        auto* grassProvider = bootstrap->getGrassRenderProvider();
        if (grassProvider)
        {
            grassProvider->setGetConfigCallback([]() {
                return events::EventDispatcher::instance().query(
                    events::vegetation::GetGlobalGrassConfigQuery{});
            });

            // Wire billboard palette: event → brush service → provider → adapter → renderer
            brushServiceImpl->setBillboardPaletteCallback(
                [grassProvider](const std::vector<vegetation::BillboardPaletteEntry>& entries, int32_t activeEntry)
                {
                    grassProvider->setBillboardPalette(entries, activeEntry);
                });

            // Wire palette query: renderer can read palette from ECS on scene load
            grassProvider->setGetBillboardPaletteCallback([]() -> std::vector<vegetation::BillboardPaletteEntry> {
                try {
                    return events::EventDispatcher::instance().query(
                        events::vegetation::GetBillboardPaletteQuery{});
                } catch (...) { return {}; }
            });
        }
    }

    void EditorHandler::createMeshBrushServices()
    {
        meshBrushService = std::make_shared<services::MeshBrushServiceImpl>();
        meshBrushModeService = std::make_shared<services::MeshBrushModeServiceImpl>();
    }

    void EditorHandler::createAIServices()
    {
        behaviorTreeService = std::make_shared<services::BehaviorTreeServiceImpl>(
            bootstrap->getBehaviorTreeProvider()
        );

        if (auto* btProvider = bootstrap->getBehaviorTreeProvider())
        {
            behaviorTreePlayModeHandler = std::make_unique<services::BehaviorTreePlayModeHandler>(btProvider);
            behaviorTreePlayModeHandler->subscribeToEvents();
        }

        runtimePickerService = std::make_shared<services::RuntimePickerServiceImpl>(
            bootstrap->getRuntimePickerProvider()
        );
    }

    void EditorHandler::createWeatherServices()
    {
        weatherService = std::make_shared<services::WeatherServiceImpl>(
            bootstrap->getVFXRuntimeProvider()
        );
    }

    void EditorHandler::registerAllEventHandlers()
    {
        sceneService->registerEventHandlers();
        renderService->registerEventHandlers();
        inputService->registerEventHandlers();
        actionMappingService->registerEventHandlers();
        windowStateService->registerEventHandlers();
        previewService->registerEventHandlers();
        editorModeService->registerEventHandlers();
        audioService->registerEventHandlers();
        scriptingService->registerEventHandlers();
        undoRedoService->registerEventHandlers();
        fileOperationsService->registerEventHandlers();
        physicsService->registerEventHandlers();
        if (physicsAnimationService) physicsAnimationService->registerEventHandlers();
        if (navmeshService) navmeshService->registerEventHandlers();
        projectService->registerEventHandlers();
        assetDatabaseService->registerEventHandlers();
        terrainService->registerEventHandlers();
        oceanService->registerEventHandlers();
        sculptModeService->registerEventHandlers();
        brushService->registerEventHandlers();
        paintModeService->registerEventHandlers();
        paintBrushService->registerEventHandlers();
        holeModeService->registerEventHandlers();
        holeBrushService->registerEventHandlers();
        caveModeService->registerEventHandlers();
        caveBrushService->registerEventHandlers();
        terrainRaycastService->registerEventHandlers();
        renderTextureService->registerEventHandlers();
        controllerService->registerEventHandlers();
        if (ikComponentService) ikComponentService->registerEventHandlers();
        exportHandler->registerEventHandlers();
        renderHookService->registerEventHandlers();
        customPipelineService->registerEventHandlers();
        postProcessEffectService->registerEventHandlers();
        pluginTextureService->registerEventHandlers();
        debugDrawService->registerEventHandlers();
        assetLifecycleService->registerEventHandlers();
        cpuMemoryService->registerEventHandlers();
        worldSectorService->registerEventHandlers();
        grassService->registerEventHandlers();
        vegetationBrushService->registerEventHandlers();
        vegetationBrushModeService->registerEventHandlers();
        meshBrushService->registerEventHandlers();
        meshBrushModeService->registerEventHandlers();
        billboardRenderService->registerEventHandlers();
        decalRenderService->registerEventHandlers();
        lightStreamingService->registerEventHandlers();
        objectStreamingService->registerEventHandlers();
        giService->registerEventHandlers();
        behaviorTreeService->registerEventHandlers();
        runtimePickerService->registerEventHandlers();
        weatherService->registerEventHandlers();
        if (destructionService) destructionService->registerEventHandlers();
        saveService->registerEventHandlers(events::EventDispatcher::instance());
        configService->registerEventHandlers(events::EventDispatcher::instance());
        editorSettingsService->registerEventHandlers(events::EventDispatcher::instance());
        editorKeybindingService->registerEventHandlers();
        splineTerrainService->registerEventHandlers();

        // Apply the persisted CPU memory budget now that the settings service is
        // registered and loadable.  Falls back to the 8 GiB default if the key is
        // absent from the settings file (first run or old settings).
        {
            auto settings = events::EventDispatcher::instance().query(
                events::editor::GetEditorSettingsQuery{});
            ::memory::CpuMemoryManager::instance().setBudget(
                settings.memory.cpuMemoryBudgetBytes);
        }

        events::render::LoadBillboardAtlasCommand atlasCmd;
        atlasCmd.atlasPath = resource::PathResolver::resolveEnginePath("../../resources/editor/billboardAtlas.vfImage");
        events::EventDispatcher::instance().execute(atlasCmd);
    }
}
