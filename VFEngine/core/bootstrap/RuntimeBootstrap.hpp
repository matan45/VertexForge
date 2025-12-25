#pragma once
#include <memory>
#include <functional>

namespace services
{
    class IOffScreenProvider;
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
    class AudioAdapter;

    class RuntimeBootstrap
    {
    private:
        std::unique_ptr<::controllers::CoreInterface> coreInterface;
        std::unique_ptr<::controllers::OffScreen> offScreen;

        std::unique_ptr<OffScreenAdapter> offScreenAdapter;
        std::unique_ptr<AudioAdapter> audioAdapter;

    public:
        explicit RuntimeBootstrap();
        ~RuntimeBootstrap();

        // Non-copyable
        RuntimeBootstrap(const RuntimeBootstrap&) = delete;
        RuntimeBootstrap& operator=(const RuntimeBootstrap&) = delete;

        void init();

        void run() const;

        void cleanUp();

        // === Provider Accessors ===

        services::IOffScreenProvider* getOffScreenProvider();

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
