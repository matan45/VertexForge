#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "print/LogEntry.hpp"
#include "imgui.h"
#include <unordered_set>
#include <vector>

namespace windows
{
    enum class LogFilter : int
    {
        All = 0,
        Info = 1,
        Warning = 2,
        Error = 3
    };

    class ConsoleLog : public controllers::imguiHandler::ImguiWindow
    {
    private:
        LogFilter currentFilter = LogFilter::All;

        std::unordered_set<uint64_t> selectedEntries;
        int64_t lastClickedIndex = -1;
        int64_t anchorIndex = -1;

        std::vector<util::LogEntry> cachedBuffer;
        std::vector<const util::LogEntry*> filteredView;
        size_t lastBufferSize = 0;
        LogFilter lastFilter = LogFilter::All;

    public:
        explicit ConsoleLog() = default;
        ~ConsoleLog() override = default;

        void draw() override;

    private:
        void drawToolbar();
        void drawLogEntries();
        void drawContextMenu();

        void handleSelection(size_t clickedIndex, bool ctrlHeld, bool shiftHeld);
        void selectRange(size_t from, size_t to);
        void copySelectedToClipboard();

        void rebuildFilteredView(const std::vector<util::LogEntry>& buffer);
        bool passesFilter(const util::LogEntry& entry) const;

        ImVec4 getColorForLevel(util::LogLevel level) const;
    };
}
