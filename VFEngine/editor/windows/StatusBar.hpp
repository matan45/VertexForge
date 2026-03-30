#pragma once
#include <cstdint>

struct ImGuiViewport;

namespace windows
{
    class StatusBar
    {
    private:
        float height = 24.0f;
        float refreshTimer = 0.0f;
        static constexpr float refreshInterval = 0.25f;

        uint32_t cachedDrawCalls = 0;
        uint64_t cachedVramMB = 0;

    public:
        void draw(const ImGuiViewport* viewport, float toolbarHeight);
        float getHeight() const { return height; }
    };
}
