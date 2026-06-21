#include "VFXSequenceEditorWindow.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"

#include "print/Log.hpp"
#include "imgui.h"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"

#include <vfx/VFXSequenceAsset.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetMetadataSerializer.hpp>

#include <filesystem>
#include <ctime>
#include <algorithm>

namespace fs = std::filesystem;

namespace windows
{
    namespace
    {
        // Mirrors RetargetingEditorWindow::writeMeta — writes a formatVersion-2
        // .vfmeta sidecar listing the sequence's child .vfVFX GUIDs as
        // dependencies, preserving the asset GUID across re-saves.
        void writeSequenceMeta(const std::string& assetPath,
                               const std::vector<asset::AssetGUID>& dependencies)
        {
            const auto metaPath = asset::AssetMetadataSerializer::getMetaPath(assetPath);

            asset::AssetMetadata meta;
            if (auto existing = asset::AssetMetadataSerializer::load(metaPath))
                meta.guid = existing->guid; // preserve GUID across re-saves
            else
                meta.guid = asset::AssetGUID::generate();

            meta.type = resource::AssetType::VFXSequence;
            meta.importSourcePath = "editor://vfxsequence";
            meta.formatVersion = asset::AssetMetadata::kCurrentFormatVersion;
            meta.dependencies = dependencies;

            std::time_t t = std::time(nullptr);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
            meta.importTimestamp = buf;

            asset::AssetMetadataSerializer::save(meta, metaPath);
        }
    }

    VFXSequenceEditorWindow::VFXSequenceEditorWindow(const std::string& seqPath)
        : seqPath(seqPath)
    {
        windowTitle = "VFX Sequence: " + fs::path(seqPath).filename().string();
    }

    void VFXSequenceEditorWindow::loadSequence()
    {
        if (auto loaded = vfx::VFXSequenceAsset::load(seqPath))
        {
            data = std::make_unique<vfx::VFXSequenceData>(std::move(*loaded));
        }
        else
        {
            vfLogInfo("Creating new VFX sequence: {}", seqPath);
            data = std::make_unique<vfx::VFXSequenceData>(
                vfx::VFXSequenceAsset::createDefault(fs::path(seqPath).stem().string()));
        }
    }

    void VFXSequenceEditorWindow::saveSequence()
    {
        if (!data) return;

        if (vfx::VFXSequenceAsset::save(*data, seqPath))
        {
            isDirty = false;
            vfLogInfo("VFX sequence saved: {}", seqPath);

            // Sidecar dependency graph: each step's child .vfVFX GUID.
            std::vector<asset::AssetGUID> deps;
            deps.reserve(data->steps.size());
            for (const auto& step : data->steps)
            {
                if (step.vfxRef.isValid())
                    deps.push_back(step.vfxRef.getGUID());
            }
            writeSequenceMeta(seqPath, deps);

            events::resource::AssetSavedNotification assetNotif;
            assetNotif.filePath = seqPath;
            events::EventDispatcher::instance().publish(assetNotif);
        }
        else
        {
            vfLogError("Failed to save VFX sequence: {}", seqPath);
        }
    }

    void VFXSequenceEditorWindow::draw()
    {
        if (!isOpen)
            return;

        if (needsInit)
        {
            loadSequence();
            needsInit = false;
        }

        ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);

