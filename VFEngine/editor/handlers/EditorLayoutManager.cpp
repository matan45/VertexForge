#include "EditorLayoutManager.hpp"
#include "resource/PathResolver.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace handlers
{
    // The engine's default editor layout ships as an ImGui ini under
    // resources/editor/. It is applied on first launch and by the Preferences >
    // Window Layout "Reset to Default Layout" button. Capturing it as a full ini
    // (rather than building docks programmatically) lets the layout be hand-tuned
    // and re-exported without touching code. The DockSpace ID inside the file
    // corresponds to ImGui::GetID("MyDockSpace") within the "Vulkan Engine" main
    // window (see MainImguiWindow.cpp), so it binds to the live runtime dockspace.
    void EditorLayoutManager::buildDefaultLayout(ImGuiID /*dockId*/)
    {
        const std::string path =
            resource::PathResolver::resolveEnginePath("../../resources/editor/default_layout.ini");

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return;

        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        if (content.empty()) return;

        ImGui::LoadIniSettingsFromMemory(content.c_str(), content.size());
    }

    void EditorLayoutManager::saveLayout(const std::string& name)
    {
        for (char c : name)
        {
            if (c == '/' || c == '\\' || c == '.' || c == ':') return;
        }

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
