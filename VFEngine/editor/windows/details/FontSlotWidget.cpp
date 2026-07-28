#include "print/Log.hpp"
#include "FontSlotWidget.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"
#include "nfd/FileDialog.hpp"
#include <imgui.h>
#include <filesystem>
#include <optional>
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
        bool assignFont(asset::AssetRef& fontRef, const std::string& path,
                        const FontSlotOptions& opts)
        {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec) ||
                !std::filesystem::is_regular_file(path, ec))
            {
                vfLogError("Could not reference missing font asset: {}", path);
                return false;
            }
            if (opts.pathValidator && !opts.pathValidator(path))
            {
                return false;
            }

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

        std::optional<std::string> makeProjectRelative(std::string_view path,
                                                       std::string_view projectRoot)
        {
            namespace fs = std::filesystem;

            if (path.empty() || projectRoot.empty())
            {
                return std::nullopt;
            }

            std::error_code ec;
            const fs::path canonicalRoot = fs::weakly_canonical(fs::path(projectRoot), ec);
            if (ec)
            {
                return std::nullopt;
            }

            ec.clear();
            const fs::path canonicalPath = fs::weakly_canonical(fs::path(path), ec);
            if (ec)
            {
                return std::nullopt;
            }

            ec.clear();
            const fs::path relative = fs::relative(canonicalPath, canonicalRoot, ec);
            if (ec || relative.empty() || relative.is_absolute())
            {
                return std::nullopt;
            }

            auto first = relative.begin();
            if (first != relative.end() && *first == "..")
            {
                return std::nullopt;
            }
            return relative.lexically_normal().generic_string();
        }
    }

    bool drawFontSlot(asset::AssetRef& fontRef, const char* idSuffix, const FontSlotOptions& opts)
    {
        bool changed = false;
        ImGui::PushID(idSuffix);

        // Resolve EXACTLY once: an unresolvable GUID is deliberately not cached
        // (AssetRef::resolve), so every extra call re-runs tryResolveByMetaScan.
        const std::string& resolved = fontRef.resolve();
        const bool hasMissingPath = !opts.missingPath.empty() && !fontRef.isValid();
        const FontSlotState state = hasMissingPath
                                        ? FontSlotState::Missing
                                        : fontSlotState(fontRef.isValid(), resolved);

        // Owns the Assigned-state tooltip. It must be a COPY, not resolved.c_str():
        // `resolved` binds to fontRef's internal cache, and a drop delivered a few
        // lines below reassigns fontRef — which would leave the pointer aimed at a
        // reassigned (or reallocated) buffer by the time the tooltip is drawn.
        std::string assignedTooltip;
        std::string missingTooltip;

        const char* hint = nullptr;
        switch (state)
        {
        case FontSlotState::Assigned:
            ImGui::Text("Font: %s", fontSlotFileName(resolved).c_str());
            assignedTooltip = resolved;
            hint = assignedTooltip.c_str();
            break;
        case FontSlotState::Missing:
            if (hasMissingPath)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Font: %s (Missing)",
                                   fontSlotFileName(opts.missingPath).c_str());
                missingTooltip = std::string(opts.missingPath) + "\n" + FONT_SLOT_MISSING_TOOLTIP;
                hint = missingTooltip.c_str();
            }
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Font: %s",
                                   FONT_SLOT_MISSING_LABEL);
                hint = FONT_SLOT_MISSING_TOOLTIP;
            }
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
            changed = assignFont(fontRef, *dropped, opts);
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
                changed = assignFont(fontRef, path, opts) || changed;
            }
        }

        ImGui::SameLine();
        const bool alreadyDefault = !fontRef.isValid() && !hasMissingPath;
        if (alreadyDefault) ImGui::BeginDisabled();
        if (ImGui::Button(opts.clearButtonLabel))
        {
            fontRef = asset::AssetRef::invalid();
            changed = true;
        }
        if (alreadyDefault) ImGui::EndDisabled();
        if (!alreadyDefault && ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", opts.clearTooltip);
        }

        ImGui::PopID();
        return changed;
    }

    bool drawProjectFontSlot(std::string& projectRelativePath,
                             std::string_view projectRoot,
                             const char* idSuffix,
                             const FontSlotOptions& opts)
    {
        namespace fs = std::filesystem;

        asset::AssetRef fontRef = asset::AssetRef::invalid();
        bool storedPathIsUsable = false;
        if (!projectRelativePath.empty() && !fs::path(projectRelativePath).is_absolute())
        {
            const fs::path candidate =
                (fs::path(projectRoot) / fs::path(projectRelativePath)).lexically_normal();
            if (makeProjectRelative(candidate.string(), projectRoot))
            {
                std::error_code ec;
                if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec))
                {
                    fontRef = asset::AssetRef::fromPath(candidate.string());
                    storedPathIsUsable = fontRef.isValid();
                }
            }
        }

        FontSlotOptions bridgeOptions = opts;
        if (!projectRelativePath.empty() && !storedPathIsUsable)
        {
            bridgeOptions.missingPath = projectRelativePath;
        }
        bridgeOptions.pathValidator =
            [projectRoot](std::string_view selectedPath)
            {
                if (makeProjectRelative(selectedPath, projectRoot))
                {
                    return true;
                }
                vfLogError("Font fallback must be inside the loaded project: {}", selectedPath);
                return false;
            };

        if (!drawFontSlot(fontRef, idSuffix, bridgeOptions))
        {
            return false;
        }

        if (!fontRef.isValid())
        {
            projectRelativePath.clear();
            return true;
        }

        const std::string& resolved = fontRef.resolve();
        auto relative = makeProjectRelative(resolved, projectRoot);
        if (!relative)
        {
            vfLogError("Could not store font fallback as a project-relative path: {}", resolved);
            return false;
        }

        projectRelativePath = std::move(*relative);
        return true;
    }
}
