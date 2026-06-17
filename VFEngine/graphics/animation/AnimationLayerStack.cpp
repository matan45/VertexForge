#include "AnimationLayerStack.hpp"
#include "print/Log.hpp"

namespace animation
{
    AnimationLayerStack::AnimationLayerStack() = default;
    AnimationLayerStack::~AnimationLayerStack() = default;

    void AnimationLayerStack::initializeSharedParameters(const animator::AnimatorData& data)
    {
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
    }

    void AnimationLayerStack::initializeLayerRuntimes(const animator::AnimatorData& data)
    {
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
                runtime.stateMachine->initializeFromGraph(layerData.graph, skeletonData, animationLoadCallback, &sharedParameters, retargetContext);
            }
            else
            {
                runtime.clipLoop = layerData.directClipLoop;
                runtime.clipSpeed = layerData.directClipSpeed;
                runtime.clipTime = 0.0f;

                if (layerData.directClipRef.isValid() && animationLoadCallback)
                {
                    runtime.directClipData = animationLoadCallback(layerData.directClipRef.resolve());
                    if (runtime.directClipData && skeletonData)
                    {
                        runtime.directClipEvaluator.loadAnimation(*runtime.directClipData, *skeletonData, retargetContext);
                    }
                }
            }

            layers.push_back(std::move(runtime));
        }
    }

    void AnimationLayerStack::initializeSingleGraphLayer(const animator::AnimatorData& data)
    {
        sharedParameters.initializeFromGraph(data.graph);

        AnimationLayerRuntime baseLayer;
        baseLayer.name = "Base Layer";
        baseLayer.weight = 1.0f;
        baseLayer.blendMode = animator::LayerBlendMode::Override;
        baseLayer.sourceMode = animator::LayerSourceMode::StateMachine;

        baseLayer.stateMachine = std::make_unique<AnimatorStateMachine>();
        baseLayer.stateMachine->initializeFromGraph(data.graph, skeletonData, animationLoadCallback, &sharedParameters, retargetContext);

        layers.push_back(std::move(baseLayer));
    }

    void AnimationLayerStack::initialize(const animator::AnimatorData& data, const resource::SkeletonData* skeleton,
                                          AnimationLoadCallback loadCallback, const RetargetContext* retarget)
    {
        animatorData = &data;
        skeletonData = skeleton;
        retargetContext = retarget;
        animationLoadCallback = loadCallback;

        layers.clear();

        if (!data.layers.empty())
        {
            initializeSharedParameters(data);
            initializeLayerRuntimes(data);
        }
        else
        {
            initializeSingleGraphLayer(data);
        }

        resolveBoneMasks();

        for (auto& layer : layers)
        {
            computeReferencePose(layer);
        }

        initialized = true;
    }

    const animator::BoneMaskDefinition* AnimationLayerStack::findBoneMaskDefinition(const std::string& maskName) const
    {
        if (!animatorData)
            return nullptr;

        for (const auto& def : animatorData->boneMasks)
        {
            if (def.name == maskName)
            {
                return &def;
            }
        }
        return nullptr;
    }

    void AnimationLayerStack::resolveBoneMaskIndices(AnimationLayerRuntime& layer, const animator::BoneMaskDefinition& maskDef)
    {
        layer.boneMask.reset();
        for (const auto& boneName : maskDef.includedBoneNames)
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

            const animator::BoneMaskDefinition* maskDef = findBoneMaskDefinition(maskName);
            if (!maskDef)
            {
                vfLogWarning("[AnimationLayerStack] Bone mask '{}' not found", maskName);
                layer.hasMask = false;
                continue;
            }

            resolveBoneMaskIndices(layer, *maskDef);
        }
    }

    const resource::AnimationData* AnimationLayerStack::findReferencePoseClip(const AnimationLayerRuntime& layer) const
    {
        if (layer.sourceMode == animator::LayerSourceMode::DirectClip)
        {
            return layer.directClipData;
        }

        if (layer.stateMachine)
        {
            const auto* graph = layer.stateMachine->getActiveGraph();
            if (graph)
            {
                const auto* defaultState = graph->findStateById(graph->defaultStateId);
                if (defaultState && defaultState->animationRef.isValid() && animationLoadCallback)
                {
                    return animationLoadCallback(defaultState->animationRef.resolve());
                }
            }
        }
        return nullptr;
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
                layer.referencePose = skeletonData->bindPoses;
            return;
        }

        float refTimeSec = 0.0f;
        if (layer.additiveRefPose == animator::AdditiveReferencePose::SpecificFrame)
            refTimeSec = layer.additiveRefFrame;

        const resource::AnimationData* clipData = findReferencePoseClip(layer);

        if (clipData && skeletonData)
        {
            AnimationEvaluator refEvaluator;
            refEvaluator.loadAnimation(*clipData, *skeletonData, retargetContext);
            float timeInTicks = refEvaluator.secondsToTicks(refTimeSec);
            layer.referencePose = refEvaluator.evaluatePose(timeInTicks);
        }
        else if (skeletonData)
        {
            layer.referencePose = skeletonData->bindPoses;
        }
    }

    void AnimationLayerStack::updateDirectClipPlayback(AnimationLayerRuntime& layer, float deltaTime)
    {
        if (!layer.clipPlaying || !layer.directClipData)
            return;

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

    void AnimationLayerStack::collectFiredEvents()
    {
        for (const auto& layer : layers)
        {
            if (layer.stateMachine)
            {
                const auto& events = layer.stateMachine->getFiredEvents();
                cachedFiredEvents.insert(cachedFiredEvents.end(), events.begin(), events.end());
            }
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
                    layer.stateMachine->update(deltaTime);
            }
            else
            {
                updateDirectClipPlayback(layer, deltaTime);
            }
        }

        collectFiredEvents();
        blendLayers();
    }

    void AnimationLayerStack::evaluateBaseLayerPose()
    {
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
    }

    std::vector<glm::mat4> AnimationLayerStack::evaluateLayerPose(AnimationLayerRuntime& layer)
    {
        if (layer.sourceMode == animator::LayerSourceMode::StateMachine && layer.stateMachine)
        {
            return layer.stateMachine->getBoneMatrices();
        }
        else if (layer.directClipData)
        {
            float timeInTicks = layer.directClipEvaluator.secondsToTicks(layer.clipTime);
            return layer.directClipEvaluator.evaluatePose(timeInTicks);
        }
        return {};
    }

    void AnimationLayerStack::applyOverlayLayer(AnimationLayerRuntime& layer)
    {
        std::vector<glm::mat4> layerPose = evaluateLayerPose(layer);
        if (layerPose.empty())
            return;

        if (layer.blendMode == animator::LayerBlendMode::Override)
        {
            AnimationBlender::blendPosesWithMask(
                finalBoneMatrices, layerPose, layer.weight, layer.boneMask, layer.hasMask);
        }
        else if (layer.blendMode == animator::LayerBlendMode::Additive)
        {
            if (layer.referencePose.empty() && !skeletonData)
                return;

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

    void AnimationLayerStack::blendLayers()
    {
        if (layers.empty())
            return;

        evaluateBaseLayerPose();

        if (finalBoneMatrices.empty())
            return;

        for (size_t i = 1; i < layers.size(); ++i)
        {
            auto& layer = layers[i];
            if (layer.weight <= 0.001f)
                continue;

            applyOverlayLayer(layer);
        }
    }

}
