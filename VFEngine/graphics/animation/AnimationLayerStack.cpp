#include "AnimationLayerStack.hpp"
#include "print/Log.hpp"

namespace animation
{
    AnimationLayerStack::AnimationLayerStack() = default;
    AnimationLayerStack::~AnimationLayerStack() = default;

    void AnimationLayerStack::initialize(const animator::AnimatorData& data, const resource::SkeletonData* skeleton,
                                          AnimationLoadCallback loadCallback)
    {
        animatorData = &data;
        skeletonData = skeleton;
        animationLoadCallback = loadCallback;

        layers.clear();

        if (!data.layers.empty())
        {
            // Multi-layer format: collect all parameters from all layers' graphs
            for (const auto& layerData : data.layers)
            {
                if (layerData.sourceMode == animator::LayerSourceMode::StateMachine)
                {
                    for (const auto& param : layerData.graph.parameters)
                    {
                        if (sharedParameters.values.find(param.name) == sharedParameters.values.end())
                        {
                            sharedParameters.values[param.name] = param.defaultValue;
                        }
                    }
                }
            }

            for (const auto& layerData : data.layers)
            {
                AnimationLayerRuntime runtime;
                runtime.name = layerData.name;
                runtime.weight = layerData.weight;
                runtime.blendMode = layerData.blendMode;
                runtime.sourceMode = layerData.sourceMode;
                runtime.additiveRefPose = layerData.additiveRefPose;
                runtime.additiveRefFrame = layerData.additiveRefFrame;

                if (layerData.sourceMode == animator::LayerSourceMode::StateMachine)
                {
                    runtime.stateMachine = std::make_unique<AnimatorStateMachine>();
                    runtime.stateMachine->initializeFromGraph(layerData.graph, skeleton, loadCallback, &sharedParameters);
                }
                else
                {
                    runtime.clipLoop = layerData.directClipLoop;
                    runtime.clipSpeed = layerData.directClipSpeed;
                    runtime.clipTime = 0.0f;

                    if (!layerData.directClipPath.empty() && loadCallback)
                    {
                        runtime.directClipData = loadCallback(layerData.directClipPath);
                        if (runtime.directClipData && skeleton)
                        {
                            runtime.directClipEvaluator.loadAnimation(*runtime.directClipData, *skeleton);
                        }
                    }
                }

                layers.push_back(std::move(runtime));
            }
        }
        else
        {
            // Single-graph backward compat: create one base layer
            sharedParameters.initializeFromGraph(data.graph);

            AnimationLayerRuntime baseLayer;
            baseLayer.name = "Base Layer";
            baseLayer.weight = 1.0f;
            baseLayer.blendMode = animator::LayerBlendMode::Override;
            baseLayer.sourceMode = animator::LayerSourceMode::StateMachine;

            baseLayer.stateMachine = std::make_unique<AnimatorStateMachine>();
            baseLayer.stateMachine->initializeFromGraph(data.graph, skeleton, loadCallback, &sharedParameters);

            layers.push_back(std::move(baseLayer));
        }

        resolveBoneMasks();

        for (auto& layer : layers)
        {
            computeReferencePose(layer);
        }

        initialized = true;
    }

    void AnimationLayerStack::resolveBoneMasks()
    {
        if (!skeletonData || !animatorData)
            return;

        if (skeletonData->bones.size() > animator::MAX_BONE_MASK_SIZE)
        {
            vfLogWarning("[AnimationLayerStack] Skeleton has {} bones but bone masks support max {}. Bones beyond index {} will be excluded from masks.",
                         skeletonData->bones.size(), animator::MAX_BONE_MASK_SIZE, animator::MAX_BONE_MASK_SIZE - 1);
        }

        for (size_t i = 0; i < layers.size(); ++i)
        {
            auto& layer = layers[i];

            // Look up mask name directly by index
            std::string maskName;
            if (i < animatorData->layers.size())
            {
                maskName = animatorData->layers[i].boneMaskName;
            }

            if (maskName.empty())
            {
                layer.hasMask = false;
                layer.boneMask.reset();
                continue;
            }

            // Find the mask definition
            const animator::BoneMaskDefinition* maskDef = nullptr;
            for (const auto& def : animatorData->boneMasks)
            {
                if (def.name == maskName)
                {
                    maskDef = &def;
                    break;
                }
            }

            if (!maskDef)
            {
                vfLogWarning("[AnimationLayerStack] Bone mask '{}' not found", maskName);
                layer.hasMask = false;
                continue;
            }

            // Resolve bone names to indices
            layer.boneMask.reset();
            for (const auto& boneName : maskDef->includedBoneNames)
            {
                for (size_t boneIdx = 0; boneIdx < skeletonData->bones.size() && boneIdx < animator::MAX_BONE_MASK_SIZE; ++boneIdx)
                {
                    if (skeletonData->bones[boneIdx].name == boneName)
                    {
                        layer.boneMask.set(boneIdx);
                        break;
                    }
                }
            }
            layer.hasMask = layer.boneMask.any();
        }
    }

