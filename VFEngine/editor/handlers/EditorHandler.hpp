#pragma once
#include <memory>
#include <string>
#include "WindowImguiHandler.hpp"

#include "interfaces/ISceneService.hpp"
#include "interfaces/IEditorRenderService.hpp"
#include "interfaces/IInputService.hpp"
#include "interfaces/IWindowStateService.hpp"
#include "interfaces/IPreviewService.hpp"
#include "interfaces/IEditorModeService.hpp"
#include "interfaces/IAudioService.hpp"
#include "interfaces/IScriptingService.hpp"
#include "interfaces/IUndoRedoService.hpp"
#include "interfaces/IFileOperationsService.hpp"
#include "interfaces/IPhysicsService.hpp"
#include "interfaces/INavmeshService.hpp"
#include "interfaces/IProjectService.hpp"
#include "interfaces/ITerrainService.hpp"
#include "interfaces/IWaterService.hpp"
#include "interfaces/ISculptModeService.hpp"
#include "interfaces/IBrushService.hpp"
#include "interfaces/IPaintModeService.hpp"
#include "interfaces/IPaintBrushService.hpp"
#include "interfaces/IHoleModeService.hpp"
#include "interfaces/IHoleBrushService.hpp"
#include "interfaces/ITerrainRaycastService.hpp"
#include "interfaces/IPhysicsAnimationService.hpp"
#include "interfaces/IRenderTextureService.hpp"
#include "interfaces/ILightBakeService.hpp"
#include "interfaces/IControllerService.hpp"
#include "impl/components/IKComponentService.hpp"
#include "events/EventTypes.hpp"

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
}

namespace handlers {

	class EditorHandler
	{
	private:
		std::unique_ptr<core::EditorBootstrap> bootstrap;

		std::unique_ptr<WindowImguiHandler> windowImguiHandler;
		
		std::shared_ptr<services::ISceneService> sceneService;
		std::shared_ptr<services::IEditorRenderService> renderService;
		std::shared_ptr<services::IInputService> inputService;
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
		std::shared_ptr<services::IWaterService> waterService;
		std::shared_ptr<services::ISculptModeService> sculptModeService;
		std::shared_ptr<services::IBrushService> brushService;
		std::shared_ptr<services::IPaintModeService> paintModeService;
		std::shared_ptr<services::IPaintBrushService> paintBrushService;
		std::shared_ptr<services::IHoleModeService> holeModeService;
		std::shared_ptr<services::IHoleBrushService> holeBrushService;
		std::shared_ptr<services::ITerrainRaycastService> terrainRaycastService;
		std::shared_ptr<services::IPhysicsAnimationService> physicsAnimationService;
		std::shared_ptr<services::INavmeshService> navmeshService;
		std::unique_ptr<core::audio::AudioSceneUpdater> audioSceneUpdater;
		std::unique_ptr<services::PhysicsPlayModeHandler> physicsPlayModeHandler;
		std::unique_ptr<services::VFXPlayModeHandler> vfxPlayModeHandler;
		std::unique_ptr<services::VFXRuntimeServiceImpl> vfxRuntimeService;
		std::shared_ptr<services::IRenderTextureService> renderTextureService;
		std::unique_ptr<services::RenderTexturePlayModeHandler> renderTexturePlayModeHandler;
		std::shared_ptr<services::ILightBakeService> lightBakeService;
		std::shared_ptr<services::IControllerService> controllerService;
		std::shared_ptr<services::IKComponentService> ikComponentService;

		std::unique_ptr<plugin::PluginManager> pluginManager;

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
		void createWaterServices();
		void registerAllEventHandlers();
		void setupEventSubscriptions();
		void cleanupEventSubscriptions();
	};
}
