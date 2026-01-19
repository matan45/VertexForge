#include "VFXGraphEditor.hpp"
#include "imgui.h"

namespace ed = ax::NodeEditor;

namespace editor::graph {

    bool VFXGraphEditor::isPinLinked(uint32_t pinId) const {
        uint32_t nodeId = getNodeIdFromPinId(pinId);
        bool isOutput = isOutputPin(pinId);

        for (const auto& link : currentGraph->links) {
            if (isOutput && link.sourceNodeId == nodeId) {
                return true;
            }
            if (!isOutput && link.targetNodeId == nodeId) {
                return true;
            }
        }
        return false;
    }

    void VFXGraphEditor::drawFlowPinShape(ImDrawList* drawList, ImVec2 center, ImU32 color,
                                           bool filled, float size, bool isOutput) const {
        // Arrow shape for flow pins - distinguishes from Material Editor circles
        float halfSize = size / 2.0f;

        if (isOutput) {
            // Right-pointing arrow for output
            ImVec2 points[3] = {
                ImVec2(center.x - halfSize, center.y - halfSize),
                ImVec2(center.x + halfSize, center.y),
                ImVec2(center.x - halfSize, center.y + halfSize)
            };
            if (filled) {
                drawList->AddConvexPolyFilled(points, 3, color);
            } else {
                drawList->AddPolyline(points, 3, color, ImDrawFlags_Closed, 2.0f);
            }
        } else {
            // Left-pointing arrow for input
            ImVec2 points[3] = {
                ImVec2(center.x + halfSize, center.y - halfSize),
                ImVec2(center.x - halfSize, center.y),
                ImVec2(center.x + halfSize, center.y + halfSize)
            };
            if (filled) {
                drawList->AddConvexPolyFilled(points, 3, color);
            } else {
                drawList->AddPolyline(points, 3, color, ImDrawFlags_Closed, 2.0f);
            }
        }
    }