    void AnimationLayerStack::computeReferencePose(AnimationLayerRuntime& layer)
    {
        if (layer.blendMode != animator::LayerBlendMode::Additive)
        {
            layer.referencePose.clear();
            return;
        }

        if (layer.additiveRefPose == animator::AdditiveReferencePose::BindPose)
        {
            if (skeletonData)
            {
                layer.referencePose = skeletonData->bindPoses;
            }
            return;
        }

        // FirstFrame or SpecificFrame: evaluate the clip at the target time
        float refTimeSec = 0.0f;
        if (layer.additiveRefPose == animator::AdditiveReferencePose::SpecificFrame)
        {
            refTimeSec = layer.additiveRefFrame;
        }

        AnimationEvaluator refEvaluator;
        const resource::AnimationData* clipData = nullptr;

        if (layer.sourceMode == animator::LayerSourceMode::DirectClip)
        {
            clipData = layer.directClipData;
        }
        else if (layer.stateMachine)
        {
            // Use the default state's animation for reference
            const auto* graph = layer.stateMachine->getActiveGraph();
            if (graph)
            {
                const auto* defaultState = graph->findStateById(graph->defaultStateId);
                if (defaultState && !defaultState->animationPath.empty() && animationLoadCallback)
                {
                    clipData = animationLoadCallback(defaultState->animationPath);
                }
            }
        }

        if (clipData && skeletonData)
        {
            refEvaluator.loadAnimation(*clipData, *skeletonData);
            float timeInTicks = refEvaluator.secondsToTicks(refTimeSec);
            layer.referencePose = refEvaluator.evaluatePose(timeInTicks);
        }
        else if (skeletonData)
        {
            // Fallback to bind poses
            layer.referencePose = skeletonData->bindPoses;
        }
    }

    void AnimationLayerStack::update(float deltaTime)
    {
        if (!initialized)
            return;

        cachedFiredEvents.clear();

        for (auto& layer : layers)
        {
            if (layer.weight <= 0.001f)
                continue;

            if (layer.sourceMode == animator::LayerSourceMode::StateMachine)
            {
                if (layer.stateMachine)
                {
                    layer.stateMachine->update(deltaTime);
                }
            }
            else
            {
                // Direct clip playback
                if (layer.clipPlaying && layer.directClipData)
                {
                    layer.clipTime += deltaTime * layer.clipSpeed;

                    float tps = layer.directClipData->ticksPerSecond > 0.0f
                                    ? layer.directClipData->ticksPerSecond : 24.0f;
                    float durationSec = layer.directClipData->duration / tps;

                    if (durationSec > 0.0f)
                    {
                        if (layer.clipLoop)
                        {
                            while (layer.clipTime >= durationSec)
                                layer.clipTime -= durationSec;
                        }
                        else
                        {
                            layer.clipTime = std::min(layer.clipTime, durationSec);
                        }
                    }
                }
            }
        }

        for (const auto& layer : layers)
        {
            if (layer.stateMachine)
            {
                const auto& events = layer.stateMachine->getFiredEvents();
                cachedFiredEvents.insert(cachedFiredEvents.end(), events.begin(), events.end());
            }
        }

        blendLayers();
    }

    void AnimationLayerStack::blendLayers()
    {
        if (layers.empty())
            return;

        // Evaluate base layer (layer 0)
        auto& baseLayer = layers[0];
        if (baseLayer.sourceMode == animator::LayerSourceMode::StateMachine && baseLayer.stateMachine)
        {
            finalBoneMatrices = baseLayer.stateMachine->getBoneMatrices();
        }
        else if (baseLayer.directClipData)
        {
            float timeInTicks = baseLayer.directClipEvaluator.secondsToTicks(baseLayer.clipTime);
            finalBoneMatrices = baseLayer.directClipEvaluator.evaluatePose(timeInTicks);
        }

        if (finalBoneMatrices.empty())
            return;

        // Apply overlay layers
        for (size_t i = 1; i < layers.size(); ++i)
        {
            auto& layer = layers[i];
            if (layer.weight <= 0.001f)
                continue;

            std::vector<glm::mat4> layerPose;

            if (layer.sourceMode == animator::LayerSourceMode::StateMachine && layer.stateMachine)
            {
                layerPose = layer.stateMachine->getBoneMatrices();
            }
            else if (layer.directClipData)
            {
                float timeInTicks = layer.directClipEvaluator.secondsToTicks(layer.clipTime);
                layerPose = layer.directClipEvaluator.evaluatePose(timeInTicks);
            }

            if (layerPose.empty())
                continue;

            if (layer.blendMode == animator::LayerBlendMode::Override)
            {
                AnimationBlender::blendPosesWithMask(
                    finalBoneMatrices, layerPose, layer.weight, layer.boneMask, layer.hasMask);
            }
            else if (layer.blendMode == animator::LayerBlendMode::Additive)
            {
                if (layer.referencePose.empty() && !skeletonData)
                    continue;

                const auto& refPose = layer.referencePose.empty()
                    ? skeletonData->bindPoses
                    : layer.referencePose;

                if (!refPose.empty())
                {
                    AnimationBlender::additivePoseBlend(
                        finalBoneMatrices, layerPose, layer.weight,
                        layer.boneMask, layer.hasMask, refPose);
                }
            }
        }
    }

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
        // Use final blended matrices with the base state machine's socket logic
        if (!skeletonData || finalBoneMatrices.empty() || sockets.empty())
        {
            outSocketModelTransforms.clear();
            return;
        }

        // Delegate to base state machine which uses currentBoneMatrices
        // But we want to use our finalBoneMatrices, so compute directly
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
}
