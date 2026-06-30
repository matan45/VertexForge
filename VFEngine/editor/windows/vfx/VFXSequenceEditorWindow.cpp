#include "VFXSequenceEditorWindow.hpp"
#include "VFXPreviewPanel.hpp"
#include "VFXPreviewParamsBuilder.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"

#include "print/Log.hpp"
#include "imgui.h"
#include "ImSequencer.h"
#include <IconsFontAwesome6.h>
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"

#include <vfx/VFXSequenceAsset.hpp>
#include <vfx/VFXAsset.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include <resource/MeshStreamHandle.hpp>

#include <glm/glm.hpp>
#include <filesystem>
#include <ctime>
#include <algorithm>
#include <string>
#include <vector>
#include <cmath>

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

    // ImSequencer adapter: one clip per step. starts/ends are in "frames"
    // (kFps frames per second). ImSequencer mutates them in place when a clip is
    // dragged; writeback() converts changes back to Start Time / Duration.
    class VFXSequenceEditorWindow::SequenceTimeline : public ImSequencer::SequenceInterface
    {
    public:
        static constexpr int kFps = 100;        // 0.01s timeline resolution
        static constexpr float kDisplayDefaultSec = 0.5f; // shown width for duration==0 steps

        vfx::VFXSequenceData* data = nullptr;
        std::vector<int> starts, ends, snapStarts, snapEnds;

        void sync(vfx::VFXSequenceData* d)
        {
            data = d;
            const size_t n = d ? d->steps.size() : 0;
            starts.resize(n);
            ends.resize(n);
            for (size_t i = 0; i < n; ++i)
            {
                const auto& s = d->steps[i];
                const float endSec = s.startTime + (s.duration > 0.0f ? s.duration : kDisplayDefaultSec);
                starts[i] = static_cast<int>(std::lround(s.startTime * kFps));
                ends[i] = static_cast<int>(std::lround(endSec * kFps));
            }
            snapStarts = starts;
            snapEnds = ends;
        }

        // Apply clip drags back onto the steps; returns true if anything changed.
        bool writeback()
        {
            if (!data)
                return false;
            bool changed = false;
            const size_t n = std::min(starts.size(), data->steps.size());
            for (size_t i = 0; i < n; ++i)
            {
                if (starts[i] != snapStarts[i])
                {
                    data->steps[i].startTime = static_cast<float>(std::max(0, starts[i])) / kFps;
                    changed = true;
                }
                if (ends[i] != snapEnds[i])
                {
                    data->steps[i].duration = static_cast<float>(std::max(0, ends[i] - starts[i])) / kFps;
                    changed = true;
                }
            }
            return changed;
        }

        int GetFrameMin() const override { return 0; }
        int GetFrameMax() const override
        {
            int mx = kFps; // at least 1s of track
            for (int e : ends)
                mx = std::max(mx, e);
            return mx;
        }
        int GetItemCount() const override { return data ? static_cast<int>(data->steps.size()) : 0; }

        const char* GetItemLabel(int index) const override
        {
            if (!data || index < 0 || index >= static_cast<int>(data->steps.size()))
                return "";
            const auto& s = data->steps[static_cast<size_t>(index)];
            return s.label.empty() ? "(step)" : s.label.c_str();
        }

        void Get(int index, int** start, int** end, int* type, unsigned int* color) override
        {
            if (index < 0 || index >= static_cast<int>(starts.size()))
                return;
            if (start) *start = &starts[static_cast<size_t>(index)];
            if (end) *end = &ends[static_cast<size_t>(index)];
            if (type) *type = 0;
            if (color)
            {
                const bool cue = data && !data->steps[static_cast<size_t>(index)].cueName.empty();
                *color = cue ? 0xFF888888u : 0xFFC84FF7u; // gray = cue-driven, pink = time-driven
            }
        }
    };

    VFXSequenceEditorWindow::VFXSequenceEditorWindow(const std::string& seqPath)
        : seqPath(seqPath)
        , previewPanel(std::make_unique<editor::vfxeditor::VFXPreviewPanel>(this))
        , timeline(std::make_unique<SequenceTimeline>())
    {
        windowTitle = "VFX Sequence: " + fs::path(seqPath).filename().string();
    }

    VFXSequenceEditorWindow::~VFXSequenceEditorWindow() = default;

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
        {
            if (previewPanel)
                previewPanel->cleanup();
            return;
        }

        if (needsInit)
        {
            loadSequence();
            needsInit = false;
        }

        if (initialSize.x <= 0.0f)
        {
            initialSize = editor::preview::initialWindowSize("VFXSequenceEditor", ImVec2(960, 620));
        }
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        maximizer.preBegin();

        std::string title = windowTitle + (isDirty ? " *" : "  ") + "###VFXSequenceEditor:" + seqPath;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar;
        flags |= maximizer.windowFlags();
        if (ImGui::Begin(title.c_str(), &isOpen, flags))
        {
            if (data)
            {
                drawToolbar();

                ImVec2 avail = ImGui::GetContentRegionAvail();
                float timelineHeight = 190.0f; // room for the ImSequencer track view
                float spacingX = ImGui::GetStyle().ItemSpacing.x;
                float spacingY = ImGui::GetStyle().ItemSpacing.y;
                float topHeight = avail.y - timelineHeight - spacingY;
                float listWidth = 220.0f;
                float previewWidth = std::max(300.0f, avail.x * 0.40f);
                float inspectorWidth = avail.x - listWidth - previewWidth - 2.0f * spacingX;
                if (inspectorWidth < 200.0f)
                    inspectorWidth = 200.0f;

                ImGui::BeginChild("StepList", ImVec2(listWidth, topHeight), true);
                drawStepList();
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("StepInspector", ImVec2(inspectorWidth, topHeight), true);
                drawStepInspector();
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("Preview", ImVec2(0, topHeight), true);
                drawPreviewViewport();
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

        if (!isOpen && !sizeSaved)
        {
            editor::preview::rememberWindowSize("VFXSequenceEditor", maximizer.effectiveSize());
            sizeSaved = true;
        }
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

        // Optional reference mesh: drives the per-step Socket dropdown. Drop a
        // .vfMesh that has the sockets your combo will attach to. Not saved.
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        const std::string meshLabel = socketMeshPath.empty()
            ? std::string("Socket Mesh: (drop a .vfMesh)")
            : ("Socket Mesh: " + fs::path(socketMeshPath).filename().string() +
               "  [" + std::to_string(socketNames.size()) + "]");
        ImGui::Button(meshLabel.c_str());
        if (auto dropped = acceptAssetDropOnLastItem("##socketMesh", {".vfmesh"}))
        {
            socketMeshPath = *dropped;
            loadSocketNames();
        }
        if (!socketMeshPath.empty())
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear Mesh"))
            {
                socketMeshPath.clear();
                socketNames.clear();
            }
        }
        ImGui::SameLine();
        maximizer.drawButton();

        ImGui::Separator();
    }

    void VFXSequenceEditorWindow::loadSocketNames()
    {
        socketNames.clear();
        if (socketMeshPath.empty())
            return;

        auto stream = resource::MeshStreamResource::openStream(socketMeshPath);
        if (!stream || !stream->hasSkeletonData())
        {
            vfLogWarning("[VFXSequence] mesh '{}' has no skeleton/sockets", socketMeshPath);
            return;
        }

        resource::SkeletonData skeleton;
        if (stream->readSkeleton(skeleton))
        {
            socketNames.reserve(skeleton.sockets.size());
            for (const auto& socket : skeleton.sockets)
                socketNames.push_back(socket.name);
        }
    }

    void VFXSequenceEditorWindow::drawSocketField(vfx::VFXSequenceStep& step)
    {
        if (socketNames.empty())
        {
            // No reference mesh loaded — free text (must match a socket on the
            // target entity's mesh at runtime).
            char socketBuf[256];
            std::strncpy(socketBuf, step.socketName.c_str(), sizeof(socketBuf) - 1);
            socketBuf[sizeof(socketBuf) - 1] = '\0';
            if (ImGui::InputText("Socket", socketBuf, IM_ARRAYSIZE(socketBuf)))
            {
                step.socketName = socketBuf;
                isDirty = true;
            }
            return;
        }

        // Dropdown of the reference mesh's sockets (+ none + keep-custom).
        const std::string preview = step.socketName.empty() ? std::string("(none)") : step.socketName;
        if (ImGui::BeginCombo("Socket", preview.c_str()))
        {
            if (ImGui::Selectable("(none)", step.socketName.empty()))
            {
                step.socketName.clear();
                isDirty = true;
            }
            for (const auto& name : socketNames)
            {
                const bool selected = (step.socketName == name);
                if (ImGui::Selectable(name.c_str(), selected))
                {
                    step.socketName = name;
                    isDirty = true;
                }
            }
            // Preserve a custom value not present on this mesh (e.g. a different target rig).
            if (!step.socketName.empty() &&
                std::find(socketNames.begin(), socketNames.end(), step.socketName) == socketNames.end())
            {
                ImGui::Separator();
                ImGui::TextDisabled("custom: %s", step.socketName.c_str());
            }
            ImGui::EndCombo();
        }
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
            if (ImGui::SmallButton(ICON_FA_ARROW_UP "##MoveStepUp") && i > 0)
            {
                std::swap(data->steps[static_cast<size_t>(i)], data->steps[static_cast<size_t>(i - 1)]);
                if (selectedStep == i) selectedStep = i - 1;
                else if (selectedStep == i - 1) selectedStep = i;
                isDirty = true;
                previewActiveStep = -1;
                ImGui::PopID();
                continue;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_ARROW_DOWN "##MoveStepDown") && i + 1 < static_cast<int>(data->steps.size()))
            {
                std::swap(data->steps[static_cast<size_t>(i)], data->steps[static_cast<size_t>(i + 1)]);
                if (selectedStep == i) selectedStep = i + 1;
                else if (selectedStep == i + 1) selectedStep = i;
                isDirty = true;
                previewActiveStep = -1;
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
                previewActiveStep = -1;
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
                previewActiveStep = -1; // force the preview to reload this step
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

        drawSocketField(step);

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
        // Transport row, then an ImSequencer track view: one draggable clip per
        // step. Drag a clip to set its Start Time, drag its right edge to set
        // Duration; the playhead (current frame) scrubs the preview.
        if (ImGui::Button(previewPlaying ? "Pause" : "Play"))
            previewPlaying = !previewPlaying;
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
        {
            previewTime = 0.0f;
            previewPlaying = false;
            previewActiveStep = -1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
            previewActiveStep = -1; // re-load the active step's (possibly edited) .vfVFX
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &previewLoop);
        ImGui::SameLine();
        ImGui::Text("t = %.2f s", previewTime);

        float maxTime = 1.0f;
        for (const auto& step : data->steps)
            maxTime = std::max(maxTime, step.startTime + std::max(step.duration, 0.5f));

        if (previewPlaying)
        {
            previewTime += ImGui::GetIO().DeltaTime;
            if (previewTime > maxTime)
            {
                if (previewLoop)
                {
                    previewTime = 0.0f;
                    previewActiveStep = -1;
                }
                else
                {
                    previewTime = maxTime;
                    previewPlaying = false;
                }
            }
        }

        // ImSequencer track view (one clip per step).
        timeline->sync(data.get());
        previewFrame = static_cast<int>(std::lround(previewTime * SequenceTimeline::kFps));
        int selected = selectedStep;
        const int options = ImSequencer::SEQUENCER_CHANGE_FRAME | ImSequencer::SEQUENCER_EDIT_STARTEND;
        ImSequencer::Sequencer(timeline.get(), &previewFrame, &timelineExpanded, &selected,
                               &timelineFirstFrame, options);

        // Scrubbing moved the playhead; clip drags edited step timing.
        previewTime = std::clamp(static_cast<float>(previewFrame) / SequenceTimeline::kFps, 0.0f, maxTime);
        if (timeline->writeback())
            isDirty = true;
        if (selected >= 0 && selected < static_cast<int>(data->steps.size()))
            selectedStep = selected;
    }

    int VFXSequenceEditorWindow::pickActiveStep() const
    {
        if (!data || data->steps.empty())
            return -1;

        if (previewPlaying)
        {
            // The most-recently-started time-driven step at/under the playhead.
            int best = -1;
            float bestStart = -1.0f;
            for (int i = 0; i < static_cast<int>(data->steps.size()); ++i)
            {
                const auto& s = data->steps[static_cast<size_t>(i)];
                if (!s.cueName.empty() || !s.vfxRef.isValid())
                    continue; // cue-driven steps don't auto-fire in the preview
                if (previewTime + 1.0e-4f >= s.startTime && s.startTime >= bestStart)
                {
                    bestStart = s.startTime;
                    best = i;
                }
            }
            if (best >= 0)
                return best;
        }

        // Not playing (or nothing active yet): preview the selected step.
        if (selectedStep >= 0 && selectedStep < static_cast<int>(data->steps.size()) &&
            data->steps[static_cast<size_t>(selectedStep)].vfxRef.isValid())
            return selectedStep;

        return -1;
    }

    void VFXSequenceEditorWindow::syncPreviewToStep(int stepIndex)
    {
        previewActiveStep = stepIndex;
        if (!previewPanel || !data)
            return;

        if (stepIndex < 0 || stepIndex >= static_cast<int>(data->steps.size()))
        {
            previewPanel->stop();
            return;
        }

        const auto& step = data->steps[static_cast<size_t>(stepIndex)];
        const std::string path = step.vfxRef.isValid() ? step.vfxRef.resolve() : std::string();
        if (path.empty())
        {
            previewPanel->stop();
            return;
        }

        auto vfxData = vfx::VFXAsset::load(path);
        if (!vfxData)
        {
            previewPanel->stop();
            return;
        }

        services::VFXPreviewParams params = editor::vfxeditor::buildVFXPreviewParams(*vfxData);

        // Apply the step's name-keyed overrides for parity with the runtime.
        for (const auto& [name, value] : step.scalarOverrides)
        {
            if (name == "spawnRate") params.spawnRate = value;
            else if (name == "lifetime") params.lifetime = value;
            else if (name == "startSize") params.startSize = value;
            else if (name == "startSpeed") params.startSpeed = value;
            else if (name == "stretchMultiplier") params.stretchMultiplier = value;
        }
        for (const auto& [name, value] : step.vectorOverrides)
        {
            if (name == "startColor") params.startColor = value;
            else if (name == "emitDirection") params.emitDirection = glm::vec3(value);
        }

        previewPanel->setParams(params);
        previewPanel->play();
    }

    void VFXSequenceEditorWindow::drawPreviewViewport()
    {
        if (!previewPanel)
            return;
        if (!previewPanel->isInitialized())
            previewPanel->init();

        const int active = pickActiveStep();
        if (active != previewActiveStep)
            syncPreviewToStep(active);

        if (previewActiveStep >= 0 && previewActiveStep < static_cast<int>(data->steps.size()))
        {
            const auto& s = data->steps[static_cast<size_t>(previewActiveStep)];
            const std::string label = s.label.empty() ? ("step " + std::to_string(previewActiveStep)) : s.label;
            ImGui::Text("Previewing: %s", label.c_str());
        }
        else
        {
            ImGui::TextDisabled("Select a step (or press Play) to preview its VFX");
        }

        previewPanel->draw();
    }
}
