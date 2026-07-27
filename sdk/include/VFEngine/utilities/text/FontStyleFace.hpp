#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// VK-1636: real Bold / Italic / BoldItalic faces via sibling .vfFont files.
//
// Bold and italic have always been synthesized - a threshold bias in the fragment
// shader (bit 0) and a 12-degree quad shear in the vertex shader (bit 1). A real
// typeface ships designed faces instead, and this is the naming convention that
// finds them: "Roboto.vfFont" + Bold -> "Roboto-Bold.vfFont".
//
// Everything here is PURE - no filesystem, no Vulkan, no font loading. The caller
// (TextFontCache) owns the probing and the memo; this file owns the policy, which
// is the part worth unit-testing.
namespace text
{
    // These are the shader's styleFlags bits verbatim. Do not renumber:
    // ui_text.glsl / text.glsl test (flags & 1u) for bold and (flags & 2u) for italic.
    inline constexpr uint32_t STYLE_BOLD = 0x1u;
    inline constexpr uint32_t STYLE_ITALIC = 0x2u;
    inline constexpr uint32_t STYLE_MASK = STYLE_BOLD | STYLE_ITALIC;

    // Takes the raw underlying value of components::FontStyle rather than the enum
    // so this header stays free of <entt/entt.hpp> (which UIComponents.hpp pulls in)
    // and can be included from Tests and from the Utilities module alike.
    // The enum is Normal=0, Bold=1, Italic=2, BoldItalic=3 - the same numbering the
    // DTOs already document - so the value IS the mask, but spell it out rather than
    // relying on that coincidence.
    [[nodiscard]] constexpr uint32_t styleBitsFromFontStyle(uint8_t fontStyle) noexcept
    {
        switch (fontStyle)
        {
        case 1: return STYLE_BOLD;
        case 2: return STYLE_ITALIC;
        case 3: return STYLE_BOLD | STYLE_ITALIC;
        default: return 0u;
        }
    }

    // Order in which to give up on an exact match. For Bold|Italic:
    //   {Bold|Italic} -> {Bold} -> {Italic} -> {}
    // A real Bold with a synthesized shear beats a real Italic with synthesized
    // weight: the shear is a passable italic, but thickening a Regular is not a
    // passable bold. The trailing {} is the base face and is never probed - it is
    // the implicit fallback - so it is NOT included in the returned list.
    [[nodiscard]] std::vector<uint32_t> styleDowngradeOrder(uint32_t styleBits);

    // Sibling file names for `styleBits`, in priority order. Keeps basePath's
    // directory and its original extension spelling (".vfFont" and ".vffont" both
    // round-trip). Returns empty for styleBits == 0 or an empty path.
    //
    //   "assets/Roboto.vfFont" + STYLE_BOLD ->
    //     assets/Roboto-Bold.vfFont, assets/Roboto_Bold.vfFont, assets/RobotoBold.vfFont, ...
    [[nodiscard]] std::vector<std::string> styledPathCandidates(std::string_view basePath,
                                                                uint32_t styleBits);

    // One probed candidate. `satisfiedBits` is what this face covers on its own
    // (e.g. STYLE_BOLD for a "-Bold" sibling); `exists` is the on-disk answer and
    // `resident` whether its atlas is already uploaded.
    struct StyleFaceProbe
    {
        uint32_t satisfiedBits = 0;
        bool exists = false;
        bool resident = false;
    };

    struct StyleChoice
    {
        // Index into the probe list, or -1 to use the base face.
        int index = -1;
        // Style bits the chosen face does NOT provide, i.e. what the shader must
        // still synthesize.
        uint32_t synthesizedBits = 0;
    };

    // Picks the first probe that both exists AND is resident.
    //
    // A face that exists but is still loading deliberately loses to nothing: we fall
    // back to the base face and keep synthesizing the FULL request, so the glyphs do
    // not visibly pop from Regular to Bold when the atlas lands a few frames later.
    // Returning the styled key before its atlas is resident would also violate
    // TextFontCache's rule that a descriptor is never keyed by a non-resident path.
    [[nodiscard]] StyleChoice chooseStyleFace(uint32_t requestedBits,
                                              std::span<const StyleFaceProbe> probes) noexcept;
}
