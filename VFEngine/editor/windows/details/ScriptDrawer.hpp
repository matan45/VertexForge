#pragma once
#include "data/EntityHandle.hpp"
#include <string>
#include <vector>

namespace windows::details
{
    class ScriptDrawer
    {
    public:
        bool draw(services::EntityHandle handle);

    private:
        bool drawHeader(size_t scriptCount, bool& outRemoveAll);
        std::string drawScriptEntry(services::EntityHandle handle,
                                    const std::string& scriptPath,
                                    int index);
        void drawAddScriptButton(services::EntityHandle handle);
        void handleScriptRemoval(services::EntityHandle handle,
                                 const std::string& scriptToRemove,
                                 bool removeAll,
                                 const std::vector<std::string>& scriptPaths);
    };
}
