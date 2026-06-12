#include "print/Log.hpp"
#include "ExportHandler.hpp"
#include "../graph/ShaderGraphCompiler.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"
#include "export/GameExporter.hpp"
#include "material/MaterialAsset.hpp"
#include <filesystem>

namespace handlers
{
	namespace fs = std::filesystem;

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

		auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
		if (!projectOpt || !projectOpt->isValid())
		{
			vfLogError("No valid project loaded");
			return false;
		}

		auto projectPathOpt = dispatcher.query(events::project::GetProjectPathQuery{});

		gameExport::ExportConfig config;
		config.gameName = projectOpt->projectName;
		config.gameVersion = projectOpt->version;
		config.outputDirectory = cmd.outputDirectory;
		config.workingDirectory = projectOpt->workingDirectory;
		config.startupScene = projectOpt->startupScene;
		config.iconPath = projectOpt->exeIconPath;
		config.cleanBuild = cmd.cleanBuild;
		config.verifyIntegrity = cmd.verifyIntegrity;

		if (projectPathOpt)
		{
			config.projectFile = *projectPathOpt;
		}

		events::gameExport::ExportStartedNotification startNotif;
		startNotif.outputDirectory = cmd.outputDirectory;
		dispatcher.publish(startNotif);

		// Pre-export passes run before the export thread spawns: the scripting
		// service and ShaderGraphCompiler belong to the main thread. A failure
		// here aborts the export — shipping stale scripts or broken materials
		// is worse than no export.
		std::string preExportError;
		if (cmd.buildScripts && !buildScriptsForExport(config.workingDirectory, preExportError))
		{
			publishFailure(cmd.outputDirectory, preExportError);
			return false;
		}

		if (!recompileStaleMaterials(config.workingDirectory, preExportError))
		{
			publishFailure(cmd.outputDirectory, preExportError);
			return false;
		}

		exporting.store(true);

		if (exportThread && exportThread->joinable())
		{
			exportThread->join();
		}

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

	bool ExportHandler::buildScriptsForExport(const fs::path& workingDirectory,
											  std::string& errorMessage)
	{
		if (!fs::exists(workingDirectory / "scripts" / "scripts.mtproj"))
		{
			return true; // Project has no scripts to build
		}

		vfLogInfo("Export: building scripts...");
		auto& dispatcher = events::EventDispatcher::instance();
		if (!dispatcher.execute(events::scripting::BuildScriptsCommand{}))
		{
			errorMessage = "Script build failed — fix script errors before exporting (see console log).";
			return false;
		}

		return true;
	}

	bool ExportHandler::recompileStaleMaterials(const fs::path& workingDirectory,
												std::string& errorMessage)
	{
		// Graph-authored materials whose cached shader is missing or stale (e.g.
		// cleared by the texture-array-outdated check on load) render wrong in
		// shipped builds — the runtime cannot regenerate them. Recompile and
		// re-save them here, where the editor-side ShaderGraphCompiler exists.
		std::vector<std::string> failedMaterials;
		int recompiledCount = 0;

		std::error_code ec;
		for (auto it = fs::recursive_directory_iterator(workingDirectory, ec);
		     it != fs::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec) break;
			if (!it->is_regular_file() || it->path().extension() != ".vfmaterial") continue;

			auto materialOpt = material::MaterialAsset::load(it->path().string());
			if (!materialOpt) continue;

			auto& materialData = *materialOpt;
			if (materialData.graph.findOutputNode() == nullptr) continue; // standard PBR material

			bool hasCachedShader = !materialData.cachedVertexShader.empty() &&
								   !materialData.cachedFragmentShader.empty();
			if (hasCachedShader && !materialData.needsRecompile) continue;

			std::string relativePath = fs::relative(it->path(), workingDirectory, ec).generic_string();
			auto compileResult = editor::graph::ShaderGraphCompiler::compileGraph(materialData.graph);
			if (!compileResult.success)
			{
				failedMaterials.push_back(relativePath + ": " + compileResult.errorMessage);
				continue;
			}

			materialData.cachedVertexShader = compileResult.vertexShader;
			materialData.cachedFragmentShader = compileResult.fragmentShader;
			materialData.needsRecompile = false;

			if (!material::MaterialAsset::save(it->path().string(), materialData))
			{
				failedMaterials.push_back(relativePath + ": failed to save recompiled material");
				continue;
			}

			recompiledCount++;
			vfLogInfo("Export: recompiled material shader: {}", relativePath);
		}

		if (recompiledCount > 0)
		{
			vfLogInfo("Export: recompiled {} stale material shader(s)", recompiledCount);
		}

		if (!failedMaterials.empty())
		{
			errorMessage = "Material shader compilation failed:";
			for (const auto& failure : failedMaterials)
			{
				errorMessage += "\n  - " + failure;
			}
			return false;
		}

		return true;
	}

	void ExportHandler::publishFailure(const std::string& outputDirectory,
									   const std::string& errorMessage)
	{
		events::gameExport::ExportCompletedNotification completeNotif;
		completeNotif.success = false;
		completeNotif.errorMessage = errorMessage;
		completeNotif.outputPath = outputDirectory;
		events::EventDispatcher::instance().publish(completeNotif);
		vfLogError("Game export failed: {}", errorMessage);
	}
}
