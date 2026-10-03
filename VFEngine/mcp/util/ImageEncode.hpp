#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mcp::util
{
    // Image helpers for tool results (viewport_screenshot). CPU-only and thread-safe:
    // they run on the HTTP connection thread, never on the render thread.

    // RGBA16F halves (row-major, 4 per pixel) -> RGBA8 as the editor shows them.
    // Each colour channel is clamped to [0,1], then sRGB-encoded and scaled by 255
    // with rounding: ImGui samples the viewport image and draws it into the
    // B8G8R8A8_SRGB swapchain (SwapChain::chooseSurfaceFormat), so the hardware
    // applies the linear->sRGB encode to the stored values before display.
    // Alpha is forced to 255. Returns empty if the input is smaller than w*h*4.
    std::vector<uint8_t> halfRgbaToRgba8(const std::vector<uint16_t>& rgba16f, uint32_t width, uint32_t height);

    // Box-filter downscale so the width is at most maxWidth, preserving the aspect
    // ratio (height rounded, at least 1). Returns a copy of `src` with unchanged
    // dimensions when width <= maxWidth or maxWidth == 0, and empty (0x0) when `src`
    // is smaller than width*height*4.
    std::vector<uint8_t> downscaleRgba8(const std::vector<uint8_t>& src, uint32_t width, uint32_t height,
                                        uint32_t maxWidth, uint32_t& outWidth, uint32_t& outHeight);

    // RGBA8 -> PNG file bytes. Returns empty on failure.
    std::vector<uint8_t> encodePng(const std::vector<uint8_t>& rgba8, uint32_t width, uint32_t height);

    // RFC 4648 base64 (standard alphabet, '=' padding).
    std::string base64Encode(const uint8_t* data, std::size_t size);
}
