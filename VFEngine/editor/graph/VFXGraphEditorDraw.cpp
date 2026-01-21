#include "VFXGraphEditor.hpp"
#include "vfx/VFXShapeTypes.hpp"
#include "imgui.h"
#include <nfd/FileDialog.hpp>
#include <filesystem>

namespace ed = ax::NodeEditor;

namespace editor::graph {

    bool VFXGraphEditor::isPinLinked(uint32_t pinId) const {
        // VK-240: Handle shape pins separately
        if (isShapePin(pinId)) {
            uint32_t nodeId = getNodeIdFromShapePinId(pinId);
            for (const auto& link : currentGraph->links) {
                if (link.targetNodeId == nodeId && link.targetPin == "Shape") {
                    return true;
                }
            }
            return false;
        }

        uint32_t nodeId = getNodeIdFromPinId(pinId);
        bool isOutput = isOutputPin(pinId);

        for (const auto& link : currentGraph->links) {
            if (isOutput && link.sourceNodeId == nodeId) {
                return true;
            }
            if (!isOutput && link.targetNodeId == nodeId && link.targetPin != "Shape") {
                return true;
            }
        }
        return false;
    }

    void VFXGraphEditor::drawFlowPinShape(ImDrawList* drawList, ImVec2 center, ImU32 color,
                                           bool filled, float size, bool isOutput) const {
        float halfSize = size / 2.0f;

        if (isOutput) {
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

        ImGui::PushStyleColor(ImGuiCol_Text, ImColor(headerColor).Value);
        ImGui::TextUnformatted(node.name.empty() ? getNodeTypeName(node.type) : node.name.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float pinSize = 10.0f;
        ImU32 pinColor = getFlowPinColor();

        if (node.type == vfx::VFXNodeType::Emitter) {
            // VK-240: Shape input pin (magenta color)
            uint32_t shapePinId = getShapePinId(node.id);
            ed::BeginPin(toEditorPinId(shapePinId), ed::PinKind::Input);

            ImVec2 shapeIconPos = ImGui::GetCursorScreenPos();
            bool shapeLinked = isPinLinked(shapePinId);
            ImU32 shapePinColor = IM_COL32(200, 100, 180, 255);  // Magenta
            drawFlowPinShape(drawList, ImVec2(shapeIconPos.x + pinSize/2, shapeIconPos.y + pinSize/2),
                           shapePinColor, shapeLinked, pinSize, false);
            ImGui::Dummy(ImVec2(pinSize, pinSize));
            ImGui::SameLine(0, 4);
            ImGui::TextUnformatted("Shape");

            ed::EndPin();

            ImGui::Spacing();

            for (auto& [propName, prop] : node.properties) {
                std::string widgetId = "##" + propName + std::to_string(node.id);

                switch (prop.type) {
                    case vfx::VFXPropertyType::Float: {
                        float* val = std::get_if<float>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(70);
                            ImGui::PushItemWidth(50);
                            if (ImGui::DragFloat(widgetId.c_str(), val, 0.1f, prop.min, prop.max, "%.1f")) {
                                if (onGraphChanged) onGraphChanged();
                            }
                            ImGui::PopItemWidth();
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Vec3: {
                        glm::vec3* val = std::get_if<glm::vec3>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::PushItemWidth(90);
                            float v[3] = {val->x, val->y, val->z};
                            if (ImGui::InputFloat3(widgetId.c_str(), v, "%.1f")) {
                                *val = glm::vec3(v[0], v[1], v[2]);
                                if (onGraphChanged) onGraphChanged();
                            }
                            ImGui::PopItemWidth();
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Color: {
                        glm::vec4* val = std::get_if<glm::vec4>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(70);
                            ImVec4 color(val->x, val->y, val->z, val->w);
                            if (ImGui::ColorButton(widgetId.c_str(), color, ImGuiColorEditFlags_AlphaPreview, ImVec2(20, 20))) {
                                ImGui::OpenPopup(("ColorPicker" + widgetId).c_str());
                            }
                            ed::Suspend();
                            if (ImGui::BeginPopup(("ColorPicker" + widgetId).c_str())) {
                                float c[4] = {val->x, val->y, val->z, val->w};
                                if (ImGui::ColorPicker4("##picker", c, ImGuiColorEditFlags_AlphaBar)) {
                                    *val = glm::vec4(c[0], c[1], c[2], c[3]);
                                    if (onGraphChanged) onGraphChanged();
                                }
                                ImGui::EndPopup();
                            }
                            ed::Resume();
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Int: {
                        int32_t* val = std::get_if<int32_t>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(70);
                            ImGui::PushItemWidth(50);
                            if (ImGui::DragInt(widgetId.c_str(), val, 1,
                                    static_cast<int>(prop.min), static_cast<int>(prop.max))) {
                                if (onGraphChanged) onGraphChanged();
                            }
                            ImGui::PopItemWidth();
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Bool: {
                        bool* val = std::get_if<bool>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(70);
                            if (ImGui::Checkbox(widgetId.c_str(), val)) {
                                if (onGraphChanged) onGraphChanged();
                            }
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::String: {
                        std::string* val = std::get_if<std::string>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            std::string displayName = val->empty() ? "(none)" :
                                std::filesystem::path(*val).filename().string();
                            ImGui::SameLine(70);
                            ImGui::TextDisabled("%s", displayName.c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton(("..." + widgetId).c_str())) {
                                nfd::FileDialog dialog;
                                std::string path = dialog.openFileDialog({
                                    {L"VF Image", L"*.vfImage"}
                                });
                                if (!path.empty()) {
                                    *val = path;
                                    if (onGraphChanged) onGraphChanged();
                                }
                            }
                            if (!val->empty()) {
                                ImGui::SameLine();
                                if (ImGui::SmallButton(("X" + widgetId).c_str())) {
                                    val->clear();
                                    if (onGraphChanged) onGraphChanged();
                                }
                            }
                        }
                        break;
                    }
                    default:
                        ImGui::TextDisabled("(unsupported)");
                        break;
                }
            }

            ImGui::Spacing();

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

            // VK-85: Show validation status indicator
            ImGui::Spacing();
            bool graphValid = currentGraph ? currentGraph->isValid() : false;
            if (graphValid) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255));
                ImGui::TextUnformatted("Ready");
                ImGui::PopStyleColor();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
                ImGui::TextUnformatted("Not Connected");
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered() && currentGraph) {
                    ImGui::SetTooltip("%s", currentGraph->getValidationError().c_str());
                }
            }
        } else if (vfx::isModifierNode(node.type)) {
            // VK-238: Draw modifier nodes with input and output pins

            // Input pin (left side)
            uint32_t inputPinId = getInputPinId(node.id);
            ed::BeginPin(toEditorPinId(inputPinId), ed::PinKind::Input);

            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            bool isLinked = isPinLinked(inputPinId);
            drawFlowPinShape(drawList, ImVec2(iconPos.x + pinSize/2, iconPos.y + pinSize/2),
                           pinColor, isLinked, pinSize, false);
            ImGui::Dummy(ImVec2(pinSize, pinSize));
            ImGui::SameLine(0, 4);
            ImGui::TextUnformatted("In");

            ed::EndPin();

            // Draw modifier-specific properties
            for (auto& [propName, prop] : node.properties) {
                std::string widgetId = "##mod" + propName + std::to_string(node.id);

                switch (prop.type) {
                    case vfx::VFXPropertyType::Float: {
                        float* val = std::get_if<float>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(100);
                            ImGui::PushItemWidth(60);
                            if (ImGui::DragFloat(widgetId.c_str(), val, 0.01f, prop.min, prop.max, "%.2f")) {
                                if (onGraphChanged) onGraphChanged();
                            }
                            ImGui::PopItemWidth();
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Color: {
                        glm::vec4* val = std::get_if<glm::vec4>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(100);
                            ImVec4 color(val->x, val->y, val->z, val->w);
                            if (ImGui::ColorButton(widgetId.c_str(), color, ImGuiColorEditFlags_AlphaPreview, ImVec2(20, 20))) {
                                ImGui::OpenPopup(("ColorPicker" + widgetId).c_str());
                            }
                            ed::Suspend();
                            if (ImGui::BeginPopup(("ColorPicker" + widgetId).c_str())) {
                                float c[4] = {val->x, val->y, val->z, val->w};
                                if (ImGui::ColorPicker4("##picker", c, ImGuiColorEditFlags_AlphaBar)) {
                                    *val = glm::vec4(c[0], c[1], c[2], c[3]);
                                    if (onGraphChanged) onGraphChanged();
                                }
                                ImGui::EndPopup();
                            }
                            ed::Resume();
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            ImGui::Spacing();

            // Output pin (right side)
            uint32_t outputPinId = getOutputPinId(node.id);
            ed::BeginPin(toEditorPinId(outputPinId), ed::PinKind::Output);

            ImGui::TextUnformatted("Out");
            ImGui::SameLine(0, 4);

            iconPos = ImGui::GetCursorScreenPos();
            isLinked = isPinLinked(outputPinId);
            drawFlowPinShape(drawList, ImVec2(iconPos.x + pinSize/2, iconPos.y + pinSize/2),
                           pinColor, isLinked, pinSize, true);
            ImGui::Dummy(ImVec2(pinSize, pinSize));

            ed::EndPin();
        } else if (vfx::isForceNode(node.type)) {
            // VK-239: Draw force nodes with input and output pins

            // Input pin (left side)
            uint32_t inputPinId = getInputPinId(node.id);
            ed::BeginPin(toEditorPinId(inputPinId), ed::PinKind::Input);

            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            bool isLinked = isPinLinked(inputPinId);
            drawFlowPinShape(drawList, ImVec2(iconPos.x + pinSize/2, iconPos.y + pinSize/2),
                           pinColor, isLinked, pinSize, false);
            ImGui::Dummy(ImVec2(pinSize, pinSize));
            ImGui::SameLine(0, 4);
            ImGui::TextUnformatted("In");

            ed::EndPin();

            // Draw force-specific properties
            for (auto& [propName, prop] : node.properties) {
                std::string widgetId = "##force" + propName + std::to_string(node.id);

                switch (prop.type) {
                    case vfx::VFXPropertyType::Float: {
                        float* val = std::get_if<float>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(100);
                            ImGui::PushItemWidth(60);
                            if (ImGui::DragFloat(widgetId.c_str(), val, 0.1f, prop.min, prop.max, "%.2f")) {
                                if (onGraphChanged) onGraphChanged();
                            }
                            ImGui::PopItemWidth();
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Vec3: {
                        glm::vec3* val = std::get_if<glm::vec3>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::PushItemWidth(120);
                            float v[3] = {val->x, val->y, val->z};
                            if (ImGui::InputFloat3(widgetId.c_str(), v, "%.2f")) {
                                *val = glm::vec3(v[0], v[1], v[2]);
                                if (onGraphChanged) onGraphChanged();
                            }
                            ImGui::PopItemWidth();
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Int: {
                        int32_t* val = std::get_if<int32_t>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(100);
                            ImGui::PushItemWidth(50);
                            if (ImGui::DragInt(widgetId.c_str(), val, 1,
                                    static_cast<int>(prop.min), static_cast<int>(prop.max))) {
                                if (onGraphChanged) onGraphChanged();
                            }
                            ImGui::PopItemWidth();
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Bool: {
                        bool* val = std::get_if<bool>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(100);
                            if (ImGui::Checkbox(widgetId.c_str(), val)) {
                                if (onGraphChanged) onGraphChanged();
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            ImGui::Spacing();

            // Output pin (right side)
            uint32_t outputPinId = getOutputPinId(node.id);
            ed::BeginPin(toEditorPinId(outputPinId), ed::PinKind::Output);

            ImGui::TextUnformatted("Out");
            ImGui::SameLine(0, 4);

            iconPos = ImGui::GetCursorScreenPos();
            isLinked = isPinLinked(outputPinId);
            drawFlowPinShape(drawList, ImVec2(iconPos.x + pinSize/2, iconPos.y + pinSize/2),
                           pinColor, isLinked, pinSize, true);
            ImGui::Dummy(ImVec2(pinSize, pinSize));

            ed::EndPin();
        } else if (vfx::isShapeNode(node.type)) {
            // VK-240: Draw shape nodes with only output pin (connects to Emitter's shape input)

            // Draw shape-specific properties
            for (auto& [propName, prop] : node.properties) {
                std::string widgetId = "##shape" + propName + std::to_string(node.id);

                // Skip shapeType - it's defined at creation and shown in node name
                if (propName == "shapeType") continue;

                switch (prop.type) {
                    case vfx::VFXPropertyType::Float: {
                        float* val = std::get_if<float>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(100);
                            ImGui::PushItemWidth(60);
                            if (ImGui::DragFloat(widgetId.c_str(), val, 0.1f, prop.min, prop.max, "%.2f")) {
                                if (onGraphChanged) onGraphChanged();
                            }
                            ImGui::PopItemWidth();
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Vec3: {
                        glm::vec3* val = std::get_if<glm::vec3>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::PushItemWidth(120);
                            float v[3] = {val->x, val->y, val->z};
                            if (ImGui::InputFloat3(widgetId.c_str(), v, "%.2f")) {
                                *val = glm::vec3(v[0], v[1], v[2]);
                                if (onGraphChanged) onGraphChanged();
                            }
                            ImGui::PopItemWidth();
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::Bool: {
                        bool* val = std::get_if<bool>(&prop.value);
                        if (val) {
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(100);
                            if (ImGui::Checkbox(widgetId.c_str(), val)) {
                                if (onGraphChanged) onGraphChanged();
                            }
                        }
                        break;
                    }
                    case vfx::VFXPropertyType::String: {
                        std::string* val = std::get_if<std::string>(&prop.value);
                        if (val && propName == "emitFrom") {
                            // EmitFrom toggle button (Volume/Surface)
                            ImGui::Text("%s", propName.c_str());
                            ImGui::SameLine(100);
                            std::string buttonLabel = *val + "##emitFrom" + std::to_string(node.id);
                            if (ImGui::Button(buttonLabel.c_str(), ImVec2(70, 0))) {
                                *val = (*val == "Volume") ? "Surface" : "Volume";
                                if (onGraphChanged) onGraphChanged();
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            ImGui::Spacing();

            // Output pin only (connects to Emitter's shape input)
            uint32_t outputPinId = getOutputPinId(node.id);
            ed::BeginPin(toEditorPinId(outputPinId), ed::PinKind::Output);

            ImGui::TextUnformatted("Shape Out");
            ImGui::SameLine(0, 4);

            ImVec2 iconPos = ImGui::GetCursorScreenPos();
            bool isLinked = isPinLinked(outputPinId);
            // Use magenta color for shape pins
            ImU32 shapePinColor = IM_COL32(200, 100, 180, 255);
            drawFlowPinShape(drawList, ImVec2(iconPos.x + pinSize/2, iconPos.y + pinSize/2),
                           shapePinColor, isLinked, pinSize, true);
            ImGui::Dummy(ImVec2(pinSize, pinSize));

            ed::EndPin();
        }

        ed::EndNode();
        ed::PopStyleColor(2);
    }

    void VFXGraphEditor::drawLinks() {
        ImU32 flowColor = getFlowPinColor();
        ImU32 shapeColor = IM_COL32(200, 100, 180, 255);  // VK-240: Magenta for shape links

        for (const auto& link : currentGraph->links) {
            const vfx::VFXNode* sourceNode = currentGraph->findNode(link.sourceNodeId);
            const vfx::VFXNode* targetNode = currentGraph->findNode(link.targetNodeId);

            if (!sourceNode || !targetNode) continue;

            uint32_t sourcePinId = getOutputPinId(link.sourceNodeId);

            // VK-240: Use shape pin ID for shape links
            uint32_t targetPinId;
            ImU32 linkColor;
            if (link.targetPin == "Shape") {
                targetPinId = getShapePinId(link.targetNodeId);
                linkColor = shapeColor;
            } else {
                targetPinId = getInputPinId(link.targetNodeId);
                linkColor = flowColor;
            }

            ed::Link(toEditorLinkId(link.id),
                    toEditorPinId(sourcePinId),
                    toEditorPinId(targetPinId),
                    ImColor(linkColor), 2.0f);
        }
    }

    void VFXGraphEditor::drawZoomControls(ImVec2 canvasPos, ImVec2 canvasSize, float currentZoom) {
        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();

        float panelX = canvasPos.x + canvasSize.x - 130;
        float panelY = canvasPos.y + canvasSize.y - 40;
        float panelWidth = 125;
        float panelHeight = 35;

        fgDrawList->AddRectFilled(
            ImVec2(panelX, panelY),
            ImVec2(panelX + panelWidth, panelY + panelHeight),
            IM_COL32(30, 30, 30, 220), 6.0f);
        fgDrawList->AddRect(
            ImVec2(panelX, panelY),
            ImVec2(panelX + panelWidth, panelY + panelHeight),
            IM_COL32(60, 60, 60, 255), 6.0f);

        float btnSize = 25;
        float btnY = panelY + 5;
        float btnSpacing = 5;

        ImVec2 mousePos = ImGui::GetMousePos();
        bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

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

        char zoomText[16];
        snprintf(zoomText, sizeof(zoomText), "%.1f%%", currentZoom * 100.0f);
        ImVec2 textSize = ImGui::CalcTextSize(zoomText);
        float textAreaWidth = 55;
        float textX = zoomOutX + btnSize + btnSpacing + (textAreaWidth - textSize.x) / 2;
        float textY = btnY + (btnSize - textSize.y) / 2;
        fgDrawList->AddText(ImVec2(textX, textY), IM_COL32(200, 200, 200, 255), zoomText);

        bool panelHovered = mousePos.x >= panelX && mousePos.x <= panelX + panelWidth &&
                            mousePos.y >= panelY && mousePos.y <= panelY + panelHeight;
        if (panelHovered) {
            ImGui::SetTooltip("Press F to fit all nodes");
        }

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
