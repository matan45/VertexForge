#include "ConsoleLog.hpp"
#include "time/Timer.hpp"
#include "imgui.h"
#include <algorithm>
#include <sstream>

namespace windows
{
    void ConsoleLog::draw()
    {
        if (ImGui::Begin("Console"))
        {
            drawToolbar();
            ImGui::Separator();
            drawLogEntries();
        }
        ImGui::End();
    }

    void ConsoleLog::drawToolbar()
    {
        if (ImGui::Button("Clear"))
        {
            std::lock_guard<std::mutex> lock(util::imguiConsoleBufferMutex);
            util::imguiConsoleBuffer.clear();
            util::logSequenceCounter.store(0, std::memory_order_relaxed);
            cachedBuffer.clear();
            filteredView.clear();
            selectedEntries.clear();
            lastClickedIndex = -1;
            anchorIndex = -1;
            lastBufferSize = 0;
        }

        ImGui::SameLine();


        ImGui::SetNextItemWidth(100.0f);
        const char* filterLabels[] = {"All", "Trace", "Debug", "Info", "Warning", "Error"};
        if (ImGui::BeginCombo("##Filter", filterLabels[static_cast<int>(currentFilter)]))
        {
            for (int i = 0; i < static_cast<int>(std::size(filterLabels)); i++)
            {
                bool isSelected = (static_cast<int>(currentFilter) == i);
                if (ImGui::Selectable(filterLabels[i], isSelected))
                {
                    currentFilter = static_cast<LogFilter>(i);

                    selectedEntries.clear();
                    lastClickedIndex = -1;
                    anchorIndex = -1;
                }
                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (!selectedEntries.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu selected)", selectedEntries.size());
        }
    }

    void ConsoleLog::drawLogEntries()
    {
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        // Check if buffer changed (thread-safe size check)
        size_t currentBufferSize;
        {
            std::lock_guard<std::mutex> lock(util::imguiConsoleBufferMutex);
            currentBufferSize = util::imguiConsoleBuffer.size();
        }

        // Only copy buffer when new entries added (expensive operation)
        bool bufferChanged = currentBufferSize != lastBufferSize;
        if (bufferChanged)
        {
            std::lock_guard<std::mutex> lock(util::imguiConsoleBufferMutex);
            cachedBuffer = util::imguiConsoleBuffer;
            lastBufferSize = currentBufferSize;
        }

        // Rebuild filtered view when buffer OR filter changes
        if (bufferChanged || currentFilter != lastFilter)
        {
            rebuildFilteredView(cachedBuffer);
            lastFilter = currentFilter;
        }


        if (ImGui::IsWindowFocused())
        {
            // Ctrl+C to copy
            if (ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_C, false))
            {
                copySelectedToClipboard();
            }
            // Ctrl+A to select all visible
            if (ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_A, false))
            {
                selectedEntries.clear();
                for (const auto* entry : filteredView)
                {
                    selectedEntries.insert(entry->sequenceNumber);
                }
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                selectedEntries.clear();
                lastClickedIndex = -1;
                anchorIndex = -1;
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filteredView.size()));

        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
            {
                const util::LogEntry* entry = filteredView[i];
                bool isSelected = selectedEntries.count(entry->sequenceNumber) > 0;

                ImGui::PushID(entry);  // Use pointer for guaranteed unique ID

                ImVec4 textColor = getColorForLevel(entry->level);
                ImGui::PushStyleColor(ImGuiCol_Text, textColor);

                ImGui::Selectable(entry->message.c_str(), isSelected);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    bool ctrlHeld = ImGui::IsKeyDown(ImGuiMod_Ctrl);
                    bool shiftHeld = ImGui::IsKeyDown(ImGuiMod_Shift);
                    handleSelection(i, ctrlHeld, shiftHeld);
                }

                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
        clipper.End();

        drawContextMenu();

        if (selectedEntries.empty() && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
    }

