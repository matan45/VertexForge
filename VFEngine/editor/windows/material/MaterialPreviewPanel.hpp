#pragma once
#include <material/MaterialTypes.hpp>
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
        bool needsInit = true;
        float panelWidth = 250.0f;
        bool isDraggingPreview = false;
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

        std::string getShaderError() const { return lastShaderError; }
        bool hasShaderError() const { return !lastShaderError.empty(); }
        void clearShaderError() { lastShaderError.clear(); }

    private:
        BlendModeChangedCallback onBlendModeChanged;
        void handleInput();
    };
}
