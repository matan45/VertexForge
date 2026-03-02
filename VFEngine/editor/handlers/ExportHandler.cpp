#include "ExportHandler.hpp"
#include "events/ProjectEvents.hpp"
#include "export/GameExporter.hpp"
#include "print/EditorLogger.hpp"
#include <filesystem>

namespace handlers
{
	ExportHandler::~ExportHandler()
	{
		unregisterEventHandlers();
		if (exportThread && exportThread->joinable())
		{
			exportThread->join();
		}
	}

	void ExportHandler::registerEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		dispatcher.registerCommandHandler<events::gameExport::ExportGameCommand>(
			[this](const events::gameExport::ExportGameCommand& cmd) -> bool {
				return handleExportCommand(cmd);
			});

		dispatcher.registerQueryHandler<events::gameExport::CanExportQuery>(
			[this](const events::gameExport::CanExportQuery& query) -> bool {
				return handleCanExportQuery(query);
			});
	}

	void ExportHandler::unregisterEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();
		dispatcher.unregisterCommandHandler<events::gameExport::ExportGameCommand>();
		dispatcher.unregisterQueryHandler<events::gameExport::CanExportQuery>();
	}

	bool ExportHandler::handleExportCommand(const events::gameExport::ExportGameCommand& cmd)
	{
		if (exporting.load())
		{
			vfLogWarning("Export already in progress");
			return false;
		}

		auto& dispatcher = events::EventDispatcher::instance();

		// Get current project config
		auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
		if (!projectOpt || !projectOpt->isValid())
		{
			vfLogError("No valid project loaded");
			return false;
		}

		auto projectPathOpt = dispatcher.query(events::project::GetProjectPathQuery{});

		// Build export config
		gameExport::ExportConfig config;
		config.gameName = projectOpt->projectName;
		config.gameVersion = projectOpt->version;
		config.outputDirectory = cmd.outputDirectory;
		config.workingDirectory = projectOpt->workingDirectory;
		config.startupScene = projectOpt->startupScene;
		config.iconPath = projectOpt->exeIconPath;

		if (projectPathOpt)
		{
			config.projectFile = *projectPathOpt;
		}

		// Publish start notification
		events::gameExport::ExportStartedNotification startNotif;
		startNotif.outputDirectory = cmd.outputDirectory;
		dispatcher.publish(startNotif);

		exporting.store(true);

		// Wait for previous thread if any
		if (exportThread && exportThread->joinable())
		{
			exportThread->join();
		}

		// Run export in background thread
		exportThread = std::make_unique<std::jthread>([this, config]() {
			auto& disp = events::EventDispatcher::instance();

			gameExport::GameExporter exporter;
			auto result = exporter.exportGame(config,
				[&disp](float progress, const std::string& status) {
					events::gameExport::ExportProgressNotification notif;
					notif.progress = progress;
					notif.currentStep = status;
					disp.publish(notif);
				});

			events::gameExport::ExportCompletedNotification completeNotif;
			completeNotif.success = result.success;
			completeNotif.errorMessage = result.errorMessage;
			completeNotif.warnings = result.warnings;
			completeNotif.outputPath = result.outputPath.string();
			disp.publish(completeNotif);

			exporting.store(false);

			if (result.success)
			{
				vfLogInfo("Game exported successfully to: {}", result.outputPath.string());
			}
			else
			{
				vfLogError("Game export failed: {}", result.errorMessage);
			}
		});

		return true;
	}

	bool ExportHandler::handleCanExportQuery(const events::gameExport::CanExportQuery&)
	{
		if (exporting.load()) return false;

		auto& dispatcher = events::EventDispatcher::instance();

		// Check project is loaded
		bool isLoaded = dispatcher.query(events::project::IsProjectLoadedQuery{});
		if (!isLoaded) return false;

		auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
		if (!projectOpt || !projectOpt->isValid()) return false;

		// Check working directory exists
		if (!std::filesystem::exists(projectOpt->workingDirectory)) return false;

		return true;
	}
}
