#include "print/Log.hpp"
#include "ProjectSettingsWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ProjectEvents.hpp"
#include "../details/FontSlotWidget.hpp"
#include "launch/GameStateLocation.hpp"
#include "launch/ProjectPathRules.hpp"
#include "launch/Utf8Path.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstddef>
#include <filesystem>

namespace fs = std::filesystem;

namespace windows
{
    void ProjectSettingsWindow::show()
    {
        visible = true;
        loadFromProject();
    }

    void ProjectSettingsWindow::loadFromProject()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
        startupSceneError.clear();

        if (projectOpt)
        {
            hasProject = true;
            projectName = projectOpt->projectName;
            version = projectOpt->version;
            publisher = projectOpt->publisher;
            workingDirectory = projectOpt->workingDirectory;
            startupScene = projectOpt->startupScene;
            exeIconPath = projectOpt->exeIconPath;
            fontFallbackChain = projectOpt->fontFallbackChain;
            if (fontFallbackChain.size() > config::ProjectConfig::maxFontFallbacks)
            {
                fontFallbackChain.resize(config::ProjectConfig::maxFontFallbacks);
            }
            isDirty = false;
        }
        else
        {
            hasProject = false;
            projectName.clear();
            version.clear();
            publisher.clear();
            workingDirectory.clear();
            startupScene.clear();
            exeIconPath.clear();
            fontFallbackChain.clear();
            isDirty = false;
        }
    }

    void ProjectSettingsWindow::saveToProject()
    {
        if (!hasProject)
        {
            return;
        }

        auto& dispatcher = events::EventDispatcher::instance();
        auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});

        if (!projectOpt)
        {
            vfLogError("Failed to get current project for saving");
            return;
        }

        config::ProjectConfig updatedConfig = *projectOpt;
        updatedConfig.projectName = projectName;
        updatedConfig.version = version;
        updatedConfig.publisher = publisher;
        updatedConfig.startupScene = startupScene;
        updatedConfig.exeIconPath = exeIconPath;
        updatedConfig.fontFallbackChain.clear();
        for (const auto& path : fontFallbackChain)
        {
            if (!path.empty() &&
                updatedConfig.fontFallbackChain.size() < config::ProjectConfig::maxFontFallbacks)
            {
                updatedConfig.fontFallbackChain.push_back(path);
            }
        }

        events::project::UpdateProjectConfigCommand updateCmd;
        updateCmd.config = updatedConfig;
        if (!dispatcher.execute(updateCmd))
        {
            vfLogError("Failed to update project configuration");
            return;
        }

        events::project::SaveProjectCommand saveCmd;
        saveCmd.filePath = "";
        if (!dispatcher.execute(saveCmd))
        {
            vfLogError("Failed to save project");
            return;
        }

        isDirty = false;
        vfLogInfo("Project settings saved");
    }

    bool ProjectSettingsWindow::validateStartupScene() const
    {
        if (startupScene.empty())
        {
            return false;
        }

        fs::path fullPath = fs::path(workingDirectory) / startupScene;
        return fs::exists(fullPath);
    }

    bool ProjectSettingsWindow::validateExeIcon() const
    {
        if (exeIconPath.empty())
        {
            return true;
        }

        fs::path fullPath = fs::path(workingDirectory) / exeIconPath;
        return fs::exists(fullPath);
    }

    void ProjectSettingsWindow::draw()
    {
        if (!visible)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(500, 650), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Project Settings", &visible))
        {
            drawContent();
        }
        ImGui::End();
    }

    void ProjectSettingsWindow::drawContent()
    {
        if (!hasProject)
        {
            loadFromProject();
        }

        if (!hasProject)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "No project loaded");
            ImGui::TextWrapped("Load a project file (.vfproj) to edit project settings.");
            return;
        }

        ImGui::Text("Project Name");
        ImGui::PushItemWidth(-1);
        char nameBuffer[256];
        std::strncpy(nameBuffer, projectName.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        if (ImGui::InputText("##ProjectName", nameBuffer, sizeof(nameBuffer)))
        {
            projectName = nameBuffer;
            isDirty = true;
        }
        ImGui::PopItemWidth();
        if (projectName.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Project name is required");
        }

        ImGui::Spacing();

        ImGui::Text("Version");
        ImGui::PushItemWidth(-1);
        char versionBuffer[64];
        std::strncpy(versionBuffer, version.c_str(), sizeof(versionBuffer) - 1);
        versionBuffer[sizeof(versionBuffer) - 1] = '\0';
        if (ImGui::InputText("##Version", versionBuffer, sizeof(versionBuffer)))
        {
            version = versionBuffer;
            isDirty = true;
        }
        ImGui::PopItemWidth();
        if (version.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Version is required");
        }

        ImGui::Spacing();

        // VK-1726: a shipped game keeps its settings, saves, logs and crash reports in
        // %LOCALAPPDATA%\<Publisher>\<Project Name> (a portable export: beside the game instead).
        ImGui::Text("Publisher (optional)");
        ImGui::PushItemWidth(-1);
        char publisherBuffer[256];
        std::strncpy(publisherBuffer, publisher.c_str(), sizeof(publisherBuffer) - 1);
        publisherBuffer[sizeof(publisherBuffer) - 1] = '\0';
        if (ImGui::InputText("##Publisher", publisherBuffer, sizeof(publisherBuffer)))
        {
            publisher = publisherBuffer;
            isDirty = true;
        }
        ImGui::PopItemWidth();
        {
            const launch::GameFolderName folder = launch::gameStateFolderName(projectName, publisher);
            const std::string preview = "%LOCALAPPDATA%\\" + launch::pathToUtf8(folder.relative);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Shipped game state: %s", preview.c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Where an exported game keeps its settings, saves, logs and crash reports.\n"
                                  "A portable build (Export window) keeps them beside the game instead.");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Working Directory");
        ImGui::PushItemWidth(-1);
        ImGui::BeginDisabled();
        char workDirBuffer[512];
        std::strncpy(workDirBuffer, workingDirectory.c_str(), sizeof(workDirBuffer) - 1);
        workDirBuffer[sizeof(workDirBuffer) - 1] = '\0';
        ImGui::InputText("##WorkingDirectory", workDirBuffer, sizeof(workDirBuffer));
        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(read-only)");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Startup Scene");
        ImGui::PushID("StartupScene");
        {
            std::string displayName = startupScene.empty()
                ? "(Not set)"
                : fs::path(startupScene).filename().string();

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 160);
            char sceneBuffer[256];
            std::strncpy(sceneBuffer, displayName.c_str(), sizeof(sceneBuffer) - 1);
            sceneBuffer[sizeof(sceneBuffer) - 1] = '\0';
            ImGui::InputText("##ScenePath", sceneBuffer, sizeof(sceneBuffer), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();

            ImGui::SameLine();
            if (ImGui::Button("Browse...", ImVec2(75, 0)))
            {
                std::vector<std::pair<std::wstring, std::wstring>> filters = {
                    {L"VF Scene Files (*.vfScene)", L"*.vfScene"}
                };
                std::string path = fileDialog.openFileDialog(filters);
                if (!path.empty())
                {
                    // VK-1687: the startup scene is stored relative to the working directory, so
                    // the project still finds it after a move, and the exporter packs it as
                    // "Assets/<startupScene>". A scene outside the working directory is refused.
                    const fs::path selectedPath(path);
                    if (const std::optional<fs::path> relative =
                            launch::relativeInside(fs::path(workingDirectory), selectedPath))
                    {
                        startupScene = relative->generic_string();
                        startupSceneError.clear();
                        isDirty = true;
                    }
                    else
                    {
                        startupSceneError = "The startup scene must be inside the project's working directory";
                    }
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Clear", ImVec2(55, 0)))
            {
                startupScene.clear();
                startupSceneError.clear();
                isDirty = true;
            }
        }
        ImGui::PopID();

        if (!startupSceneError.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", startupSceneError.c_str());
        }
        if (!validateStartupScene())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Scene file not found");
        }

        ImGui::Spacing();

        ImGui::Text("Executable Icon (optional)");
        ImGui::PushID("ExeIcon");
        {
            std::string displayName = exeIconPath.empty()
                ? "(Not set)"
                : fs::path(exeIconPath).filename().string();

            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - 160);
            char iconBuffer[256];
            std::strncpy(iconBuffer, displayName.c_str(), sizeof(iconBuffer) - 1);
            iconBuffer[sizeof(iconBuffer) - 1] = '\0';
            ImGui::InputText("##IconPath", iconBuffer, sizeof(iconBuffer), ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();

            ImGui::SameLine();
            if (ImGui::Button("Browse...", ImVec2(75, 0)))
            {
                std::vector<std::pair<std::wstring, std::wstring>> filters = {
                    {L"Icon Files (*.ico)", L"*.ico"}
                };
                std::string path = fileDialog.openFileDialog(filters);
                if (!path.empty())
                {
                    // VK-1687: inside the working directory the icon is stored relative to it;
                    // outside it keeps its absolute path (the exporter reads either).
                    const fs::path selectedPath(path);
                    if (const std::optional<fs::path> relative =
                            launch::relativeInside(fs::path(workingDirectory), selectedPath))
                    {
                        exeIconPath = relative->generic_string();
                    }
                    else
                    {
                        exeIconPath = selectedPath.string();
                    }
                    isDirty = true;
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Clear", ImVec2(55, 0)))
            {
                exeIconPath.clear();
                isDirty = true;
            }
        }
        ImGui::PopID();

        if (!validateExeIcon())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Icon file not found");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Font Fallbacks (optional)");
        ImGui::TextWrapped(
            "Fonts are tried in this order when the selected font lacks a glyph.");

        for (size_t i = 0; i < fontFallbackChain.size();)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::Text("Fallback %zu", i + 1);

            details::FontSlotOptions options;
            options.emptyLabel = "Select a project font";
            options.emptyTooltip =
                "Choose an imported .vfFont inside this project.";
            options.clearButtonLabel = "Remove";
            options.clearTooltip = "Remove this font from the fallback chain.";

            if (details::drawProjectFontSlot(
                    fontFallbackChain[i], workingDirectory, "ProjectFallback", options))
            {
                isDirty = true;

                // Only an ACTIVE clear removes the row - drawProjectFontSlot returns
                // true and empties the string when Remove is pressed. A row that
                // "Add Font Fallback" just appended is also empty, but unchanged, and
                // has to survive until the user picks a font into it. applyChanges
                // drops any still-empty entry, so an unfilled row is never saved.
                if (fontFallbackChain[i].empty())
                {
                    fontFallbackChain.erase(
                        fontFallbackChain.begin() + static_cast<std::ptrdiff_t>(i));
                    ImGui::PopID();
                    continue;
                }
            }

            const bool isFirst = i == 0;
            if (isFirst)
            {
                ImGui::BeginDisabled();
            }
            // The i > 0 test is what makes the swap safe; BeginDisabled only hides
            // the button, and an unsigned i - 1 would wrap.
            if (ImGui::Button("Move Up") && i > 0)
            {
                std::swap(fontFallbackChain[i], fontFallbackChain[i - 1]);
                isDirty = true;
            }
            if (isFirst)
            {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            const bool isLast = i + 1 == fontFallbackChain.size();
            if (isLast)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Move Down") && !isLast)
            {
                std::swap(fontFallbackChain[i], fontFallbackChain[i + 1]);
                isDirty = true;
            }
            if (isLast)
            {
                ImGui::EndDisabled();
            }

            ImGui::Separator();
            ImGui::PopID();
            ++i;
        }

        if (fontFallbackChain.size() < config::ProjectConfig::maxFontFallbacks)
        {
            if (ImGui::Button("Add Font Fallback"))
            {
                fontFallbackChain.emplace_back();
                isDirty = true;
            }
        }
        ImGui::TextDisabled("Built-in Default Font (implicit final fallback)");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool canSave = !projectName.empty() && !version.empty() && validateStartupScene();

        if (!canSave)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Save", ImVec2(80, 0)))
        {
            saveToProject();
        }

        if (!canSave)
        {
            ImGui::EndDisabled();
        }

        if (isDirty)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "(Modified)");
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Save writes these settings into the project configuration file (.vfproj).");
    }
}
