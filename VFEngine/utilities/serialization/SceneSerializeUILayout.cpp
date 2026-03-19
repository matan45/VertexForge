#include "SceneSerialization.hpp"
#include "AssetRefSerializationHelper.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"

namespace serialization {

    // ---- UI Animation helpers ----

    namespace
    {
        std::string easingFunctionToString(components::UIEasingFunction easing)
        {
            switch (easing)
            {
            case components::UIEasingFunction::Linear: return "Linear";
            case components::UIEasingFunction::EaseIn: return "EaseIn";
            case components::UIEasingFunction::EaseOut: return "EaseOut";
            case components::UIEasingFunction::EaseInOut: return "EaseInOut";
            case components::UIEasingFunction::Bounce: return "Bounce";
            case components::UIEasingFunction::Elastic: return "Elastic";
            default: return "Linear";
            }
        }

        components::UIEasingFunction stringToEasingFunction(const std::string& str)
        {
            if (str == "EaseIn") return components::UIEasingFunction::EaseIn;
            if (str == "EaseOut") return components::UIEasingFunction::EaseOut;
            if (str == "EaseInOut") return components::UIEasingFunction::EaseInOut;
            if (str == "Bounce") return components::UIEasingFunction::Bounce;
            if (str == "Elastic") return components::UIEasingFunction::Elastic;
            return components::UIEasingFunction::Linear;
        }

        std::string loopModeToString(components::UIAnimationLoopMode mode)
        {
            switch (mode)
            {
            case components::UIAnimationLoopMode::Once: return "Once";
            case components::UIAnimationLoopMode::Loop: return "Loop";
            case components::UIAnimationLoopMode::PingPong: return "PingPong";
            default: return "Once";
            }
        }

        components::UIAnimationLoopMode stringToLoopMode(const std::string& str)
        {
            if (str == "Loop") return components::UIAnimationLoopMode::Loop;
            if (str == "PingPong") return components::UIAnimationLoopMode::PingPong;
            return components::UIAnimationLoopMode::Once;
        }

        std::string tweenPropertyToString(components::UITweenProperty prop)
        {
            switch (prop)
            {
            case components::UITweenProperty::Opacity: return "Opacity";
            case components::UITweenProperty::PositionX: return "PositionX";
            case components::UITweenProperty::PositionY: return "PositionY";
            case components::UITweenProperty::ScaleX: return "ScaleX";
            case components::UITweenProperty::ScaleY: return "ScaleY";
            case components::UITweenProperty::Rotation: return "Rotation";
            case components::UITweenProperty::ColorR: return "ColorR";
            case components::UITweenProperty::ColorG: return "ColorG";
            case components::UITweenProperty::ColorB: return "ColorB";
            case components::UITweenProperty::ColorA: return "ColorA";
            default: return "Opacity";
            }
        }

        components::UITweenProperty stringToTweenProperty(const std::string& str)
        {
            if (str == "PositionX") return components::UITweenProperty::PositionX;
            if (str == "PositionY") return components::UITweenProperty::PositionY;
            if (str == "ScaleX") return components::UITweenProperty::ScaleX;
            if (str == "ScaleY") return components::UITweenProperty::ScaleY;
            if (str == "Rotation") return components::UITweenProperty::Rotation;
            if (str == "ColorR") return components::UITweenProperty::ColorR;
            if (str == "ColorG") return components::UITweenProperty::ColorG;
            if (str == "ColorB") return components::UITweenProperty::ColorB;
            if (str == "ColorA") return components::UITweenProperty::ColorA;
            return components::UITweenProperty::Opacity;
        }

        std::string animNodeTypeToString(components::UIAnimationNodeType type)
        {
            switch (type)
            {
            case components::UIAnimationNodeType::Clip: return "Clip";
            case components::UIAnimationNodeType::Parallel: return "Parallel";
            case components::UIAnimationNodeType::Sequence: return "Sequence";
            default: return "Clip";
            }
        }

        components::UIAnimationNodeType stringToAnimNodeType(const std::string& str)
        {
            if (str == "Parallel") return components::UIAnimationNodeType::Parallel;
            if (str == "Sequence") return components::UIAnimationNodeType::Sequence;
            return components::UIAnimationNodeType::Clip;
        }

        json serializeUIAnimationNode(const components::UIAnimationNode& node)
        {
            json j;
            j["type"] = animNodeTypeToString(node.type);
            j["loopMode"] = loopModeToString(node.loopMode);

            if (node.type == components::UIAnimationNodeType::Clip)
            {
                json clipJson;
                clipJson["property"] = tweenPropertyToString(node.clip.property);
                clipJson["startValue"] = node.clip.startValue;
                clipJson["endValue"] = node.clip.endValue;
                clipJson["duration"] = node.clip.duration;
                clipJson["delay"] = node.clip.delay;
                clipJson["easing"] = easingFunctionToString(node.clip.easing);
                clipJson["loopMode"] = loopModeToString(node.clip.loopMode);
                j["clip"] = clipJson;
            }

            if (!node.children.empty())
            {
                json childrenArr = json::array();
                for (const auto& child : node.children)
                {
                    childrenArr.push_back(serializeUIAnimationNode(child));
                }
                j["children"] = childrenArr;
            }

            return j;
        }

