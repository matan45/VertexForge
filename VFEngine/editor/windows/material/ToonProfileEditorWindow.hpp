#pragma once
#include "nfd/FileDialog.hpp"
#include "../preview/PreviewWindowChrome.hpp"
#include <material/ToonProfile.hpp>
#include <memory>
#include <string>

namespace material { struct MaterialData; }
namespace editor::materialeditor { class MaterialPreviewPanel; }

namespace windows
{
    // VK-1493 — authoring window for the reusable `.vfToonProfile` asset. Left column edits
    // every profile field; right column is a live sphere preview (a scratch Toon material
    // rendered through MaterialPreviewPanel). Value edits recompile the scratch shader on a
    // short debounce (self-contained defines path — see ShaderGraphCompiler). Saving writes
    // the `.vfToonProfile` (+ `.vfmeta`) and notifies ToonProfileManager, which live-updates
    // the GPU table so every referencing material in the viewport updates immediately.
    class ToonProfileEditorWindow
    {
    public:
        ToonProfileEditorWindow();
        ~ToonProfileEditorWindow();

        ToonProfileEditorWindow(const ToonProfileEditorWindow&) = delete;
        ToonProfileEditorWindow& operator=(const ToonProfileEditorWindow&) = delete;

        void show() { visible = true; }
        void draw();

    private:
        void drawFields();          // returns via markDirty on any change
        void newProfile();
        void loadProfile();
        void saveProfile(bool saveAs);
        void markDirty();
        void refreshPreview();      // recompile scratch material + push to the preview panel

        bool visible = false;
        editor::preview::WindowMaximizer maximizer;
        nfd::FileDialog fileDialog;

        material::ToonProfile profile;
        std::string currentPath;    // empty = unsaved
        bool dirty = false;
        std::string statusMessage;

        std::unique_ptr<editor::materialeditor::MaterialPreviewPanel> previewPanel;
        std::shared_ptr<material::MaterialData> scratchMaterial;
        bool previewDirty = true;
        float lastEditTime = 0.0f;
        static constexpr float PREVIEW_DEBOUNCE_SECONDS = 0.15f;
    };
}
