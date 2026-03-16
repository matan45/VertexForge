#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/ActionMappingTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "nfd/FileDialog.hpp"
#include <vector>
#include <string>

namespace windows
{
    class InputActionMappingWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        InputActionMappingWindow();
        ~InputActionMappingWindow() override;

        void draw() override;
        void show() { visible = true; needsRefresh = true; }

    private:
        struct ActionEntry
        {
            std::string name;
            std::string context;
            std::vector<services::InputBinding> bindings;
        };

        struct Axis1DEntry
        {
            std::string name;
            std::string positiveAction;
            std::string negativeAction;
        };

        struct Axis2DEntry
        {
            std::string name;
            std::string upAction;
            std::string downAction;
            std::string leftAction;
            std::string rightAction;
            bool normalize = true;
        };

        bool visible = false;
        bool needsRefresh = true;
        events::SubscriptionToken mappingChangedToken;
        bool waitingForKey = false;
        int captureActionIndex = -1;
        char newActionName[128] = {};
        char newAxis1DName[128] = {};
        char newAxis2DName[128] = {};
        char newContextName[128] = {};
        std::string selectedContextFilter = "All";
        nfd::FileDialog fileDialog;
        std::vector<ActionEntry> entries;
        std::vector<Axis1DEntry> axis1DEntries;
        std::vector<Axis2DEntry> axis2DEntries;
        std::vector<std::string> actionNames;
        std::vector<std::string> contextNames;

        void refresh();
        void drawActionEntry(int index);
        void drawAxis1DSection();
        void drawAxis2DSection();
        void drawContextSection();
        void drawContextFilter();
        bool drawActionCombo(const char* label, std::string& current);
        bool drawContextCombo(const char* label, std::string& current);
        std::string getKeyName(int keyCode) const;
        std::string getMouseButtonName(int button) const;
        std::string getBindingDisplayName(const services::InputBinding& binding) const;
    };
}
