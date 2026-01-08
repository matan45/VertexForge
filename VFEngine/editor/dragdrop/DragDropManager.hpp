#pragma once
#include "data/FileOperationsTypes.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>

namespace windows
{
    static constexpr const char* DND_CONTENT_BROWSER = "DND_CONTENT_BROWSER";

    namespace DragDropColors
    {
        constexpr ImU32 DROP_TARGET_VALID = IM_COL32(70, 200, 70, 100);
        constexpr ImU32 DROP_TARGET_INVALID = IM_COL32(200, 70, 70, 100);
        constexpr float CUT_ITEM_ALPHA = 0.4f;
    }


    class DragDropManager
    {
    private:
        std::vector<std::string> dragPaths;
        bool moveOperation = true;

    public:
        static DragDropManager& instance();

        DragDropManager(const DragDropManager&) = delete;
        DragDropManager& operator=(const DragDropManager&) = delete;

        void beginDrag(const std::vector<std::string>& paths);

        void setDragPayload();

        services::FileOperationResult acceptDrop(const std::string& targetPath);

        bool isValidDropTarget(const std::string& targetPath) const;

        void drawDragPreview();

        static void drawDropTargetHighlight(const ImRect& rect, bool isValid);

        void endDrag();

        void updateModifiers();

    private:
        DragDropManager() = default;
        ~DragDropManager() = default;
    };
}
