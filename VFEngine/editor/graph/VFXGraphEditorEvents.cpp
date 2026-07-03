#include "VFXGraphEditor.hpp"
#include "vfx/VFXShapeProperties.hpp"
#include "vfx/VFXShapeTypes.hpp"
#include "vfx/VFXKillVolume.hpp"
#include "imgui.h"
#include <algorithm>
#include <map>

namespace ed = ax::NodeEditor;

namespace editor::graph
{
    bool VFXGraphEditor::canCreateLink(uint32_t startPinId, uint32_t endPinId) const
    {
        bool startIsShapePin = isShapePin(startPinId);
        bool endIsShapePin = isShapePin(endPinId);

        uint32_t startNodeId = startIsShapePin ? getNodeIdFromShapePinId(startPinId) : getNodeIdFromPinId(startPinId);
        uint32_t endNodeId = endIsShapePin ? getNodeIdFromShapePinId(endPinId) : getNodeIdFromPinId(endPinId);

        if (startNodeId == endNodeId) return false;

        const vfx::VFXNode* startNode = currentGraph->findNode(startNodeId);
        const vfx::VFXNode* endNode = currentGraph->findNode(endNodeId);

        if (!startNode || !endNode) return false;

        if (startIsShapePin || endIsShapePin)
        {
            const vfx::VFXNode* shapePinNode = startIsShapePin ? startNode : endNode;
            const vfx::VFXNode* outputPinNode = startIsShapePin ? endNode : startNode;
            uint32_t outputPinId = startIsShapePin ? endPinId : startPinId;

            if (!isOutputPin(outputPinId)) return false;
            if (shapePinNode->type != vfx::VFXNodeType::Emitter) return false;
            if (outputPinNode->type != vfx::VFXNodeType::Shape) return false;

            return true;
        }

        bool startIsOutput = isOutputPin(startPinId);
        bool endIsOutput = isOutputPin(endPinId);

        if (startIsOutput == endIsOutput) return false;

        const vfx::VFXNode* sourceNode = startIsOutput ? startNode : endNode;
        const vfx::VFXNode* targetNode = startIsOutput ? endNode : startNode;

        if (!vfx::hasOutputPin(sourceNode->type)) return false;
        if (!vfx::hasInputPin(targetNode->type)) return false;
        if (sourceNode->type == vfx::VFXNodeType::Shape) return false;

        return true;
    }

