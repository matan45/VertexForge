#include "ConsoleLog.hpp"
#include "print/EditorLogger.hpp"
#include "time/Timer.hpp"
#include "imgui.h"

namespace windows {
	void ConsoleLog::draw()
	{
		if (ImGui::Begin("Console")) {
			if (ImGui::Button("clear")) {
				std::lock_guard<std::mutex> lock(util::imguiConsoleBufferMutex);
				util::imguiConsoleBuffer.clear();
			}


			ImGui::SameLine();
			ImGui::Text("FPS: %.2f", engineTime::Timer::getFPS());
			ImGui::SameLine();
			ImGui::Text("Delta Time: %.4f", engineTime::Timer::getDeltaTime());

			ImGui::Separator();
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.15f, 1.0f));  // Dark blue background
			ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);

			// Display all messages from the buffer (thread-safe copy)
			std::vector<std::string> bufferCopy;
			{
				std::lock_guard<std::mutex> lock(util::imguiConsoleBufferMutex);
				bufferCopy = util::imguiConsoleBuffer;
			}
			for (const auto& logEntry : bufferCopy) {
				ImVec4 textColor;
				if (logEntry.find("ERROR:") != std::string::npos) {
					textColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red for errors
				} else if (logEntry.find("WARNING:") != std::string::npos) {
					textColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow for warnings
				} else {
					textColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);  // White for info
				}
				ImGui::PushStyleColor(ImGuiCol_Text, textColor);
				ImGui::TextUnformatted(logEntry.c_str());
				ImGui::PopStyleColor();
			}


			// Scroll to the bottom to show the latest log entry
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);

			ImGui::PopStyleColor();
			ImGui::EndChild();
		}
		ImGui::End();
	}
}
