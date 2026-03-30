#include "EditorLayoutManager.hpp"
#include <imgui_internal.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace handlers
{
    void EditorLayoutManager::buildDefaultLayout(ImGuiID dockId)
    {
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderSetNodeSize(dockId, viewport->WorkSize);

        // Split left panel (20%) for SceneGraph
        ImGuiID leftId, centerId;
        ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.20f, &leftId, &centerId);

        // Split right panel (25% of remaining) for Details/Inspector
        ImGuiID rightId, middleId;
        ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.25f, &rightId, &middleId);

        // Split bottom panel (25% of middle) for Console + Content Browser
        ImGuiID bottomId, viewportId;
        ImGui::DockBuilderSplitNode(middleId, ImGuiDir_Down, 0.25f, &bottomId, &viewportId);

        // Dock windows
        ImGui::DockBuilderDockWindow("SceneGraph", leftId);
        ImGui::DockBuilderDockWindow("Folder Structure", leftId);
        ImGui::DockBuilderDockWindow("ViewPort", viewportId);
        ImGui::DockBuilderDockWindow("Details", rightId);
        ImGui::DockBuilderDockWindow("Console", bottomId);
        ImGui::DockBuilderDockWindow("Content Folder", bottomId);

        ImGui::DockBuilderFinish(dockId);
    }

    void EditorLayoutManager::saveLayout(const std::string& name)
    {
        std::string layoutsDir = getLayoutsPath();
        if (layoutsDir.empty()) return;

        std::filesystem::create_directories(layoutsDir);

        std::string destPath = layoutsDir + "/" + name + ".ini";

        // Get current settings from memory (always up-to-date)
        size_t settingsSize = 0;
        const char* settingsData = ImGui::SaveIniSettingsToMemory(&settingsSize);

        // Write to file
        std::ofstream file(destPath, std::ios::binary);
        if (file.is_open())
        {
            file.write(settingsData, settingsSize);
        }
    }

    void EditorLayoutManager::loadLayout(const std::string& name)
    {
        std::string layoutsDir = getLayoutsPath();
        if (layoutsDir.empty()) return;

        std::string srcPath = layoutsDir + "/" + name + ".ini";
        if (!std::filesystem::exists(srcPath)) return;

        // Read the ini file content
        std::ifstream file(srcPath);
        if (!file.is_open()) return;
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        file.close();

        // Load from memory so ImGui applies it immediately
        ImGui::LoadIniSettingsFromMemory(content.c_str(), content.size());

        // Mark settings as loaded so ImGui applies docking layout
        ImGuiContext& g = *ImGui::GetCurrentContext();
        g.SettingsLoaded = true;
    }

    void EditorLayoutManager::resetLayout(ImGuiID dockId)
    {
        buildDefaultLayout(dockId);
    }

    std::vector<std::string> EditorLayoutManager::getSavedLayouts()
    {
        std::vector<std::string> layouts;
        std::string layoutsDir = getLayoutsPath();
        if (layoutsDir.empty() || !std::filesystem::exists(layoutsDir)) return layouts;

        for (const auto& entry : std::filesystem::directory_iterator(layoutsDir))
        {
            if (entry.path().extension() == ".ini")
                layouts.push_back(entry.path().stem().string());
        }

        return layouts;
    }

    std::string EditorLayoutManager::getLayoutsPath()
    {
        const char* home = std::getenv("USERPROFILE");
        if (!home)
            home = std::getenv("HOME");
        if (!home)
            return "";

        return std::string(home) + "/.vertexforge/layouts";
    }
}
