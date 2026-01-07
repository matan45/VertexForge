#include "DragDropManager.hpp"
#include "events/EventDispatcher.hpp"
#include "events/FileOperationsEvents.hpp"
#include "print/EditorLogger.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace windows
{
    DragDropManager& DragDropManager::instance()
    {
        static DragDropManager instance;
        return instance;
    }

    void DragDropManager::beginDrag(const std::vector<std::string>& paths)
    {
        dragPaths = paths;
        dragging = true;
        moveOperation = true;  // Default to move
        updateModifiers();
    }

    void DragDropManager::setDragPayload()
    {
        if (dragPaths.empty())
        {
            return;
        }

        ContentBrowserDragPayload payload;
        payload.paths = dragPaths;
        payload.isMove = moveOperation;

        ImGui::SetDragDropPayload(DND_CONTENT_BROWSER, &payload, sizeof(ContentBrowserDragPayload));
    }

    services::FileOperationResult DragDropManager::acceptDrop(const std::string& targetPath)
    {
        services::FileOperationResult result;

        if (dragPaths.empty())
        {
            result.success = false;
            result.errorMessage = "No items being dragged";
            return result;
        }

        // Verify target is a directory
        if (!fs::is_directory(targetPath))
        {
            result.success = false;
            result.errorMessage = "Drop target is not a folder";
            return result;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        if (moveOperation)
        {
            events::fileops::MoveFilesCommand cmd;
            cmd.sourcePaths = dragPaths;
            cmd.destFolder = targetPath;
            result = dispatcher.execute(cmd);
        }
        else
        {
            events::fileops::CopyFilesCommand cmd;
            cmd.sourcePaths = dragPaths;
            cmd.destFolder = targetPath;
            result = dispatcher.execute(cmd);
        }

        // Clear drag state after drop
        endDrag();

        return result;
    }

    bool DragDropManager::isValidDropTarget(const std::string& targetPath) const
    {
        if (!dragging || dragPaths.empty())
        {
            return false;
        }

        // Target must be a directory
        if (!fs::exists(targetPath) || !fs::is_directory(targetPath))
        {
            return false;
        }

        fs::path target(targetPath);

        // Check each source path
        for (const auto& sourcePath : dragPaths)
        {
            fs::path source(sourcePath);

            // Can't drop on self
            if (fs::equivalent(source.parent_path(), target))
            {
                return false;
            }

            // Can't drop a folder into itself or its descendants
            if (fs::is_directory(source))
            {
                // Check if target is inside source
                auto relative = fs::relative(target, source);
                if (!relative.empty() && relative.native()[0] != '.')
                {
                    return false;
                }
            }

            // Check for name conflicts
            fs::path destFile = target / source.filename();
            if (fs::exists(destFile))
            {
                return false;  // Would need conflict resolution
            }
        }

        return true;
    }

    void DragDropManager::drawDragPreview()
    {
        if (!dragging || dragPaths.empty())
        {
            return;
        }

        ImGui::BeginTooltip();

        if (dragPaths.size() == 1)
        {
            fs::path p(dragPaths[0]);
            ImGui::Text("%s %s", moveOperation ? "Move:" : "Copy:", p.filename().string().c_str());
        }
        else
        {
            ImGui::Text("%s %zu items", moveOperation ? "Move" : "Copy", dragPaths.size());
        }

        if (!moveOperation)
        {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "(Ctrl held - Copy mode)");
        }

        ImGui::EndTooltip();
    }

    void DragDropManager::drawDropTargetHighlight(const ImRect& rect, bool isValid)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 color = isValid ? DragDropColors::DROP_TARGET_VALID : DragDropColors::DROP_TARGET_INVALID;
        drawList->AddRectFilled(rect.Min, rect.Max, color);
    }

    void DragDropManager::drawDropTargetBorder(const ImRect& rect, bool isValid)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 color = isValid ? DragDropColors::DROP_TARGET_BORDER_VALID : DragDropColors::DROP_TARGET_BORDER_INVALID;
        drawList->AddRect(rect.Min, rect.Max, color, 0.0f, 0, 2.0f);
    }

    bool DragDropManager::isDragging() const
    {
        return dragging;
    }

    const std::vector<std::string>& DragDropManager::getDragPaths() const
    {
        return dragPaths;
    }

    bool DragDropManager::isMove() const
    {
        return moveOperation;
    }

    void DragDropManager::endDrag()
    {
        dragPaths.clear();
        dragging = false;
        moveOperation = true;
    }

    void DragDropManager::updateModifiers()
    {
        // Check if Ctrl is held - switches to copy mode
        moveOperation = !ImGui::IsKeyDown(ImGuiMod_Ctrl);
    }
}
