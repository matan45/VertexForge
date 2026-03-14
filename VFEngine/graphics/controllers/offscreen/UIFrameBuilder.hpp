#pragma once
#include <entt/entt.hpp>

namespace controllers::offscreen
{
    struct FrameContext;
    class UIInteractionSystem;
    class UIAnimationSystem;

    class UIFrameBuilder
    {
    public:
        UIFrameBuilder() = default;

        void prepareUIImages(const FrameContext& ctx, UIInteractionSystem& interactionSystem,
                             UIAnimationSystem& animationSystem);
        void prepareUILabels(const FrameContext& ctx);

    private:
        void prepareUIImagesScreenSpace(const FrameContext& ctx, UIInteractionSystem& interactionSystem,
                                         UIAnimationSystem& animationSystem);
        void prepareUIImagesWorldSpace(const FrameContext& ctx);
        void prepareUILabelsScreenSpace(const FrameContext& ctx);
        void prepareUILabelsWorldSpace(const FrameContext& ctx);
    };
}
