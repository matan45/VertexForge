#include "print/Log.hpp"
#include "FontSlotWidget.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <string>

namespace windows::details
{
    namespace
    {
        // Shared by the drop path and the dialog path (same factoring as
        // UIImageDrawer::assignUIImageTexture). Both hand us a file that
        // exists — the content browser only drags entries it enumerated, and an
        // nfd *open* dialog is FOS_FILEMUSTEXIST — so the old std::ifstream
        // probe is gone. The failure that actually happens is fromPath()
        // refusing to mint a GUID; assigning its invalid() result would blank
        // the slot, which now looks identical to a deliberate "Use Default".
        bool assignFont(asset::AssetRef& fontRef, const std::string& path)
        {
            asset::AssetRef next = asset::AssetRef::fromPath(path);
            if (!next.isValid())
            {
                vfLogError("Could not reference font asset "
                           "(is it inside the loaded project?): {}", path);
                return false;
            }
            if (next == fontRef)
            {
                return false; // re-picking the same font is not an edit
            }
            fontRef = next;
            return true;
        }
    }

    bool drawFontSlot(asset::AssetRef& fontRef, const char* idSuffix, const FontSlotOptions& opts)
    {
        bool changed = false;
        ImGui::PushID(idSuffix);

        // Resolve EXACTLY once: an unresolvable GUID is deliberately not cached
        // (AssetRef::resolve), so every extra call re-runs tryResolveByMetaScan.
        const std::string& resolved = fontRef.resolve();
        const FontSlotState state = fontSlotState(fontRef.isValid(), resolved);

        // Owns the Assigned-state tooltip. It must be a COPY, not resolved.c_str():
        // `resolved` binds to fontRef's internal cache, and a drop delivered a few
        // lines below reassigns fontRef — which would leave the pointer aimed at a
        // reassigned (or reallocated) buffer by the time the tooltip is drawn.
        std::string assignedTooltip;

        const char* hint = nullptr;
        switch (state)
        {
        case FontSlotState::Assigned:
            ImGui::Text("Font: %s", fontSlotFileName(resolved).c_str());
            assignedTooltip = resolved;
            hint = assignedTooltip.c_str();
            break;
        case FontSlotState::Missing:
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Font: %s", FONT_SLOT_MISSING_LABEL);
            hint = FONT_SLOT_MISSING_TOOLTIP;
            break;
        case FontSlotState::Unset:
            ImGui::TextDisabled("%s", opts.emptyLabel);
            hint = opts.emptyTooltip;
            break;
        }

        // Must run on the line above's item rect, BEFORE anything that submits
        // another item — a tooltip window would clobber the last-item data.
        if (auto dropped = acceptAssetDropOnLastItem("FontDrop", {".vffont"}))
        {
            changed = assignFont(fontRef, *dropped);
        }

        // GetDragDropPayload() is the public drag-active test; IsDragDropActive()
        // lives in imgui_internal.h, which this TU deliberately does not pull in.
        if (hint && ImGui::IsItemHovered() && ImGui::GetDragDropPayload() == nullptr)
        {
            ImGui::SetTooltip("%s", hint);
        }

        if (ImGui::Button("Select Font"))
        {
            nfd::FileDialog fileDialog;
            std::string path = fileDialog.openFileDialog(
                {{L"VF Font Files (*.vfFont)", L"*.vfFont"}});
            if (!path.empty())
            {
                changed = assignFont(fontRef, path) || changed;
            }
        }

        ImGui::SameLine();
        const bool alreadyDefault = !fontRef.isValid();
        if (alreadyDefault) ImGui::BeginDisabled();
        if (ImGui::Button("Use Default"))
        {
            fontRef = asset::AssetRef::invalid();
            changed = true;
        }
        if (alreadyDefault) ImGui::EndDisabled();
        if (!alreadyDefault && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Drop the reference; text renders with the built-in default font.");
        }

        ImGui::PopID();
        return changed;
    }
}
