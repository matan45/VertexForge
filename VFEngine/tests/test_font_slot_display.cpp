#include <doctest.h>
#include <windows/details/FontSlotDisplay.hpp>
#include <string>

// ============================================================
// VK-1630 — inspector font slot display logic.
//
// VK-1628 gave every text emission site a default-font fallback that keys off
// AssetRef::resolve().empty(), NOT !isValid(). That makes two very different
// authoring states render identically: "never assigned" and "assigned but the
// asset is gone". The inspector must keep them apart, so these tests pin the
// tri-state classification. The widget itself (FontSlotWidget) is ImGui code
// and cannot be tested here — Tests compiles no editor .cpp and links no
// Editor project — which is exactly why the pure part lives in its own header.
// ============================================================

using namespace windows::details;

TEST_CASE("a null GUID classifies as Unset regardless of the resolved path")
{
    CHECK(fontSlotState(false, "") == FontSlotState::Unset);

    // Validity dominates: a stale path string must never promote an unset ref.
    CHECK(fontSlotState(false, "C:/proj/assets/Roboto.vfFont") == FontSlotState::Unset);
}

TEST_CASE("a GUID that resolves to a path classifies as Assigned")
{
    CHECK(fontSlotState(true, "C:/proj/assets/Roboto.vfFont") == FontSlotState::Assigned);
    CHECK(fontSlotState(true, "x") == FontSlotState::Assigned);
}

TEST_CASE("a valid GUID with an empty resolve classifies as Missing, not Unset")
{
    // The regression that matters. If Missing ever collapses into Unset, a
    // deleted or moved .vfFont silently reads as a deliberate "(Default)".
    CHECK(fontSlotState(true, "") == FontSlotState::Missing);
    CHECK(fontSlotState(true, "") != FontSlotState::Unset);

    // Constexpr so the classification is a compile-time fact, not just a
    // runtime one — cheap insurance against someone adding a side effect.
    static_assert(fontSlotState(true, "") == FontSlotState::Missing);
    static_assert(fontSlotState(false, "") == FontSlotState::Unset);
    static_assert(fontSlotState(true, "a.vfFont") == FontSlotState::Assigned);
}

TEST_CASE("fontSlotFileName strips both separator styles")
{
    SUBCASE("forward slashes")
    {
        CHECK(fontSlotFileName("C:/proj/assets/Roboto.vfFont") == "Roboto.vfFont");
    }
    SUBCASE("backslashes")
    {
        CHECK(fontSlotFileName("C:\\proj\\assets\\Roboto.vfFont") == "Roboto.vfFont");
    }
    SUBCASE("mixed separators take the last one")
    {
        CHECK(fontSlotFileName("C:\\a/b\\c.vfFont") == "c.vfFont");
        CHECK(fontSlotFileName("C:/a\\b/c.vfFont") == "c.vfFont");
    }
    SUBCASE("a bare file name is returned unchanged")
    {
        CHECK(fontSlotFileName("Roboto.vfFont") == "Roboto.vfFont");
    }
    SUBCASE("degenerate inputs do not throw or read out of range")
    {
        CHECK(fontSlotFileName("") == "");
        CHECK(fontSlotFileName("a/b/") == "");
        CHECK(fontSlotFileName("/") == "");
    }
}

TEST_CASE("font slot labels pin the shared UX vocabulary")
{
    // "(Default)" is the engine-wide string for "no override, a fallback is
    // substituted". If someone reverts this to "No font selected" the inspector
    // starts lying about VK-1628's fallback again.
    CHECK(std::string(FONT_SLOT_DEFAULT_LABEL) == "(Default)");

    // The missing state must stay visibly distinct from the default state.
    CHECK(std::string(FONT_SLOT_MISSING_LABEL).empty() == false);
    CHECK(std::string(FONT_SLOT_MISSING_LABEL) != std::string(FONT_SLOT_DEFAULT_LABEL));

    CHECK(std::string(FONT_SLOT_DEFAULT_TOOLTIP).empty() == false);
    CHECK(std::string(FONT_SLOT_MISSING_TOOLTIP).empty() == false);
    CHECK(std::string(FONT_SLOT_MISSING_TOOLTIP) != std::string(FONT_SLOT_DEFAULT_TOOLTIP));
}
