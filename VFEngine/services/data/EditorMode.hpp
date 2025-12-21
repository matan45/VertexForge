#pragma once
#include <cstdint>

namespace services {
    enum class EditorMode : uint8_t {
        Edit,       // Full editor functionality
        Play,       // Simulating runtime - disable editor-only features
        Pause       // Play mode paused (future expansion)
    };
}