    void ConsoleLog::drawContextMenu()
    {
        if (ImGui::BeginPopupContextWindow())
        {
            bool hasSelection = !selectedEntries.empty();

            if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection))
            {
                copySelectedToClipboard();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Select All", "Ctrl+A"))
            {
                selectedEntries.clear();
                for (const auto* entry : filteredView)
                {
                    selectedEntries.insert(entry->sequenceNumber);
                }
            }

            if (ImGui::MenuItem("Clear Selection", "Escape", false, hasSelection))
            {
                selectedEntries.clear();
                lastClickedIndex = -1;
                anchorIndex = -1;
            }

            ImGui::EndPopup();
        }
    }

    void ConsoleLog::handleSelection(size_t clickedIndex, bool ctrlHeld, bool shiftHeld)
    {
        if (clickedIndex >= filteredView.size()) return;

        uint64_t clickedSeq = filteredView[clickedIndex]->sequenceNumber;

        if (shiftHeld && anchorIndex >= 0)
        {
            selectedEntries.clear();
            selectRange(static_cast<size_t>(anchorIndex), clickedIndex);
        }
        else if (ctrlHeld)
        {
            if (selectedEntries.count(clickedSeq))
            {
                selectedEntries.erase(clickedSeq);
            }
            else
            {
                selectedEntries.insert(clickedSeq);
            }
            anchorIndex = static_cast<int64_t>(clickedIndex);
        }
        else
        {
            selectedEntries.clear();
            selectedEntries.insert(clickedSeq);
            anchorIndex = static_cast<int64_t>(clickedIndex);
        }

        lastClickedIndex = static_cast<int64_t>(clickedIndex);
    }

    void ConsoleLog::selectRange(size_t from, size_t to)
    {
        size_t start = std::min(from, to);
        size_t end = std::max(from, to);

        for (size_t i = start; i <= end && i < filteredView.size(); i++)
        {
            selectedEntries.insert(filteredView[i]->sequenceNumber);
        }
    }

    void ConsoleLog::copySelectedToClipboard()
    {
        if (selectedEntries.empty()) return;

        std::vector<const util::LogEntry*> orderedSelection;
        for (const auto* entry : filteredView)
        {
            if (selectedEntries.count(entry->sequenceNumber))
            {
                orderedSelection.push_back(entry);
            }
        }

        std::ostringstream oss;
        for (size_t i = 0; i < orderedSelection.size(); i++)
        {
            oss << orderedSelection[i]->message;
            if (i < orderedSelection.size() - 1)
            {
                oss << "\n";
            }
        }

        ImGui::SetClipboardText(oss.str().c_str());
    }

    void ConsoleLog::rebuildFilteredView(const std::vector<util::LogEntry>& buffer)
    {
        filteredView.clear();
        filteredView.reserve(buffer.size());

        for (const auto& entry : buffer)
        {
            if (passesFilter(entry))
            {
                filteredView.push_back(&entry);
            }
        }
    }

    bool ConsoleLog::passesFilter(const util::LogEntry& entry) const
    {
        switch (currentFilter)
        {
        case LogFilter::All:
            return true;
        case LogFilter::Trace:
            return entry.level == util::LogLevel::Trace;
        case LogFilter::Debug:
            return entry.level == util::LogLevel::Debug;
        case LogFilter::Info:
            return entry.level == util::LogLevel::Info;
        case LogFilter::Warning:
            return entry.level == util::LogLevel::Warning;
        case LogFilter::Error:
            return entry.level == util::LogLevel::Error;
        default:
            return true;
        }
    }

    ImVec4 ConsoleLog::getColorForLevel(util::LogLevel level) const
    {
        switch (level)
        {
        case util::LogLevel::Error:
            return ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // Red
        case util::LogLevel::Warning:
            return ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
        case util::LogLevel::Debug:
            return ImVec4(0.4f, 0.8f, 1.0f, 1.0f); // Cyan
        case util::LogLevel::Trace:
            return ImVec4(0.6f, 0.6f, 0.6f, 1.0f); // Gray
        case util::LogLevel::Info:
        default:
            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // White
        }
    }
}
