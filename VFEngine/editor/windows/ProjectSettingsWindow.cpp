#include "print/Log.hpp"
#include "ProjectSettingsWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ProjectEvents.hpp"
#include <imgui.h>
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

        if (projectOpt)
        {
            hasProject = true;
            projectName = projectOpt->projectName;
            version = projectOpt->version;
            workingDirectory = projectOpt->workingDirectory;
            startupScene = projectOpt->startupScene;
            exeIconPath = projectOpt->exeIconPath;
            isDirty = false;
        }
        else
        {
            hasProject = false;
            projectName.clear();
            version.clear();
            workingDirectory.clear();
            startupScene.clear();
            exeIconPath.clear();
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
        updatedConfig.startupScene = startupScene;
        updatedConfig.exeIconPath = exeIconPath;

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

        ImGui::SetNextWindowSize(ImVec2(450, 350), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Project Settings", &visible))
        {
            if (!hasProject)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "No project loaded");
                ImGui::TextWrapped("Load a project file (.vfproj) to edit project settings.");
                ImGui::End();
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
                        fs::path selectedPath(path);
                        fs::path workDir(workingDirectory);

                        if (selectedPath.string().find(workDir.string()) == 0)
                        {
                            startupScene = fs::relative(selectedPath, workDir).string();
                        }
                        else
                        {
                            startupScene = selectedPath.filename().string();
                        }
                        isDirty = true;
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("Clear", ImVec2(55, 0)))
                {
                    startupScene.clear();
                    isDirty = true;
                }
            }
            ImGui::PopID();

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
                        fs::path selectedPath(path);
                        fs::path workDir(workingDirectory);

                        if (selectedPath.string().find(workDir.string()) == 0)
                        {
                            exeIconPath = fs::relative(selectedPath, workDir).string();
                        }
                        else
                        {
                            exeIconPath = selectedPath.filename().string();
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
        }
        ImGui::End();
    }
}
