#pragma once
#include <imgui.h>
#include <string>
#include <vector>

namespace handlers
{
    class EditorLayoutManager
    {
    public:
        static void buildDefaultLayout(ImGuiID dockId);
        static void saveLayout(const std::string& name);
        static void loadLayout(const std::string& name);
        static void resetLayout(ImGuiID dockId);
        static std::vector<std::string> getSavedLayouts();

    private:
        static std::string getLayoutsPath();
    };
}