        std::string title = windowTitle + (isDirty ? " *" : "  ") + "###VFXSequenceEditor:" + seqPath;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar;
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            if (data)
            {
                drawToolbar();

                ImVec2 avail = ImGui::GetContentRegionAvail();
                float timelineHeight = 70.0f;
                float spacing = ImGui::GetStyle().ItemSpacing.y;
                float topHeight = avail.y - timelineHeight - spacing;
                float listWidth = 240.0f;

                ImGui::BeginChild("StepList", ImVec2(listWidth, topHeight), true);
                drawStepList();
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("StepInspector", ImVec2(0, topHeight), true);
                drawStepInspector();
                ImGui::EndChild();

                ImGui::BeginChild("Timeline", ImVec2(0, timelineHeight), true);
                drawTimeline();
                ImGui::EndChild();
            }
            else
            {
                ImGui::TextDisabled("Failed to load sequence");
            }
        }
        ImGui::End();
    }

    void VFXSequenceEditorWindow::drawToolbar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                    saveSequence();
                ImGui::Separator();
                if (ImGui::MenuItem("Close"))
                    isOpen = false;
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        if (ImGui::Button("Save"))
            saveSequence();
        ImGui::SameLine();
        if (ImGui::Button("Add Step"))
        {
            vfx::VFXSequenceStep step;
            step.label = "Step " + std::to_string(data->steps.size() + 1);
            data->steps.push_back(std::move(step));
            selectedStep = static_cast<int>(data->steps.size()) - 1;
            isDirty = true;
        }
        ImGui::Separator();
    }

    void VFXSequenceEditorWindow::drawStepList()
    {
        if (data->steps.empty())
        {
            ImGui::TextDisabled("No steps. Use \"Add Step\".");
            return;
        }

        for (int i = 0; i < static_cast<int>(data->steps.size()); ++i)
        {
            auto& step = data->steps[static_cast<size_t>(i)];
            ImGui::PushID(i);

            // Reorder / remove controls.
            if (ImGui::SmallButton("^") && i > 0)
            {
                std::swap(data->steps[static_cast<size_t>(i)], data->steps[static_cast<size_t>(i - 1)]);
                if (selectedStep == i) selectedStep = i - 1;
                else if (selectedStep == i - 1) selectedStep = i;
                isDirty = true;
                ImGui::PopID();
                continue;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("v") && i + 1 < static_cast<int>(data->steps.size()))
            {
                std::swap(data->steps[static_cast<size_t>(i)], data->steps[static_cast<size_t>(i + 1)]);
                if (selectedStep == i) selectedStep = i + 1;
                else if (selectedStep == i + 1) selectedStep = i;
                isDirty = true;
                ImGui::PopID();
                continue;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                data->steps.erase(data->steps.begin() + i);
                if (selectedStep == i) selectedStep = -1;
                else if (selectedStep > i) --selectedStep;
                isDirty = true;
                ImGui::PopID();
                break; // container mutated — restart next frame
            }
            ImGui::SameLine();

            std::string label = step.label.empty() ? ("(step " + std::to_string(i) + ")") : step.label;
            if (ImGui::Selectable(label.c_str(), selectedStep == i))
                selectedStep = i;

            ImGui::PopID();
        }
    }

    void VFXSequenceEditorWindow::drawStepInspector()
    {
        if (selectedStep < 0 || selectedStep >= static_cast<int>(data->steps.size()))
        {
            ImGui::TextDisabled("Select a step to edit.");
            return;
        }

        auto& step = data->steps[static_cast<size_t>(selectedStep)];

        // --- Child .vfVFX reference (drop target) ---
        ImGui::TextUnformatted("VFX Asset");
        std::string refLabel;
        if (step.vfxRef.isValid())
        {
            const std::string& resolved = step.vfxRef.resolve();
            refLabel = resolved.empty() ? "(missing)" : fs::path(resolved).filename().string();
        }
        else
        {
            refLabel = "(none) - drop a .vfVFX here";
        }

        bool missing = step.vfxRef.isValid() && step.vfxRef.resolve().empty();
        if (missing)
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 80, 80, 255));
        ImGui::Button(refLabel.c_str(), ImVec2(-1.0f, 0.0f));
        if (missing)
            ImGui::PopStyleColor();

        // Reuses the content-browser single-asset drag payload (DND_CONTENT_BROWSER)
        // via the shared helper, restricted to .vfVFX.
        if (auto dropped = acceptAssetDropOnLastItem("##vfxSeqRef", {".vfvfx"}))
        {
            auto ref = asset::AssetRef::fromPath(*dropped);
            if (ref.isValid())
            {
                step.vfxRef = ref;
                isDirty = true;
            }
        }

        ImGui::Separator();

        char labelBuf[256];
        std::strncpy(labelBuf, step.label.c_str(), sizeof(labelBuf) - 1);
        labelBuf[sizeof(labelBuf) - 1] = '\0';
        if (ImGui::InputText("Label", labelBuf, IM_ARRAYSIZE(labelBuf)))
        {
            step.label = labelBuf;
            isDirty = true;
        }

        if (ImGui::DragFloat("Start Time", &step.startTime, 0.01f, 0.0f, 1000.0f, "%.2f s"))
            isDirty = true;

        char cueBuf[256];
        std::strncpy(cueBuf, step.cueName.c_str(), sizeof(cueBuf) - 1);
        cueBuf[sizeof(cueBuf) - 1] = '\0';
        if (ImGui::InputText("Cue Name", cueBuf, IM_ARRAYSIZE(cueBuf)))
        {
            step.cueName = cueBuf;
            isDirty = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Empty = time-driven (uses Start Time). Otherwise fired by a named cue.");

        ImGui::Separator();

        if (ImGui::DragFloat3("Local Position", &step.localPosition.x, 0.05f))
            isDirty = true;
        if (ImGui::DragFloat3("Local Euler (deg)", &step.localEulerDeg.x, 0.5f))
            isDirty = true;
        if (ImGui::DragFloat3("Local Scale", &step.localScale.x, 0.01f, 0.0001f, 1000.0f))
            isDirty = true;

        char socketBuf[256];
        std::strncpy(socketBuf, step.socketName.c_str(), sizeof(socketBuf) - 1);
        socketBuf[sizeof(socketBuf) - 1] = '\0';
        if (ImGui::InputText("Socket", socketBuf, IM_ARRAYSIZE(socketBuf)))
        {
            step.socketName = socketBuf;
            isDirty = true;
        }

        ImGui::Separator();

        if (ImGui::Checkbox("Loop", &step.loop))
            isDirty = true;

        if (ImGui::DragFloat("Duration", &step.duration, 0.01f, 0.0f, 1000.0f, "%.2f s"))
            isDirty = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0 = play to completion");

        const char* stopModes[] = {"Play To Completion", "Stop After Duration"};
        int stopIdx = static_cast<int>(step.stopMode);
        if (ImGui::Combo("Stop Mode", &stopIdx, stopModes, IM_ARRAYSIZE(stopModes)))
        {
            step.stopMode = static_cast<vfx::VFXStepStopMode>(stopIdx);
            isDirty = true;
        }

        ImGui::Separator();

        // --- Scalar overrides ---
        ImGui::TextUnformatted("Scalar Overrides");
        for (size_t i = 0; i < step.scalarOverrides.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i) + 1000);
            auto& [name, value] = step.scalarOverrides[i];

            char nameBuf[128];
            std::strncpy(nameBuf, name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::InputText("##sname", nameBuf, IM_ARRAYSIZE(nameBuf)))
            {
                name = nameBuf;
                isDirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::DragFloat("##sval", &value, 0.01f))
                isDirty = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                step.scalarOverrides.erase(step.scalarOverrides.begin() + static_cast<long>(i));
                isDirty = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+ Scalar"))
        {
            step.scalarOverrides.emplace_back("param", 0.0f);
            isDirty = true;
        }

        ImGui::Separator();

        // --- Vector overrides ---
        ImGui::TextUnformatted("Vector Overrides");
        for (size_t i = 0; i < step.vectorOverrides.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i) + 2000);
            auto& [name, value] = step.vectorOverrides[i];

            char nameBuf[128];
            std::strncpy(nameBuf, name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::InputText("##vname", nameBuf, IM_ARRAYSIZE(nameBuf)))
            {
                name = nameBuf;
                isDirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::DragFloat4("##vval", &value.x, 0.01f))
                isDirty = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                step.vectorOverrides.erase(step.vectorOverrides.begin() + static_cast<long>(i));
                isDirty = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::SmallButton("+ Vector"))
        {
            step.vectorOverrides.emplace_back("param", glm::vec4(0.0f));
            isDirty = true;
        }
    }

    void VFXSequenceEditorWindow::drawTimeline()
    {
        // Time-driven marker strip: shows each step at its startTime along a
        // scrub bar. Live child-VFX spawning against IVFXPreviewProvider (and
        // socket-following animation) is a follow-up — markers only for now.
        float maxTime = 1.0f;
        for (const auto& step : data->steps)
            maxTime = std::max(maxTime, step.startTime + std::max(step.duration, 0.5f));

        ImGui::SetNextItemWidth(-120.0f);
        ImGui::SliderFloat("##previewTime", &previewTime, 0.0f, maxTime, "t = %.2f s");
        ImGui::SameLine();
        if (ImGui::Button(previewPlaying ? "Pause" : "Play"))
            previewPlaying = !previewPlaying;
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
        {
            previewTime = 0.0f;
            previewPlaying = false;
        }

        if (previewPlaying)
        {
            previewTime += ImGui::GetIO().DeltaTime;
            if (previewTime > maxTime)
            {
                previewTime = maxTime;
                previewPlaying = false;
            }
        }

        // Marker bar.
        ImVec2 barMin = ImGui::GetCursorScreenPos();
        ImVec2 region = ImGui::GetContentRegionAvail();
        float barH = std::max(region.y, 12.0f);
        ImVec2 barMax = ImVec2(barMin.x + region.x, barMin.y + barH);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(barMin, barMax, IM_COL32(40, 40, 48, 255));

        auto timeToX = [&](float t) {
            float frac = maxTime > 0.0f ? std::clamp(t / maxTime, 0.0f, 1.0f) : 0.0f;
            return barMin.x + frac * region.x;
        };

        for (int i = 0; i < static_cast<int>(data->steps.size()); ++i)
        {
            float x = timeToX(data->steps[static_cast<size_t>(i)].startTime);
            ImU32 col = (i == selectedStep) ? IM_COL32(255, 200, 80, 255) : IM_COL32(247, 79, 200, 255);
            dl->AddLine(ImVec2(x, barMin.y), ImVec2(x, barMax.y), col, 2.0f);
        }

        // Scrub cursor.
        float cx = timeToX(previewTime);
        dl->AddLine(ImVec2(cx, barMin.y), ImVec2(cx, barMax.y), IM_COL32(255, 255, 255, 220), 1.5f);

        ImGui::Dummy(ImVec2(region.x, barH));
    }
}
