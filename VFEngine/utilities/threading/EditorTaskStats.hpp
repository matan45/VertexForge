#pragma once
#include <atomic>
#include <cstdint>

namespace threading {
    // Always-on (independent of TaskProfiler) timing of editor-only work.
    // Written each frame by the ImGuiDraw task in EditorFrameTaskGraph.
    // Runtime never writes it (no ImGuiDraw task) -> stays 0, which is correct.
    struct EditorTaskStats {
        static inline std::atomic<uint64_t> imguiDrawDurationNs{0};
    };
}
