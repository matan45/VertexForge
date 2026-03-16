#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/ActionMappingTypes.hpp"
#include "nfd/FileDialog.hpp"
#include <vector>
#include <string>

namespace windows
{
    class InputActionMappingWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        InputActionMappingWindow() = default;
        ~InputActionMappingWindow() override = default;

        void draw() override;
        void show() { visible = true; needsRefresh = true; }

    private:
        struct ActionEntry
        {
            std::string name;
            std::vector<services::InputBinding> bindings;
            std::vector<services::InputBinding> defaultBindings;
            bool isModified = false;
        };

        bool visible = false;
        bool needsRefresh = true;
        bool waitingForKey = false;
        int captureActionIndex = -1;
        char newActionName[128] = {};
        nfd::FileDialog fileDialog;
        std::vector<ActionEntry> entries;

        void refresh();
        void drawActionEntry(int index);
        const char* getKeyName(int keyCode) const;
        const char* getMouseButtonName(int button) const;
        std::string getBindingDisplayName(const services::InputBinding& binding) const;
    };
}
