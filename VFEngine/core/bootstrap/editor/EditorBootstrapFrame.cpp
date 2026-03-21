#include "EditorBootstrap.hpp"
#include "../../controllers/CoreInterface.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../adapters/audio/AudioAdapter.hpp"
#include "../../adapters/render/MeshPreviewAdapter.hpp"
#include "../../adapters/render/EditorTextureAdapter.hpp"
#include "../../adapters/render/DebugDrawAdapter.hpp"
#include "../../adapters/scripting/NativeAPIRegistry.hpp"

namespace core
{
    void EditorBootstrap::setFrameCallback(std::function<void()> callback)
    {
        if (coreInterface)
        {
            // Wrap the callback to also update audio and async loading each frame
            coreInterface->setFrameCallback([this, cb = std::move(callback)]()
            {
                // Reset per-frame rate limiters for script API
                NativeAPIRegistry::beginFrame();

                if (audioAdapter)
                {
                    audioAdapter->update();
                }

                if (meshPreviewAdapter)
                {
                    meshPreviewAdapter->processAsyncLoading();
                }

                if (textureAdapter)
                {
                    textureAdapter->processAsyncLoading();
                }
                if (cb)
                {
                    cb();
                }

                // Consume immediate debug draw data and forward to renderer
                if (debugDrawAdapter)
                {
                    auto drawList = debugDrawAdapter->consumeDrawList();
                    offScreen->updateImmediateDebugDrawList(std::move(drawList));
                }
            });
        }
    }

    void EditorBootstrap::setPostUpdateCallback(std::function<void()> callback)
    {
        if (coreInterface)
        {
            coreInterface->setPostUpdateCallback(std::move(callback));
        }
    }

    std::function<void()> EditorBootstrap::getSceneGraphUpdateFn() const
    {
        return coreInterface ? coreInterface->getSceneGraphUpdateFn() : nullptr;
    }

    std::function<void()> EditorBootstrap::getImguiDrawFn() const
    {
        return coreInterface ? coreInterface->getImguiDrawFn() : nullptr;
    }

    std::function<void()> EditorBootstrap::getRenderFn() const
    {
        return coreInterface ? coreInterface->getRenderFn() : nullptr;
    }

    void EditorBootstrap::setPreRenderCallback(std::function<void()> callback)
    {
        if (coreInterface)
        {
            coreInterface->setPreRenderCallback(std::move(callback));
        }
    }
}
