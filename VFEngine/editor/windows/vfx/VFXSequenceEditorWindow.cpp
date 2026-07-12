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

#include <data/VFXOverrideApplier.hpp>
#include <impl/vfx/VFXPreviewOverrideBridge.hpp>
#include <vfx/VFXSequenceAsset.hpp>
#include <vfx/VFXSequenceValidation.hpp>
#include <vfx/VFXComboTimeline.hpp>
#include <vfx/VFXAsset.hpp>
#include <vfx/VFXBoundsUtil.hpp>
#include <vfx/VFXParameterRegistry.hpp>
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
#include <variant>

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
                *color = 0xFFC84FF7u; // default pink (time-driven VFX)
                if (data && index < static_cast<int>(data->steps.size()))
                {
                    const auto& s = data->steps[static_cast<size_t>(index)];
                    if (!s.cueName.empty())
                    {
                        *color = 0xFF888888u; // gray = cue-driven (any kind) takes precedence
                    }
                    else switch (s.kind) // VK-1496 — tint time-driven clips by kind (ABGR)
                    {
                    case vfx::VFXStepKind::Sound:     *color = 0xFF00A5FFu; break; // amber
                    case vfx::VFXStepKind::ScriptCue: *color = 0xFF4FC84Fu; break; // green
                    default:                          *color = 0xFFC84FF7u; break; // pink (VFX)
                    }
                }
            }
        }
    };

    namespace
    {
        vfx::VFXPropertyType inferEditorValueType(const vfx::VFXPropertyValue& value)
        {
            if (std::holds_alternative<float>(value)) return vfx::VFXPropertyType::Float;
            if (std::holds_alternative<glm::vec2>(value)) return vfx::VFXPropertyType::Vec2;
            if (std::holds_alternative<glm::vec3>(value)) return vfx::VFXPropertyType::Vec3;
            if (std::holds_alternative<glm::vec4>(value)) return vfx::VFXPropertyType::Vec4;
            if (std::holds_alternative<int32_t>(value)) return vfx::VFXPropertyType::Int;
            if (std::holds_alternative<bool>(value)) return vfx::VFXPropertyType::Bool;
            if (std::holds_alternative<std::string>(value)) return vfx::VFXPropertyType::String;
            if (std::holds_alternative<vfx::VFXCurve>(value)) return vfx::VFXPropertyType::Curve;
            if (std::holds_alternative<vfx::VFXGradient>(value)) return vfx::VFXPropertyType::Gradient;
            return vfx::VFXPropertyType::Float;
        }

        bool drawOverrideValueWidget(vfx::VFXParamOverride& overrideValue)
        {
            const vfx::VFXExposedParameter* parameter = vfx::findExposedParameter(overrideValue.name);
            const vfx::VFXPropertyType type = parameter ? parameter->type : inferEditorValueType(overrideValue.value);

            if (parameter && !vfx::valueMatchesType(overrideValue.value, parameter->type))
            {
                ImGui::TextDisabled("%s stored", vfx::propertyTypeToString(inferEditorValueType(overrideValue.value)));
                ImGui::SameLine();
                if (ImGui::SmallButton("Reset"))
                {
                    overrideValue.value = vfx::defaultValueFor(parameter->type);
                    return true;
                }
                return false;
            }

            switch (type)
            {
            case vfx::VFXPropertyType::Float:
            {
                float* value = std::get_if<float>(&overrideValue.value);
                return value && ImGui::DragFloat("##oval", value, 0.01f);
            }
            case vfx::VFXPropertyType::Int:
            {
                auto* stored = std::get_if<int32_t>(&overrideValue.value);
                if (!stored) return false;
                int value = static_cast<int>(*stored);
                if (ImGui::InputInt("##oval", &value))
                {
                    *stored = static_cast<int32_t>(value);
                    return true;
                }
                return false;
            }
            case vfx::VFXPropertyType::Bool:
            {
                bool* value = std::get_if<bool>(&overrideValue.value);
                return value && ImGui::Checkbox("##oval", value);
            }
            case vfx::VFXPropertyType::Vec3:
            {
                glm::vec3* value = std::get_if<glm::vec3>(&overrideValue.value);
                return value && ImGui::DragFloat3("##oval", &value->x, 0.01f);
            }
            case vfx::VFXPropertyType::Vec4:
            {
                glm::vec4* value = std::get_if<glm::vec4>(&overrideValue.value);
                return value && ImGui::DragFloat4("##oval", &value->x, 0.01f);
            }
            case vfx::VFXPropertyType::Color:
            {
                glm::vec4* value = std::get_if<glm::vec4>(&overrideValue.value);
                return value && ImGui::ColorEdit4("##oval", &value->x);
            }
            case vfx::VFXPropertyType::String:
            {
                auto* stored = std::get_if<std::string>(&overrideValue.value);
                if (!stored) return false;
                char buf[256];
                std::strncpy(buf, stored->c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                if (ImGui::InputText("##oval", buf, IM_ARRAYSIZE(buf)))
                {
                    *stored = buf;
                    return true;
                }
                return false;
            }
            default:
                ImGui::TextDisabled("%s", vfx::propertyTypeToString(type));
                return false;
            }
        }
    }

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
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        if (ImGui::Button("Recalc Bounds"))
            recalcSequenceBounds();
        ImGui::SetItemTooltip("Union each step's bounds (transformed by its placement) and capture as the sequence's Fixed bounds.");
        ImGui::SameLine();
        ImGui::Checkbox("Show Bounds", &showBounds);

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

    void VFXSequenceEditorWindow::drawOverrideList(std::vector<vfx::VFXParamOverride>& overrides, const char* label)
    {
        ImGui::TextUnformatted(label);
        ImGui::PushID(label);

        for (size_t i = 0; i < overrides.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            auto& overrideValue = overrides[i];

            char nameBuf[128];
            std::strncpy(nameBuf, overrideValue.name.c_str(), sizeof(nameBuf) - 1);
            nameBuf[sizeof(nameBuf) - 1] = '\0';
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputText("##oname", nameBuf, IM_ARRAYSIZE(nameBuf)))
            {
                overrideValue.name = nameBuf;
                isDirty = true;
                previewDirty = true;
            }

            ImGui::SameLine();
            const vfx::VFXExposedParameter* parameter = vfx::findExposedParameter(overrideValue.name);
            std::string preview = parameter
                ? std::string(parameter->label.empty() ? parameter->name : parameter->label)
                : std::string("(custom)");
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::BeginCombo("##opick", preview.c_str()))
            {
                for (const auto& candidate : vfx::kExposedParameters)
                {
                    const bool selected = overrideValue.name == candidate.name;
                    const std::string itemLabel = std::string(candidate.label.empty() ? candidate.name : candidate.label);
                    if (ImGui::Selectable(itemLabel.c_str(), selected))
                    {
                        overrideValue.name = std::string(candidate.name);
                        if (!vfx::valueMatchesType(overrideValue.value, candidate.type))
                            overrideValue.value = vfx::defaultValueFor(candidate.type);
                        isDirty = true;
                        previewDirty = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                if (!parameter && !overrideValue.name.empty())
                {
                    ImGui::Separator();
                    ImGui::TextDisabled("custom: %s", overrideValue.name.c_str());
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(190.0f);
            if (drawOverrideValueWidget(overrideValue))
            {
                isDirty = true;
                previewDirty = true;
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("X"))
            {
                overrides.erase(overrides.begin() + static_cast<long>(i));
                isDirty = true;
                previewDirty = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }

        if (ImGui::SmallButton("+ Override"))
        {
            const auto& first = vfx::kExposedParameters.front();
            overrides.push_back(vfx::VFXParamOverride{std::string(first.name), vfx::defaultValueFor(first.type)});
            isDirty = true;
            previewDirty = true;
        }

        ImGui::PopID();
    }

    void VFXSequenceEditorWindow::drawCuePayload(vfx::VFXCuePayload& payload)
    {
        bool hasPosition = payload.position.has_value();
        if (ImGui::Checkbox("Position", &hasPosition))
        {
            if (hasPosition) payload.position = glm::vec3(0.0f);
            else payload.position.reset();
            isDirty = true;
            previewDirty = true;
        }
        if (payload.position)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(210.0f);
            if (ImGui::DragFloat3("##payloadPosition", &payload.position->x, 0.01f))
            {
                isDirty = true;
                previewDirty = true;
            }
        }

        bool hasColor = payload.color.has_value();
        if (ImGui::Checkbox("Color", &hasColor))
        {
            if (hasColor) payload.color = glm::vec4(1.0f);
            else payload.color.reset();
            isDirty = true;
            previewDirty = true;
        }
        if (payload.color)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(210.0f);
            if (ImGui::ColorEdit4("##payloadColor", &payload.color->x))
            {
                isDirty = true;
                previewDirty = true;
            }
        }

        bool hasScalar = payload.scalar.has_value();
        if (ImGui::Checkbox("Scalar", &hasScalar))
        {
            if (hasScalar) payload.scalar = 0.0f;
            else payload.scalar.reset();
            isDirty = true;
            previewDirty = true;
        }
        if (payload.scalar)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::DragFloat("##payloadScalar", &(*payload.scalar), 0.01f))
            {
                isDirty = true;
                previewDirty = true;
            }
        }

        drawOverrideList(payload.custom, "Payload Overrides");
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

        // --- Kind (VK-1496) — selects the payload and swaps the field set below ---
        const char* kindNames[] = {"VFX", "Sound", "Script Cue"};
        int kindIdx = static_cast<int>(step.kind);
        if (kindIdx < 0 || kindIdx >= IM_ARRAYSIZE(kindNames))
            kindIdx = 0;
        if (ImGui::Combo("Kind", &kindIdx, kindNames, IM_ARRAYSIZE(kindNames)))
        {
            step.kind = static_cast<vfx::VFXStepKind>(kindIdx);
            isDirty = true;
            previewDirty = true; // a converted step drops out of / into the composited preview
        }

        ImGui::Separator();

        // --- Common fields (every kind) ---
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

        // --- Kind-specific fields ---
        if (step.kind == vfx::VFXStepKind::VFX)
        {
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

            drawOverrideList(step.overrides, "Overrides");
        }
        else if (step.kind == vfx::VFXStepKind::Sound)
        {
            ImGui::TextUnformatted("Audio Asset");
            std::string audioLabel;
            if (step.audioRef.isValid())
            {
                const std::string& resolved = step.audioRef.resolve();
                audioLabel = resolved.empty() ? "(missing)" : fs::path(resolved).filename().string();
            }
            else
            {
                audioLabel = "(none) - drop a .vfAudio here";
            }

            bool audioMissing = step.audioRef.isValid() && step.audioRef.resolve().empty();
            if (audioMissing)
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 80, 80, 255));
            ImGui::Button(audioLabel.c_str(), ImVec2(-1.0f, 0.0f));
            if (audioMissing)
                ImGui::PopStyleColor();

            if (auto dropped = acceptAssetDropOnLastItem("##vfxSeqAudioRef", {".vfaudio"}))
            {
                auto ref = asset::AssetRef::fromPath(*dropped);
                if (ref.isValid())
                {
                    step.audioRef = ref;
                    isDirty = true;
                }
            }

            ImGui::Separator();

            if (ImGui::DragFloat("Volume", &step.volume, 0.01f, 0.0f, 2.0f, "%.2f"))
                isDirty = true;
            if (ImGui::DragFloat("Pitch", &step.pitch, 0.01f, 0.1f, 4.0f, "%.2f"))
                isDirty = true;
            if (ImGui::Checkbox("Spatialized (3D)", &step.spatialized))
                isDirty = true;
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("On = 3D at the step's world transform; off = 2D. Sound is fire-and-forget "
                                  "(no loop / stop) and is silent during editor scrub — audible only in play mode.");

            if (step.spatialized)
            {
                if (ImGui::DragFloat3("Local Position", &step.localPosition.x, 0.05f))
                    isDirty = true;
                drawSocketField(step);
            }
        }
        else if (step.kind == vfx::VFXStepKind::ScriptCue)
        {
            char emitBuf[256];
            std::strncpy(emitBuf, step.emitCueName.c_str(), sizeof(emitBuf) - 1);
            emitBuf[sizeof(emitBuf) - 1] = '\0';
            if (ImGui::InputText("Emit Cue", emitBuf, IM_ARRAYSIZE(emitBuf)))
            {
                step.emitCueName = emitBuf;
                isDirty = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cue published to gameplay scripts (onComboCue) when this step fires. "
                                  "Fires on forward playback only — silent during editor scrub/seek.");

            ImGui::Separator();
            ImGui::TextUnformatted("Payload");
            drawCuePayload(step.cuePayload);
        }
        else
        {
            ImGui::TextDisabled("Unknown step kind.");
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

            drawCuePayload(marker.payload);

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
            // VK-1496 — the composited preview is VFX-only. Skip Sound/ScriptCue kinds up front
            // so a step converted away from VFX but retaining a stale vfxRef doesn't render a
            // ghost effect (and typed steps stay silent/visual-free in the editor by construction).
            if (step.kind != vfx::VFXStepKind::VFX)
                continue;
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

            services::applyToPreviewParams(ps.params, services::toEmitterOverrides(step.overrides));

            ps.localTransform = vfx::VFXComboTimeline::composeStepLocal(step);
            // Seed by the SOURCE step index i, not the compacted output count, so a
            // skipped (invalid/empty) earlier step doesn't desync the preview from the
            // runtime, which seeds via combo.timeline.derivedSeed(stepIndex) over the
            // 1:1 combo.steps built from data->steps.
            ps.seed = vfx::VFXComboTimeline::deriveSeed(comboSeed, static_cast<int>(i));
            ps.startTime = step.startTime;
            ps.duration = step.duration;
            ps.loop = step.loop;
            ps.stopMode = static_cast<int>(static_cast<uint8_t>(step.stopMode));
            ps.cueName = step.cueName;
            desc.steps.push_back(std::move(ps));
        }

        return desc;
    }

    math::AABB VFXSequenceEditorWindow::computeSequenceBoundsUnion() const
    {
        if (!data)
            return math::AABB(glm::vec3(-1.0f), glm::vec3(1.0f));

        bool any = false;
        math::AABB result;
        for (const auto& step : data->steps)
        {
            if (step.kind != vfx::VFXStepKind::VFX) // VK-1496 — only VFX steps contribute bounds
                continue;
            if (!step.vfxRef.isValid())
                continue;
            const std::string path = step.vfxRef.resolve();
            if (path.empty())
                continue;

            std::shared_ptr<vfx::VFXData> vfxData;
            if (auto it = vfxCache.find(path); it != vfxCache.end())
                vfxData = it->second;
            else
            {
                if (auto loaded = vfx::VFXAsset::load(path))
                    vfxData = std::make_shared<vfx::VFXData>(std::move(*loaded));
                vfxCache[path] = vfxData; // cache nulls too, so a bad path isn't retried
            }
            if (!vfxData)
                continue;

            const math::AABB local = vfx::resolveBounds(vfxData->bounds, *vfxData);
            const math::AABB world = local.getTransformed(vfx::VFXComboTimeline::composeStepLocal(step));
            if (!any)
            {
                result = world;
                any = true;
            }
            else
            {
                result.expand(world.min);
                result.expand(world.max);
            }
        }

        if (!any)
            return math::AABB(glm::vec3(-1.0f), glm::vec3(1.0f));
        return result;
    }

    void VFXSequenceEditorWindow::recalcSequenceBounds()
    {
        if (!data)
            return;
        const math::AABB a = computeSequenceBoundsUnion();
        data->bounds.mode = vfx::VFXBoundsMode::Fixed;
        data->bounds.center = a.getCenter();
        data->bounds.extents = a.getExtents();
        isDirty = true;
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

        if (showBounds && data)
        {
            const auto& b = data->bounds;
            math::AABB ov;
            if (b.mode == vfx::VFXBoundsMode::Fixed &&
                (b.extents.x > 0.0f || b.extents.y > 0.0f || b.extents.z > 0.0f))
                ov = math::AABB(b.center - b.extents, b.center + b.extents);
            else
                ov = computeSequenceBoundsUnion();
            previewPanel->setBoundsOverlay(true, ov);
        }
        else
        {
            previewPanel->setBoundsOverlay(false, math::AABB{});
        }

        previewPanel->draw();
    }
}
