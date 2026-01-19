#pragma once
#include <memory>
#include <functional>

namespace services
{
    class IOffScreenProvider;
    class IAudioProvider;
    class IScriptingProvider;
    class IPhysicsProvider;
    class IAnimatorProvider;
    class IVFXRuntimeProvider;
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
    class ScriptingAdapter;
    class PhysicsAdapter;
    class AnimatorAdapter;
    class VFXRuntimeAdapter;

    class RuntimeBootstrap
    {
    private:
        std::unique_ptr<::controllers::CoreInterface> coreInterface;
        std::unique_ptr<::controllers::OffScreen> offScreen;

        std::unique_ptr<OffScreenAdapter> offScreenAdapter;
        std::unique_ptr<AudioAdapter> audioAdapter;
        std::unique_ptr<ScriptingAdapter> scriptingAdapter;
        std::unique_ptr<PhysicsAdapter> physicsAdapter;
        std::unique_ptr<AnimatorAdapter> animatorAdapter;
        std::unique_ptr<VFXRuntimeAdapter> vfxRuntimeAdapter;

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

        services::IScriptingProvider* getScriptingProvider();

        services::IPhysicsProvider* getPhysicsProvider();

        services::IAnimatorProvider* getAnimatorProvider();

        services::IVFXRuntimeProvider* getVFXRuntimeProvider();

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
