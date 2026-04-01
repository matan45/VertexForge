#include "EnvironmentWindow.hpp"
#include "AtmosphereConfigWindow.hpp"
#include "CloudConfigWindow.hpp"
#include "VolumetricFogConfigWindow.hpp"
#include "../weather/WeatherEditorWindow.hpp"
#include <imgui.h>
#include <IconsFontAwesome6.h>

namespace windows
{
    void EnvironmentWindow::setWindows(AtmosphereConfigWindow* atmo, CloudConfigWindow* cloud,
                                        VolumetricFogConfigWindow* fog, WeatherEditorWindow* weather)
    {
        atmosphereWindow = atmo;
        cloudWindow = cloud;
        fogWindow = fog;
        weatherWindow = weather;
    }

    void EnvironmentWindow::show(int tab)
    {
        visible = true;
        if (tab >= 0 && tab < COUNT)
            selectedTab = tab;
    }

    void EnvironmentWindow::draw()
    {
        if (!visible) return;

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Environment", &visible, ImGuiWindowFlags_NoCollapse))
        {
            ImVec2 contentSize = ImGui::GetContentRegionAvail();
            float tabWidth = 160.0f;

            ImGui::BeginChild("TabList", ImVec2(tabWidth, contentSize.y), true);
            drawTabList();
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("TabContent", ImVec2(0, contentSize.y), true);
            drawTabContent();
            ImGui::EndChild();
        }
        ImGui::End();
    }

    void EnvironmentWindow::drawTabList()
    {
        struct TabEntry
        {
            const char* icon;
            const char* label;
        };

        static const TabEntry tabs[] = {
            { ICON_FA_SUN,            "Atmosphere" },
            { ICON_FA_CLOUD,          "Clouds" },
            { ICON_FA_SMOG,           "Volumetric Fog" },
            { ICON_FA_CLOUD_RAIN,     "Weather" },
        };

        for (int i = 0; i < COUNT; ++i)
        {
            char label[128];
            snprintf(label, sizeof(label), "%s  %s", tabs[i].icon, tabs[i].label);

            if (ImGui::Selectable(label, selectedTab == i, 0, ImVec2(0, 24)))
                selectedTab = i;
        }
    }

    void EnvironmentWindow::drawTabContent()
    {
        switch (selectedTab)
        {
        case Atmosphere:
            if (atmosphereWindow) atmosphereWindow->drawContent();
            break;
        case Clouds:
            if (cloudWindow) cloudWindow->drawContent();
            break;
        case VolumetricFog:
            if (fogWindow) fogWindow->drawContent();
            break;
        case Weather:
            if (weatherWindow) weatherWindow->drawContent();
            break;
        default:
            ImGui::TextDisabled("Select a tab");
            break;
        }
    }
}
