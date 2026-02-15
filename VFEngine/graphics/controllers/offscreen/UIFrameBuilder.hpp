#pragma once
#include <entt/entt.hpp>

namespace controllers::offscreen
{
    struct FrameContext;
    class UIInteractionSystem;

    class UIFrameBuilder
    {
    public:
        UIFrameBuilder() = default;

        void prepareUIImages(const FrameContext& ctx, UIInteractionSystem& interactionSystem);
        void prepareUILabels(const FrameContext& ctx);

    private:
        void prepareUIImagesScreenSpace(const FrameContext& ctx, UIInteractionSystem& interactionSystem);
        void prepareUIImagesWorldSpace(const FrameContext& ctx);
        void prepareUILabelsScreenSpace(const FrameContext& ctx);
        void prepareUILabelsWorldSpace(const FrameContext& ctx);
    };
}
