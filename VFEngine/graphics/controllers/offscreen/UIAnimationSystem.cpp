#include "UIAnimationSystem.hpp"
#include "FramePreparationSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "math/EasingFunctions.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIEvents.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace controllers::offscreen
{
    void UIAnimationSystem::processAnimations(const FrameContext& ctx)
    {
        if (ctx.deltaTime <= 0.0f)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::UIAnimationComponent>();

        // Clean up playback states for removed entities
        for (auto it = playbackStates.begin(); it != playbackStates.end();)
        {
            if (!registry.valid(it->first) || !registry.all_of<components::UIAnimationComponent>(it->first))
                it = playbackStates.erase(it);
            else
                ++it;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        for (auto entity : view)
        {
            auto& anim = view.get<components::UIAnimationComponent>(entity);

            // Handle autoPlay on first encounter
            if (anim.autoPlay && !anim.isPlaying && !anim.completedFired)
            {
                anim.isPlaying = true;
                anim.elapsedTime = 0.0f;
                anim.startedFired = false;
            }

            if (!anim.isPlaying)
            {
                // Clean up stale playback state when animation is stopped externally
                playbackStates.erase(entity);
                continue;
            }

            if (anim.isPaused)
                continue;

            // Fetch entity name once for use in both started and completed notifications
            std::string entityName;
            if (registry.all_of<components::NameComponent>(entity))
                entityName = registry.get<components::NameComponent>(entity).name;

            // Publish started notification
            if (!anim.startedFired)
            {
                anim.startedFired = true;
                events::ui::UIAnimationStartedNotification notif;
                notif.entity = services::EntityHandle{static_cast<uint64_t>(entity)};
                notif.entityName = entityName;
                dispatcher.publish(notif);
            }

            // Get or create playback state
            auto& state = playbackStates[entity];
            ensurePlaybackState(anim.rootNode, state);

            // Advance the animation tree
            bool finished = advanceNode(anim.rootNode, state, ctx.deltaTime, registry, entity);

            anim.elapsedTime += ctx.deltaTime;

            if (finished)
            {
                anim.isPlaying = false;
                anim.completedFired = true;

                events::ui::UIAnimationCompletedNotification notif;
                notif.entity = services::EntityHandle{static_cast<uint64_t>(entity)};
                notif.entityName = entityName;
                dispatcher.publish(notif);

                playbackStates.erase(entity);
            }
        }
    }

    void UIAnimationSystem::ensurePlaybackState(const components::UIAnimationNode& node,
                                                 NodePlaybackState& state)
    {
        if (node.type != components::UIAnimationNodeType::Clip && state.children.empty())
        {
            state.children.resize(node.children.size());
            for (size_t i = 0; i < node.children.size(); ++i)
            {
                ensurePlaybackState(node.children[i], state.children[i]);
            }
        }
    }

    bool UIAnimationSystem::advanceNode(const components::UIAnimationNode& node,
                                         NodePlaybackState& state,
                                         float dt, entt::registry& registry,
                                         entt::entity entity)
    {
        if (state.finished)
            return true;

        switch (node.type)
        {
        case components::UIAnimationNodeType::Clip:
        {
            state.elapsed += dt;

            float effectiveElapsed = state.elapsed - node.clip.delay;
            if (effectiveElapsed < 0.0f)
            {
                // Still in delay, apply start value
                applyClipValue(node.clip, 0.0f, registry, entity);
                return false;
            }

            float duration = node.clip.duration;
            if (duration <= 0.0f)
                duration = 0.001f;

            float t = effectiveElapsed / duration;

            // Handle loop modes
            switch (node.clip.loopMode)
            {
            case components::UIAnimationLoopMode::Once:
                if (t >= 1.0f)
                {
                    t = 1.0f;
                    state.finished = true;
                }
                break;

            case components::UIAnimationLoopMode::Loop:
                t = t - std::floor(t); // wrap to [0,1)
                break;

            case components::UIAnimationLoopMode::PingPong:
            {
                int cycle = static_cast<int>(t);
                t = t - std::floor(t);
                if (cycle % 2 == 1)
                    t = 1.0f - t;
                break;
            }
            }

            float easedT = math::evaluateEasing(node.clip.easing, t);

            // If reversing (PingPong at group level), swap start/end
            if (state.reversing)
                easedT = 1.0f - easedT;

            applyClipValue(node.clip, easedT, registry, entity);
            return state.finished;
        }

        case components::UIAnimationNodeType::Parallel:
        {
            bool allDone = true;
            for (size_t i = 0; i < node.children.size(); ++i)
            {
                if (i < state.children.size())
                {
                    bool childDone = advanceNode(node.children[i], state.children[i],
                                                  dt, registry, entity);
                    if (!childDone)
                        allDone = false;
                }
            }

            if (allDone)
            {
                switch (node.loopMode)
                {
                case components::UIAnimationLoopMode::Once:
                    state.finished = true;
                    break;
                case components::UIAnimationLoopMode::Loop:
                    // Reset all children
                    for (auto& child : state.children)
                        child = NodePlaybackState{};
                    for (size_t i = 0; i < node.children.size(); ++i)
                        ensurePlaybackState(node.children[i], state.children[i]);
                    break;
                case components::UIAnimationLoopMode::PingPong:
                    state.reversing = !state.reversing;
                    for (auto& child : state.children)
                    {
                        child = NodePlaybackState{};
                        child.reversing = state.reversing;
                    }
                    for (size_t i = 0; i < node.children.size(); ++i)
                        ensurePlaybackState(node.children[i], state.children[i]);
                    break;
                }
            }
            return state.finished;
        }

        case components::UIAnimationNodeType::Sequence:
        {
            if (state.currentChild >= static_cast<int>(node.children.size()))
            {
                switch (node.loopMode)
                {
                case components::UIAnimationLoopMode::Once:
                    state.finished = true;
                    return true;
                case components::UIAnimationLoopMode::Loop:
                    state.currentChild = 0;
                    for (auto& child : state.children)
                        child = NodePlaybackState{};
                    for (size_t i = 0; i < node.children.size(); ++i)
                        ensurePlaybackState(node.children[i], state.children[i]);
                    break;
                case components::UIAnimationLoopMode::PingPong:
                    state.reversing = !state.reversing;
                    state.currentChild = 0;
                    for (auto& child : state.children)
                    {
                        child = NodePlaybackState{};
                        child.reversing = state.reversing;
                    }
                    for (size_t i = 0; i < node.children.size(); ++i)
                        ensurePlaybackState(node.children[i], state.children[i]);
                    break;
                }
            }

            int idx = state.currentChild;
            if (state.reversing)
                idx = static_cast<int>(node.children.size()) - 1 - idx;

            if (idx >= 0 && idx < static_cast<int>(node.children.size())
                && idx < static_cast<int>(state.children.size()))
            {
                // Ensure child state is clean before advancing in reverse pass
                // (forward pass may have left finished=true on this child)
                if (state.children[idx].finished)
                {
                    state.children[idx].finished = false;
                    state.children[idx].elapsed = 0.0f;
                    state.children[idx].currentChild = 0;
                    state.children[idx].reversing = state.reversing;
                    for (auto& grandchild : state.children[idx].children)
                    {
                        grandchild = NodePlaybackState{};
                        grandchild.reversing = state.reversing;
                    }
                    ensurePlaybackState(node.children[idx], state.children[idx]);
                }

                bool childDone = advanceNode(node.children[idx], state.children[idx],
                                              dt, registry, entity);
                if (childDone)
                    state.currentChild++;
            }

            return state.finished;
        }
        }

        return true;
    }

    void UIAnimationSystem::applyClipValue(const components::UIAnimationClip& clip, float easedT,
                                            entt::registry& registry, entt::entity entity)
    {
        float value = clip.startValue + (clip.endValue - clip.startValue) * easedT;

        switch (clip.property)
        {
        case components::UITweenProperty::Opacity:
        case components::UITweenProperty::ColorA:
            if (registry.all_of<components::UIImageComponent>(entity))
                registry.get<components::UIImageComponent>(entity).colorTint.a = value;
            if (registry.all_of<components::UILabelComponent>(entity))
                registry.get<components::UILabelComponent>(entity).color.a = value;
            break;

        case components::UITweenProperty::PositionX:
            if (registry.all_of<components::UIRectComponent>(entity))
                registry.get<components::UIRectComponent>(entity).anchoredPosition.x = value;
            break;

        case components::UITweenProperty::PositionY:
            if (registry.all_of<components::UIRectComponent>(entity))
                registry.get<components::UIRectComponent>(entity).anchoredPosition.y = value;
            break;

        case components::UITweenProperty::ScaleX:
            if (registry.all_of<components::UIRectComponent>(entity))
                registry.get<components::UIRectComponent>(entity).sizeDelta.x = value;
            break;

        case components::UITweenProperty::ScaleY:
            if (registry.all_of<components::UIRectComponent>(entity))
                registry.get<components::UIRectComponent>(entity).sizeDelta.y = value;
            break;

        case components::UITweenProperty::ColorR:
            if (registry.all_of<components::UIImageComponent>(entity))
                registry.get<components::UIImageComponent>(entity).colorTint.r = value;
            break;

        case components::UITweenProperty::ColorG:
            if (registry.all_of<components::UIImageComponent>(entity))
                registry.get<components::UIImageComponent>(entity).colorTint.g = value;
            break;

        case components::UITweenProperty::ColorB:
            if (registry.all_of<components::UIImageComponent>(entity))
                registry.get<components::UIImageComponent>(entity).colorTint.b = value;
            break;

        case components::UITweenProperty::Rotation:
            // Deferred — UIRectComponent lacks rotation field
            break;
        }
    }
}
