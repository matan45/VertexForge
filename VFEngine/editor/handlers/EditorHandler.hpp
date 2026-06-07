#pragma once
#include <memory>
#include <string>
#include "WindowImguiHandler.hpp"

#include "interfaces/project/ISceneService.hpp"
#include "interfaces/render/IEditorRenderService.hpp"
#include "interfaces/input/IInputService.hpp"
#include "interfaces/input/IActionMappingService.hpp"
#include "interfaces/editor/IWindowStateService.hpp"
#include "interfaces/render/IPreviewService.hpp"
#include "interfaces/editor/IEditorModeService.hpp"
#include "interfaces/audio/IAudioService.hpp"
#include "interfaces/scripting/IScriptingService.hpp"
#include "interfaces/editor/IUndoRedoService.hpp"
#include "interfaces/project/IFileOperationsService.hpp"
#include "interfaces/physics/IPhysicsService.hpp"
#include "interfaces/navmesh/INavmeshService.hpp"
#include "interfaces/project/IProjectService.hpp"
#include "interfaces/terrain/ITerrainService.hpp"
#include "interfaces/terrain/IOceanService.hpp"
#include "interfaces/editor/ISculptModeService.hpp"
#include "interfaces/terrain/IBrushService.hpp"
#include "interfaces/terrain/IPaintModeService.hpp"
#include "interfaces/terrain/IPaintBrushService.hpp"
#include "interfaces/terrain/IHoleModeService.hpp"
#include "interfaces/terrain/IHoleBrushService.hpp"
#include "interfaces/terrain/ICaveModeService.hpp"
#include "interfaces/terrain/ICaveBrushService.hpp"
#include "interfaces/terrain/ITerrainRaycastService.hpp"
#include "interfaces/terrain/ISplineTerrainService.hpp"
#include "interfaces/vegetation/IGrassService.hpp"
#include "interfaces/vegetation/IVegetationBrushService.hpp"
#include "interfaces/vegetation/IVegetationBrushModeService.hpp"
#include "interfaces/meshbrush/IMeshBrushService.hpp"
#include "interfaces/meshbrush/IMeshBrushModeService.hpp"
#include "interfaces/physics/IPhysicsAnimationService.hpp"
#include "interfaces/render/IRenderTextureService.hpp"
#include "interfaces/physics/IControllerService.hpp"
#include "interfaces/render/IRenderHookService.hpp"
#include "interfaces/render/ICustomPipelineService.hpp"
#include "interfaces/render/IPluginTextureService.hpp"
#include "interfaces/render/IDebugDrawService.hpp"
#include "interfaces/render/IBillboardRenderService.hpp"
#include "interfaces/render/IDecalRenderService.hpp"
#include "interfaces/render/ILightStreamingService.hpp"
#include "interfaces/render/IObjectStreamingService.hpp"
#include "interfaces/render/IGIService.hpp"
#include "interfaces/ai/IBehaviorTreeService.hpp"
#include "interfaces/input/IRuntimePickerService.hpp"
#include "interfaces/weather/IWeatherService.hpp"
#include "interfaces/destruction/IDestructionService.hpp"
#include "impl/components/IKComponentService.hpp"
#include "interfaces/lifecycle/IAssetLifecycleService.hpp"
#include "interfaces/asset/IAssetDatabaseService.hpp"
#include "interfaces/world/IWorldSectorService.hpp"
#include "events/EventTypes.hpp"

namespace services {
	class FrameTaskGraph;
}

namespace plugin {
	class PluginManager;
}

namespace core {
	class EditorBootstrap;
}

namespace core::audio {
	class AudioSceneUpdater;
}

namespace services {
	class PhysicsPlayModeHandler;
	class VFXPlayModeHandler;
	class VFXRuntimeServiceImpl;
	class RenderTexturePlayModeHandler;
	class BehaviorTreePlayModeHandler;
	class EditorRenderServiceImpl;
	class SaveService;
	class ConfigService;
	class EditorSettingsService;
	class EditorKeybindingServiceImpl;
}

namespace handlers {
	class ExportHandler;
}

namespace handlers {

	class EditorHandler
	{
	private:
		std::unique_ptr<core::EditorBootstrap> bootstrap;

		std::unique_ptr<WindowImguiHandler> windowImguiHandler;
		
