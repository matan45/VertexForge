#pragma once
#include "scene/SceneBVH.hpp"
#include "math/Frustum.hpp"
#include <memory>
#include <vector>
#include <cstdint>

namespace events { struct SubscriptionToken; }

namespace render
{
    class RenderPassHandler;
}

namespace controllers::offscreen
{
    class SceneBVHManager
    {
    private:
        scene::SceneBVH sceneBVH;
        std::unique_ptr<events::SubscriptionToken> meshDataChangedSubscription;
        std::unique_ptr<events::SubscriptionToken> entityDeletedSubscription;
        std::unique_ptr<events::SubscriptionToken> entityStaticChangedSubscription;
        std::unique_ptr<events::SubscriptionToken> sceneLoadedSubscription;
        std::unique_ptr<events::SubscriptionToken> sceneClearedSubscription;
        std::unique_ptr<events::SubscriptionToken> prefabInstantiatedSubscription;
        std::unique_ptr<events::SubscriptionToken> entityDuplicatedSubscription;

        void cleanUp();

    public:
        explicit SceneBVHManager();
        ~SceneBVHManager();

        void init(render::RenderPassHandler* renderHandler);

        void rebuild();
        void markDirty();

        void updateOcclusionCullingData(render::RenderPassHandler* renderHandler);

        void queryFrustum(const math::Frustum& frustum, std::vector<uint32_t>& results) const
        {
            sceneBVH.queryFrustum(frustum, results);
        }

        bool isBuilt() const { return sceneBVH.isBuilt(); }
        bool isStaticDirty() const { return sceneBVH.isStaticDirty(); }
        bool isDynamicDirty() const { return sceneBVH.isDynamicDirty(); }
        bool needsDynamicRebuild() const { return sceneBVH.needsDynamicRebuild(); }

        void rebuildStaticBVH() { sceneBVH.rebuildStaticBVH(); }
        void updateDynamicBVH() { sceneBVH.updateDynamicBVH(); }
        void markDynamicEntityDirty(uint32_t entityId) { sceneBVH.markDynamicEntityDirty(entityId); }

        size_t getStaticEntityCount() const { return sceneBVH.getStaticEntityCount(); }
        size_t getDynamicEntityCount() const { return sceneBVH.getDynamicEntityCount(); }
        size_t getStaticNodeCount() const { return sceneBVH.getStaticNodeCount(); }
        size_t getDynamicNodeCount() const { return sceneBVH.getDynamicNodeCount(); }
    };
}
