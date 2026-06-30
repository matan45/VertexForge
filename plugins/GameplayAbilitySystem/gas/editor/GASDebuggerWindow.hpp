#pragma once

// Gameplay Ability System (VK-816) — runtime debugger. The plugin forwards every
// gas.* PluginEventBus event here (record()); the window shows a rolling event
// log plus the latest attribute values seen per entity.

#include "imguiHandler/ImguiWindow.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <deque>
#include <map>
#include <string>

namespace gas
{
    class GASDebuggerWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        // Called by the plugin for each gas.* event (off the PluginEventBus).
        void record(const std::string& name, const nlohmann::json& data)
        {
            std::string line = name;
            const int entity = data.value("entity", -1);
            if (entity >= 0) line += "  e=" + std::to_string(entity);
            if (data.contains("ability")) line += "  ability=" + data.value("ability", std::string());
            if (data.contains("attribute"))
            {
                const std::string attr = data.value("attribute", std::string());
                const float nv = data.value("newValue", 0.0f);
                line += "  " + attr + ": " + fmt(data.value("oldValue", 0.0f)) + " -> " + fmt(nv);
                if (entity >= 0) attrs[entity][attr] = nv;
            }
            if (data.contains("handle")) line += "  handle=" + std::to_string(data.value("handle", 0));
            if (data.contains("status")) line += "  status=" + std::to_string(data.value("status", 0));
            if (data.contains("reason")) line += "  reason=" + data.value("reason", std::string());

            log.push_back(line);
            while (log.size() > 300) log.pop_front();
        }

        void draw() override
        {
            if (!ImGui::Begin("GAS Debugger"))
            {
                ImGui::End();
                return;
            }

            if (ImGui::Button("Clear"))
            {
                log.clear();
                attrs.clear();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%d events", static_cast<int>(log.size()));

            if (ImGui::CollapsingHeader("Attributes (latest per entity)", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (attrs.empty())
                    ImGui::TextDisabled("No attribute changes yet — enter Play and activate an ability.");
                for (const auto& [entity, values] : attrs)
                {
                    ImGui::Text("Entity %d", entity);
                    ImGui::Indent();
                    for (const auto& [attrName, value] : values)
                        ImGui::BulletText("%s = %.2f", attrName.c_str(), value);
                    ImGui::Unindent();
                }
            }

            ImGui::SeparatorText("Event Log");
            ImGui::BeginChild("gas_event_log", ImVec2(0, 0), ImGuiChildFlags_Borders);
            for (const auto& l : log)
                ImGui::TextUnformatted(l.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();

            ImGui::End();
        }

    private:
        static std::string fmt(float v)
        {
            char b[32];
            std::snprintf(b, sizeof(b), "%.2f", v);
            return b;
        }

        std::deque<std::string> log;
        std::map<int, std::map<std::string, float>> attrs;
    };
}
