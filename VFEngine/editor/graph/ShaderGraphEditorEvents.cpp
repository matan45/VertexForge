#include "ShaderGraphEditor.hpp"
#include "nodes/ShaderNode.hpp"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace ed = ax::NodeEditor;

namespace editor::graph {

    namespace
    {
        struct NodePaletteEntry
        {
            const char* category;
            const char* label;
            material::NodeType type;
        };

        constexpr std::array<NodePaletteEntry, 48> kNodePaletteEntries = {{
            // Constants
            {"Constants", "Scalar", material::NodeType::ConstantScalar},
            {"Constants", "Vector2", material::NodeType::ConstantVec2},
            {"Constants", "Vector3", material::NodeType::ConstantVec3},
            {"Constants", "Color", material::NodeType::ConstantColor},
            // Math
            {"Math", "Add", material::NodeType::Add},
            {"Math", "Subtract", material::NodeType::Subtract},
            {"Math", "Multiply", material::NodeType::Multiply},
            {"Math", "Divide", material::NodeType::Divide},
            {"Math", "Power", material::NodeType::Power},
            {"Math", "Lerp", material::NodeType::Lerp},
            {"Math", "Mix Color", material::NodeType::MixColor},
            {"Math", "Clamp", material::NodeType::Clamp},
            {"Math", "Saturate", material::NodeType::Saturate},
            {"Math", "One Minus", material::NodeType::OneMinus},
            {"Math", "Abs", material::NodeType::Abs},
            // Trigonometry
            {"Trigonometry", "Sin", material::NodeType::Sin},
            {"Trigonometry", "Cos", material::NodeType::Cos},
            // Vector
            {"Vector", "Make Vec2", material::NodeType::MakeVec2},
            {"Vector", "Make Vec3", material::NodeType::MakeVec3},
            {"Vector", "Normalize", material::NodeType::Normalize},
            {"Vector", "Length", material::NodeType::Length},
            {"Vector", "Dot", material::NodeType::Dot},
            {"Vector", "Cross", material::NodeType::Cross},
            {"Vector", "Fresnel", material::NodeType::Fresnel},
            // Inputs
            {"Inputs", "UV", material::NodeType::VertexUV},
            {"Inputs", "Normal", material::NodeType::VertexNormal},
            {"Inputs", "World Position", material::NodeType::WorldPosition},
            {"Inputs", "Time", material::NodeType::Time},
            // Utility
            {"Utility", "Panner", material::NodeType::Panner},
            {"Utility", "UV Transform", material::NodeType::UVTransform},
            {"Utility", "Remap", material::NodeType::Remap},
            {"Utility", "Flipbook", material::NodeType::Flipbook},
            {"Utility", "Rotator", material::NodeType::Rotator},
            {"Utility", "Custom Rotator", material::NodeType::CustomRotator},
            // Texture
            {"Texture", "Texture Sample", material::NodeType::TextureSample},
            {"Texture", "ORM Sample", material::NodeType::OrmSample},
            // Conversion (nested two-level menu flattened into searchable labels)
            {"Conversion", "Float to Vec2", material::NodeType::FloatToVec2},
            {"Conversion", "Float to Vec3", material::NodeType::FloatToVec3},
            {"Conversion", "Float to Vec4", material::NodeType::FloatToVec4},
            {"Conversion", "Vec2 to Float", material::NodeType::Vec2ToFloat},
            {"Conversion", "Vec2 to Vec3", material::NodeType::Vec2ToVec3},
            {"Conversion", "Vec2 to Vec4", material::NodeType::Vec2ToVec4},
            {"Conversion", "Vec3 to Float", material::NodeType::Vec3ToFloat},
            {"Conversion", "Vec3 to Vec2", material::NodeType::Vec3ToVec2},
            {"Conversion", "Vec3 to Vec4", material::NodeType::Vec3ToVec4},
            {"Conversion", "Vec4 to Float", material::NodeType::Vec4ToFloat},
            {"Conversion", "Vec4 to Vec2", material::NodeType::Vec4ToVec2},
            {"Conversion", "Vec4 to Vec3", material::NodeType::Vec4ToVec3},
        }};

