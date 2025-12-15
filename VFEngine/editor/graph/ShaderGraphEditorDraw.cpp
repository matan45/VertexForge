#include "ShaderGraphEditor.hpp"
#include "imgui.h"

namespace ed = ax::NodeEditor;

namespace editor::graph {

    bool ShaderGraphEditor::isPinLinked(uint32_t pinId) const {
        for (const auto& node : currentGraph->nodes) {
            for (const auto& pin : node.outputs) {
                if (pin.id == pinId) {
                    for (const auto& link : currentGraph->links) {
                        if (link.sourceNodeId == node.id && link.sourcePin == pin.name) {
                            return true;
                        }
                    }
                    return false;
                }
            }
            for (const auto& pin : node.inputs) {
                if (pin.id == pinId) {
                    for (const auto& link : currentGraph->links) {
                        if (link.targetNodeId == node.id && link.targetPin == pin.name) {
                            return true;
                        }
                    }
                    return false;
                }
            }
        }
        return false;
    }

    void ShaderGraphEditor::drawPinShape(ImDrawList* drawList, ImVec2 center, material::PinType type,
                                          ImU32 color, bool filled, float size) const {
        float halfSize = size / 2.0f;

        switch (type) {
            case material::PinType::Float: {
                if (filled) {
                    drawList->AddCircleFilled(center, halfSize, color);
                } else {
                    drawList->AddCircle(center, halfSize, color, 12, 2.0f);
                }
                break;
            }
            case material::PinType::Vec2: {
                ImVec2 points[4] = {
                    ImVec2(center.x, center.y - halfSize),
                    ImVec2(center.x + halfSize, center.y),
                    ImVec2(center.x, center.y + halfSize),
                    ImVec2(center.x - halfSize, center.y)
                };
                if (filled) {
                    drawList->AddConvexPolyFilled(points, 4, color);
                } else {
                    drawList->AddPolyline(points, 4, color, ImDrawFlags_Closed, 2.0f);
                }
                break;
            }
            case material::PinType::Vec3: {
                ImVec2 points[3] = {
                    ImVec2(center.x, center.y - halfSize),
                    ImVec2(center.x + halfSize, center.y + halfSize * 0.7f),
                    ImVec2(center.x - halfSize, center.y + halfSize * 0.7f)
                };
                if (filled) {
                    drawList->AddConvexPolyFilled(points, 3, color);
                } else {
                    drawList->AddPolyline(points, 3, color, ImDrawFlags_Closed, 2.0f);
                }
                break;
            }
            case material::PinType::Vec4: {
                ImVec2 points[6];
                for (int i = 0; i < 6; i++) {
                    float angle = (float)i / 6.0f * 2.0f * 3.14159f - 3.14159f / 2.0f;
                    points[i] = ImVec2(center.x + halfSize * cosf(angle),
                                       center.y + halfSize * sinf(angle));
                }
                if (filled) {
                    drawList->AddConvexPolyFilled(points, 6, color);
                } else {
                    drawList->AddPolyline(points, 6, color, ImDrawFlags_Closed, 2.0f);
                }
                break;
            }
            case material::PinType::Texture2D: {
                if (filled) {
                    drawList->AddRectFilled(
                        ImVec2(center.x - halfSize, center.y - halfSize),
                        ImVec2(center.x + halfSize, center.y + halfSize),
                        color);
                } else {
                    drawList->AddRect(
                        ImVec2(center.x - halfSize, center.y - halfSize),
                        ImVec2(center.x + halfSize, center.y + halfSize),
                        color, 0.0f, 0, 2.0f);
                }
                break;
            }
            default: {
                if (filled) {
                    drawList->AddCircleFilled(center, halfSize, color);
                } else {
                    drawList->AddCircle(center, halfSize, color, 12, 2.0f);
                }
                break;
            }
        }
    }