		std::shared_ptr<services::ISceneService> sceneService;
		std::shared_ptr<services::IEditorRenderService> renderService;
		services::EditorRenderServiceImpl* editorRenderServiceImpl = nullptr;
		std::shared_ptr<services::IInputService> inputService;
		std::shared_ptr<services::IActionMappingService> actionMappingService;
		std::shared_ptr<services::IWindowStateService> windowStateService;
		std::shared_ptr<services::IPreviewService> previewService;
		std::shared_ptr<services::IEditorModeService> editorModeService;
		std::shared_ptr<services::IAudioService> audioService;
		std::shared_ptr<services::IScriptingService> scriptingService;
		std::shared_ptr<services::IUndoRedoService> undoRedoService;
		std::shared_ptr<services::IFileOperationsService> fileOperationsService;
		std::shared_ptr<services::IPhysicsService> physicsService;
		std::shared_ptr<services::IProjectService> projectService;
		std::shared_ptr<services::ITerrainService> terrainService;
		std::shared_ptr<services::IOceanService> oceanService;
		std::shared_ptr<services::ISculptModeService> sculptModeService;
		std::shared_ptr<services::IBrushService> brushService;
		std::shared_ptr<services::IPaintModeService> paintModeService;
		std::shared_ptr<services::IPaintBrushService> paintBrushService;
		std::shared_ptr<services::IHoleModeService> holeModeService;
		std::shared_ptr<services::IHoleBrushService> holeBrushService;
		std::shared_ptr<services::ICaveModeService> caveModeService;
		std::shared_ptr<services::ICaveBrushService> caveBrushService;
		std::shared_ptr<services::ITerrainRaycastService> terrainRaycastService;
		std::shared_ptr<services::ISplineTerrainService> splineTerrainService;
		std::shared_ptr<services::IPhysicsAnimationService> physicsAnimationService;
		std::shared_ptr<services::INavmeshService> navmeshService;
		std::unique_ptr<core::audio::AudioSceneUpdater> audioSceneUpdater;
		std::unique_ptr<services::PhysicsPlayModeHandler> physicsPlayModeHandler;
		std::unique_ptr<services::VFXPlayModeHandler> vfxPlayModeHandler;
		std::unique_ptr<services::VFXRuntimeServiceImpl> vfxRuntimeService;
		std::shared_ptr<services::IRenderTextureService> renderTextureService;
		std::unique_ptr<services::RenderTexturePlayModeHandler> renderTexturePlayModeHandler;
		std::shared_ptr<services::IControllerService> controllerService;
		std::shared_ptr<services::IKComponentService> ikComponentService;
		std::shared_ptr<services::IRenderHookService> renderHookService;
		std::shared_ptr<services::ICustomPipelineService> customPipelineService;
		std::shared_ptr<services::IPluginTextureService> pluginTextureService;
		std::shared_ptr<services::IDebugDrawService> debugDrawService;
		std::shared_ptr<services::IAssetLifecycleService> assetLifecycleService;
		std::shared_ptr<services::IAssetDatabaseService> assetDatabaseService;
		std::shared_ptr<services::IWorldSectorService> worldSectorService;
		std::shared_ptr<services::IGrassService> grassService;
		std::shared_ptr<services::IVegetationBrushService> vegetationBrushService;
		std::shared_ptr<services::IVegetationBrushModeService> vegetationBrushModeService;
		std::shared_ptr<services::IMeshBrushService> meshBrushService;
		std::shared_ptr<services::IMeshBrushModeService> meshBrushModeService;
		std::shared_ptr<services::IBillboardRenderService> billboardRenderService;
		std::shared_ptr<services::IDecalRenderService> decalRenderService;
		std::shared_ptr<services::ILightStreamingService> lightStreamingService;
		std::shared_ptr<services::IObjectStreamingService> objectStreamingService;
		std::shared_ptr<services::IGIService> giService;
		std::shared_ptr<services::IBehaviorTreeService> behaviorTreeService;
		std::unique_ptr<services::BehaviorTreePlayModeHandler> behaviorTreePlayModeHandler;
		std::shared_ptr<services::IRuntimePickerService> runtimePickerService;
		std::shared_ptr<services::IWeatherService> weatherService;
		std::shared_ptr<services::IDestructionService> destructionService;

		std::unique_ptr<handlers::ExportHandler> exportHandler;

		std::unique_ptr<services::SaveService> saveService;
		std::unique_ptr<services::ConfigService> configService;
		std::unique_ptr<services::EditorSettingsService> editorSettingsService;
		std::shared_ptr<services::EditorKeybindingServiceImpl> editorKeybindingService;

		std::unique_ptr<plugin::PluginManager> pluginManager;

		std::unique_ptr<services::FrameTaskGraph> frameTaskGraph;

		events::SubscriptionToken resizeSubscription;

	public:
		explicit EditorHandler();
		~EditorHandler();

		void init();
		void run() const;
		void cleanUp();

		bool loadProject(const std::string& projectPath);

	private:
		void initializeServices();
		void createCoreServices();
		void createMediaServices();
		void createPhysicsServices();
		void createVFXServices();
		void createTerrainServices();
		void createOceanServices();
		void createVegetationServices();
		void createMeshBrushServices();
		void createAIServices();
		void createWeatherServices();
		void registerAllEventHandlers();
		void setupEventSubscriptions();
		void cleanupEventSubscriptions();
		void buildFrameTaskGraph();
	};
}
