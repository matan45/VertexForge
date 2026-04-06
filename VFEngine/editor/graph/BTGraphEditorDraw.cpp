#include "BTGraphEditor.hpp"

namespace ed = ax::NodeEditor;
using namespace behaviortree;

namespace editor::graph
{
    static void drawPinIcon(bool isConnected, ImU32 color)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float size = 10.0f;
        ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);

        if (isConnected)
        {
            drawList->AddCircleFilled(center, size * 0.5f, color);
        }
        else
        {
            drawList->AddCircle(center, size * 0.5f, color, 12, 2.0f);
        }

        ImGui::Dummy(ImVec2(size, size));
    }

    static bool isPinConnected(const BTGraph* graph, uint32_t nodeId, bool isInput)
    {
        for (const auto& link : graph->links)
        {
            if (isInput && link.targetNodeId == nodeId) return true;
            if (!isInput && link.sourceNodeId == nodeId) return true;
        }
        return false;
    }

    void BTGraphEditor::drawNode(BTNode& node)
    {
        ImU32 nodeColor = getNodeColor(node.type);

        ed::PushStyleColor(ed::StyleColor_NodeBg, ImGui::ColorConvertU32ToFloat4(nodeColor));
        ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.78f, 0.78f, 0.78f, 0.39f));

        ed::BeginNode(toEditorNodeId(node.id));

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[%s]", getCategoryName(node.type));
        ImGui::Text("%s", node.name.empty() ? nodeTypeToString(node.type) : node.name.c_str());

        ImGui::PopStyleColor();

        ImGui::Spacing();

        if (!isRootNode(node.type))
        {
            bool connected = isPinConnected(currentGraph, node.id, true);
            ed::BeginPin(toInputPinId(node.id), ed::PinKind::Input);
            drawPinIcon(connected, IM_COL32(220, 220, 220, 255));
            ImGui::SameLine();
            ImGui::Text("In");
            ed::EndPin();
        }

        if (hasOutputPin(node.type))
        {
            bool connected = isPinConnected(currentGraph, node.id, false);
            ed::BeginPin(toOutputPinId(node.id), ed::PinKind::Output);
            ImGui::Text("Out");
            ImGui::SameLine();
            drawPinIcon(connected, IM_COL32(220, 220, 220, 255));
            ed::EndPin();
        }

        drawNodeInlineProperties(node);

        ed::EndNode();

        ed::PopStyleColor(2);
    }

    void BTGraphEditor::drawLinks()
    {
        for (const auto& link : currentGraph->links)
        {
            ed::Link(toEditorLinkId(link.id),
                      toOutputPinId(link.sourceNodeId),
                      toInputPinId(link.targetNodeId),
                      ImVec4(0.8f, 0.8f, 0.8f, 1.0f), 2.0f);
        }
    }

    void BTGraphEditor::drawNodeInlineProperties(const BTNode& node)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 180, 180, 255));

        switch (node.type)
        {
        case BTNodeType::Wait:
        {
            float duration = 1.0f;
            auto it = node.properties.find("duration");
            if (it != node.properties.end() && std::holds_alternative<float>(it->second))
                duration = std::get<float>(it->second);
            ImGui::Text("Duration: %.1fs", duration);
            break;
        }
        case BTNodeType::Log:
        {
            auto it = node.properties.find("message");
            if (it != node.properties.end() && std::holds_alternative<std::string>(it->second))
            {
                const auto& msg = std::get<std::string>(it->second);
                if (msg.length() > 20)
                    ImGui::Text("\"%.17s...\"", msg.c_str());
                else
                    ImGui::Text("\"%s\"", msg.c_str());
            }
            break;
        }
        case BTNodeType::MoveTo:
        {
            auto it = node.properties.find("targetKey");
            if (it != node.properties.end() && std::holds_alternative<std::string>(it->second))
                ImGui::Text("Key: %s", std::get<std::string>(it->second).c_str());
            break;
        }
        case BTNodeType::PlayAnimation:
        {
            auto it = node.properties.find("stateName");
            if (it != node.properties.end() && std::holds_alternative<std::string>(it->second))
                ImGui::Text("State: %s", std::get<std::string>(it->second).c_str());
            break;
        }
        case BTNodeType::Parallel:
        {
            auto it = node.properties.find("policy");
            if (it != node.properties.end() && std::holds_alternative<std::string>(it->second))
                ImGui::Text("Policy: %s", std::get<std::string>(it->second).c_str());
            break;
        }
        case BTNodeType::Repeater:
        {
            int count = 1;
            auto it = node.properties.find("repeatCount");
            if (it != node.properties.end() && std::holds_alternative<int32_t>(it->second))
                count = std::get<int32_t>(it->second);
            ImGui::Text("Count: %d", count);
            break;
        }
        case BTNodeType::Cooldown:
        {
            float time = 1.0f;
            auto it = node.properties.find("cooldownTime");
            if (it != node.properties.end() && std::holds_alternative<float>(it->second))
                time = std::get<float>(it->second);
            ImGui::Text("Cooldown: %.1fs", time);
            break;
        }
        case BTNodeType::TimeLimit:
        {
            float time = 5.0f;
            auto it = node.properties.find("timeLimit");
            if (it != node.properties.end() && std::holds_alternative<float>(it->second))
                time = std::get<float>(it->second);
            ImGui::Text("Limit: %.1fs", time);
            break;
        }
        case BTNodeType::ScriptTask:
        {
            if (!node.scriptClassName.empty())
                ImGui::Text("Class: %s", node.scriptClassName.c_str());
            break;
        }
        case BTNodeType::CheckBlackboardValue:
        {
            auto keyIt = node.properties.find("key");
            if (keyIt != node.properties.end() && std::holds_alternative<std::string>(keyIt->second))
                ImGui::Text("Check: %s", std::get<std::string>(keyIt->second).c_str());
            break;
        }
        case BTNodeType::SetBlackboardValue:
        {
            auto keyIt = node.properties.find("key");
            if (keyIt != node.properties.end() && std::holds_alternative<std::string>(keyIt->second))
                ImGui::Text("Set: %s", std::get<std::string>(keyIt->second).c_str());
            break;
        }
        case BTNodeType::LineOfSight:
        {
            auto keyIt = node.properties.find("targetKey");
            if (keyIt != node.properties.end() && std::holds_alternative<std::string>(keyIt->second))
                ImGui::Text("Target: %s", std::get<std::string>(keyIt->second).c_str());
            float dist = 50.0f;
            auto distIt = node.properties.find("maxDistance");
            if (distIt != node.properties.end() && std::holds_alternative<float>(distIt->second))
                dist = std::get<float>(distIt->second);
            ImGui::Text("Range: %.0fm", dist);
            break;
        }
        default:
            break;
        }

        ImGui::PopStyleColor();
    }
}
