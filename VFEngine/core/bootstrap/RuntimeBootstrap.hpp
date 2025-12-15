#pragma once
#include <memory>
#include <functional>

namespace services
{
    class IOffScreenProvider;
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

    class RuntimeBootstrap
    {
    private:
        std::unique_ptr<::controllers::CoreInterface> coreInterface;
        std::unique_ptr<::controllers::OffScreen> offScreen;

        // Adapters that implement provider interfaces
        std::unique_ptr<OffScreenAdapter> offScreenAdapter;
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
