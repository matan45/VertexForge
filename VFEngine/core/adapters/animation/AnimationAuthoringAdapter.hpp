#pragma once

namespace core
{
    // VK-1656: answers events::animationAuthoring (services/events/animation/
    // AnimationAuthoringEvents.hpp) for the MCP animation tools: .vfAnimator load/save
    // straight from disk with a content hash, a mesh's skeleton/sockets/IK chains, clip
    // summaries, retarget bindings, and the edit-mode preview on RuntimeAnimatorSystem.
    // Editor only; every handler runs on the main thread (MCP drain).
    class AnimationAuthoringAdapter
    {
    public:
        AnimationAuthoringAdapter() = default;
        ~AnimationAuthoringAdapter();

        AnimationAuthoringAdapter(const AnimationAuthoringAdapter&) = delete;
        AnimationAuthoringAdapter& operator=(const AnimationAuthoringAdapter&) = delete;

        void init();    // registers the CQRS handlers
        void cleanUp(); // unregisters them

    private:
        bool handlersRegistered = false;
    };
}
