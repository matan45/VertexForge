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
				showWindow.store(true);
				currentProgress.store(0.0f);
				exportFinished.store(false);
				exportSuccess = false;
				exportError.clear();
				exportWarnings.clear();
				exportOutputPath.clear();
				{
					std::lock_guard<std::mutex> lock(stepMutex);
					currentStep = "Starting export...";
				}
			});

		progressToken = dispatcher.subscribe<events::gameExport::ExportProgressNotification>(
			[this](const events::gameExport::ExportProgressNotification& notif) {
				currentProgress.store(notif.progress);
				{
					std::lock_guard<std::mutex> lock(stepMutex);
					currentStep = notif.currentStep;
				}
			});

		completeToken = dispatcher.subscribe<events::gameExport::ExportCompletedNotification>(
			[this](const events::gameExport::ExportCompletedNotification& notif) {
				currentProgress.store(1.0f);
				exportFinished.store(true);
				exportSuccess = notif.success;
				exportError = notif.errorMessage;
				exportWarnings = notif.warnings;
				exportOutputPath = notif.outputPath;
				{
					std::lock_guard<std::mutex> lock(stepMutex);
					currentStep = notif.success ? "Export complete!" : "Export failed";
				}
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

		ImGui::SetNextWindowSize(ImVec2(450, 250), ImGuiCond_Always);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
		if (ImGui::Begin("Exporting Game...", &windowOpen, flags))
		{
			// Current step
			std::string step;
			{
				std::lock_guard<std::mutex> lock(stepMutex);
				step = currentStep;
			}
			ImGui::Text("Status: %s", step.c_str());
			ImGui::Spacing();

			// Progress bar
			ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
			ImGui::Spacing();

			if (finished)
			{
				ImGui::Separator();
				ImGui::Spacing();

				if (exportSuccess)
				{
					ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Export successful!");
					ImGui::Spacing();

					if (!exportWarnings.empty())
					{
						ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Warnings:");
						for (const auto& warning : exportWarnings)
						{
							ImGui::BulletText("%s", warning.c_str());
						}
						ImGui::Spacing();
					}

#ifdef _WIN32
					if (ImGui::Button("Open Output Folder", ImVec2(-1.0f, 0.0f)))
					{
						ShellExecuteA(nullptr, "explore", exportOutputPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
#endif
				}
				else
				{
					ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Export failed!");
					ImGui::Spacing();
					ImGui::TextWrapped("%s", exportError.c_str());
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
