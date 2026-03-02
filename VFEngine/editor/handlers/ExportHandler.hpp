#pragma once
#include "events/EventDispatcher.hpp"
#include "events/ExportEvents.hpp"
#include <thread>
#include <atomic>

namespace handlers
{
	class ExportHandler
	{
	public:
		ExportHandler() = default;
		~ExportHandler();

		void registerEventHandlers();
		void unregisterEventHandlers();

	private:
		bool handleExportCommand(const events::gameExport::ExportGameCommand& cmd);
		bool handleCanExportQuery(const events::gameExport::CanExportQuery& query);

		std::unique_ptr<std::jthread> exportThread;
		std::atomic<bool> exporting{false};
	};
}
