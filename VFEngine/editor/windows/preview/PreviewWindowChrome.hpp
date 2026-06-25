#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include "events/EventDispatcher.hpp"
#include "events/editor/EditorSettingsEvents.hpp"

namespace editor::preview
{
    // Shared window chrome for the asset preview windows (mesh/audio/material/
    // animation/AI-tree/prefab/font/...): one-click maximize, draggable panel
    // splitters, and remembered per-type window sizes.

    // Maximize/restore toggle for floating preview windows.
    // Usage: call preBegin() right before ImGui::Begin (after the window's own
    // SetNextWindowSize), then drawButton() inside the window.
    class WindowMaximizer
    {
    public:
        void preBegin()
        {
            if (!applyPending) return;
            ImGui::SetNextWindowPos(pendingPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(pendingSize, ImGuiCond_Always);
            applyPending = false;
        }

        // Right-aligns the toggle on the current layout line (works both on a
        // dedicated row and inside a menu bar). Hidden while docked — the dock
        // node owns the window geometry there.
        void drawButton()
        {
            lastSize = ImGui::GetWindowSize();

            if (ImGui::IsWindowDocked())
            {
                maximized = false;
                return;
            }

            // Manual resize while maximized drops back to normal mode so the
            // dragged size becomes the one we persist.
            if (maximized && !applyPending &&
                (std::fabs(lastSize.x - pendingSize.x) > 1.0f ||
                 std::fabs(lastSize.y - pendingSize.y) > 1.0f))
            {
                maximized = false;
            }

            const char* icon = maximized ? ICON_FA_COMPRESS : ICON_FA_EXPAND;
            const ImGuiStyle& style = ImGui::GetStyle();
            float buttonWidth = ImGui::CalcTextSize(icon).x + style.FramePadding.x * 2.0f;
            float targetX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonWidth;
            if (targetX > ImGui::GetCursorPosX())
            {
                ImGui::SetCursorPosX(targetX);
            }

            if (ImGui::SmallButton(icon))
            {
                if (!maximized)
                {
                    restorePos = ImGui::GetWindowPos();
                    restoreSize = ImGui::GetWindowSize();
                    computeMaximizedRect(pendingPos, pendingSize);
                    maximized = true;
                }
                else
                {
                    pendingPos = restorePos;
                    pendingSize = restoreSize;
                    maximized = false;
                }
                applyPending = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(maximized ? "Restore" : "Maximize");
            }
        }

        bool isMaximized() const { return maximized; }

        // Flags to OR into the owning window's ImGui::Begin while maximized:
        // pin it (NoMove) and drop the resize grips (NoResize) so a maximized
        // preview is an immovable full-editor fill (VK-1436). Returns 0 when
        // not maximized, so non-maximized behavior is byte-identical.
        ImGuiWindowFlags windowFlags() const
        {
            return maximized ? (ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)
                             : ImGuiWindowFlags_None;
        }

        // The size worth persisting: the pre-maximize size while maximized,
        // otherwise the current window size.
        ImVec2 effectiveSize() const { return maximized ? restoreSize : lastSize; }

    private:
        static void computeMaximizedRect(ImVec2& outPos, ImVec2& outSize)
        {
            // The dockspace host already excludes the toolbar and status bar.
            if (ImGuiWindow* host = ImGui::FindWindowByName("Vulkan Engine"))
            {
                outPos = host->Pos;
                outSize = host->Size;
                return;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            outPos = viewport->WorkPos;
            outSize = viewport->WorkSize;
        }

        bool maximized = false;
        bool applyPending = false;
        ImVec2 pendingPos{};
        ImVec2 pendingSize{};
        ImVec2 restorePos{};
        ImVec2 restoreSize{};
        ImVec2 lastSize{};
    };

    namespace detail
    {
        inline bool splitter(const char* strId, ImGuiAxis axis, float thickness,
                             float* size1, float* size2, float minSize1, float minSize2,
                             float longAxisSize)
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            ImGuiID id = window->GetID(strId);

            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (longAxisSize < 0.0f)
            {
                longAxisSize = (axis == ImGuiAxis_X) ? avail.y : avail.x;
            }

            ImRect bb;
            bb.Min = window->DC.CursorPos;
            bb.Max = (axis == ImGuiAxis_X)
                         ? ImVec2(bb.Min.x + thickness, bb.Min.y + longAxisSize)
                         : ImVec2(bb.Min.x + longAxisSize, bb.Min.y + thickness);

            bool changed = ImGui::SplitterBehavior(bb, id, axis, size1, size2,
                                                   minSize1, minSize2, 3.0f, 0.0f);
            ImGui::ItemSize((axis == ImGuiAxis_X) ? ImVec2(thickness, longAxisSize)
                                                  : ImVec2(longAxisSize, thickness));
            return changed;
        }
    }

    // Vertical divider between two side-by-side panels. Place between the two
    // BeginChild calls with zero-spacing SameLine on both sides:
    //   EndChild(); SameLine(0, 0); splitterV(...); SameLine(0, 0); BeginChild(...)
    inline bool splitterV(const char* strId, float thickness, float* size1, float* size2,
                          float minSize1, float minSize2, float height = -1.0f)
    {
        return detail::splitter(strId, ImGuiAxis_X, thickness, size1, size2,
                                minSize1, minSize2, height);
    }

    // Horizontal divider between two stacked panels.
    inline bool splitterH(const char* strId, float thickness, float* size1, float* size2,
                          float minSize1, float minSize2, float width = -1.0f)
    {
        return detail::splitter(strId, ImGuiAxis_Y, thickness, size1, size2,
                                minSize1, minSize2, width);
    }

    // Remembered per-window-type sizes, stored in EditorPreferences
    // (~/.vertexforge/editor_settings.json). Applied with ImGuiCond_FirstUseEver
    // so the per-asset imgui.ini entry still wins for previously opened assets.
    inline ImVec2 initialWindowSize(const char* windowType, ImVec2 fallback)
    {
        auto prefs = ::events::EventDispatcher::instance().query(
            ::events::editor::GetEditorSettingsQuery{});
        auto it = prefs.previewWindows.lastSizes.find(windowType);
        if (it == prefs.previewWindows.lastSizes.end())
        {
            return fallback;
        }
        return ImVec2(std::max(it->second.x, 300.0f), std::max(it->second.y, 200.0f));
    }

    inline void rememberWindowSize(const char* windowType, ImVec2 size)
    {
        if (size.x < 50.0f || size.y < 50.0f) return;

        auto prefs = ::events::EventDispatcher::instance().query(
            ::events::editor::GetEditorSettingsQuery{});
        glm::vec2& stored = prefs.previewWindows.lastSizes[windowType];
        if (std::fabs(stored.x - size.x) < 1.0f && std::fabs(stored.y - size.y) < 1.0f)
        {
            return;
        }
        stored = glm::vec2(size.x, size.y);

        ::events::editor::SetEditorSettingsCommand cmd;
        cmd.settings = prefs;
        ::events::EventDispatcher::instance().execute(cmd);
    }
}
