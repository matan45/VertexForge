#pragma once
#include "data/FileOperationsTypes.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>

namespace windows
{
    // Unified drag-drop payload type for content browser items
    static constexpr const char* DND_CONTENT_BROWSER = "DND_CONTENT_BROWSER";

    // Payload data structure for content browser drag-drop
    struct ContentBrowserDragPayload
    {
        std::vector<std::string> paths;
        bool isMove = true;  // false = copy (when Ctrl is held)
    };

    // Visual feedback colors
    namespace DragDropColors
    {
        constexpr ImU32 DROP_TARGET_VALID = IM_COL32(70, 200, 70, 100);     // Green tint
        constexpr ImU32 DROP_TARGET_INVALID = IM_COL32(200, 70, 70, 100);   // Red tint
        constexpr ImU32 DROP_TARGET_BORDER_VALID = IM_COL32(70, 200, 70, 255);
        constexpr ImU32 DROP_TARGET_BORDER_INVALID = IM_COL32(200, 70, 70, 255);
        constexpr float CUT_ITEM_ALPHA = 0.4f;  // Dimmed appearance for cut items
    }

    // Singleton manager for drag-drop operations in the Content Browser
    class DragDropManager
    {
    public:
        static DragDropManager& instance();

        // Prevent copying
        DragDropManager(const DragDropManager&) = delete;
        DragDropManager& operator=(const DragDropManager&) = delete;

        // ============================================
        // Drag Operations
        // ============================================

        // Begin a drag operation with the given paths
        void beginDrag(const std::vector<std::string>& paths);

        // Set the drag payload for ImGui
        // Call this when starting a drag source
        void setDragPayload();

        // ============================================
        // Drop Operations
        // ============================================

        // Accept a drop at the target path
        // Returns the result of the file operation
        services::FileOperationResult acceptDrop(const std::string& targetPath);

        // Check if the current drag can be dropped at the target
        bool isValidDropTarget(const std::string& targetPath) const;

        // ============================================
        // Visual Feedback
        // ============================================

        // Draw a drag preview tooltip
        void drawDragPreview();

        // Draw a highlight rectangle for drop target
        // Call this within the drop target area
        static void drawDropTargetHighlight(const ImRect& rect, bool isValid);

        // Draw a border around a drop target
        static void drawDropTargetBorder(const ImRect& rect, bool isValid);

        // ============================================
        // State Queries
        // ============================================

        // Check if a drag operation is in progress
        bool isDragging() const;

        // Get the paths being dragged
        const std::vector<std::string>& getDragPaths() const;

        // Check if the operation is a move (vs copy)
        bool isMove() const;

        // ============================================
        // State Management
        // ============================================

        // Clear drag state (called when drag ends)
        void endDrag();

        // Update move/copy state based on modifier keys
        void updateModifiers();

    private:
        DragDropManager() = default;
        ~DragDropManager() = default;

        std::vector<std::string> dragPaths;
        bool dragging = false;
        bool moveOperation = true;  // false when Ctrl is held
    };
}
