#pragma once

#include <entt/entt.hpp>
#include <unordered_map>
#include <vector>
#include <components/UIComponents.hpp>

namespace controllers::offscreen
{
    struct FrameContext;

    class UIAnimationSystem
    {
    private:
        struct NodePlaybackState
        {
            float elapsed = 0.0f;
            int currentChild = 0;
            bool reversing = false;
            bool finished = false;
            std::vector<NodePlaybackState> children;
        };

        std::unordered_map<entt::entity, NodePlaybackState> playbackStates;
    public:
        void processAnimations(const FrameContext& ctx);

    private:

        bool advanceNode(const components::UIAnimationNode& node, NodePlaybackState& state,
                         float dt, entt::registry& registry, entt::entity entity);
        void applyClipValue(const components::UIAnimationClip& clip, float easedT,
                            entt::registry& registry, entt::entity entity);
        void ensurePlaybackState(const components::UIAnimationNode& node, NodePlaybackState& state);
    };
}
