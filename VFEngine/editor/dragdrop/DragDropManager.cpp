#include "DragDropManager.hpp"
#include "events/EventDispatcher.hpp"
#include "events/FileOperationsEvents.hpp"
#include "events/UndoRedoEvents.hpp"
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
        moveOperation = true;
        updateModifiers();
    }

    void DragDropManager::setDragPayload()
    {
        if (dragPaths.empty())
        {
            return;
        }

        // Use a simple marker - actual data is stored in DragDropManager
        static const char marker = 1;
        ImGui::SetDragDropPayload(DND_CONTENT_BROWSER, &marker, sizeof(marker));
    }

    services::FileOperationResult DragDropManager::acceptDrop(const std::string& targetPathRef)
    {
        // Make copies - references may become invalid if UI refreshes during operation
        std::string targetPath = targetPathRef;
        std::vector<std::string> pathsToMove = dragPaths;
        bool isMove = moveOperation;

        services::FileOperationResult result;

        if (pathsToMove.empty())
        {
            result.success = false;
            result.errorMessage = "No items being dragged";
            return result;
        }

        if (!fs::is_directory(targetPath))
        {
            result.success = false;
            result.errorMessage = "Drop target is not a folder";
            return result;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // End drag early to prevent re-entry issues
        endDrag();

        // Begin batch for grouped undo
        if (pathsToMove.size() > 1)
        {
            events::undoredo::BeginBatchCommand batchCmd;
            batchCmd.description = (isMove ? "Move " : "Copy ") + std::to_string(pathsToMove.size()) + " items";
            dispatcher.execute(batchCmd);
        }

        result.success = true;
        for (const auto& sourcePath : pathsToMove)
        {
            services::FileOperationResult opResult;
            if (isMove)
            {
                events::fileops::MoveFileCommand cmd;
                cmd.sourcePath = sourcePath;
                cmd.destPath = targetPath;
                opResult = dispatcher.execute(cmd);
            }
            else
            {
                events::fileops::CopyFileCommand cmd;
                cmd.sourcePath = sourcePath;
                cmd.destPath = targetPath;
                opResult = dispatcher.execute(cmd);
            }

            if (!opResult.success)
            {
                result.success = false;
                result.errorMessage += opResult.errorMessage + "\n";
            }
            result.updatedReferences.insert(result.updatedReferences.end(),
                opResult.updatedReferences.begin(), opResult.updatedReferences.end());
        }

        // End batch
        if (pathsToMove.size() > 1)
        {
            dispatcher.execute(events::undoredo::EndBatchCommand{});
        }

        return result;
    }

    bool DragDropManager::isValidDropTarget(const std::string& targetPath) const
    {
        if (dragPaths.empty())
        {
            return false;
        }

        if (!fs::exists(targetPath) || !fs::is_directory(targetPath))
        {
            return false;
        }

        fs::path target(targetPath);

        for (const auto& sourcePath : dragPaths)
        {
            fs::path source(sourcePath);

            if (fs::equivalent(source.parent_path(), target))
            {
                return false;
            }

            if (fs::is_directory(source))
            {
                auto relative = fs::relative(target, source);
                if (!relative.empty() && relative.native()[0] != '.')
                {
                    return false;
                }
            }

            fs::path destFile = target / source.filename();
            if (fs::exists(destFile))
            {
                return false;
            }
        }

        return true;
    }

    void DragDropManager::drawDragPreview()
    {
        if (dragPaths.empty())
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

    void DragDropManager::endDrag()
    {
        dragPaths.clear();
        moveOperation = true;
    }

    void DragDropManager::updateModifiers()
    {
        moveOperation = !ImGui::IsKeyDown(ImGuiMod_Ctrl);
    }
}
