#pragma once
#include "events/EventDispatcher.hpp"
#include "events/project/ExportEvents.hpp"
#include <filesystem>
#include <thread>
#include <atomic>

namespace handlers
{
	class ExportHandler
	{
	private:
		std::unique_ptr<std::jthread> exportThread;
		std::atomic<bool> exporting{false};
	public:
		ExportHandler() = default;
		~ExportHandler();

		void registerEventHandlers();
		void unregisterEventHandlers();

	private:
		bool handleExportCommand(const events::gameExport::ExportGameCommand& cmd);
		bool handleCanExportQuery(const events::gameExport::CanExportQuery& query);

		// Pre-export passes (run on the calling thread, before the export thread
		// spawns — they use dispatcher-driven services / editor-layer compilers
		// that must not be called from a worker thread).
		bool buildScriptsForExport(const std::filesystem::path& workingDirectory,
								   std::string& errorMessage);
		bool recompileStaleMaterials(const std::filesystem::path& workingDirectory,
									 std::string& errorMessage);

		void publishFailure(const std::string& outputDirectory, const std::string& errorMessage);
	};
}