    void ShaderGraphEditor::drawNode(material::ShaderNode& node) {
        ed::BeginNode(toEditorNodeId(node.id));

        // Node header
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        ImGui::TextUnformatted(node.name.empty() ? getNodeTypeName(node.type).data() : node.name.c_str());
        ImGui::PopStyleColor();

        // Show color preview for ConstantColor nodes
        if (node.type == material::NodeType::ConstantColor) {
            glm::vec4 color(1.0f);
            auto it = node.properties.find("value");
            if (it != node.properties.end()) {
                if (auto* col = std::get_if<glm::vec4>(&it->second)) {
                    color = *col;
                }
            }
            ImGui::ColorButton("##preview", ImVec4(color.x, color.y, color.z, color.w),
                ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
        }

        // Show texture path for TextureSample nodes
        if (node.type == material::NodeType::TextureSample) {
            std::string texPath = "";
            auto it = node.properties.find("texturePath");
            if (it != node.properties.end()) {
                if (auto* path = std::get_if<std::string>(&it->second)) {
                    texPath = *path;
                }
            }
            std::string displayText = texPath.empty() ? "(No texture)" :
                (texPath.length() > 15 ? "..." + texPath.substr(texPath.length() - 12) : texPath);
            ImGui::TextDisabled("%s", displayText.c_str());
        }

        ImGui::Spacing();

        float pinSize = 10.0f;
        float rowHeight = pinSize + 4.0f;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // Calculate widths for layout
        float maxInputWidth = 0.0f;
        float maxOutputWidth = 0.0f;
        for (const auto& pin : node.inputs) {
            maxInputWidth = std::max(maxInputWidth, ImGui::CalcTextSize(pin.name.c_str()).x);
        }
        for (const auto& pin : node.outputs) {
            maxOutputWidth = std::max(maxOutputWidth, ImGui::CalcTextSize(pin.name.c_str()).x);
        }

        // Calculate column positions
        float columnSpacing = 25.0f;
        float inputColumnWidth = pinSize + 4 + maxInputWidth;
        float outputColumnWidth = maxOutputWidth + 4 + pinSize;
        float totalWidth = inputColumnWidth + columnSpacing + outputColumnWidth;
        totalWidth = std::max(totalWidth, 80.0f);

        // Starting X position for this node's content
        float startX = ImGui::GetCursorPosX();

        // Draw inputs and outputs on the same rows
        size_t maxPins = std::max(node.inputs.size(), node.outputs.size());

        for (size_t i = 0; i < maxPins; ++i) {
            float rowY = ImGui::GetCursorPosY();

            // Input pin (left side): [icon] Label
            if (i < node.inputs.size()) {
                const auto& pin = node.inputs[i];

                ImGui::SetCursorPos(ImVec2(startX, rowY));
                ed::BeginPin(toEditorPinId(pin.id), ed::PinKind::Input);

                ImVec2 iconPos = ImGui::GetCursorScreenPos();
                ImU32 pinColor = getPinColor(pin.type);
                bool isLinked = isPinLinked(pin.id);
                drawPinShape(drawList, ImVec2(iconPos.x + pinSize/2, iconPos.y + pinSize/2),
                            pin.type, pinColor, isLinked, pinSize);

                ImGui::Dummy(ImVec2(pinSize, pinSize));
                ImGui::SameLine(0, 4);
                ImGui::TextUnformatted(pin.name.c_str());

                ed::EndPin();
            }

            // Output pin (right side): Label [icon] - right-aligned
            if (i < node.outputs.size()) {
                const auto& pin = node.outputs[i];

                // Calculate positions to right-align the icon
                float labelWidth = ImGui::CalcTextSize(pin.name.c_str()).x;
                float iconX = startX + totalWidth - pinSize;
                float labelX = iconX - 4 - labelWidth;

                ImGui::SetCursorPos(ImVec2(labelX, rowY));
                ed::BeginPin(toEditorPinId(pin.id), ed::PinKind::Output);

                ImGui::TextUnformatted(pin.name.c_str());
                ImGui::SameLine(0, 4);

                ImVec2 iconPos = ImGui::GetCursorScreenPos();
                ImU32 pinColor = getPinColor(pin.type);
                bool isLinked = isPinLinked(pin.id);
                drawPinShape(drawList, ImVec2(iconPos.x + pinSize/2, iconPos.y + pinSize/2),
                            pin.type, pinColor, isLinked, pinSize);

                ImGui::Dummy(ImVec2(pinSize, pinSize));

                ed::EndPin();
            }

            // Move to next row
            ImGui::SetCursorPosY(rowY + rowHeight);
        }

        ed::EndNode();
    }

    void ShaderGraphEditor::drawLinks() {
        for (const auto& link : currentGraph->links) {
            const material::NodePin* sourcePin = findPin(link.sourceNodeId);
            ImU32 color = sourcePin ? getPinColor(sourcePin->type) : IM_COL32(255, 255, 255, 200);

            uint32_t sourcePinId = 0;
            uint32_t targetPinId = 0;

            for (const auto& node : currentGraph->nodes) {
                if (node.id == link.sourceNodeId) {
                    for (const auto& pin : node.outputs) {
                        if (pin.name == link.sourcePin) {
                            sourcePinId = pin.id;
                            break;
                        }
                    }
                }
                if (node.id == link.targetNodeId) {
                    for (const auto& pin : node.inputs) {
                        if (pin.name == link.targetPin) {
                            targetPinId = pin.id;
                            break;
                        }
                    }
                }
            }

            if (sourcePinId && targetPinId) {
                ed::Link(toEditorLinkId(link.id), toEditorPinId(sourcePinId), toEditorPinId(targetPinId),
                        ImColor(color), 2.0f);
            }
        }
    }

    void ShaderGraphEditor::drawZoomControls(ImVec2 canvasPos, ImVec2 canvasSize, float currentZoom) {
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
