#include "AnimatorPreviewController.hpp"
#include "animator/AnimatorAsset.hpp"
#include "print/Logger.hpp"
#include "resource/AnimationResource.hpp"
#include <algorithm>

namespace controllers
{
    AnimatorPreviewController::AnimatorPreviewController()
    {
    }

    AnimatorPreviewController::~AnimatorPreviewController()
    {
        cleanUp();
    }

    void AnimatorPreviewController::init()
    {
        if (initialized)
            return;

        initialized = true;
        loggerInfo("AnimatorPreviewController initialized");
    }

    void AnimatorPreviewController::cleanUp()
    {
        if (!initialized)
            return;

        stateMachine.reset();
        animatorData.reset();
        animationCache.clear();
        initialized = false;

        loggerInfo("AnimatorPreviewController cleaned up");
    }

    bool AnimatorPreviewController::loadAnimatorData(const std::string& path)
    {
        auto data = animator::AnimatorAsset::load(path);
        if (!data)
        {
            loggerError("Failed to load animator data from: {}", path);
            return false;
        }

        animatorData = std::make_unique<animator::AnimatorData>(std::move(*data));
        currentAnimatorPath = path;
        isModified = false;

        // Initialize the state machine with the loaded data
        initializeStateMachine();

        loggerInfo("Loaded animator data from: {}", path);
        return true;
    }

    bool AnimatorPreviewController::saveAnimatorData(const std::string& path)
    {
        if (!animatorData)
        {
            loggerError("No animator data to save");
            return false;
        }

        if (animator::AnimatorAsset::save(path, *animatorData))
        {
            currentAnimatorPath = path;
            isModified = false;
            loggerInfo("Saved animator data to: {}", path);
            return true;
        }

        loggerError("Failed to save animator data to: {}", path);
        return false;
    }

    bool AnimatorPreviewController::createNewAnimator(const std::string& name)
    {
        animatorData = std::make_unique<animator::AnimatorData>(animator::AnimatorAsset::createDefault(name));
        currentAnimatorPath.clear();
        isModified = true;

        // Initialize the state machine
        initializeStateMachine();

        loggerInfo("Created new animator: {}", name);
        return true;
    }

    void AnimatorPreviewController::initializeStateMachine()
    {
        if (!animatorData)
            return;

        stateMachine = std::make_unique<animation::AnimatorStateMachine>();

        // Create animation loader callback
        auto loadCallback = [this](const std::string& path) -> const resource::AnimationData*
        {
            return loadAnimation(path);
        };

        stateMachine->initialize(*animatorData, loadCallback);
    }

    const resource::AnimationData* AnimatorPreviewController::loadAnimation(const std::string& path)
    {
        if (path.empty())
            return nullptr;

        // Check cache first
        auto it = animationCache.find(path);
        if (it != animationCache.end())
        {
            return it->second.get();
        }

        // Load from file
        try
        {
            auto animData = std::make_unique<resource::AnimationData>(
                resource::AnimationResource::loadAnimation(path));

            if (!animData->hasInverseBindPoses())
            {
                loggerWarning("Animation '{}' has no inverse bind poses", path);
            }

            const resource::AnimationData* ptr = animData.get();
            animationCache[path] = std::move(animData);
            return ptr;
        }
        catch (const std::exception& e)
        {
            loggerWarning("Failed to load animation '{}': {}", path, e.what());
            animationCache[path] = nullptr;
            return nullptr;
        }
    }

    uint32_t AnimatorPreviewController::addState(const std::string& name, const glm::vec2& position)
    {
        if (!animatorData)
            return 0;

        animator::AnimatorState state;
        state.id = animatorData->graph.nextStateId++;
        state.name = name;
        state.position = position;

        animatorData->graph.states.push_back(std::move(state));
        isModified = true;

        return animatorData->graph.states.back().id;
    }

