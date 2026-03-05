#include "RuntimeBootstrap.hpp"
#include "../../controllers/CoreInterface.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../adapters/audio/AudioAdapter.hpp"
#include "../../adapters/render/DebugDrawAdapter.hpp"
#include "../../adapters/scripting/NativeAPIRegistry.hpp"

namespace core
{
    void RuntimeBootstrap::setFrameCallback(std::function<void()> callback)
    {
        if (coreInterface)
        {
            // Wrap the callback to also update audio each frame
            coreInterface->setFrameCallback([this, cb = std::move(callback)]()
            {
                // Reset per-frame rate limiters for script API
                NativeAPIRegistry::beginFrame();

                if (audioAdapter)
                {
                    audioAdapter->update();
                }
                if (cb)
                {
                    cb();
                }

                // Consume immediate debug draw data and forward to renderer
                if (debugDrawAdapter)
                {
                    auto drawList = debugDrawAdapter->consumeDrawList();
                    if (!drawList.empty())
                    {
                        offScreen->updateImmediateDebugDrawList(std::move(drawList));
                    }
                }
            });
        }
    }

    void RuntimeBootstrap::setPostUpdateCallback(std::function<void()> callback)
    {
        if (coreInterface)
        {
            coreInterface->setPostUpdateCallback(std::move(callback));
        }
    }
}
