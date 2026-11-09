#pragma once
#include <cstdint>
#include <vector>

namespace services
{
    // VK-1651: CPU copy of the editor viewport's final colour image. The pixels are
    // the raw RGBA16F halves of the image the viewport panel samples (~0..1). The
    // editor displays them through the sRGB swapchain, so a faithful 8-bit copy
    // applies the linear->sRGB encode (mcp::util::halfRgbaToRgba8). Row 0 is the top row.
    struct ViewportReadbackResult
    {
        enum class State : uint8_t { Pending, Ready, Failed };

        State state = State::Pending;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint16_t> rgba16f;  // width * height * 4 halves when Ready
    };
}
