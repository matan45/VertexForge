#include "ExportProgressWindow.hpp"
#include <imgui.h>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#endif

namespace windows
{
	ExportProgressWindow::ExportProgressWindow()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		startToken = dispatcher.subscribe<events::gameExport::ExportStartedNotification>(
			[this](const events::gameExport::ExportStartedNotification&) {
				{
					std::lock_guard<std::mutex> lock(dataMutex);
					currentStep = "Starting export...";
					exportSuccess = false;
					exportError.clear();
					exportWarnings.clear();
					exportOutputPath.clear();
				}
				currentProgress.store(0.0f);
				exportFinished.store(false);
				showWindow.store(true);
			});

		progressToken = dispatcher.subscribe<events::gameExport::ExportProgressNotification>(
			[this](const events::gameExport::ExportProgressNotification& notif) {
				currentProgress.store(notif.progress);
				{
					std::lock_guard<std::mutex> lock(dataMutex);
					currentStep = notif.currentStep;
				}
			});

		completeToken = dispatcher.subscribe<events::gameExport::ExportCompletedNotification>(
			[this](const events::gameExport::ExportCompletedNotification& notif) {
				{
					std::lock_guard<std::mutex> lock(dataMutex);
					currentStep = notif.success ? "Export complete!" : "Export failed";
					exportSuccess = notif.success;
					exportError = notif.errorMessage;
					exportWarnings = notif.warnings;
					exportOutputPath = notif.outputPath;
				}
				currentProgress.store(1.0f);
				exportFinished.store(true);
			});
	}

	ExportProgressWindow::~ExportProgressWindow()
	{
		auto& dispatcher = events::EventDispatcher::instance();
		dispatcher.unsubscribe(startToken);
		dispatcher.unsubscribe(progressToken);
		dispatcher.unsubscribe(completeToken);
	}

	void ExportProgressWindow::draw()
	{
		if (!showWindow.load()) return;

		bool windowOpen = true;
		float progress = currentProgress.load();
		bool finished = exportFinished.load();

		std::string step;
		bool success = false;
		std::string error;
		std::vector<std::string> warnings;
		std::string outputPath;
		{
			std::lock_guard<std::mutex> lock(dataMutex);
			step = currentStep;
			if (finished)
			{
				success = exportSuccess;
				error = exportError;
				warnings = exportWarnings;
				outputPath = exportOutputPath;
			}
		}

		ImGui::SetNextWindowSize(ImVec2(450, 250), ImGuiCond_FirstUseEver);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin("Exporting Game...", &windowOpen, flags))
		{
			ImGui::Text("Status: %s", step.c_str());
			ImGui::Spacing();

			ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
			ImGui::Spacing();

			if (finished)
			{
				ImGui::Separator();
				ImGui::Spacing();

				if (success)
				{
					ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Export successful!");
					ImGui::Spacing();

					if (!warnings.empty())
					{
						ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Warnings:");
						for (const auto& warning : warnings)
						{
							ImGui::BulletText("%s", warning.c_str());
						}
						ImGui::Spacing();
					}

#ifdef _WIN32
					if (ImGui::Button("Open Output Folder", ImVec2(-1.0f, 0.0f)))
					{
						ShellExecuteA(nullptr, "explore", outputPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
#endif
				}
				else
				{
					ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Export failed!");
					ImGui::Spacing();
					ImGui::TextWrapped("%s", error.c_str());
				}

				ImGui::Spacing();
				if (ImGui::Button("Close", ImVec2(-1.0f, 0.0f)))
				{
					windowOpen = false;
				}
			}
		}
		ImGui::End();

		if (!windowOpen)
		{
			showWindow.store(false);
		}
	}
}
