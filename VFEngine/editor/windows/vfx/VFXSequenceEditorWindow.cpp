#include "VFXSequenceEditorWindow.hpp"
#include "../../dragdrop/AssetDropTarget.hpp"

#include "print/Log.hpp"
#include "imgui.h"
#include "events/EventDispatcher.hpp"
#include "events/project/ResourceEvents.hpp"

#include <vfx/VFXSequenceAsset.hpp>
#include <vfx/VFXAsset.hpp>
#include <vfx/VFXTypes.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetMetadataSerializer.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <variant>

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

        // ===== CPU combo preview (editor-only; the runtime uses the GPU path) =====
        // A deliberately lightweight simulation: enough to see WHEN each step
        // fires, WHERE it sits (offset), and roughly its color/size/spread. Not a
        // pixel match to the GPU shaders.
        struct PreviewEmitterCfg
        {
            bool valid = false;
            float spawnRate = 10.0f, lifetime = 2.0f, startSize = 1.0f, startSpeed = 1.0f;
            glm::vec3 emitDir{0.0f, 1.0f, 0.0f};
            glm::vec4 startColor{1.0f, 1.0f, 1.0f, 1.0f};
            glm::vec3 gravity{0.0f};
        };

        struct PreviewParticle
        {
            glm::vec3 pos{0.0f};
            glm::vec3 vel{0.0f};
            float age = 0.0f, life = 1.0f, size = 1.0f;
            glm::vec4 color{1.0f};
        };

        struct PreviewEmitterRuntime
        {
            std::vector<PreviewParticle> particles;
            float spawnAccum = 0.0f;
            uint32_t rng = 0x1234567u;
        };

        // xorshift32 -> [0,1)
        inline float prngFloat(uint32_t& s)
        {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            return static_cast<float>(s & 0xFFFFFFu) / static_cast<float>(0x1000000);
        }
        inline float prngSym(uint32_t& s) { return prngFloat(s) * 2.0f - 1.0f; }

        // Read a property off a VFX graph node directly from the variant. Avoids
        // VFXEmitterConfigLoader (which pulls in graphics-only render::vfx types).
        float nodeFloat(const vfx::VFXNode& n, const char* key, float def)
        {
            auto it = n.properties.find(key);
            if (it == n.properties.end()) return def;
            if (const auto* p = std::get_if<float>(&it->second.value)) return *p;
            if (const auto* p = std::get_if<int32_t>(&it->second.value)) return static_cast<float>(*p);
            return def;
        }
        glm::vec3 nodeVec3(const vfx::VFXNode& n, const char* key, const glm::vec3& def)
        {
            auto it = n.properties.find(key);
            if (it == n.properties.end()) return def;
            if (const auto* p = std::get_if<glm::vec3>(&it->second.value)) return *p;
            return def;
        }
        glm::vec4 nodeVec4(const vfx::VFXNode& n, const char* key, const glm::vec4& def)
        {
            auto it = n.properties.find(key);
            if (it == n.properties.end()) return def;
            if (const auto* p = std::get_if<glm::vec4>(&it->second.value)) return *p; // covers Vec4 and Color
            return def;
        }

        PreviewEmitterCfg loadPreviewCfg(const vfx::VFXSequenceStep& step)
        {
            PreviewEmitterCfg cfg;
            const std::string path = step.vfxRef.isValid() ? step.vfxRef.resolve() : std::string();
            if (path.empty())
                return cfg;
            auto data = vfx::VFXAsset::load(path);
            if (!data)
                return cfg;
            const vfx::VFXNode* emitter = data->graph.findEmitterNode();
            if (!emitter)
                return cfg;

            cfg.spawnRate = nodeFloat(*emitter, "spawnRate", vfx::EmitterDefaults::SPAWN_RATE);
            cfg.lifetime = nodeFloat(*emitter, "lifetime", vfx::EmitterDefaults::LIFETIME);
            cfg.startSize = nodeFloat(*emitter, "startSize", vfx::EmitterDefaults::START_SIZE);
            cfg.startSpeed = nodeFloat(*emitter, "startSpeed", vfx::EmitterDefaults::START_SPEED);
            cfg.emitDir = nodeVec3(*emitter, "startVelocity", glm::vec3(0.0f, 1.0f, 0.0f));
            cfg.startColor = nodeVec4(*emitter, "startColor", glm::vec4(1.0f));
            // Gravity/forces are intentionally not simulated here — this preview is for
            // timing/placement/color, not physics. cfg.gravity stays zero.

            // Apply the step's name-keyed overrides (same names the runtime uses).
            for (const auto& [name, value] : step.scalarOverrides)
            {
                if (name == "spawnRate") cfg.spawnRate = value;
                else if (name == "lifetime") cfg.lifetime = value;
                else if (name == "startSize") cfg.startSize = value;
                else if (name == "startSpeed") cfg.startSpeed = value;
            }
            for (const auto& [name, value] : step.vectorOverrides)
            {
                if (name == "startColor") cfg.startColor = value;
                else if (name == "emitDirection") cfg.emitDir = glm::vec3(value);
            }

            cfg.spawnRate = std::max(cfg.spawnRate, 0.0f);
            cfg.lifetime = std::max(cfg.lifetime, 0.05f);
            cfg.startSize = std::max(cfg.startSize, 0.001f);
            cfg.valid = true;
            return cfg;
        }
    }

    struct VFXSequenceEditorWindow::PreviewState
    {
        std::vector<PreviewEmitterCfg> cfgs;     // per step
        std::vector<PreviewEmitterRuntime> rt;   // per step
        float yaw = 0.7f, pitch = 0.35f, dist = 9.0f;
        bool cfgsDirty = true;
    };

    VFXSequenceEditorWindow::VFXSequenceEditorWindow(const std::string& seqPath)
        : seqPath(seqPath)
        , preview(std::make_unique<PreviewState>())
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
                float spacingX = ImGui::GetStyle().ItemSpacing.x;
                float spacingY = ImGui::GetStyle().ItemSpacing.y;
                float topHeight = avail.y - timelineHeight - spacingY;
                float listWidth = 220.0f;
                float previewWidth = std::max(280.0f, avail.x * 0.38f);
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
                if (preview) preview->cfgsDirty = true;
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
        // Scrub bar + transport that drives the CPU combo preview (see
        // drawPreviewViewport). Step markers show each step at its startTime.
        float maxTime = 1.0f;
        for (const auto& step : data->steps)
            maxTime = std::max(maxTime, step.startTime + std::max(step.duration, 0.5f));

        ImGui::SetNextItemWidth(-300.0f);
        if (ImGui::SliderFloat("##previewTime", &previewTime, 0.0f, maxTime, "t = %.2f s"))
            resetPreview(); // jumping time invalidates the incremental sim
        ImGui::SameLine();
        if (ImGui::Button(previewPlaying ? "Pause" : "Play"))
        {
            previewPlaying = !previewPlaying;
            if (previewPlaying && previewTime >= maxTime)
            {
                previewTime = 0.0f;
                resetPreview();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
        {
            previewTime = 0.0f;
            previewPlaying = false;
            resetPreview();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload"))
            resetPreview(); // pick up edited child .vfVFX / params
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &previewLoop);

        if (previewPlaying)
        {
            const float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
            previewTime += dt;
            stepPreview(dt);
            if (previewTime > maxTime)
            {
                if (previewLoop)
                {
                    previewTime = 0.0f;
                    resetPreview();
                }
                else
                {
                    previewTime = maxTime;
                    previewPlaying = false;
                }
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

    void VFXSequenceEditorWindow::resetPreview()
    {
        if (!preview)
            return;
        preview->cfgs.clear();
        preview->rt.clear();
        preview->cfgsDirty = true;
    }

    void VFXSequenceEditorWindow::stepPreview(float dt)
    {
        if (!preview || !data)
            return;

        const size_t n = data->steps.size();
        if (preview->cfgsDirty || preview->cfgs.size() != n)
        {
            preview->cfgs.resize(n);
            preview->rt.assign(n, PreviewEmitterRuntime{});
            for (size_t i = 0; i < n; ++i)
            {
                preview->cfgs[i] = loadPreviewCfg(data->steps[i]);
                preview->rt[i].rng = 0x9E3779B9u ^ static_cast<uint32_t>(i * 2654435761u + 1u);
            }
            preview->cfgsDirty = false;
        }

        constexpr size_t kMaxParticlesPerEmitter = 400;
        for (size_t i = 0; i < n; ++i)
        {
            const auto& step = data->steps[i];
            const auto& cfg = preview->cfgs[i];
            auto& rt = preview->rt[i];
            if (!cfg.valid)
                continue;

            // Time-driven steps emit while the playhead is inside their window;
            // cue-driven steps don't auto-fire in the preview.
            const bool timeDriven = step.cueName.empty();
            const float emitWindow = step.loop ? 1.0e9f
                                               : (step.duration > 0.0f ? step.duration : cfg.lifetime);
            const bool emitting = timeDriven && previewTime >= step.startTime &&
                                  previewTime < step.startTime + emitWindow;

            if (emitting)
            {
                rt.spawnAccum += cfg.spawnRate * dt;
                while (rt.spawnAccum >= 1.0f && rt.particles.size() < kMaxParticlesPerEmitter)
                {
                    rt.spawnAccum -= 1.0f;

                    glm::vec3 dir = cfg.emitDir;
                    if (glm::dot(dir, dir) < 1.0e-6f)
                        dir = glm::vec3(0.0f, 1.0f, 0.0f);
                    dir = glm::normalize(dir);
                    const glm::vec3 jitter(prngSym(rt.rng), prngSym(rt.rng), prngSym(rt.rng));
                    dir = glm::normalize(dir + 0.25f * jitter);

                    PreviewParticle p;
                    p.pos = step.localPosition;
                    p.vel = dir * cfg.startSpeed;
                    p.life = cfg.lifetime;
                    p.size = cfg.startSize;
                    p.color = cfg.startColor;
                    rt.particles.push_back(p);
                }
            }

            for (auto& p : rt.particles)
            {
                p.age += dt;
                p.vel += cfg.gravity * dt;
                p.pos += p.vel * dt;
            }
            rt.particles.erase(
                std::remove_if(rt.particles.begin(), rt.particles.end(),
                    [](const PreviewParticle& p) { return p.age >= p.life; }),
                rt.particles.end());
        }
    }

    void VFXSequenceEditorWindow::drawPreviewViewport()
    {
        ImVec2 size = ImGui::GetContentRegionAvail();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p0, p1, IM_COL32(18, 18, 22, 255));
        if (size.x < 16.0f || size.y < 16.0f || !preview)
            return;

        dl->PushClipRect(p0, p1, true);

        // Orbit interaction over the viewport.
        ImGui::InvisibleButton("##previewVP", size);
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            preview->yaw += d.x * 0.01f;
            preview->pitch = std::clamp(preview->pitch + d.y * 0.01f, -1.45f, 1.45f);
        }
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0f)
            preview->dist = std::clamp(preview->dist - ImGui::GetIO().MouseWheel * 0.6f, 1.5f, 40.0f);

        // Camera.
        const float cp = std::cos(preview->pitch), sp = std::sin(preview->pitch);
        const float cy = std::cos(preview->yaw), sy = std::sin(preview->yaw);
        const glm::vec3 target(0.0f, 1.0f, 0.0f);
        const glm::vec3 eye = target + glm::vec3(sy * cp, sp, cy * cp) * preview->dist;
        const glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
        const float fov = glm::radians(45.0f);
        const float aspect = size.x / size.y;
        const glm::mat4 proj = glm::perspective(fov, aspect, 0.05f, 200.0f);
        const float focal = size.y / (2.0f * std::tan(fov * 0.5f));

        auto project = [&](const glm::vec3& wp, ImVec2& out, float& depth) -> bool
        {
            const glm::vec4 vp = view * glm::vec4(wp, 1.0f);
            depth = -vp.z;
            if (depth <= 0.05f)
                return false;
            const glm::vec4 c = proj * vp;
            if (c.w <= 0.0f)
                return false;
            const glm::vec3 ndc = glm::vec3(c) / c.w;
            out.x = p0.x + (ndc.x * 0.5f + 0.5f) * size.x;
            out.y = p0.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y;
            return true;
        };

        // Reference ground grid.
        for (int g = -5; g <= 5; ++g)
        {
            ImVec2 a, b; float da = 0.0f, db = 0.0f;
            if (project(glm::vec3(static_cast<float>(g), 0.0f, -5.0f), a, da) &&
                project(glm::vec3(static_cast<float>(g), 0.0f, 5.0f), b, db))
                dl->AddLine(a, b, IM_COL32(60, 60, 70, 110));
            if (project(glm::vec3(-5.0f, 0.0f, static_cast<float>(g)), a, da) &&
                project(glm::vec3(5.0f, 0.0f, static_cast<float>(g)), b, db))
                dl->AddLine(a, b, IM_COL32(60, 60, 70, 110));
        }

        // Gather particle splats, sort back-to-front, draw.
        struct Splat { float depth; ImVec2 sc; float r; ImU32 col; };
        std::vector<Splat> splats;
        for (const auto& emitter : preview->rt)
        {
            for (const auto& part : emitter.particles)
            {
                ImVec2 sc; float depth = 0.0f;
                if (!project(part.pos, sc, depth))
                    continue;
                const float t = part.life > 0.0f ? part.age / part.life : 1.0f;
                const float alpha = std::clamp((1.0f - t) * part.color.a, 0.0f, 1.0f);
                if (alpha <= 0.01f)
                    continue;
                const float r = std::clamp(part.size * focal / depth * 0.5f, 1.5f, 64.0f);
                const ImU32 col = IM_COL32(
                    static_cast<int>(std::clamp(part.color.r, 0.0f, 1.0f) * 255.0f),
                    static_cast<int>(std::clamp(part.color.g, 0.0f, 1.0f) * 255.0f),
                    static_cast<int>(std::clamp(part.color.b, 0.0f, 1.0f) * 255.0f),
                    static_cast<int>(alpha * 255.0f));
                splats.push_back({depth, sc, r, col});
            }
        }
        std::sort(splats.begin(), splats.end(),
                  [](const Splat& a, const Splat& b) { return a.depth > b.depth; });
        for (const auto& s : splats)
            dl->AddCircleFilled(s.sc, s.r, s.col);

        char info[80];
        std::snprintf(info, sizeof(info), "t=%.2fs   particles=%d   (drag: orbit, wheel: zoom)",
                      previewTime, static_cast<int>(splats.size()));
        dl->AddText(ImVec2(p0.x + 6.0f, p0.y + 4.0f), IM_COL32(200, 200, 210, 255), info);

        dl->PopClipRect();
    }
}
