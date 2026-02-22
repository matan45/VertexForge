#include "VFXGraphEditor.hpp"
#include "vfx/VFXShapeTypes.hpp"
#include "imgui.h"
#include <algorithm>

namespace ed = ax::NodeEditor;

namespace editor::graph {

    bool VFXGraphEditor::canCreateLink(uint32_t startPinId, uint32_t endPinId) const {
        bool startIsShapePin = isShapePin(startPinId);
        bool endIsShapePin = isShapePin(endPinId);

        uint32_t startNodeId = startIsShapePin ? getNodeIdFromShapePinId(startPinId) : getNodeIdFromPinId(startPinId);
        uint32_t endNodeId = endIsShapePin ? getNodeIdFromShapePinId(endPinId) : getNodeIdFromPinId(endPinId);

        if (startNodeId == endNodeId) return false;

        const vfx::VFXNode* startNode = currentGraph->findNode(startNodeId);
        const vfx::VFXNode* endNode = currentGraph->findNode(endNodeId);

        if (!startNode || !endNode) return false;

        if (startIsShapePin || endIsShapePin) {
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

    void VFXGraphEditor::removeExistingLinks(uint32_t nodeId, const std::string& pin, bool isSource) {
        currentGraph->links.erase(
            std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                [nodeId, &pin, isSource](const vfx::VFXNodeLink& existing) {
                    if (isSource) {
                        return existing.sourceNodeId == nodeId;
                    }
                    return existing.targetNodeId == nodeId && existing.targetPin == pin;
                }),
            currentGraph->links.end());
    }

    void VFXGraphEditor::handleCreation() {
        if (ed::BeginCreate()) {
            ed::PinId startPinId, endPinId;
            if (ed::QueryNewLink(&startPinId, &endPinId)) {
                uint32_t startId = fromEditorPinId(startPinId);
                uint32_t endId = fromEditorPinId(endPinId);

                if (startId && endId && canCreateLink(startId, endId)) {
                    if (ed::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
                        vfx::VFXNodeLink newLink;
                        newLink.id = currentGraph->nextLinkId++;

                        bool startIsShapePin = isShapePin(startId);
                        bool endIsShapePin = isShapePin(endId);

                        if (startIsShapePin || endIsShapePin) {
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
                        } else {
                            bool startIsOutput = isOutputPin(startId);
                            uint32_t sourceNodeId = startIsOutput ? getNodeIdFromPinId(startId) : getNodeIdFromPinId(endId);
                            uint32_t targetNodeId = startIsOutput ? getNodeIdFromPinId(endId) : getNodeIdFromPinId(startId);

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
                } else {
                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);

                    if (startId && endId) {
                        uint32_t startNodeId = getNodeIdFromPinId(startId);
                        uint32_t endNodeId = getNodeIdFromPinId(endId);

                        std::string errorMsg;
                        if (startNodeId == endNodeId) {
                            errorMsg = "Cannot connect node to itself";
                        } else if (isOutputPin(startId) == isOutputPin(endId)) {
                            errorMsg = "Cannot connect same pin types";
                        } else {
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
            if (ed::QueryNewNode(&pinId)) {
                ed::RejectNewItem();
            }
        }
        ed::EndCreate();
    }

    void VFXGraphEditor::handleDeletion() {
        if (ed::BeginDelete()) {
            ed::LinkId linkId;
            while (ed::QueryDeletedLink(&linkId)) {
                if (ed::AcceptDeletedItem()) {
                    uint32_t id = fromEditorLinkId(linkId);
                    auto it = std::find_if(currentGraph->links.begin(), currentGraph->links.end(),
                        [id](const vfx::VFXNodeLink& link) { return link.id == id; });
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
                if (node) {
                    if (node->type == vfx::VFXNodeType::Emitter ||
                        node->type == vfx::VFXNodeType::OutSystem) {
                        ed::RejectDeletedItem();
                    } else {
                        if (ed::AcceptDeletedItem()) {
                            currentGraph->links.erase(
                                std::remove_if(currentGraph->links.begin(), currentGraph->links.end(),
                                    [id](const vfx::VFXNodeLink& link) {
                                        return link.sourceNodeId == id || link.targetNodeId == id;
                                    }),
                                currentGraph->links.end()
                            );

                            auto it = std::find_if(currentGraph->nodes.begin(), currentGraph->nodes.end(),
                                [id](const vfx::VFXNode& n) { return n.id == id; });
                            if (it != currentGraph->nodes.end()) {
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

    static void initializeModifierProperties(vfx::VFXNode& node) {
        switch (node.type) {
            case vfx::VFXNodeType::ColorOverLifetime:
                node.properties["gradient"] = vfx::VFXProperty{
                    "gradient", vfx::VFXPropertyType::Gradient,
                    vfx::VFXGradient::fromStartEnd(
                        vfx::ModifierDefaults::COLOR_START, vfx::ModifierDefaults::COLOR_END),
                    0.0f, 1.0f
                };
                break;

            case vfx::VFXNodeType::SizeOverLifetime:
                node.properties["curve"] = vfx::VFXProperty{
                    "curve", vfx::VFXPropertyType::Curve,
                    vfx::VFXCurve::fromStartEnd(
                        vfx::ModifierDefaults::SIZE_START_MULTIPLIER,
                        vfx::ModifierDefaults::SIZE_END_MULTIPLIER),
                    0.0f, 10.0f
                };
                break;

            case vfx::VFXNodeType::SpeedOverLifetime:
                node.properties["curve"] = vfx::VFXProperty{
                    "curve", vfx::VFXPropertyType::Curve,
                    vfx::VFXCurve::fromStartEnd(
                        vfx::ModifierDefaults::SPEED_START_MULTIPLIER,
                        vfx::ModifierDefaults::SPEED_END_MULTIPLIER),
                    0.0f, 10.0f
                };
                break;

            case vfx::VFXNodeType::RotationOverLifetime:
                node.properties["curve"] = vfx::VFXProperty{
                    "curve", vfx::VFXPropertyType::Curve,
                    vfx::VFXCurve::constant(vfx::ModifierDefaults::ANGULAR_VELOCITY),
                    -720.0f, 720.0f
                };
                break;

            default:
                break;
        }
    }

    static void initializeForceProperties(vfx::VFXNode& node) {
        switch (node.type) {
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

            default:
                break;
        }
    }

    static void initializeShapeProperties(vfx::VFXNode& node, vfx::ShapeType shapeType) {
        node.properties["shapeType"] = vfx::VFXProperty{
            "shapeType", vfx::VFXPropertyType::String,
            std::string(vfx::shapeTypeToString(shapeType)), 0.0f, 1.0f
        };

        node.properties["emitFrom"] = vfx::VFXProperty{
            "emitFrom", vfx::VFXPropertyType::String,
            std::string("Volume"), 0.0f, 1.0f
        };

        node.properties["randomDirection"] = vfx::VFXProperty{
            "randomDirection", vfx::VFXPropertyType::Bool,
            false, 0.0f, 1.0f
        };

        switch (shapeType) {
            case vfx::ShapeType::Sphere:
                node.properties["radius"] = vfx::VFXProperty{
                    "radius", vfx::VFXPropertyType::Float,
                    vfx::ShapeDefaults::SPHERE_RADIUS, 0.01f, 100.0f
                };
                break;

            case vfx::ShapeType::Cone:
                node.properties["radius"] = vfx::VFXProperty{
                    "radius", vfx::VFXPropertyType::Float,
                    vfx::ShapeDefaults::CONE_BASE_RADIUS, 0.01f, 100.0f
                };
                node.properties["height"] = vfx::VFXProperty{
                    "height", vfx::VFXPropertyType::Float,
                    vfx::ShapeDefaults::CONE_HEIGHT, 0.01f, 100.0f
                };
                node.properties["angle"] = vfx::VFXProperty{
                    "angle", vfx::VFXPropertyType::Float,
                    vfx::ShapeDefaults::CONE_ANGLE, 0.0f, 1.57f  // 0 to 90 degrees in radians
                };
                break;

            case vfx::ShapeType::Box:
                node.properties["halfExtents"] = vfx::VFXProperty{
                    "halfExtents", vfx::VFXPropertyType::Vec3,
                    glm::vec3(vfx::ShapeDefaults::BOX_HALF_EXTENT_X,
                              vfx::ShapeDefaults::BOX_HALF_EXTENT_Y,
                              vfx::ShapeDefaults::BOX_HALF_EXTENT_Z), 0.01f, 100.0f
                };
                break;

            case vfx::ShapeType::Torus:
                node.properties["majorRadius"] = vfx::VFXProperty{
                    "majorRadius", vfx::VFXPropertyType::Float,
                    vfx::ShapeDefaults::TORUS_MAJOR_RADIUS, 0.01f, 100.0f
                };
                node.properties["minorRadius"] = vfx::VFXProperty{
                    "minorRadius", vfx::VFXPropertyType::Float,
                    vfx::ShapeDefaults::TORUS_MINOR_RADIUS, 0.01f, 50.0f
                };
                break;

            case vfx::ShapeType::Point:
            default:
                break;
        }
    }

    void VFXGraphEditor::addNode(vfx::VFXNodeType type) {
        vfx::VFXNode newNode;
        newNode.id = currentGraph->nextNodeId++;
        newNode.type = type;
        newNode.name = getNodeTypeName(type);
        newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);

        if (vfx::isModifierNode(type)) {
            initializeModifierProperties(newNode);
        } else if (vfx::isForceNode(type)) {
            initializeForceProperties(newNode);
        }

        currentGraph->nodes.push_back(std::move(newNode));
        if (onGraphChanged) onGraphChanged();
    }

    void VFXGraphEditor::addShapeNode(vfx::ShapeType shapeType, const std::string& name) {
        vfx::VFXNode newNode;
        newNode.id = currentGraph->nextNodeId++;
        newNode.type = vfx::VFXNodeType::Shape;
        newNode.name = name;
        newNode.position = glm::vec2(contextMenuPosition.x, contextMenuPosition.y);
        initializeShapeProperties(newNode, shapeType);
        currentGraph->nodes.push_back(std::move(newNode));
        if (onGraphChanged) onGraphChanged();
    }

    void VFXGraphEditor::handleContextMenu() {
        if (showContextMenu) {
            ImGui::OpenPopup("VFXContextMenu");
            showContextMenu = false;
        }

        if (ImGui::BeginPopup("VFXContextMenu")) {
            ImGui::TextDisabled("Add Node");
            ImGui::Separator();

            if (ImGui::BeginMenu("Modifiers")) {
                if (ImGui::MenuItem("Color Over Lifetime")) addNode(vfx::VFXNodeType::ColorOverLifetime);
                if (ImGui::MenuItem("Size Over Lifetime")) addNode(vfx::VFXNodeType::SizeOverLifetime);
                if (ImGui::MenuItem("Speed Over Lifetime")) addNode(vfx::VFXNodeType::SpeedOverLifetime);
                if (ImGui::MenuItem("Rotation Over Lifetime")) addNode(vfx::VFXNodeType::RotationOverLifetime);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Forces")) {
                if (ImGui::MenuItem("Gravity")) addNode(vfx::VFXNodeType::ForceGravity);
                if (ImGui::MenuItem("Wind")) addNode(vfx::VFXNodeType::ForceWind);
                if (ImGui::MenuItem("Turbulence")) addNode(vfx::VFXNodeType::ForceTurbulence);
                if (ImGui::MenuItem("Vortex")) addNode(vfx::VFXNodeType::ForceVortex);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Shapes")) {
                if (ImGui::MenuItem("Point")) addShapeNode(vfx::ShapeType::Point, "Point");
                if (ImGui::MenuItem("Sphere")) addShapeNode(vfx::ShapeType::Sphere, "Sphere");
                if (ImGui::MenuItem("Cone")) addShapeNode(vfx::ShapeType::Cone, "Cone");
                if (ImGui::MenuItem("Box")) addShapeNode(vfx::ShapeType::Box, "Box");
                if (ImGui::MenuItem("Torus")) addShapeNode(vfx::ShapeType::Torus, "Torus");
                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }
    }

}
