#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ExportEvents.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace windows
{
	class ExportProgressWindow : public controllers::imguiHandler::ImguiWindow
	{
	private:
		std::atomic<bool> showWindow{false};
		std::atomic<float> currentProgress{0.0f};
		std::atomic<bool> exportFinished{false};

		mutable std::mutex dataMutex;
		std::string currentStep;
		bool exportSuccess = false;
		std::string exportError;
		std::vector<std::string> exportWarnings;
		std::string exportOutputPath;

		events::SubscriptionToken startToken;
		events::SubscriptionToken progressToken;
		events::SubscriptionToken completeToken;
		
	public:
		ExportProgressWindow();
		~ExportProgressWindow() override;

		void draw() override;
		
	};
}
