#include "VFXGraphEditor.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace ed = ax::NodeEditor;

namespace editor::graph
{
    VFXGraphEditor::VFXGraphEditor() = default;

    VFXGraphEditor::~VFXGraphEditor()
    {
        cleanUp();
    }

    void VFXGraphEditor::init()
    {
        if (editorContext) return;

        ed::Config config;
        config.SettingsFile = nullptr;
        config.NavigateButtonIndex = 1;
        editorContext = ed::CreateEditor(&config);
    }

    void VFXGraphEditor::cleanUp()
    {
        if (editorContext)
        {
            ed::DestroyEditor(editorContext);
            editorContext = nullptr;
        }
    }

    void VFXGraphEditor::setGraph(vfx::VFXGraph* graph)
    {
        currentGraph = graph;
        needsPositionInit = true;
    }

    void VFXGraphEditor::navigateToContent()
    {
        if (editorContext)
        {
            ed::SetCurrentEditor(editorContext);
            ed::NavigateToContent();
            ed::SetCurrentEditor(nullptr);
        }
    }

    void VFXGraphEditor::draw()
    {
        if (!editorContext || !currentGraph)
        {
            ImGui::TextDisabled("No VFX loaded");
            return;
        }

        ed::SetCurrentEditor(editorContext);

        ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();

        ed::Begin("VFXGraphEditor");

        float currentZoom = ed::GetCurrentZoom();

        if (pendingZoomSteps != 0)
        {
            float zoomStep = 0.1f;
            float newZoom;
            if (pendingZoomSteps > 0)
            {
                newZoom = currentZoom + zoomStep;
            }
            else
            {
                newZoom = currentZoom - zoomStep;
            }
            newZoom = std::round(newZoom * 10.0f) / 10.0f;
            newZoom = std::clamp(newZoom, 0.1f, 5.0f);
            ed::SetCurrentZoom(newZoom);
            currentZoom = ed::GetCurrentZoom();
            pendingZoomSteps = 0;
        }

        if (needsPositionInit)
        {
            for (const auto& node : currentGraph->nodes)
            {
                ed::SetNodePosition(toEditorNodeId(node.id), ImVec2(node.position.x, node.position.y));
            }
            needsPositionInit = false;
        }

        for (auto& node : currentGraph->nodes)
        {
            drawNode(node);
        }

        drawLinks();

        handleCreation();

        handleDeletion();

        ed::Suspend();
        if (ed::ShowBackgroundContextMenu())
        {
            contextMenuPosition = ed::ScreenToCanvas(ImGui::GetMousePos());
            showContextMenu = true;
        }
        ed::Resume();

        ed::End();

        drawZoomControls(canvasPos, canvasSize, currentZoom);

        if (ed::HasSelectionChanged())
        {
            ed::NodeId selectedNodes[1];
            int count = ed::GetSelectedNodes(selectedNodes, 1);
            selectedNodeId = count > 0 ? fromEditorNodeId(selectedNodes[0]) : 0;
        }

        for (auto& node : currentGraph->nodes)
        {
            ImVec2 pos = ed::GetNodePosition(toEditorNodeId(node.id));
            if (pos.x != node.position.x || pos.y != node.position.y)
            {
                node.position.x = pos.x;
                node.position.y = pos.y;
            }
        }

        ed::SetCurrentEditor(nullptr);

        handleContextMenu();
    }

    vfx::VFXNode* VFXGraphEditor::findNodeByPinId(uint32_t pinId)
    {
        uint32_t nodeId = getNodeIdFromPinId(pinId);
        return currentGraph->findNode(nodeId);
    }

    const vfx::VFXNode* VFXGraphEditor::findNodeByPinId(uint32_t pinId) const
    {
        uint32_t nodeId = getNodeIdFromPinId(pinId);
        return currentGraph->findNode(nodeId);
    }

    ImU32 VFXGraphEditor::getNodeHeaderColor(vfx::VFXNodeType type) const
    {
        switch (type)
        {
        case vfx::VFXNodeType::Emitter:
            return IM_COL32(100, 180, 100, 255);  // Green
        case vfx::VFXNodeType::OutSystem:
            return IM_COL32(180, 100, 100, 255);  // Red
        // Modifier nodes (VK-238)
        case vfx::VFXNodeType::ColorOverLifetime:
            return IM_COL32(180, 120, 200, 255);  // Purple
        case vfx::VFXNodeType::SizeOverLifetime:
            return IM_COL32(200, 160, 80, 255);   // Orange
        case vfx::VFXNodeType::SpeedOverLifetime:
            return IM_COL32(80, 160, 200, 255);   // Blue
        case vfx::VFXNodeType::RotationOverLifetime:
            return IM_COL32(200, 200, 80, 255);   // Yellow
        // Force nodes (VK-239)
        case vfx::VFXNodeType::ForceGravity:
        case vfx::VFXNodeType::ForceWind:
        case vfx::VFXNodeType::ForceTurbulence:
        case vfx::VFXNodeType::ForceVortex:
            return IM_COL32(80, 200, 200, 255);   // Cyan
        // Shape nodes (VK-240)
        case vfx::VFXNodeType::Shape:
            return IM_COL32(200, 100, 180, 255);  // Magenta
        default:
            return IM_COL32(100, 100, 100, 255);
        }
    }

    const char* VFXGraphEditor::getNodeTypeName(vfx::VFXNodeType type) const
    {
        switch (type)
        {
        case vfx::VFXNodeType::Emitter: return "Emitter";
        case vfx::VFXNodeType::OutSystem: return "Output";
        // Modifier nodes (VK-238)
        case vfx::VFXNodeType::ColorOverLifetime: return "Color Over Lifetime";
        case vfx::VFXNodeType::SizeOverLifetime: return "Size Over Lifetime";
        case vfx::VFXNodeType::SpeedOverLifetime: return "Speed Over Lifetime";
        case vfx::VFXNodeType::RotationOverLifetime: return "Rotation Over Lifetime";
        // Force nodes (VK-239)
        case vfx::VFXNodeType::ForceGravity: return "Gravity";
        case vfx::VFXNodeType::ForceWind: return "Wind";
        case vfx::VFXNodeType::ForceTurbulence: return "Turbulence";
        case vfx::VFXNodeType::ForceVortex: return "Vortex";
        // Shape nodes (VK-240)
        case vfx::VFXNodeType::Shape: return "Shape";
        default: return "Unknown";
        }
    }

    ImU32 VFXGraphEditor::getFlowPinColor() const
    {
        return IM_COL32(200, 200, 200, 255);
    }
}
