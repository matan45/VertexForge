#pragma once
#include <memory>
#include <functional>

namespace services
{
    class IOffScreenProvider;
    class IEditorTextureProvider;
    class IMaterialPreviewProvider;
    class IMeshPreviewProvider;
    class IAudioProvider;
}

namespace window
{
    class Window;
}

namespace scene
{
    class SceneGraphSystem;
}

namespace controllers
{
    class CoreInterface;
    class OffScreen;
}

namespace core
{
    class OffScreenAdapter;
    class EditorTextureAdapter;
    class MaterialPreviewAdapter;
    class MeshPreviewAdapter;
    class AudioAdapter;

    class EditorBootstrap
    {
    private:
        std::unique_ptr<::controllers::CoreInterface> coreInterface;
        std::unique_ptr<::controllers::OffScreen> offScreen;

        std::unique_ptr<OffScreenAdapter> offScreenAdapter;
        std::unique_ptr<EditorTextureAdapter> textureAdapter;
        std::unique_ptr<MaterialPreviewAdapter> materialPreviewAdapter;
        std::unique_ptr<MeshPreviewAdapter> meshPreviewAdapter;
        std::unique_ptr<AudioAdapter> audioAdapter;
    public:
        explicit EditorBootstrap();
        ~EditorBootstrap();

        // Non-copyable
        EditorBootstrap(const EditorBootstrap&) = delete;
        EditorBootstrap& operator=(const EditorBootstrap&) = delete;

        void init();

        void run() const;

        void cleanUp();

        // === Provider Accessors ===

        services::IOffScreenProvider* getOffScreenProvider();

        services::IEditorTextureProvider* getEditorTextureProvider();

        services::IMaterialPreviewProvider* getMaterialPreviewProvider();

        services::IMeshPreviewProvider* getMeshPreviewProvider();

        services::IAudioProvider* getAudioProvider();

        // === Other Accessors ===

        window::Window* getWindow();

        std::shared_ptr<scene::SceneGraphSystem> getSceneGraphSystem();

        // === Frame Callbacks ===

        // Set callback to be called each frame (for service updates)
        void setFrameCallback(std::function<void()> callback);

        // Trigger window resize handling
        void triggerResize();
    };
}
