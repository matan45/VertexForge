#pragma once
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace windows
{
    // Makes the last submitted ImGui item a drop target for single-asset drags
    // from the content browser. Uses a custom-rect target so it also works on
    // ID-less items (plain ImGui::Text rows). Highlights the item green when
    // the dragged asset's extension matches (red otherwise) and returns the
    // dragged path on delivery. Extensions are matched case-insensitively and
    // include the dot (".vfmesh").
    std::optional<std::string> acceptAssetDropOnLastItem(
        const char* idSuffix,
        std::initializer_list<std::string_view> extensions);
}