    void VFXGraphEditor::removeExistingLinks(uint32_t nodeId, const std::string& pin, bool isSource)
    {
        currentGraph->links.erase(
            std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                           [nodeId, &pin, isSource](const vfx::VFXNodeLink& existing)
                           {
                               if (isSource)
                               {
                                   return existing.sourceNodeId == nodeId;
                               }
                               return existing.targetNodeId == nodeId && existing.targetPin == pin;
                           }),
            currentGraph->links.end());
    }

    void VFXGraphEditor::handleCreation()
    {
        if (ed::BeginCreate())
        {
            ed::PinId startPinId, endPinId;
            if (ed::QueryNewLink(&startPinId, &endPinId))
            {
                uint32_t startId = fromEditorPinId(startPinId);
                uint32_t endId = fromEditorPinId(endPinId);

                if (startId && endId && canCreateLink(startId, endId))
                {
                    if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f))
                    {
                        vfx::VFXNodeLink newLink;
                        newLink.id = currentGraph->nextLinkId++;

                        bool startIsShapePin = isShapePin(startId);
                        bool endIsShapePin = isShapePin(endId);

                        if (startIsShapePin || endIsShapePin)
                        {
                            uint32_t emitterNodeId = startIsShapePin
                                                         ? getNodeIdFromShapePinId(startId)
                                                         : getNodeIdFromShapePinId(endId);
                            uint32_t shapeNodeId = startIsShapePin
                                                       ? getNodeIdFromPinId(endId)
                                                       : getNodeIdFromPinId(startId);

                            newLink.sourceNodeId = shapeNodeId;
                            newLink.targetNodeId = emitterNodeId;
                            newLink.sourcePin = "Output";
                            newLink.targetPin = "Shape";

                            removeExistingLinks(emitterNodeId, "Shape", false);
                            removeExistingLinks(shapeNodeId, "", true);
                        }
                        else
                        {
                            bool startIsOutput = isOutputPin(startId);
                            uint32_t sourceNodeId = startIsOutput
                                                        ? getNodeIdFromPinId(startId)
                                                        : getNodeIdFromPinId(endId);
                            uint32_t targetNodeId = startIsOutput
                                                        ? getNodeIdFromPinId(endId)
                                                        : getNodeIdFromPinId(startId);

                            newLink.sourceNodeId = sourceNodeId;
                            newLink.targetNodeId = targetNodeId;
                            newLink.sourcePin = "Output";
                            newLink.targetPin = "Input";

                            removeExistingLinks(targetNodeId, "Input", false);
                            removeExistingLinks(sourceNodeId, "", true);
                        }

                        currentGraph->links.push_back(newLink);
                        if (onGraphChanged) onGraphChanged();
                    }
                }
                else
                {
                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);

                    if (startId && endId)
                    {
                        uint32_t startNodeId = getNodeIdFromPinId(startId);
                        uint32_t endNodeId = getNodeIdFromPinId(endId);

                        std::string errorMsg;
                        if (startNodeId == endNodeId)
                        {
                            errorMsg = "Cannot connect node to itself";
                        }
                        else if (isOutputPin(startId) == isOutputPin(endId))
                        {
                            errorMsg = "Cannot connect same pin types";
                        }
                        else
                        {
                            errorMsg = "Invalid connection";
                        }

                        ImGui::BeginTooltip();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                        ImGui::TextUnformatted(errorMsg.c_str());
                        ImGui::PopStyleColor();
                        ImGui::EndTooltip();
                    }
                }
            }

            ed::PinId pinId;
            if (ed::QueryNewNode(&pinId))
            {
                ed::RejectNewItem();
            }
        }
        ed::EndCreate();
    }

    void VFXGraphEditor::handleDeletion()
    {
        if (ed::BeginDelete())
        {
            ed::LinkId linkId;
            while (ed::QueryDeletedLink(&linkId))
            {
                if (ed::AcceptDeletedItem())
                {
                    uint32_t id = fromEditorLinkId(linkId);
                    auto it = std::find_if(currentGraph->links.begin(), currentGraph->links.end(),
                                           [id](const vfx::VFXNodeLink& link) { return link.id == id; });
                    if (it != currentGraph->links.end())
                    {
                        currentGraph->links.erase(it);
                        if (onGraphChanged) onGraphChanged();
                    }
                }
            }

            ed::NodeId nodeId;
            while (ed::QueryDeletedNode(&nodeId))
            {
                uint32_t id = fromEditorNodeId(nodeId);
                auto node = currentGraph->findNode(id);
                if (node)
                {
                    if (node->type == vfx::VFXNodeType::Emitter ||
                        node->type == vfx::VFXNodeType::OutSystem)
                    {
                        ed::RejectDeletedItem();
                    }
                    else
                    {
                        if (ed::AcceptDeletedItem())
                        {
                            currentGraph->links.erase(
                                std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                                               [id](const vfx::VFXNodeLink& link)
                                               {
                                                   return link.sourceNodeId == id || link.targetNodeId == id;
                                               }),
                                currentGraph->links.end()
                            );

                            auto it = std::find_if(currentGraph->nodes.begin(), currentGraph->nodes.end(),
                                                   [id](const vfx::VFXNode& n) { return n.id == id; });
                            if (it != currentGraph->nodes.end())
                            {
                                currentGraph->nodes.erase(it);
                                if (onGraphChanged) onGraphChanged();
                            }
                        }
                    }
                }
            }
        }
        ed::EndDelete();
    }

    void VFXGraphEditor::handleClipboardShortcuts()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            return;

        const ImGuiIO& io = ImGui::GetIO();
        if (!io.KeyCtrl || io.WantTextInput)
            return;

        if (ImGui::IsKeyPressed(ImGuiKey_C, false))
            copySelection();
        else if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            pasteClipboard();
        else if (ImGui::IsKeyPressed(ImGuiKey_D, false))
            duplicateSelection();
    }

    void VFXGraphEditor::copySelection()
    {
        if (!currentGraph)
            return;

        int selectedCount = ed::GetSelectedObjectCount();
        if (selectedCount <= 0)
            return;

        std::vector<ed::NodeId> selectedNodes(static_cast<size_t>(selectedCount));
        int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), selectedCount);

        std::vector<vfx::VFXNode> copied;
        for (int i = 0; i < nodeCount; ++i)
        {
            uint32_t id = fromEditorNodeId(selectedNodes[static_cast<size_t>(i)]);
            const vfx::VFXNode* node = currentGraph->findNode(id);
            if (!node)
                continue;

            // Single-emitter format: Emitter and OutSystem are unique per graph
            if (node->type == vfx::VFXNodeType::Emitter ||
                node->type == vfx::VFXNodeType::OutSystem)
                continue;

            copied.push_back(*node);
        }

        if (copied.empty())
            return;

        clipboardNodes = std::move(copied);

        // Keep only links fully inside the copied set
        clipboardLinks.clear();
        auto isCopied = [this](uint32_t nodeId) {
            for (const auto& node : clipboardNodes)
                if (node.id == nodeId) return true;
            return false;
        };
        for (const auto& link : currentGraph->links)
        {
            if (isCopied(link.sourceNodeId) && isCopied(link.targetNodeId))
                clipboardLinks.push_back(link);
        }

        pasteCount = 0;
    }

    void VFXGraphEditor::pasteClipboard()
    {
        if (!currentGraph || clipboardNodes.empty())
            return;

        ++pasteCount;
        const float offset = 40.0f * static_cast<float>(pasteCount);

        std::map<uint32_t, uint32_t> idRemap;
        for (const auto& source : clipboardNodes)
        {
            vfx::VFXNode node = source;
            node.id = currentGraph->nextNodeId++;
            node.position += glm::vec2(offset, offset);
            idRemap[source.id] = node.id;
            currentGraph->nodes.push_back(std::move(node));
        }

        for (const auto& source : clipboardLinks)
        {
            vfx::VFXNodeLink link = source;
            link.id = currentGraph->nextLinkId++;
            link.sourceNodeId = idRemap[source.sourceNodeId];
            link.targetNodeId = idRemap[source.targetNodeId];
            currentGraph->links.push_back(std::move(link));
        }

        needsPositionInit = true;
        if (onGraphChanged) onGraphChanged();
    }

    void VFXGraphEditor::duplicateSelection()
    {
        copySelection();
        pasteClipboard();
    }

    void VFXGraphEditor::initializeModifierProperties(vfx::VFXNode& node)
    {
        switch (node.type) {
        case vfx::VFXNodeType::ColorOverLifetime:
            node.properties["gradient"] = {"gradient", vfx::VFXPropertyType::Gradient,
                vfx::VFXGradient::fromStartEnd(vfx::ModifierDefaults::COLOR_START, vfx::ModifierDefaults::COLOR_END), 0.0f, 1.0f};
            break;
        case vfx::VFXNodeType::SizeOverLifetime:
            node.properties["curve"] = {"curve", vfx::VFXPropertyType::Curve,
                vfx::VFXCurve::fromStartEnd(vfx::ModifierDefaults::SIZE_START_MULTIPLIER, vfx::ModifierDefaults::SIZE_END_MULTIPLIER), 0.0f, 10.0f};
            break;
        case vfx::VFXNodeType::SpeedOverLifetime:
            node.properties["curve"] = {"curve", vfx::VFXPropertyType::Curve,
                vfx::VFXCurve::fromStartEnd(vfx::ModifierDefaults::SPEED_START_MULTIPLIER, vfx::ModifierDefaults::SPEED_END_MULTIPLIER), 0.0f, 10.0f};
            break;
        case vfx::VFXNodeType::RotationOverLifetime:
            node.properties["curve"] = {"curve", vfx::VFXPropertyType::Curve,
                vfx::VFXCurve::constant(vfx::ModifierDefaults::ANGULAR_VELOCITY), -720.0f, 720.0f};
            break;
        case vfx::VFXNodeType::GlowOverLifetime:
            node.properties["curve"] = {"curve", vfx::VFXPropertyType::Curve, vfx::VFXCurve::fromStartEnd(1.0f, 0.0f), 0.0f, 10.0f};
            node.properties["glowColor"] = {"glowColor", vfx::VFXPropertyType::Color, glm::vec4(1.0f), 0.0f, 1.0f};
            break;
        default: break;
        }
    }

    void VFXGraphEditor::initializeForceProperties(vfx::VFXNode& node)
    {
        switch (node.type)
        {
        case vfx::VFXNodeType::ForceGravity:
            node.properties["direction"] = vfx::VFXProperty{
                "direction", vfx::VFXPropertyType::Vec3,
                vfx::ForceDefaults::GRAVITY_DIRECTION, -10.0f, 10.0f
            };
            node.properties["strength"] = vfx::VFXProperty{
                "strength", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::GRAVITY_STRENGTH, 0.0f, 50.0f
            };
            node.properties["localSpace"] = vfx::VFXProperty{
                "localSpace", vfx::VFXPropertyType::Bool,
                false, 0.0f, 1.0f
            };
            break;

        case vfx::VFXNodeType::ForceWind:
            node.properties["direction"] = vfx::VFXProperty{
                "direction", vfx::VFXPropertyType::Vec3,
                vfx::ForceDefaults::WIND_DIRECTION, -10.0f, 10.0f
            };
            node.properties["strength"] = vfx::VFXProperty{
                "strength", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::WIND_STRENGTH, 0.0f, 50.0f
            };
            node.properties["noiseStrength"] = vfx::VFXProperty{
                "noiseStrength", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::WIND_NOISE_STRENGTH, 0.0f, 10.0f
            };
            node.properties["noiseFrequency"] = vfx::VFXProperty{
                "noiseFrequency", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::WIND_NOISE_FREQUENCY, 0.1f, 10.0f
            };
            node.properties["localSpace"] = vfx::VFXProperty{
                "localSpace", vfx::VFXPropertyType::Bool,
                false, 0.0f, 1.0f
            };
            break;

        case vfx::VFXNodeType::ForceTurbulence:
            node.properties["strength"] = vfx::VFXProperty{
                "strength", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::TURBULENCE_STRENGTH, 0.0f, 50.0f
            };
            node.properties["frequency"] = vfx::VFXProperty{
                "frequency", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::TURBULENCE_FREQUENCY, 0.1f, 10.0f
            };
            node.properties["scrollSpeed"] = vfx::VFXProperty{
                "scrollSpeed", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::TURBULENCE_SCROLL_SPEED, 0.0f, 10.0f
            };
            node.properties["octaves"] = vfx::VFXProperty{
                "octaves", vfx::VFXPropertyType::Int,
                vfx::ForceDefaults::TURBULENCE_OCTAVES, 1.0f, 4.0f
            };
            node.properties["localSpace"] = vfx::VFXProperty{
                "localSpace", vfx::VFXPropertyType::Bool,
                false, 0.0f, 1.0f
            };
            break;

        case vfx::VFXNodeType::ForceVortex:
            node.properties["axis"] = vfx::VFXProperty{
                "axis", vfx::VFXPropertyType::Vec3,
                vfx::ForceDefaults::VORTEX_AXIS, -1.0f, 1.0f
            };
            node.properties["center"] = vfx::VFXProperty{
                "center", vfx::VFXPropertyType::Vec3,
                vfx::ForceDefaults::VORTEX_CENTER, -100.0f, 100.0f
            };
            node.properties["strength"] = vfx::VFXProperty{
                "strength", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::VORTEX_STRENGTH, 0.0f, 50.0f
            };
            node.properties["radialPull"] = vfx::VFXProperty{
                "radialPull", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::VORTEX_RADIAL_PULL, -50.0f, 50.0f
            };
            node.properties["localSpace"] = vfx::VFXProperty{
                "localSpace", vfx::VFXPropertyType::Bool,
                false, 0.0f, 1.0f
            };
            break;

        case vfx::VFXNodeType::ForceDrag:
            node.properties["linearCoeff"] = vfx::VFXProperty{
                "linearCoeff", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::DRAG_LINEAR_COEFF, 0.0f, 10.0f
            };
            node.properties["quadraticCoeff"] = vfx::VFXProperty{
                "quadraticCoeff", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::DRAG_QUADRATIC_COEFF, 0.0f, 10.0f
            };
            node.properties["localSpace"] = vfx::VFXProperty{
                "localSpace", vfx::VFXPropertyType::Bool,
                false, 0.0f, 1.0f
            };
            break;

        case vfx::VFXNodeType::ForcePointAttractor:
            node.properties["position"] = vfx::VFXProperty{
                "position", vfx::VFXPropertyType::Vec3,
                vfx::ForceDefaults::ATTRACTOR_POSITION, -100.0f, 100.0f
            };
            node.properties["strength"] = vfx::VFXProperty{
                "strength", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::ATTRACTOR_STRENGTH, -50.0f, 50.0f
            };
            node.properties["radius"] = vfx::VFXProperty{
                "radius", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::ATTRACTOR_RADIUS, 0.0f, 100.0f
            };
            node.properties["falloff"] = vfx::VFXProperty{
                "falloff", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::ATTRACTOR_FALLOFF, 0.0f, 10.0f
            };
            node.properties["killAtCenter"] = vfx::VFXProperty{
                "killAtCenter", vfx::VFXPropertyType::Bool,
                vfx::ForceDefaults::ATTRACTOR_KILL_AT_CENTER, 0.0f, 1.0f
            };
            node.properties["localSpace"] = vfx::VFXProperty{
                "localSpace", vfx::VFXPropertyType::Bool,
                false, 0.0f, 1.0f
            };
            break;

        case vfx::VFXNodeType::ForceCurlNoise:
            node.properties["strength"] = vfx::VFXProperty{
                "strength", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::CURLNOISE_STRENGTH, 0.0f, 50.0f
            };
            node.properties["frequency"] = vfx::VFXProperty{
                "frequency", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::CURLNOISE_FREQUENCY, 0.1f, 10.0f
            };
            node.properties["scrollSpeed"] = vfx::VFXProperty{
                "scrollSpeed", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::CURLNOISE_SCROLL_SPEED, 0.0f, 10.0f
            };
            node.properties["octaves"] = vfx::VFXProperty{
                "octaves", vfx::VFXPropertyType::Int,
                vfx::ForceDefaults::CURLNOISE_OCTAVES, 1.0f, 4.0f
            };
            node.properties["localSpace"] = vfx::VFXProperty{
                "localSpace", vfx::VFXPropertyType::Bool,
                false, 0.0f, 1.0f
            };
            break;

        case vfx::VFXNodeType::ForceKillVolume:
            node.properties["shape"] = vfx::VFXProperty{
                "shape", vfx::VFXPropertyType::String,
                std::string(vfx::ForceDefaults::KILLVOLUME_SHAPE), 0.0f, 1.0f
            };
            node.properties["center"] = vfx::VFXProperty{
                "center", vfx::VFXPropertyType::Vec3,
                vfx::ForceDefaults::KILLVOLUME_CENTER, -100.0f, 100.0f
            };
            node.properties["normal"] = vfx::VFXProperty{
                "normal", vfx::VFXPropertyType::Vec3,
                vfx::ForceDefaults::KILLVOLUME_NORMAL, -1.0f, 1.0f
            };
            node.properties["radius"] = vfx::VFXProperty{
                "radius", vfx::VFXPropertyType::Float,
                vfx::ForceDefaults::KILLVOLUME_RADIUS, 0.0f, 100.0f
            };
            node.properties["halfExtents"] = vfx::VFXProperty{
                "halfExtents", vfx::VFXPropertyType::Vec3,
                vfx::ForceDefaults::KILLVOLUME_HALF_EXTENTS, 0.0f, 100.0f
            };
            node.properties["invert"] = vfx::VFXProperty{
                "invert", vfx::VFXPropertyType::Bool,
                vfx::ForceDefaults::KILLVOLUME_INVERT, 0.0f, 1.0f
            };
            node.properties["localSpace"] = vfx::VFXProperty{
                "localSpace", vfx::VFXPropertyType::Bool,
                false, 0.0f, 1.0f
            };
            break;

        default:
            break;
        }
    }

    void VFXGraphEditor::initializeShapeProperties(vfx::VFXNode& node, vfx::ShapeType shapeType)
    {
        node.properties["emitFrom"] = vfx::VFXProperty{
            "emitFrom", vfx::VFXPropertyType::String,
            std::string("Volume"), 0.0f, 1.0f
        };

        node.properties["randomDirection"] = vfx::VFXProperty{
            "randomDirection", vfx::VFXPropertyType::Bool,
            false, 0.0f, 1.0f
        };

        vfx::applyShapeTypeProperties(node, shapeType);
    }

    void VFXGraphEditor::addNode(vfx::VFXNodeType type)
    {
        vfx::VFXNode newNode;
        newNode.id = currentGraph->nextNodeId++;
        newNode.type = type;
        newNode.name = getNodeTypeName(type);
        newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);

        if (vfx::isModifierNode(type))
        {
            initializeModifierProperties(newNode);
        }
        else if (vfx::isForceNode(type))
        {
            initializeForceProperties(newNode);
        }

        currentGraph->nodes.push_back(std::move(newNode));
        if (onGraphChanged) onGraphChanged();
    }

    void VFXGraphEditor::addShapeNode()
    {
        vfx::VFXNode newNode;
        newNode.id = currentGraph->nextNodeId++;
        newNode.type = vfx::VFXNodeType::Shape;
        newNode.name = "Shape";
        newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);
        initializeShapeProperties(newNode, vfx::ShapeType::Point);
        currentGraph->nodes.push_back(std::move(newNode));
        if (onGraphChanged) onGraphChanged();
    }

    void VFXGraphEditor::handleContextMenu()
    {
        if (showContextMenu)
        {
            ImGui::OpenPopup("VFXContextMenu");
            showContextMenu = false;
        }

        if (ImGui::BeginPopup("VFXContextMenu"))
        {
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !clipboardNodes.empty()))
            {
                pasteClipboard();
            }
            ImGui::Separator();

            ImGui::TextDisabled("Add Node");
            ImGui::Separator();

            if (ImGui::BeginMenu("Modifiers"))
            {
                if (ImGui::MenuItem("Color Over Lifetime")) addNode(vfx::VFXNodeType::ColorOverLifetime);
                if (ImGui::MenuItem("Size Over Lifetime")) addNode(vfx::VFXNodeType::SizeOverLifetime);
                if (ImGui::MenuItem("Speed Over Lifetime")) addNode(vfx::VFXNodeType::SpeedOverLifetime);
                if (ImGui::MenuItem("Rotation Over Lifetime")) addNode(vfx::VFXNodeType::RotationOverLifetime);
                if (ImGui::MenuItem("Glow Over Lifetime")) addNode(vfx::VFXNodeType::GlowOverLifetime);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Forces"))
            {
                if (ImGui::MenuItem("Gravity")) addNode(vfx::VFXNodeType::ForceGravity);
                if (ImGui::MenuItem("Wind")) addNode(vfx::VFXNodeType::ForceWind);
                if (ImGui::MenuItem("Turbulence")) addNode(vfx::VFXNodeType::ForceTurbulence);
                if (ImGui::MenuItem("Vortex")) addNode(vfx::VFXNodeType::ForceVortex);
                if (ImGui::MenuItem("Drag")) addNode(vfx::VFXNodeType::ForceDrag);
                if (ImGui::MenuItem("Point Attractor")) addNode(vfx::VFXNodeType::ForcePointAttractor);
                if (ImGui::MenuItem("Curl Noise")) addNode(vfx::VFXNodeType::ForceCurlNoise);
                if (ImGui::MenuItem("Kill Volume")) addNode(vfx::VFXNodeType::ForceKillVolume);
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Shape")) addShapeNode();

            ImGui::EndPopup();
        }
    }
}