    bool AnimatorPreviewController::removeState(uint32_t stateId)
    {
        if (!animatorData)
            return false;

        // Don't allow removing the default state
        if (stateId == animatorData->graph.defaultStateId)
        {
            loggerWarning("Cannot remove the default state");
            return false;
        }

        auto& states = animatorData->graph.states;
        auto it = std::find_if(states.begin(), states.end(),
            [stateId](const animator::AnimatorState& s) { return s.id == stateId; });

        if (it == states.end())
            return false;

        states.erase(it);

        // Remove transitions that reference this state
        auto& transitions = animatorData->graph.transitions;
        transitions.erase(
            std::remove_if(transitions.begin(), transitions.end(),
                [stateId](const animator::AnimatorTransition& t)
                {
                    return t.sourceStateId == stateId || t.targetStateId == stateId;
                }),
            transitions.end());

        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::updateState(uint32_t stateId, const std::string& name,
                                                 const std::string& animationPath,
                                                 float playbackSpeed, bool loop)
    {
        if (!animatorData)
            return false;

        auto* state = animatorData->graph.findStateById(stateId);
        if (!state)
            return false;

        state->name = name;
        state->animationPath = animationPath;
        state->playbackSpeed = playbackSpeed;
        state->loop = loop;

        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::setStatePosition(uint32_t stateId, const glm::vec2& position)
    {
        if (!animatorData)
            return false;

        auto* state = animatorData->graph.findStateById(stateId);
        if (!state)
            return false;

        state->position = position;
        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::setDefaultState(uint32_t stateId)
    {
        if (!animatorData)
            return false;

        // Verify state exists
        if (!animatorData->graph.findStateById(stateId))
            return false;

        animatorData->graph.defaultStateId = stateId;
        isModified = true;
        return true;
    }

    uint32_t AnimatorPreviewController::addTransition(uint32_t sourceStateId, uint32_t targetStateId)
    {
        if (!animatorData)
            return 0;

        animator::AnimatorTransition transition;
        transition.id = animatorData->graph.nextTransitionId++;
        transition.sourceStateId = sourceStateId;
        transition.targetStateId = targetStateId;

        animatorData->graph.transitions.push_back(std::move(transition));
        isModified = true;

        return animatorData->graph.transitions.back().id;
    }

    bool AnimatorPreviewController::removeTransition(uint32_t transitionId)
    {
        if (!animatorData)
            return false;

        auto& transitions = animatorData->graph.transitions;
        auto it = std::find_if(transitions.begin(), transitions.end(),
            [transitionId](const animator::AnimatorTransition& t) { return t.id == transitionId; });

        if (it == transitions.end())
            return false;

        transitions.erase(it);
        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::updateTransition(uint32_t transitionId, float blendDuration,
                                                      bool hasExitTime, float exitTime, int32_t priority)
    {
        if (!animatorData)
            return false;

        auto& transitions = animatorData->graph.transitions;
        auto it = std::find_if(transitions.begin(), transitions.end(),
            [transitionId](const animator::AnimatorTransition& t) { return t.id == transitionId; });

        if (it == transitions.end())
            return false;

        it->blendDuration = blendDuration;
        it->hasExitTime = hasExitTime;
        it->exitTime = exitTime;
        it->priority = priority;

        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::addTransitionCondition(uint32_t transitionId,
                                                            const std::string& parameterName,
                                                            animator::ComparisonOperator op,
                                                            const animator::AnimatorParameterValue& value)
    {
        if (!animatorData)
            return false;

        auto& transitions = animatorData->graph.transitions;
        auto it = std::find_if(transitions.begin(), transitions.end(),
            [transitionId](const animator::AnimatorTransition& t) { return t.id == transitionId; });

        if (it == transitions.end())
            return false;

        animator::TransitionCondition condition;
        condition.parameterName = parameterName;
        condition.op = op;
        condition.value = value;

        it->conditions.push_back(std::move(condition));
        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::removeTransitionCondition(uint32_t transitionId, size_t conditionIndex)
    {
        if (!animatorData)
            return false;

        auto& transitions = animatorData->graph.transitions;
        auto it = std::find_if(transitions.begin(), transitions.end(),
            [transitionId](const animator::AnimatorTransition& t) { return t.id == transitionId; });

        if (it == transitions.end() || conditionIndex >= it->conditions.size())
            return false;

        it->conditions.erase(it->conditions.begin() + conditionIndex);
        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::updateTransitionCondition(uint32_t transitionId, size_t conditionIndex,
                                                               const std::string& parameterName,
                                                               animator::ComparisonOperator op,
                                                               const animator::AnimatorParameterValue& value)
    {
        if (!animatorData)
            return false;

        auto& transitions = animatorData->graph.transitions;
        auto it = std::find_if(transitions.begin(), transitions.end(),
            [transitionId](const animator::AnimatorTransition& t) { return t.id == transitionId; });

        if (it == transitions.end() || conditionIndex >= it->conditions.size())
            return false;

        it->conditions[conditionIndex].parameterName = parameterName;
        it->conditions[conditionIndex].op = op;
        it->conditions[conditionIndex].value = value;

        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::addParameter(const std::string& name,
                                                  animator::AnimatorParameterType type,
                                                  const animator::AnimatorParameterValue& defaultValue)
    {
        if (!animatorData)
            return false;

        // Check if parameter already exists
        if (animatorData->graph.findParameter(name))
        {
            loggerWarning("Parameter '{}' already exists", name);
            return false;
        }

        animator::AnimatorParameter param;
        param.name = name;
        param.type = type;
        param.defaultValue = defaultValue;

        animatorData->graph.parameters.push_back(std::move(param));
        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::removeParameter(const std::string& name)
    {
        if (!animatorData)
            return false;

        auto& params = animatorData->graph.parameters;
        auto it = std::find_if(params.begin(), params.end(),
            [&name](const animator::AnimatorParameter& p) { return p.name == name; });

        if (it == params.end())
            return false;

        // Remove conditions that use this parameter
        for (auto& transition : animatorData->graph.transitions)
        {
            transition.conditions.erase(
                std::remove_if(transition.conditions.begin(), transition.conditions.end(),
                    [&name](const animator::TransitionCondition& c)
                    {
                        return c.parameterName == name;
                    }),
                transition.conditions.end());
        }

        params.erase(it);
        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::updateParameter(const std::string& oldName, const std::string& newName,
                                                     animator::AnimatorParameterType type,
                                                     const animator::AnimatorParameterValue& defaultValue)
    {
        if (!animatorData)
            return false;

        auto& params = animatorData->graph.parameters;
        auto it = std::find_if(params.begin(), params.end(),
            [&oldName](const animator::AnimatorParameter& p) { return p.name == oldName; });

        if (it == params.end())
            return false;

        // If renaming, update all condition references
        if (oldName != newName)
        {
            for (auto& transition : animatorData->graph.transitions)
            {
                for (auto& condition : transition.conditions)
                {
                    if (condition.parameterName == oldName)
                    {
                        condition.parameterName = newName;
                    }
                }
            }
        }

        it->name = newName;
        it->type = type;
        it->defaultValue = defaultValue;

        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::setAnyStatePosition(const glm::vec2& position)
    {
        if (!animatorData)
            return false;

        animatorData->graph.anyStatePosition = position;
        isModified = true;
        return true;
    }

    bool AnimatorPreviewController::setEntryPosition(const glm::vec2& position)
    {
        if (!animatorData)
            return false;

        animatorData->graph.entryPosition = position;
        isModified = true;
        return true;
    }

    void AnimatorPreviewController::update(float deltaTime)
    {
        if (stateMachine && stateMachine->isInitialized())
        {
            stateMachine->update(deltaTime);
        }
    }

    void AnimatorPreviewController::play()
    {
        if (stateMachine)
        {
            stateMachine->play();
        }
    }

    void AnimatorPreviewController::pause()
    {
        if (stateMachine)
        {
            stateMachine->pause();
        }
    }

    void AnimatorPreviewController::stop()
    {
        if (stateMachine)
        {
            stateMachine->stop();
        }
    }

    void AnimatorPreviewController::reset()
    {
        if (stateMachine)
        {
            stateMachine->reset();
        }
    }

    bool AnimatorPreviewController::isPlaying() const
    {
        return stateMachine && stateMachine->isPlaying();
    }

    void AnimatorPreviewController::setPreviewFloat(const std::string& name, float value)
    {
        if (stateMachine)
        {
            stateMachine->setFloat(name, value);
        }
    }

    void AnimatorPreviewController::setPreviewInt(const std::string& name, int32_t value)
    {
        if (stateMachine)
        {
            stateMachine->setInt(name, value);
        }
    }

    void AnimatorPreviewController::setPreviewBool(const std::string& name, bool value)
    {
        if (stateMachine)
        {
            stateMachine->setBool(name, value);
        }
    }

    void AnimatorPreviewController::setPreviewTrigger(const std::string& name)
    {
        if (stateMachine)
        {
            stateMachine->setTrigger(name);
        }
    }

    uint32_t AnimatorPreviewController::getCurrentStateId() const
    {
        return stateMachine ? stateMachine->getState().currentStateId : 0;
    }

    float AnimatorPreviewController::getCurrentStateTime() const
    {
        return stateMachine ? stateMachine->getState().stateTime : 0.0f;
    }

    float AnimatorPreviewController::getNormalizedStateTime() const
    {
        return stateMachine ? stateMachine->getNormalizedStateTime() : 0.0f;
    }

    bool AnimatorPreviewController::isBlending() const
    {
        return stateMachine && stateMachine->isBlending();
    }

    float AnimatorPreviewController::getBlendWeight() const
    {
        return stateMachine ? stateMachine->getState().blendWeight : 0.0f;
    }

    void AnimatorPreviewController::forceTransitionTo(uint32_t stateId, float blendDuration)
    {
        if (stateMachine)
        {
            stateMachine->forceTransitionTo(stateId, blendDuration);
        }
    }

    void AnimatorPreviewController::setModelMatrix(const glm::mat4& matrix)
    {
        modelMatrix = matrix;
    }

    void AnimatorPreviewController::setAlbedo(const glm::vec4& color)
    {
        albedo = color;
    }

    void AnimatorPreviewController::setMetallic(float value)
    {
        metallic = value;
    }

    void AnimatorPreviewController::setRoughness(float value)
    {
        roughness = value;
    }

    void AnimatorPreviewController::updateCamera(const glm::mat4& view, const glm::mat4& projection,
                                                  const glm::vec3& cameraPos)
    {
        viewMatrix = view;
        projectionMatrix = projection;
        cameraPosition = cameraPos;
    }

    void* AnimatorPreviewController::render()
    {
        // For now, return nullptr. The actual rendering will be implemented
        // when integrating with the graphics rendering system.
        // This would typically render the animated mesh with the current pose
        // from the state machine.
        return nullptr;
    }

    const std::vector<animation::EvaluatedBone>& AnimatorPreviewController::getEvaluatedBones() const
    {
        // Return bones from the state machine's current evaluator if available
        // For now, return empty vector
        return emptyBones;
    }

    const std::vector<resource::SkeletonBone>& AnimatorPreviewController::getAnimationSkeleton() const
    {
        // Return skeleton from the loaded animation
        // For now, return empty vector
        return emptySkeleton;
    }
}
