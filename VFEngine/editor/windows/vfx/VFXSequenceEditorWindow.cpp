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
#include <vfx/VFXSequenceValidation.hpp>
#include <vfx/VFXComboTimeline.hpp>
#include <vfx/VFXAsset.hpp>
#include <asset/AssetRef.hpp>
#include <resource/MeshStreamHandle.hpp>

#include <glm/glm.hpp>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>
#include <cmath>

namespace fs = std::filesystem;

namespace windows
{
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
        // The timeline row below drives transport, so hide the panel's built-in buttons.
        previewPanel->setBuiltInControls(false);
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
                drawValidationStrip();

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

    void VFXSequenceEditorWindow::drawValidationStrip()
    {
        if (!data)
            return;

        std::unordered_set<std::string> knownSockets;
        if (!socketNames.empty())
            knownSockets.insert(socketNames.begin(), socketNames.end());

        vfx::validation::ValidationContext context;
        context.checkRefResolvable = true;
        context.knownSocketNames = knownSockets.empty() ? nullptr : &knownSockets;

        const vfx::validation::ValidationReport report =
            vfx::validation::validateSequence(*data, context);
        if (report.diagnostics.empty())
            return;

        for (const auto& diagnostic : report.diagnostics)
        {
            ImVec4 color(0.55f, 0.70f, 1.0f, 1.0f);
            const char* icon = ICON_FA_CIRCLE_INFO;
            if (diagnostic.severity == vfx::validation::Severity::Error)
            {
                color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
                icon = ICON_FA_CIRCLE_EXCLAMATION;
            }
            else if (diagnostic.severity == vfx::validation::Severity::Warning)
            {
                color = ImVec4(1.0f, 0.80f, 0.30f, 1.0f);
                icon = ICON_FA_TRIANGLE_EXCLAMATION;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s %s", icon, diagnostic.message.c_str());
            ImGui::PopStyleColor();

            if (ImGui::IsItemClicked() &&
                diagnostic.stepIndex >= 0 &&
                diagnostic.stepIndex < static_cast<int>(data->steps.size()))
            {
                selectedStep = diagnostic.stepIndex;
            }
        }
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
                previewDirty = true;
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
                previewDirty = true;
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
                previewDirty = true;
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
                vfxCache.clear();
                previewDirty = true; // force the preview to reload this step
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
        // Transport row: play/pause, reset, reload, playback rate, seed, prewarm.
        // VK-1451 — drives the composited preview deterministically (mirrors
        // AnimationTimelinePanel: ImSequencer change -> seek).
        if (ImGui::Button(previewPlaying ? "Pause" : "Play"))
        {
            previewPlaying = !previewPlaying;
            if (previewPlaying) previewPanel->play();
            else previewPanel->pause();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
        {
            previewTime = 0.0f;
            previewPanel->seek(0.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
        {
            vfxCache.clear();    // drop cached .vfVFX so edits on disk are picked up
            previewDirty = true; // re-load each step's .vfVFX + overrides into the composite
        }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &previewLoop);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::DragFloat("Rate", &previewRate, 0.05f, 0.1f, 4.0f, "%.2fx"))
        {
            previewRate = std::clamp(previewRate, 0.1f, 4.0f);
            previewPanel->setRate(previewRate);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        int seedProxy = static_cast<int>(previewSeed);
        if (ImGui::InputInt("Seed", &seedProxy, 0, 0))
        {
            previewSeed = static_cast<uint32_t>(std::max(0, seedProxy));
            previewDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Randomize"))
        {
            previewSeed = static_cast<uint32_t>(ImGui::GetTime() * 1000.0) ^ 0x9E3779B9u;
            if (previewSeed == 0) previewSeed = 1u;
            previewDirty = true;
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragFloat("Prewarm", &previewPrewarm, 0.05f, 0.0f, 30.0f, "%.2fs");
        ImGui::SameLine();
        if (ImGui::Button("Apply##prewarm"))
        {
            previewTime = previewPrewarm;
            previewPanel->seek(previewPrewarm);
        }
        ImGui::SameLine();
        ImGui::Text("t = %.2f s", previewTime);

        float maxTime = 1.0f;
        for (const auto& step : data->steps)
            maxTime = std::max(maxTime, step.startTime + std::max(step.duration, 0.5f));
        for (const auto& marker : data->eventMarkers)
            maxTime = std::max(maxTime, marker.time + 0.5f);

        // While playing, advance the local playhead in lock-step with the panel's sim.
        if (previewPlaying)
        {
            previewTime += ImGui::GetIO().DeltaTime * (previewRate > 0.0f ? previewRate : 1.0f);
            if (previewTime > maxTime)
            {
                if (previewLoop)
                {
                    previewTime = 0.0f;
                    previewPanel->seek(0.0f);
                }
                else
                {
                    previewTime = maxTime;
                    previewPlaying = false;
                    previewPanel->pause();
                }
            }
        }

        // ImSequencer track view (one clip per step). A user drag of the playhead is a
        // scrub -> seek; clip drags edit step timing (writeback).
        timeline->sync(data.get());
        previewFrame = static_cast<int>(std::lround(previewTime * SequenceTimeline::kFps));
        const int frameBefore = previewFrame;
        int selected = selectedStep;
        const int options = ImSequencer::SEQUENCER_CHANGE_FRAME | ImSequencer::SEQUENCER_EDIT_STARTEND;
        ImSequencer::Sequencer(timeline.get(), &previewFrame, &timelineExpanded, &selected,
                               &timelineFirstFrame, options);

        previewTime = std::clamp(static_cast<float>(previewFrame) / SequenceTimeline::kFps, 0.0f, maxTime);
        if (previewFrame != frameBefore)
            previewPanel->seek(previewTime); // user scrubbed the playhead

        if (timeline->writeback())
        {
            isDirty = true;
            previewDirty = true; // step timing changed -> rebuild the composite schedule
        }
        if (selected >= 0 && selected < static_cast<int>(data->steps.size()))
            selectedStep = selected;

        drawMarkersRow();
    }

    void VFXSequenceEditorWindow::drawMarkersRow()
    {
        if (!data)
            return;

        ImGui::Separator();
        ImGui::TextUnformatted("Event Markers (one-shot cues)");
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Add"))
        {
            data->eventMarkers.push_back(vfx::VFXSequenceEventMarker{previewTime, ""});
            isDirty = true;
            previewDirty = true;
        }

        int toRemove = -1;
        for (size_t m = 0; m < data->eventMarkers.size(); ++m)
        {
            ImGui::PushID(static_cast<int>(m));
            auto& marker = data->eventMarkers[m];

            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::DragFloat("Time", &marker.time, 0.01f, 0.0f, 120.0f, "%.2fs"))
            {
                isDirty = true;
                previewDirty = true;
            }
            ImGui::SameLine();

            char buf[64];
            std::strncpy(buf, marker.cueName.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::InputText("Cue", buf, sizeof(buf)))
            {
                marker.cueName = buf;
                isDirty = true;
                previewDirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
                toRemove = static_cast<int>(m);

            ImGui::PopID();
        }

        if (toRemove >= 0)
        {
            data->eventMarkers.erase(data->eventMarkers.begin() + toRemove);
            isDirty = true;
            previewDirty = true;
        }
    }

    services::VFXSequencePreviewDesc VFXSequenceEditorWindow::buildSequenceDesc() const
    {
        services::VFXSequencePreviewDesc desc;
        if (!data)
            return desc;

        desc.seed = previewSeed != 0 ? previewSeed : data->seed;
        desc.playbackRate = previewRate > 0.0f ? previewRate : 1.0f;
        desc.fixedStep = data->fixedStep;
        for (const auto& marker : data->eventMarkers)
            desc.markers.emplace_back(marker.time, marker.cueName);

        const uint32_t comboSeed = desc.seed != 0 ? desc.seed : 1u;

        for (size_t i = 0; i < data->steps.size(); ++i)
        {
            const auto& step = data->steps[i];
            if (!step.vfxRef.isValid())
                continue;
            const std::string path = step.vfxRef.resolve();
            if (path.empty())
                continue;

            std::shared_ptr<vfx::VFXData> vfxData;
            if (auto cacheIt = vfxCache.find(path); cacheIt != vfxCache.end())
            {
                vfxData = cacheIt->second;
            }
            else
            {
                if (auto loaded = vfx::VFXAsset::load(path))
                    vfxData = std::make_shared<vfx::VFXData>(std::move(*loaded));
                vfxCache[path] = vfxData; // cache nulls too, so a bad path isn't retried each frame
            }
            if (!vfxData)
                continue;

            services::VFXSequencePreviewStep ps;
            ps.params = editor::vfxeditor::buildVFXPreviewParams(*vfxData);

            // Step overrides (parity with the runtime spawnStep path).
            for (const auto& [name, value] : step.scalarOverrides)
            {
                if (name == "spawnRate") ps.params.spawnRate = value;
                else if (name == "lifetime") ps.params.lifetime = value;
                else if (name == "startSize") ps.params.startSize = value;
                else if (name == "startSpeed") ps.params.startSpeed = value;
                else if (name == "stretchMultiplier") ps.params.stretchMultiplier = value;
            }
            for (const auto& [name, value] : step.vectorOverrides)
            {
                if (name == "startColor") ps.params.startColor = value;
                else if (name == "emitDirection") ps.params.emitDirection = glm::vec3(value);
            }

            ps.localTransform = vfx::VFXComboTimeline::composeStepLocal(step);
            ps.seed = vfx::VFXComboTimeline::deriveSeed(comboSeed, static_cast<int>(desc.steps.size()));
            ps.startTime = step.startTime;
            ps.duration = step.duration;
            ps.loop = step.loop;
            ps.stopMode = static_cast<int>(static_cast<uint8_t>(step.stopMode));
            ps.cueName = step.cueName;
            desc.steps.push_back(std::move(ps));
        }

        return desc;
    }

    void VFXSequenceEditorWindow::drawPreviewViewport()
    {
        if (!previewPanel)
            return;
        if (!previewPanel->isInitialized())
        {
            previewPanel->init();
            previewPanel->setBuiltInControls(false);
            previewDirty = true;
        }

        if (previewDirty)
        {
            previewPanel->setSequence(buildSequenceDesc());
            previewDirty = false;
            // setSequence resets the controller to t=0; restore the playhead + run state.
            if (previewTime > 0.0f)
                previewPanel->seek(previewTime);
            if (previewPlaying)
                previewPanel->play();
            else
                previewPanel->pause();
        }

        if (data && data->steps.empty())
            ImGui::TextDisabled("Add steps to preview the composited combo");
        else
            ImGui::Text("Composited preview — %d step(s)", data ? static_cast<int>(data->steps.size()) : 0);

        previewPanel->draw();
    }
}
