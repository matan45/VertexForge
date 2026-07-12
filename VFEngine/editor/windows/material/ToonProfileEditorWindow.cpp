#include "ToonProfileEditorWindow.hpp"
#include "MaterialPreviewPanel.hpp"
#include "../../graph/ShaderGraphCompiler.hpp"
#include <material/MaterialTypes.hpp>
#include <material/MaterialAsset.hpp>
#include <material/ToonProfileManager.hpp>
#include <asset/AssetMetadata.hpp>
#include <asset/AssetMetadataSerializer.hpp>
#include <asset/AssetGUID.hpp>
#include <resource/AssetTypes.hpp>
#include <time/Timer.hpp>
#include "imgui.h"
#include <ctime>

namespace windows
{
    namespace
    {
        const std::vector<std::pair<std::wstring, std::wstring>> kToonFilter =
            {{L"Toon Profile (*.vfToonProfile)", L"*.vfToonProfile"}};

        // The preview controller only injects a custom shader when the material path is
        // non-empty (MaterialPreviewController.cpp:403). The scratch material has no real
        // path, so give it a stable synthetic key — the shader cache still recompiles on
        // cachedFragmentShader change (hashed), so live edits update.
        constexpr const char* kScratchPath = "__toonProfilePreview__";
    }

    ToonProfileEditorWindow::ToonProfileEditorWindow()
        : previewPanel(std::make_unique<editor::materialeditor::MaterialPreviewPanel>(this))
    {
        previewPanel->setShowSettings(false); // fields are edited on the left; keep the right pure preview
        scratchMaterial = std::make_shared<material::MaterialData>(
            material::MaterialAsset::createDefault("ToonProfilePreview"));
        scratchMaterial->shadingModel = material::ShadingModel::Toon;
    }

    ToonProfileEditorWindow::~ToonProfileEditorWindow() = default; // unique_ptr -> ~MaterialPreviewPanel::cleanup()

    void ToonProfileEditorWindow::markDirty()
    {
        dirty = true;
        previewDirty = true;
        lastEditTime = static_cast<float>(engineTime::Timer::getElapsedTime());
    }

    void ToonProfileEditorWindow::newProfile()
    {
        profile = material::ToonProfile{};
        currentPath.clear();
        statusMessage.clear();
        markDirty();
        dirty = false; // a fresh default is not "unsaved edits"
    }

    void ToonProfileEditorWindow::loadProfile()
    {
        std::string path = fileDialog.openFileDialog(kToonFilter);
        if (path.empty())
            return;
        if (auto loaded = material::ToonProfileManager::instance().getOrLoad(path))
        {
            profile = *loaded;
            currentPath = path;
            dirty = false;
            previewDirty = true;
            lastEditTime = static_cast<float>(engineTime::Timer::getElapsedTime());
            statusMessage = "Loaded " + path;
        }
        else
        {
            statusMessage = "Failed to load " + path;
        }
    }

    void ToonProfileEditorWindow::saveProfile(bool saveAs)
    {
        std::string path = (saveAs || currentPath.empty())
            ? fileDialog.saveFileDialog(kToonFilter, L".vfToonProfile")
            : currentPath;
        if (path.empty())
            return;

        // Write the asset + refresh the cache + notify (GPU table re-uploads the row in place,
        // so every material referencing this profile updates live in the viewport).
        if (!material::ToonProfileManager::instance().save(path, profile))
        {
            statusMessage = "Failed to save " + path;
            return;
        }

        // .vfmeta sidecar so the content browser + game export recognise the asset.
        auto metaPath = asset::AssetMetadataSerializer::getMetaPath(path);
        asset::AssetMetadata metadata;
        metadata.guid = asset::AssetGUID::generate();
        metadata.type = resource::AssetType::ToonProfile;
        metadata.importSourcePath = "editor://toonprofile";
        metadata.formatVersion = 1;
        auto t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        metadata.importTimestamp = buf;
        asset::AssetMetadataSerializer::save(metadata, metaPath);

        currentPath = path;
        dirty = false;
        statusMessage = "Saved " + path;
    }

