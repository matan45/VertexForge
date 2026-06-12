#include "AssetDropTarget.hpp"
#include "DragDropManager.hpp"
#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>

namespace windows
{
    namespace
    {
        bool extensionMatches(const std::string& path,
                              std::initializer_list<std::string_view> extensions)
        {
            std::string ext = std::filesystem::path(path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            for (std::string_view candidate : extensions)
            {
                std::string lowered(candidate);
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
                if (ext == lowered)
                    return true;
            }
            return false;
        }
    }

    std::optional<std::string> acceptAssetDropOnLastItem(
        const char* idSuffix,
        std::initializer_list<std::string_view> extensions)
    {
        ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (!ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID(idSuffix)))
            return std::nullopt;

        std::optional<std::string> dropped;
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
            DND_CONTENT_BROWSER,
            ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
        if (payload)
        {
            const auto& dragPaths = DragDropManager::instance().getDragPaths();
            bool valid = dragPaths.size() == 1 && extensionMatches(dragPaths[0], extensions);
            DragDropManager::drawDropTargetHighlight(rect, valid);

            if (valid && payload->IsDelivery())
            {
                dropped = dragPaths[0];
                DragDropManager::instance().endDrag();
            }
        }
        ImGui::EndDragDropTarget();
        return dropped;
    }
}
