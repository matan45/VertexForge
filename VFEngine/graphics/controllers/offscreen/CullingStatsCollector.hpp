#pragma once
#include "../../../services/providers/IOffScreenProvider.hpp"

namespace render
{
    class RenderPassHandler;
}

namespace controllers::offscreen
{
    class SceneBVHManager;

    class CullingStatsCollector
    {
    public:
        CullingStatsCollector() = default;

        services::CullingDebugStats collect(render::RenderPassHandler* renderHandler,
                                            SceneBVHManager* bvhManager) const;
    };
}