        void deserializeUIAnimationNode(const json& j, components::UIAnimationNode& node)
        {
            node.type = stringToAnimNodeType(j.value("type", std::string("Clip")));
            node.loopMode = stringToLoopMode(j.value("loopMode", std::string("Once")));

            if (j.contains("clip") && j["clip"].is_object())
            {
                const auto& clipJson = j["clip"];
                node.clip.property = stringToTweenProperty(clipJson.value("property", std::string("Opacity")));
                node.clip.startValue = clipJson.value("startValue", 0.0f);
                node.clip.endValue = clipJson.value("endValue", 1.0f);
                node.clip.duration = clipJson.value("duration", 1.0f);
                node.clip.delay = clipJson.value("delay", 0.0f);
                node.clip.easing = stringToEasingFunction(clipJson.value("easing", std::string("Linear")));
                node.clip.loopMode = stringToLoopMode(clipJson.value("loopMode", std::string("Once")));
            }

            if (j.contains("children") && j["children"].is_array())
            {
                node.children.clear();
                for (const auto& childJson : j["children"])
                {
                    components::UIAnimationNode child;
                    deserializeUIAnimationNode(childJson, child);
                    node.children.push_back(std::move(child));
                }
            }
        }
    } // anonymous namespace

    // ---- UI Animation ----

    json SceneSerialization::serializeUIAnimation(const components::UIAnimationComponent& anim)
    {
        json j;
        j["rootNode"] = serializeUIAnimationNode(anim.rootNode);
        j["autoPlay"] = anim.autoPlay;
        return j;
    }

    void SceneSerialization::deserializeUIAnimation(const json& j, components::UIAnimationComponent& anim)
    {
        if (j.contains("rootNode") && j["rootNode"].is_object())
        {
            deserializeUIAnimationNode(j["rootNode"], anim.rootNode);
        }
        anim.autoPlay = j.value("autoPlay", false);

        // Reset runtime state
        anim.isPlaying = false;
        anim.isPaused = false;
        anim.elapsedTime = 0.0f;
        anim.startedFired = false;
        anim.completedFired = false;
    }

    // ---- UI Mask ----

    json SceneSerialization::serializeUIMask(const components::UIMaskComponent& mask)
    {
        json j;
        writeAssetRef(j, "maskTextureRef", mask.maskTextureRef);
        j["alphaThreshold"] = mask.alphaThreshold;
        j["showMaskGraphic"] = mask.showMaskGraphic;
        return j;
    }

    void SceneSerialization::deserializeUIMask(const json& j, components::UIMaskComponent& mask)
    {
        mask.maskTextureRef = readAssetRef(j, "maskTextureRef");
        if (j.contains("alphaThreshold"))
            mask.alphaThreshold = j["alphaThreshold"].get<float>();
        if (j.contains("showMaskGraphic"))
            mask.showMaskGraphic = j["showMaskGraphic"].get<bool>();
    }

    // ---- UI Draggable ----

    json SceneSerialization::serializeUIDraggable(const components::UIDraggableComponent& comp)
    {
        json j;
        j["ghostOpacity"] = comp.ghostOpacity;
        j["ghostOffset"] = writeVec2(comp.ghostOffset);
        j["constrainToParent"] = comp.constrainToParent;
        if (!comp.dragTag.empty())
        {
            j["dragTag"] = comp.dragTag;
        }
        return j;
    }

    void SceneSerialization::deserializeUIDraggable(const json& j, components::UIDraggableComponent& comp)
    {
        comp.ghostOpacity = j.value("ghostOpacity", 0.5f);
        readVec2(j, "ghostOffset", comp.ghostOffset);
        comp.constrainToParent = j.value("constrainToParent", true);
        comp.dragTag = j.value("dragTag", std::string(""));
    }

    // ---- UI Drop Target ----

    json SceneSerialization::serializeUIDropTarget(const components::UIDropTargetComponent& comp)
    {
        json j;
        if (!comp.acceptTag.empty())
        {
            j["acceptTag"] = comp.acceptTag;
        }
        j["highlightColor"] = writeVec4(comp.highlightColor);
        j["rejectColor"] = writeVec4(comp.rejectColor);
        j["interactable"] = comp.interactable;
        return j;
    }

    void SceneSerialization::deserializeUIDropTarget(const json& j, components::UIDropTargetComponent& comp)
    {
        comp.acceptTag = j.value("acceptTag", std::string(""));
        readVec4(j, "highlightColor", comp.highlightColor);
        readVec4(j, "rejectColor", comp.rejectColor);
        comp.interactable = j.value("interactable", true);
    }

} // namespace serialization
