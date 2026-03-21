#pragma once

#include <imgui.h>
#include <vector>

namespace imguiPass
{
    /// Deep copy of ImDrawData for cross-thread transfer.
    /// Main thread: ImGui::Render() → snapshot()
    /// Render thread: ImGui_ImplVulkan_RenderDrawData() with the snapshot
    class ImDrawDataSnapshot
    {
    public:
        ImDrawDataSnapshot() = default;
        ~ImDrawDataSnapshot() { clear(); }

        ImDrawDataSnapshot(const ImDrawDataSnapshot&) = delete;
        ImDrawDataSnapshot& operator=(const ImDrawDataSnapshot&) = delete;

        ImDrawDataSnapshot(ImDrawDataSnapshot&& other) noexcept
            : drawData(other.drawData)
            , drawListCopies(std::move(other.drawListCopies))
            , drawListPtrs(std::move(other.drawListPtrs))
            , valid(other.valid)
        {
            other.valid = false;
            // Fix the pointer in drawData
            if (valid && !drawListPtrs.empty())
                drawData.CmdLists = ImVector<ImDrawList*>();
        }

        /// Snapshot the current ImDrawData (deep copy all draw lists).
        /// Call this on the main thread after ImGui::Render().
        void snapshot(const ImDrawData* src)
        {
            clear();

            if (!src || src->CmdListsCount == 0)
                return;

            // Copy scalar fields
            drawData.Valid = src->Valid;
            drawData.TotalIdxCount = src->TotalIdxCount;
            drawData.TotalVtxCount = src->TotalVtxCount;
            drawData.DisplayPos = src->DisplayPos;
            drawData.DisplaySize = src->DisplaySize;
            drawData.FramebufferScale = src->FramebufferScale;
            drawData.OwnerViewport = src->OwnerViewport;

            // Deep copy each draw list
            drawListCopies.resize(src->CmdListsCount);
            drawListPtrs.resize(src->CmdListsCount);

            for (int i = 0; i < src->CmdListsCount; i++)
            {
                drawListCopies[i] = src->CmdLists[i]->CloneOutput();
                drawListPtrs[i] = drawListCopies[i];
            }

            // Point drawData.CmdLists to our copies
            drawData.CmdLists.Data = drawListPtrs.data();
            drawData.CmdLists.Size = src->CmdListsCount;
            drawData.CmdLists.Capacity = src->CmdListsCount;

            valid = true;
        }

        /// Get the snapshotted draw data (for render thread).
        ImDrawData* getDrawData() { return valid ? &drawData : nullptr; }
        bool isValid() const { return valid; }

        void clear()
        {
            for (auto* list : drawListCopies)
            {
                if (list)
                {
                    IM_DELETE(list);
                }
            }
            drawListCopies.clear();
            drawListPtrs.clear();
            drawData = ImDrawData();
            valid = false;
        }

    private:
        ImDrawData drawData{};
        std::vector<ImDrawList*> drawListCopies;
        std::vector<ImDrawList*> drawListPtrs;
        bool valid = false;
    };
}