        bool containsCaseInsensitive(const char* text, const char* filter)
        {
            if (!filter || filter[0] == '\0')
            {
                return true;
            }

            if (!text)
            {
                return false;
            }

            auto toLower = [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            };

            std::string haystack(text);
            std::string needle(filter);
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), toLower);
            std::transform(needle.begin(), needle.end(), needle.begin(), toLower);
            return haystack.find(needle) != std::string::npos;
        }

        bool matchesPaletteFilter(const NodePaletteEntry& entry, const char* filter)
        {
            return containsCaseInsensitive(entry.label, filter) ||
                   containsCaseInsensitive(entry.category, filter);
        }
    }

    void ShaderGraphEditor::handleCreation() {
        if (ed::BeginCreate()) {
            ed::PinId startPinId, endPinId;
            if (ed::QueryNewLink(&startPinId, &endPinId)) {
                uint32_t startId = fromEditorPinId(startPinId);
                uint32_t endId = fromEditorPinId(endPinId);

                if (startId && endId && canCreateLink(startId, endId)) {
                    if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
                        material::NodeLink newLink;
                        newLink.id = currentGraph->nextLinkId++;

                        // Find both pins and determine correct direction
                        const material::NodePin* startPin = findPin(startId);
                        const material::NodePin* endPin = findPin(endId);

                        // Determine which is source (output) and which is target (input)
                        uint32_t sourcePinId = startId;
                        uint32_t targetPinId = endId;
                        if (startPin && startPin->kind == material::PinKind::Input) {
                            // User dragged from input to output, swap them
                            sourcePinId = endId;
                            targetPinId = startId;
                        }

                        // Find source node and pin (must be an output pin)
                        for (const auto& node : currentGraph->nodes) {
                            for (const auto& pin : node.outputs) {
                                if (pin.id == sourcePinId) {
                                    newLink.sourceNodeId = node.id;
                                    newLink.sourcePin = pin.name;
                                    break;
                                }
                            }
                        }

                        // Find target node and pin (must be an input pin)
                        for (const auto& node : currentGraph->nodes) {
                            for (const auto& pin : node.inputs) {
                                if (pin.id == targetPinId) {
                                    newLink.targetNodeId = node.id;
                                    newLink.targetPin = pin.name;
                                    break;
                                }
                            }
                        }

                        // Remove any existing link to this input pin (input pins only accept one connection)
                        currentGraph->links.erase(
                            std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                                [&newLink](const material::NodeLink& existing) {
                                    return existing.targetNodeId == newLink.targetNodeId &&
                                           existing.targetPin == newLink.targetPin;
                                }),
                            currentGraph->links.end());

                        currentGraph->links.push_back(newLink);
                        if (onGraphChanged) onGraphChanged();
                    }
                } else {
                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);

                    std::string errorMsg = getTypeMismatchMessage(startId, endId);
                    if (!errorMsg.empty()) {
                        ImGui::BeginTooltip();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                        ImGui::TextUnformatted(errorMsg.c_str());
                        ImGui::PopStyleColor();
                        ImGui::EndTooltip();
                    }
                }
            }

            ed::PinId pinId;
            if (ed::QueryNewNode(&pinId)) {
                ed::RejectNewItem();
            }
        }
        ed::EndCreate();
    }

    void ShaderGraphEditor::handleDeletion() {
        if (ed::BeginDelete()) {
            ed::LinkId linkId;
            while (ed::QueryDeletedLink(&linkId)) {
                if (ed::AcceptDeletedItem()) {
                    uint32_t id = fromEditorLinkId(linkId);
                    auto it = std::find_if(currentGraph->links.begin(), currentGraph->links.end(),
                        [id](const material::NodeLink& link) { return link.id == id; });
                    if (it != currentGraph->links.end()) {
                        currentGraph->links.erase(it);
                        if (onGraphChanged) onGraphChanged();
                    }
                }
            }

            ed::NodeId nodeId;
            while (ed::QueryDeletedNode(&nodeId)) {
                uint32_t id = fromEditorNodeId(nodeId);
                auto node = currentGraph->findNode(id);
                if (node && node->type == material::NodeType::PBROutput) {
                    ed::RejectDeletedItem();
                } else if (ed::AcceptDeletedItem()) {
                    // Remove all links connected to this node
                    currentGraph->links.erase(
                        std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                            [id](const material::NodeLink& link) {
                                return link.sourceNodeId == id || link.targetNodeId == id;
                            }),
                        currentGraph->links.end()
                    );

                    // Remove the node
                    auto it = std::find_if(currentGraph->nodes.begin(), currentGraph->nodes.end(),
                        [id](const material::ShaderNode& n) { return n.id == id; });
                    if (it != currentGraph->nodes.end()) {
                        currentGraph->nodes.erase(it);
                        if (onGraphChanged) onGraphChanged();
                    }
                }
            }
        }
        ed::EndDelete();
    }

    void ShaderGraphEditor::handleContextMenu() {
        static char nodePaletteFilter[128] = {};
        static bool focusNodePaletteFilter = false;

        auto createPaletteNode = [this](const NodePaletteEntry& entry) {
            createNode(entry.type, newNodePosition);
            nodePaletteFilter[0] = '\0';
            ImGui::CloseCurrentPopup();
        };

        if (showCreateNodeMenu) {
            nodePaletteFilter[0] = '\0';
            focusNodePaletteFilter = true;
            ImGui::OpenPopup("AddNode");
            showCreateNodeMenu = false;
        }

        if (ImGui::BeginPopup("AddNode")) {
            ImGui::TextDisabled("Add Node");

            if (focusNodePaletteFilter) {
                ImGui::SetKeyboardFocusHere();
                focusNodePaletteFilter = false;
            }
            ImGui::SetNextItemWidth(240.0f);
            bool searchSubmitted = ImGui::InputTextWithHint(
                "##ShaderNodePaletteSearch",
                "Search nodes...",
                nodePaletteFilter,
                sizeof(nodePaletteFilter),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

            bool hasFilter = nodePaletteFilter[0] != '\0';
            if (hasFilter && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                nodePaletteFilter[0] = '\0';
                focusNodePaletteFilter = true;
                hasFilter = false;
            }

            const NodePaletteEntry* firstMatch = nullptr;
            if (hasFilter) {
                for (const auto& entry : kNodePaletteEntries) {
                    if (matchesPaletteFilter(entry, nodePaletteFilter)) {
                        firstMatch = &entry;
                        break;
                    }
                }
            }

            bool createdPaletteNode = false;
            if (hasFilter && searchSubmitted && firstMatch) {
                createPaletteNode(*firstMatch);
                createdPaletteNode = true;
            }

            ImGui::Separator();

            if (!createdPaletteNode && hasFilter) {
                bool anyMatch = false;
                for (const auto& entry : kNodePaletteEntries) {
                    if (!matchesPaletteFilter(entry, nodePaletteFilter)) {
                        continue;
                    }
                    anyMatch = true;
                    std::string menuLabel = (entry.category[0] != '\0')
                        ? std::string(entry.category) + " / " + entry.label
                        : std::string(entry.label);
                    if (ImGui::MenuItem(menuLabel.c_str())) {
                        createPaletteNode(entry);
                        break;
                    }
                }
                if (!anyMatch) {
                    ImGui::TextDisabled("No matching nodes");
                }
            } else if (!createdPaletteNode) {
            // Constants
            if (ImGui::BeginMenu("Constants")) {
                if (ImGui::MenuItem("Scalar")) {
                    createNode(material::NodeType::ConstantScalar, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Vector2")) {
                    createNode(material::NodeType::ConstantVec2, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Vector3")) {
                    createNode(material::NodeType::ConstantVec3, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Color")) {
                    createNode(material::NodeType::ConstantColor, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            // Math
            if (ImGui::BeginMenu("Math")) {
                if (ImGui::MenuItem("Add")) {
                    createNode(material::NodeType::Add, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Subtract")) {
                    createNode(material::NodeType::Subtract, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Multiply")) {
                    createNode(material::NodeType::Multiply, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Divide")) {
                    createNode(material::NodeType::Divide, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Power")) {
                    createNode(material::NodeType::Power, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Lerp")) {
                    createNode(material::NodeType::Lerp, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Mix Color")) {
                    createNode(material::NodeType::MixColor, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Clamp")) {
                    createNode(material::NodeType::Clamp, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Saturate")) {
                    createNode(material::NodeType::Saturate, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("One Minus")) {
                    createNode(material::NodeType::OneMinus, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Abs")) {
                    createNode(material::NodeType::Abs, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            // Trig
            if (ImGui::BeginMenu("Trigonometry")) {
                if (ImGui::MenuItem("Sin")) {
                    createNode(material::NodeType::Sin, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Cos")) {
                    createNode(material::NodeType::Cos, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            // Vector
            if (ImGui::BeginMenu("Vector")) {
                if (ImGui::MenuItem("Make Vec2")) {
                    createNode(material::NodeType::MakeVec2, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Make Vec3")) {
                    createNode(material::NodeType::MakeVec3, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Normalize")) {
                    createNode(material::NodeType::Normalize, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Length")) {
                    createNode(material::NodeType::Length, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Dot")) {
                    createNode(material::NodeType::Dot, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Cross")) {
                    createNode(material::NodeType::Cross, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Fresnel")) {
                    createNode(material::NodeType::Fresnel, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            // Inputs
            if (ImGui::BeginMenu("Inputs")) {
                if (ImGui::MenuItem("UV")) {
                    createNode(material::NodeType::VertexUV, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Normal")) {
                    createNode(material::NodeType::VertexNormal, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("World Position")) {
                    createNode(material::NodeType::WorldPosition, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Time")) {
                    createNode(material::NodeType::Time, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            // Utility
            if (ImGui::BeginMenu("Utility")) {
                if (ImGui::MenuItem("Panner")) {
                    createNode(material::NodeType::Panner, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("UV Transform")) {
                    createNode(material::NodeType::UVTransform, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Remap")) {
                    createNode(material::NodeType::Remap, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Flipbook")) {
                    createNode(material::NodeType::Flipbook, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Rotator")) {
                    createNode(material::NodeType::Rotator, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("Custom Rotator")) {
                    createNode(material::NodeType::CustomRotator, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            // Texture
            if (ImGui::BeginMenu("Texture")) {
                if (ImGui::MenuItem("Texture Sample")) {
                    createNode(material::NodeType::TextureSample, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::MenuItem("ORM Sample")) {
                    createNode(material::NodeType::OrmSample, newNodePosition);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Conversion")) {
                if (ImGui::BeginMenu("Float To...")) {
                    if (ImGui::MenuItem("Vec2")) {
                        createNode(material::NodeType::FloatToVec2, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Vec3")) {
                        createNode(material::NodeType::FloatToVec3, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Vec4")) {
                        createNode(material::NodeType::FloatToVec4, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Vec2 To...")) {
                    if (ImGui::MenuItem("Float")) {
                        createNode(material::NodeType::Vec2ToFloat, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Vec3")) {
                        createNode(material::NodeType::Vec2ToVec3, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Vec4")) {
                        createNode(material::NodeType::Vec2ToVec4, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Vec3 To...")) {
                    if (ImGui::MenuItem("Float")) {
                        createNode(material::NodeType::Vec3ToFloat, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Vec2")) {
                        createNode(material::NodeType::Vec3ToVec2, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Vec4")) {
                        createNode(material::NodeType::Vec3ToVec4, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Vec4 To...")) {
                    if (ImGui::MenuItem("Float")) {
                        createNode(material::NodeType::Vec4ToFloat, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Vec2")) {
                        createNode(material::NodeType::Vec4ToVec2, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::MenuItem("Vec3")) {
                        createNode(material::NodeType::Vec4ToVec3, newNodePosition);
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            }

            ImGui::EndPopup();
        }
    }

    void ShaderGraphEditor::createNode(material::NodeType type, const ImVec2& position) {
        material::ShaderNode node;
        node.id = currentGraph->nextNodeId++;
        node.type = type;
        node.position = glm::vec2(position.x, position.y);
        node.name = getNodeTypeName(type);

        // Initialize pins based on node type using factory
        ShaderNodeFactory::initializeNode(node, currentGraph->nextPinId);

        currentGraph->nodes.push_back(node);

        // Set node position in editor
        ed::SetCurrentEditor(editorContext);
        ed::SetNodePosition(toEditorNodeId(node.id), position);
        ed::SetCurrentEditor(nullptr);

        if (onGraphChanged) onGraphChanged();
    }

}