    void ToonProfileEditorWindow::refreshPreview()
    {
        scratchMaterial->toonProfileValues = profile;
        editor::graph::ShaderCompileOptions opts;
        opts.toonEnabled = true;
        opts.toonProfile = profile;
        auto result = editor::graph::ShaderGraphCompiler::compileGraph(scratchMaterial->graph, opts);
        if (result.success)
        {
            scratchMaterial->cachedVertexShader = result.vertexShader;
            scratchMaterial->cachedFragmentShader = result.fragmentShader;
            scratchMaterial->needsRecompile = false;
            previewPanel->updateFromGraph(scratchMaterial, kScratchPath, true);
        }
    }

    void ToonProfileEditorWindow::drawFields()
    {
        ImGui::TextUnformatted("Diffuse bands");
        if (ImGui::ColorEdit3("Shade Color", &profile.shadeColor.x)) markDirty();
        if (ImGui::ColorEdit3("Mid Color", &profile.midColor.x)) markDirty();
        if (ImGui::SliderFloat("Shadow Threshold", &profile.shadowThreshold, 0.0f, 1.0f, "%.3f")) markDirty();
        if (ImGui::SliderFloat("Mid Threshold", &profile.midThreshold, 0.0f, 1.0f, "%.3f")) markDirty();
        if (ImGui::SliderFloat("Band Smoothness", &profile.bandSmoothness, 0.0f, 0.25f, "%.3f")) markDirty();
        if (ImGui::SliderFloat("GI Scale", &profile.giScale, 0.0f, 2.0f, "%.3f")) markDirty();

        ImGui::Separator();
        ImGui::TextUnformatted("Toon specular");
        if (ImGui::ColorEdit3("Spec Color", &profile.specColor.x)) markDirty();
        if (ImGui::SliderFloat("Spec Threshold", &profile.specThreshold, 0.0f, 1.0f, "%.3f")) markDirty();
        if (ImGui::SliderFloat("Spec Smoothness", &profile.specSmoothness, 0.0f, 0.25f, "%.3f")) markDirty();
        if (ImGui::SliderFloat("Spec Intensity", &profile.specIntensity, 0.0f, 4.0f, "%.3f")) markDirty();
        if (ImGui::SliderFloat("Spec Shininess", &profile.specShininess, 1.0f, 256.0f, "%.1f")) markDirty();

        ImGui::Separator();
        ImGui::TextUnformatted("Rim");
        if (ImGui::ColorEdit3("Rim Color", &profile.rimColor.x)) markDirty();
        if (ImGui::SliderFloat("Rim Power", &profile.rimPower, 0.1f, 8.0f, "%.3f")) markDirty();
        if (ImGui::SliderFloat("Rim Intensity", &profile.rimIntensity, 0.0f, 4.0f, "%.3f")) markDirty();
    }

    void ToonProfileEditorWindow::draw()
    {
        if (!visible)
        {
            if (previewPanel)
                previewPanel->cleanup();
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(780, 480), ImGuiCond_FirstUseEver);
        maximizer.preBegin();
        std::string title = std::string("Toon Profile Editor") + (dirty ? " *" : "") + "###ToonProfileEditor";
        if (ImGui::Begin(title.c_str(), &visible, maximizer.windowFlags()))
        {
            if (ImGui::Button("New")) newProfile();
            ImGui::SameLine();
            if (ImGui::Button("Load...")) loadProfile();
            ImGui::SameLine();
            if (ImGui::Button("Save")) saveProfile(false);
            ImGui::SameLine();
            if (ImGui::Button("Save As...")) saveProfile(true);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", currentPath.empty() ? "(unsaved)" : currentPath.c_str());
            ImGui::SameLine();
            maximizer.drawButton();
            if (!statusMessage.empty())
                ImGui::TextWrapped("%s", statusMessage.c_str());
            ImGui::Separator();

            ImGui::BeginChild("ToonFields", ImVec2(320.0f, 0.0f), true);
            drawFields();
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("ToonPreview", ImVec2(0.0f, 0.0f), true);
            previewPanel->draw(scratchMaterial, kScratchPath);
            if (previewPanel->hasShaderError())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Shader error:");
                ImGui::TextWrapped("%s", previewPanel->getShaderError().c_str());
            }
            ImGui::EndChild();

            // Debounced recompile: only rebuild the scratch shader once edits settle.
            const float now = static_cast<float>(engineTime::Timer::getElapsedTime());
            if (previewDirty && (now - lastEditTime) >= PREVIEW_DEBOUNCE_SECONDS)
            {
                refreshPreview();
                previewDirty = false;
            }
        }
        ImGui::End();
    }
}
