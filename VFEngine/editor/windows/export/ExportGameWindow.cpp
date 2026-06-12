#include "ExportGameWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorSettingsEvents.hpp"
#include "events/project/ExportEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include <imgui.h>

namespace windows
{
	void ExportGameWindow::show()
	{
		loadFromPreferences();

		auto& dispatcher = events::EventDispatcher::instance();
		if (auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{}))
		{
			gameName = projectOpt->projectName;
			gameVersion = projectOpt->version;
		}

		visible = true;
	}

	void ExportGameWindow::loadFromPreferences()
	{
		auto prefs = events::EventDispatcher::instance().query(events::editor::GetEditorSettingsQuery{});
		outputDirectory = prefs.exportSettings.lastOutputDirectory;
		cleanBuild = prefs.exportSettings.cleanBuild;
		verifyIntegrity = prefs.exportSettings.verifyIntegrity;
		buildScripts = prefs.exportSettings.buildScripts;
	}

	void ExportGameWindow::saveToPreferences() const
	{
		auto& dispatcher = events::EventDispatcher::instance();
		auto prefs = dispatcher.query(events::editor::GetEditorSettingsQuery{});
		prefs.exportSettings.lastOutputDirectory = outputDirectory;
		prefs.exportSettings.cleanBuild = cleanBuild;
		prefs.exportSettings.verifyIntegrity = verifyIntegrity;
		prefs.exportSettings.buildScripts = buildScripts;

		events::editor::SetEditorSettingsCommand setCmd;
		setCmd.settings = prefs;
		dispatcher.execute(setCmd);
		dispatcher.execute(events::editor::SaveEditorSettingsCommand{});
	}

	void ExportGameWindow::startExport()
	{
		saveToPreferences();

		events::gameExport::ExportGameCommand cmd;
		cmd.outputDirectory = outputDirectory;
		cmd.cleanBuild = cleanBuild;
		cmd.verifyIntegrity = verifyIntegrity;
		cmd.buildScripts = buildScripts;
		events::EventDispatcher::instance().execute(cmd);

		visible = false;
	}

	void ExportGameWindow::draw()
	{
		if (!visible) return;

		ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Export Game", &visible, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::Text("Game: %s", gameName.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("v%s", gameVersion.c_str());
			ImGui::Separator();
			ImGui::Spacing();

			ImGui::Text("Output Directory");
			char pathBuffer[1024];
			snprintf(pathBuffer, sizeof(pathBuffer), "%s", outputDirectory.c_str());
			ImGui::SetNextItemWidth(-90.0f);
			if (ImGui::InputText("##exportOutputDir", pathBuffer, sizeof(pathBuffer)))
			{
				outputDirectory = pathBuffer;
			}
			ImGui::SameLine();
			if (ImGui::Button("Browse...", ImVec2(-1.0f, 0.0f)))
			{
				std::string selected = fileDialog.selectFolderDialog();
				if (!selected.empty())
				{
					outputDirectory = selected;
				}
			}

			ImGui::Spacing();
			ImGui::SeparatorText("Options");

			ImGui::Checkbox("Build scripts before export", &buildScripts);
			ImGui::SetItemTooltip("Compile mType scripts (scripts.mtcLib) before packing.\nIf the build fails, the export is aborted.");

			ImGui::Checkbox("Clean build", &cleanBuild);
			ImGui::SetItemTooltip("Ignore the previous export manifest and recompile/repack everything.\nSlower, but guarantees no stale cached output.");

			ImGui::Checkbox("Verify archive integrity", &verifyIntegrity);
			ImGui::SetItemTooltip("After packing, re-read every archive entry and check its content hash.");

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			bool canExport = !outputDirectory.empty();
			ImGui::BeginDisabled(!canExport);
			if (ImGui::Button("Export", ImVec2(120, 0)))
			{
				startExport();
			}
			ImGui::EndDisabled();
			if (!canExport && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip("Choose an output directory first");
			}

			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120, 0)))
			{
				visible = false;
			}
		}
		ImGui::End();
	}
}
