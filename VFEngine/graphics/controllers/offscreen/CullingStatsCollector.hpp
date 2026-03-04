#pragma once
#include "../../../services/providers/render/IOffScreenProvider.hpp"

namespace render
{
    class RenderPassHandler;
}

namespace controllers::offscreen
{
    class SceneBVHManager;
    class LightBVHManager;

    class CullingStatsCollector
    {
    public:
        CullingStatsCollector() = default;

        services::CullingDebugStats collect(render::RenderPassHandler* renderHandler,
                                            SceneBVHManager* bvhManager,
                                            LightBVHManager* lightBvhManager) const;
    };
}
