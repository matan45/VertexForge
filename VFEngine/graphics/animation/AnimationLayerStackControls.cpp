#include "AnimationLayerStack.hpp"

namespace animation
{
    void AnimationLayerStack::setLayerWeight(uint32_t layerIndex, float weight)
    {
        if (layerIndex < layers.size())
            layers[layerIndex].weight = glm::clamp(weight, 0.0f, 1.0f);
    }

    float AnimationLayerStack::getLayerWeight(uint32_t layerIndex) const
    {
        if (layerIndex < layers.size())
            return layers[layerIndex].weight;
        return 0.0f;
    }

    uint32_t AnimationLayerStack::getLayerCount() const
    {
        return static_cast<uint32_t>(layers.size());
    }

    std::string AnimationLayerStack::getLayerName(uint32_t layerIndex) const
    {
        if (layerIndex < layers.size())
            return layers[layerIndex].name;
        return "";
    }

    AnimatorStateMachine* AnimationLayerStack::getBaseStateMachine()
    {
        if (!layers.empty() && layers[0].stateMachine)
            return layers[0].stateMachine.get();
        return nullptr;
    }

    const AnimatorStateMachine* AnimationLayerStack::getBaseStateMachine() const
    {
        if (!layers.empty() && layers[0].stateMachine)
            return layers[0].stateMachine.get();
        return nullptr;
    }

    AnimatorStateMachine* AnimationLayerStack::getLayerStateMachine(uint32_t layerIndex)
    {
        if (layerIndex < layers.size() && layers[layerIndex].stateMachine)
            return layers[layerIndex].stateMachine.get();
        return nullptr;
    }

    const AnimatorStateMachine* AnimationLayerStack::getLayerStateMachine(uint32_t layerIndex) const
    {
        if (layerIndex < layers.size() && layers[layerIndex].stateMachine)
            return layers[layerIndex].stateMachine.get();
        return nullptr;
    }

    void AnimationLayerStack::setFloat(const std::string& name, float value)
    {
        sharedParameters.setFloat(name, value);
    }

    void AnimationLayerStack::setInt(const std::string& name, int32_t value)
    {
        sharedParameters.setInt(name, value);
    }

    void AnimationLayerStack::setBool(const std::string& name, bool value)
    {
        sharedParameters.setBool(name, value);
    }

    void AnimationLayerStack::setTrigger(const std::string& name)
    {
        sharedParameters.setTrigger(name);
    }

    float AnimationLayerStack::getFloat(const std::string& name) const
    {
        return sharedParameters.getFloat(name);
    }

    int32_t AnimationLayerStack::getInt(const std::string& name) const
    {
        return sharedParameters.getInt(name);
    }

    bool AnimationLayerStack::getBool(const std::string& name) const
    {
        return sharedParameters.getBool(name);
    }

    void AnimationLayerStack::play()
    {
        for (auto& layer : layers)
        {
            if (layer.stateMachine)
                layer.stateMachine->play();
            layer.clipPlaying = true;
        }
    }

    void AnimationLayerStack::pause()
    {
        for (auto& layer : layers)
        {
            if (layer.stateMachine)
                layer.stateMachine->pause();
            layer.clipPlaying = false;
        }
    }

    void AnimationLayerStack::stop()
    {
        for (auto& layer : layers)
        {
            if (layer.stateMachine)
                layer.stateMachine->stop();
            layer.clipPlaying = false;
            layer.clipTime = 0.0f;
        }
    }

    void AnimationLayerStack::reset()
    {
        for (auto& layer : layers)
        {
            if (layer.stateMachine)
                layer.stateMachine->reset();
            layer.clipTime = 0.0f;
            layer.clipPlaying = true;
        }
    }

    bool AnimationLayerStack::isPlaying() const
    {
        if (!layers.empty() && layers[0].stateMachine)
            return layers[0].stateMachine->isPlaying();
        return false;
    }

    bool AnimationLayerStack::isBlending() const
    {
        if (!layers.empty() && layers[0].stateMachine)
            return layers[0].stateMachine->isBlending();
        return false;
    }

    std::string AnimationLayerStack::getCurrentStateName() const
    {
        if (!layers.empty() && layers[0].stateMachine)
        {
            const auto* state = layers[0].stateMachine->getCurrentAnimatorState();
            if (state)
                return state->name;
        }
        return "";
    }

    float AnimationLayerStack::getNormalizedTime() const
    {
        if (!layers.empty() && layers[0].stateMachine)
            return layers[0].stateMachine->getNormalizedStateTime();
        return 0.0f;
    }

    bool AnimationLayerStack::forceTransitionTo(const std::string& stateName, float blendDuration)
    {
        if (!layers.empty() && layers[0].stateMachine)
        {
            layers[0].stateMachine->forceTransitionTo(stateName, blendDuration);
            return true;
        }
        return false;
    }

    void AnimationLayerStack::setRootMotionEnabled(bool enabled)
    {
        if (!layers.empty() && layers[0].stateMachine)
            layers[0].stateMachine->setRootMotionEnabled(enabled);
    }

    glm::vec3 AnimationLayerStack::consumeRootMotionDelta()
    {
        if (!layers.empty() && layers[0].stateMachine)
            return layers[0].stateMachine->consumeRootMotionDelta();
        return glm::vec3(0.0f);
    }

    const std::vector<const animator::AnimationEvent*>& AnimationLayerStack::getFiredEvents() const
    {
        return cachedFiredEvents;
    }

    void AnimationLayerStack::computeSocketTransforms(
        const std::vector<animator::SocketDefinition>& sockets,
        std::vector<glm::mat4>& outSocketModelTransforms) const
    {
        if (!skeletonData || finalBoneMatrices.empty() || sockets.empty())
        {
            outSocketModelTransforms.clear();
            return;
        }

        outSocketModelTransforms.resize(sockets.size());

        for (size_t i = 0; i < sockets.size(); ++i)
        {
            const auto& socket = sockets[i];
            if (socket.boneIndex >= 0 &&
                socket.boneIndex < static_cast<int32_t>(finalBoneMatrices.size()) &&
                socket.boneIndex < static_cast<int32_t>(skeletonData->bindPoses.size()))
            {
                glm::vec3 boneMeshPos = glm::vec3(
                    finalBoneMatrices[socket.boneIndex]
                    * skeletonData->bindPoses[socket.boneIndex]
                    * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

                outSocketModelTransforms[i] = glm::translate(glm::mat4(1.0f),
                    boneMeshPos + socket.localPosition);
            }
            else
            {
                outSocketModelTransforms[i] = glm::mat4(1.0f);
            }
        }
    }

    void AnimationLayerStack::restoreLayerState(uint32_t layerIndex,
                                                 const AnimatorStateMachineState& machineState,
                                                 float clipTime, bool clipPlaying, float weight)
    {
        if (layerIndex >= layers.size())
            return;

        auto& layer = layers[layerIndex];
        layer.weight = weight;

        if (layer.stateMachine)
        {
            layer.stateMachine->setMachineState(machineState);
        }

        layer.clipTime = clipTime;
        layer.clipPlaying = clipPlaying;
    }

    void AnimationLayerStack::restoreSharedParameters(
        const std::unordered_map<std::string, std::variant<float, int32_t, bool>>& params)
    {
        for (const auto& [name, value] : params)
        {
            std::visit([&](auto&& v)
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, float>)
                    sharedParameters.setFloat(name, v);
                else if constexpr (std::is_same_v<T, int32_t>)
                    sharedParameters.setInt(name, v);
                else if constexpr (std::is_same_v<T, bool>)
                    sharedParameters.setBool(name, v);
            }, value);
        }
    }
}