    void VFXGraphEditor::drawNode(vfx::VFXNode& node) {
        ImU32 headerColor = getNodeHeaderColor(node.type);

        ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(40, 40, 40, 200));
        ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(60, 60, 60, 255));

        ed::BeginNode(toEditorNodeId(node.id));

        // Node header with colored background
        ImVec2 headerMin = ImGui::GetCursorScreenPos();

        // Node title
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        ImGui::TextUnformatted(node.name.empty() ? getNodeTypeName(node.type) : node.name.c_str());
        ImGui::PopStyleColor();

        ImVec2 headerMax = ImGui::GetCursorScreenPos();
        headerMax.x = headerMin.x + ImGui::GetContentRegionAvail().x + 20;

        // Draw header background
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            ImVec2(headerMin.x - 8, headerMin.y - 4),
            ImVec2(headerMax.x, headerMax.y),
            headerColor, 4.0f, ImDrawFlags_RoundCornersTop);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float pinSize = 10.0f;
        ImU32 pinColor = getFlowPinColor();

        if (node.type == vfx::VFXNodeType::Emitter) {
            // Emitter node: show properties + output pin

            // Draw properties
            ImGui::PushItemWidth(100);
            for (auto& [propName, prop] : node.properties) {
                ImGui::TextUnformatted(propName.c_str());
                ImGui::SameLine(80);

                std::string widgetId = "##" + propName + std::to_string(node.id);

                switch (prop.type) {
                    case vfx::VFXPropertyType::Float: {
                        float* val = std::get_if<float>(&prop.value);
                        if (val) {
                            if (ImGui::DragFloat(widgetId.c_str(), val, 0.1f, prop.min, prop.max)) {
                                if (onGraphChanged) onGraphChanged();
                            }
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Vec3: {
                        glm::vec3* val = std::get_if<glm::vec3>(&prop.value);
                        if (val) {
                            float v[3] = {val->x, val->y, val->z};
                            if (ImGui::DragFloat3(widgetId.c_str(), v, 0.1f)) {
                                *val = glm::vec3(v[0], v[1], v[2]);
                                if (onGraphChanged) onGraphChanged();
                            }
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Color: {
                        glm::vec4* val = std::get_if<glm::vec4>(&prop.value);
                        if (val) {
                            float c[4] = {val->x, val->y, val->z, val->w};
                            if (ImGui::ColorEdit4(widgetId.c_str(), c,
                                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
                                *val = glm::vec4(c[0], c[1], c[2], c[3]);
                                if (onGraphChanged) onGraphChanged();
                            }
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Int: {
                        int32_t* val = std::get_if<int32_t>(&prop.value);
                        if (val) {
                            if (ImGui::DragInt(widgetId.c_str(), val, 1,
                                    static_cast<int>(prop.min), static_cast<int>(prop.max))) {
                                if (onGraphChanged) onGraphChanged();
                            }
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Bool: {
                        bool* val = std::get_if<bool>(&prop.value);
                        if (val) {
                            if (ImGui::Checkbox(widgetId.c_str(), val)) {
                                if (onGraphChanged) onGraphChanged();
                            }
                        }
                        break;
                    }
                    default:
                        ImGui::TextDisabled("(unsupported)");
                        break;
                }
            }
            ImGui::PopItemWidth();

            ImGui::Spacing();

            // Output pin (right side)
            float nodeWidth = ImGui::GetContentRegionAvail().x;
            float labelWidth = ImGui::CalcTextSize("Output").x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + nodeWidth - labelWidth - pinSize - 8);

            uint32_t outputPinId = getOutputPinId(node.id);
            ed::BeginPin(toEditorPinId(outputPinId), ed::PinKind::Output);

            ImGui::TextUnformatted("Output");
            ImGui::SameLine(0, 4);

            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            bool isLinked = isPinLinked(outputPinId);
            drawFlowPinShape(drawList, ImVec2(iconPos.x + pinSize/2, iconPos.y + pinSize/2),
                           pinColor, isLinked, pinSize, true);
            ImGui::Dummy(ImVec2(pinSize, pinSize));

            ed::EndPin();

        } else if (node.type == vfx::VFXNodeType::OutSystem) {
            // OutSystem node: only input pin

            uint32_t inputPinId = getInputPinId(node.id);
            ed::BeginPin(toEditorPinId(inputPinId), ed::PinKind::Input);

            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            bool isLinked = isPinLinked(inputPinId);
            drawFlowPinShape(drawList, ImVec2(iconPos.x + pinSize/2, iconPos.y + pinSize/2),
                           pinColor, isLinked, pinSize, false);
            ImGui::Dummy(ImVec2(pinSize, pinSize));
            ImGui::SameLine(0, 4);
            ImGui::TextUnformatted("Input");

            ed::EndPin();
        }

        ed::EndNode();
        ed::PopStyleColor(2);
    }

    void VFXGraphEditor::drawLinks() {
        ImU32 flowColor = getFlowPinColor();

        for (const auto& link : currentGraph->links) {
            // Find source and target nodes to get pin IDs
            const vfx::VFXNode* sourceNode = currentGraph->findNode(link.sourceNodeId);
            const vfx::VFXNode* targetNode = currentGraph->findNode(link.targetNodeId);

            if (!sourceNode || !targetNode) continue;

            // Source is always output pin, target is always input pin
            uint32_t sourcePinId = getOutputPinId(link.sourceNodeId);
            uint32_t targetPinId = getInputPinId(link.targetNodeId);

            ed::Link(toEditorLinkId(link.id),
                    toEditorPinId(sourcePinId),
                    toEditorPinId(targetPinId),
                    ImColor(flowColor), 2.0f);
        }
    }

    void VFXGraphEditor::drawZoomControls(ImVec2 canvasPos, ImVec2 canvasSize, float currentZoom) {
        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();

        // Position in bottom-right of the canvas
        float panelX = canvasPos.x + canvasSize.x - 130;
        float panelY = canvasPos.y + canvasSize.y - 40;
        float panelWidth = 125;
        float panelHeight = 35;

        // Draw background panel
        fgDrawList->AddRectFilled(
            ImVec2(panelX, panelY),
            ImVec2(panelX + panelWidth, panelY + panelHeight),
            IM_COL32(30, 30, 30, 220), 6.0f);
        fgDrawList->AddRect(
            ImVec2(panelX, panelY),
            ImVec2(panelX + panelWidth, panelY + panelHeight),
            IM_COL32(60, 60, 60, 255), 6.0f);

        // Button dimensions
        float btnSize = 25;
        float btnY = panelY + 5;
        float btnSpacing = 5;

        // Get mouse position for hit testing
        ImVec2 mousePos = ImGui::GetMousePos();
        bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        // Zoom out button (-)
        float zoomOutX = panelX + 5;
        ImVec2 zoomOutMin(zoomOutX, btnY);
        ImVec2 zoomOutMax(zoomOutX + btnSize, btnY + btnSize);
        bool zoomOutHovered = mousePos.x >= zoomOutMin.x && mousePos.x <= zoomOutMax.x &&
                              mousePos.y >= zoomOutMin.y && mousePos.y <= zoomOutMax.y;

        ImU32 zoomOutColor = zoomOutHovered ? IM_COL32(80, 80, 80, 255) : IM_COL32(50, 50, 50, 255);
        fgDrawList->AddRectFilled(zoomOutMin, zoomOutMax, zoomOutColor, 4.0f);
        fgDrawList->AddLine(
            ImVec2(zoomOutX + 6, btnY + btnSize/2),
            ImVec2(zoomOutX + btnSize - 6, btnY + btnSize/2),
            IM_COL32(220, 220, 220, 255), 2.0f);

        if (zoomOutHovered && mouseClicked) {
            pendingZoomSteps = -1;
        }

        // Zoom percentage text
        char zoomText[16];
        snprintf(zoomText, sizeof(zoomText), "%.1f%%", currentZoom * 100.0f);
        ImVec2 textSize = ImGui::CalcTextSize(zoomText);
        float textAreaWidth = 55;
        float textX = zoomOutX + btnSize + btnSpacing + (textAreaWidth - textSize.x) / 2;
        float textY = btnY + (btnSize - textSize.y) / 2;
        fgDrawList->AddText(ImVec2(textX, textY), IM_COL32(200, 200, 200, 255), zoomText);

        // Tooltip for the panel
        bool panelHovered = mousePos.x >= panelX && mousePos.x <= panelX + panelWidth &&
                            mousePos.y >= panelY && mousePos.y <= panelY + panelHeight;
        if (panelHovered) {
            ImGui::SetTooltip("Press F to fit all nodes");
        }

        // Zoom in button (+)
        float zoomInX = zoomOutX + btnSize + btnSpacing + textAreaWidth + btnSpacing;
        ImVec2 zoomInMin(zoomInX, btnY);
        ImVec2 zoomInMax(zoomInX + btnSize, btnY + btnSize);
        bool zoomInHovered = mousePos.x >= zoomInMin.x && mousePos.x <= zoomInMax.x &&
                             mousePos.y >= zoomInMin.y && mousePos.y <= zoomInMax.y;

        ImU32 zoomInColor = zoomInHovered ? IM_COL32(80, 80, 80, 255) : IM_COL32(50, 50, 50, 255);
        fgDrawList->AddRectFilled(zoomInMin, zoomInMax, zoomInColor, 4.0f);
        ImVec2 plusCenter(zoomInX + btnSize/2, btnY + btnSize/2);
        fgDrawList->AddLine(
            ImVec2(plusCenter.x - 6, plusCenter.y),
            ImVec2(plusCenter.x + 6, plusCenter.y),
            IM_COL32(220, 220, 220, 255), 2.0f);
        fgDrawList->AddLine(
            ImVec2(plusCenter.x, plusCenter.y - 6),
            ImVec2(plusCenter.x, plusCenter.y + 6),
            IM_COL32(220, 220, 220, 255), 2.0f);

        if (zoomInHovered && mouseClicked) {
            pendingZoomSteps = 1;
        }
    }

}
