#pragma once
#include <material/MaterialTypes.hpp>
#include "../preview/PreviewEnvironment.hpp"
#include <memory>
#include <functional>
#include <string>

namespace editor
{
    class OrbitCamera;
}

namespace editor::materialeditor
{
    class MaterialPreviewPanel
    {
    private:
        void* instanceId;
        std::unique_ptr<OrbitCamera> camera;
        preview::PreviewEnvironment environment;
        bool needsInit = true;
        float panelWidth = 250.0f;
        bool isDraggingOrbit = false;
        bool isDraggingPan = false;
        bool showSettings = true;
        std::string lastShaderError;
    
    public:
        using BlendModeChangedCallback = std::function<void()>;

        explicit MaterialPreviewPanel(void* instanceId);
        ~MaterialPreviewPanel();

        void init();
        void draw(std::shared_ptr<::material::MaterialData> materialData,
                  const std::string& materialPath);
        void updateFromGraph(std::shared_ptr<::material::MaterialData> materialData,
                             const std::string& materialPath,
                             bool useCustomShader);
        void cleanup();

        bool isInitialized() const { return !needsInit; }
        void setOnBlendModeChanged(BlendModeChangedCallback callback) { onBlendModeChanged = callback; }
        // VK-1493: hide the material settings block (shading model / blend mode) when the panel
        // is embedded purely as a preview viewport (e.g. the Toon Profile editor's sphere).
        void setShowSettings(bool show) { showSettings = show; }

        std::string getShaderError() const { return lastShaderError; }
        bool hasShaderError() const { return !lastShaderError.empty(); }
        void clearShaderError() { lastShaderError.clear(); }

    private:
        BlendModeChangedCallback onBlendModeChanged;
        void handleInput();
    };
}
