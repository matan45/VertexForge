#pragma once

#include "VFXParameterRegistry.hpp"
#include "VFXSequenceTypes.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vfx::validation
{
    enum class Severity
    {
        Error,
        Warning,
        Info
    };

    struct Diagnostic
    {
        Severity severity = Severity::Info;
        int stepIndex = -1;
        std::string message;
    };

    struct ValidationReport
    {
        std::vector<Diagnostic> diagnostics;

        [[nodiscard]] bool hasErrors() const
        {
            return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d)
            {
                return d.severity == Severity::Error;
            });
        }

        [[nodiscard]] bool hasWarnings() const
        {
            return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d)
            {
                return d.severity == Severity::Warning;
            });
        }

        [[nodiscard]] bool ok() const { return !hasErrors(); }
    };

    struct ValidationContext
    {
        const std::unordered_set<std::string>* knownSocketNames = nullptr;
        bool checkRefResolvable = false;
    };

    namespace detail
    {
        inline std::string trim(std::string value)
        {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        }

        inline std::string lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        inline std::string stepName(int index, const VFXSequenceStep& step)
        {
            const std::string label = step.label.empty() ? std::string("(step)") : step.label;
            return "Step " + std::to_string(index) + " ('" + label + "'): ";
        }

        inline void add(ValidationReport& report, Severity severity, int stepIndex, std::string message)
        {
            report.diagnostics.push_back(Diagnostic{severity, stepIndex, std::move(message)});
        }

        inline void addStep(ValidationReport& report, Severity severity, int stepIndex,
                            const VFXSequenceStep& step, std::string message)
        {
            add(report, severity, stepIndex, stepName(stepIndex, step) + std::move(message));
        }

        inline VFXPropertyType valueType(const VFXPropertyValue& value)
        {
            if (std::holds_alternative<float>(value)) return VFXPropertyType::Float;
            if (std::holds_alternative<glm::vec2>(value)) return VFXPropertyType::Vec2;
            if (std::holds_alternative<glm::vec3>(value)) return VFXPropertyType::Vec3;
            if (std::holds_alternative<glm::vec4>(value)) return VFXPropertyType::Vec4;
            if (std::holds_alternative<int32_t>(value)) return VFXPropertyType::Int;
            if (std::holds_alternative<bool>(value)) return VFXPropertyType::Bool;
            if (std::holds_alternative<std::string>(value)) return VFXPropertyType::String;
            if (std::holds_alternative<VFXCurve>(value)) return VFXPropertyType::Curve;
            if (std::holds_alternative<VFXGradient>(value)) return VFXPropertyType::Gradient;
            return VFXPropertyType::Float;
        }

        inline bool runtimeSupportedType(VFXPropertyType type)
        {
            return type == VFXPropertyType::Float ||
                   type == VFXPropertyType::Vec3 ||
                   type == VFXPropertyType::Color ||
                   type == VFXPropertyType::Int ||
                   type == VFXPropertyType::Bool;
        }

        inline void validateOverrides(ValidationReport& report,
                                      const std::vector<VFXParamOverride>& overrides,
                                      int stepIndex,
                                      const VFXSequenceStep& step)
        {
            std::unordered_set<std::string> seen;
            for (const auto& overrideValue : overrides)
            {
                const std::string& name = overrideValue.name;
                if (!seen.insert(name).second)
                {
                    addStep(report, Severity::Info, stepIndex, step,
                            "duplicate override '" + name + "'; the last value wins.");
                }

                const VFXExposedParameter* parameter = findExposedParameter(name);
                if (!parameter)
                {
                    addStep(report, Severity::Warning, stepIndex, step,
                            "unknown override '" + name + "' is ignored at runtime.");
                    continue;
                }

                if (!runtimeSupportedType(parameter->type))
                {
                    addStep(report, Severity::Warning, stepIndex, step,
                            "override '" + name + "' has type " + propertyTypeToString(parameter->type) +
                            " which is not applied at runtime.");
                }

                if (!valueMatchesType(overrideValue.value, parameter->type))
                {
                    addStep(report, Severity::Warning, stepIndex, step,
                            "override '" + name + "' expects " + propertyTypeToString(parameter->type) +
                            " but stores " + propertyTypeToString(valueType(overrideValue.value)) + ".");
                }
            }
        }
    }

    inline ValidationReport validateSequence(const VFXSequenceData& sequence,
                                             const ValidationContext& context = {})
    {
        ValidationReport report;

        std::unordered_map<std::string, std::vector<int>> exactCueSteps;
        std::unordered_map<std::string, std::vector<int>> normalizedCueSteps;
        std::unordered_map<int, std::vector<int>> variantGroups; // VK-1497
        std::unordered_map<int, int> outputEdges;                // VK-1524: receiver step -> source step

        for (int i = 0; i < static_cast<int>(sequence.steps.size()); ++i)
        {
            const VFXSequenceStep& step = sequence.steps[static_cast<size_t>(i)];

            // VK-1496 — kind-aware reference checks. Only VFX steps require a .vfVFX; a
            // ref-less Sound/ScriptCue step is valid, so the pre-1.4 blanket Error would be a
            // false positive (and would flip the whole report to hasErrors()).
            switch (step.kind)
            {
            case VFXStepKind::VFX:
                if (!step.vfxRef.isValid())
                {
                    detail::addStep(report, Severity::Error, i, step, "missing .vfVFX reference.");
                }
                else if (context.checkRefResolvable && step.vfxRef.resolve().empty())
                {
                    detail::addStep(report, Severity::Warning, i, step,
                                    ".vfVFX reference does not currently resolve.");
                }
                break;
            case VFXStepKind::Sound:
                if (!step.audioRef.isValid())
                {
                    detail::addStep(report, Severity::Error, i, step, "missing .vfAudio reference.");
                }
                else if (context.checkRefResolvable && step.audioRef.resolve().empty())
                {
                    detail::addStep(report, Severity::Warning, i, step,
                                    ".vfAudio reference does not currently resolve.");
                }
                if (step.spatialized && step.localPosition == glm::vec3(0.0f) &&
                    detail::trim(step.socketName).empty())
                {
                    detail::addStep(report, Severity::Warning, i, step,
                                    "spatialized sound has no local offset or socket; it plays at the combo origin.");
                }
                if (step.loop)
                {
                    detail::addStep(report, Severity::Info, i, step,
                                    "loop is ignored for Sound steps in this version.");
                }
                break;
            case VFXStepKind::ScriptCue:
                if (detail::trim(step.emitCueName).empty())
                {
                    detail::addStep(report, Severity::Error, i, step, "ScriptCue step has an empty cue name.");
                }
                break;
            default:
                detail::addStep(report, Severity::Warning, i, step, "unknown step kind is ignored at runtime.");
                break;
            }

            if (step.startTime < 0.0f)
            {
                detail::addStep(report, Severity::Warning, i, step,
                                "negative start time is clamped by authoring tools but not meaningful.");
            }
            if (step.duration < 0.0f)
            {
                detail::addStep(report, Severity::Warning, i, step,
                                "negative duration means no forced stop will be applied.");
            }

            // VK-1497 — per-step variety diagnostics.
            if (step.probability < 0.0f || step.probability > 1.0f)
            {
                detail::addStep(report, Severity::Warning, i, step,
                                "probability is outside [0,1]; it is treated as always/never play.");
            }
            if (step.variantGroup >= 0)
            {
                variantGroups[step.variantGroup].push_back(i);
                if (step.probability != 1.0f)
                {
                    detail::addStep(report, Severity::Info, i, step,
                                    "probability acts as a selection weight inside a variant group and "
                                    "is ignored (uniform) in this version.");
                }
            }
            else if (step.probability == 0.0f)
            {
                detail::addStep(report, Severity::Info, i, step,
                                "probability 0 means this step never plays.");
            }

            const std::string trimmedCue = detail::trim(step.cueName);
            if (!step.cueName.empty())
            {
                if (trimmedCue.empty())
                {
                    detail::addStep(report, Severity::Warning, i, step,
                                    "cue name is whitespace only.");
                }
                else
                {
                    if (trimmedCue != step.cueName)
                    {
                        detail::addStep(report, Severity::Warning, i, step,
                                        "cue name has leading or trailing whitespace.");
                    }
                    if (step.startTime != 0.0f)
                    {
                        detail::addStep(report, Severity::Info, i, step,
                                        "start time is ignored for cue-driven steps.");
                    }
                    exactCueSteps[step.cueName].push_back(i);
                    normalizedCueSteps[detail::lower(trimmedCue)].push_back(i);
                }
            }

            const std::string trimmedSocket = detail::trim(step.socketName);
            if (!step.socketName.empty())
            {
                if (trimmedSocket.empty())
                {
                    detail::addStep(report, Severity::Warning, i, step,
                                    "socket name is whitespace only.");
                }
                else
                {
                    if (trimmedSocket != step.socketName)
                    {
                        detail::addStep(report, Severity::Warning, i, step,
                                        "socket name has leading or trailing whitespace.");
                    }
                    if (context.knownSocketNames &&
                        context.knownSocketNames->find(trimmedSocket) == context.knownSocketNames->end())
                    {
                        detail::addStep(report, Severity::Warning, i, step,
                                        "socket '" + trimmedSocket + "' is not on the reference mesh.");
                    }
                }
            }

            if (step.kind == VFXStepKind::VFX) // overrides only apply to VFX children
                detail::validateOverrides(report, step.overrides, i, step);

            // VK-1524 — step-output event binding validation.
            const bool isSource = !detail::trim(step.outputEventName).empty();
            const bool isReceiver = (step.trigger == VFXStepTrigger::StepOutput);
            if (isSource && isReceiver)
            {
                detail::addStep(report, Severity::Error, i, step,
                                "a step cannot be both an event source and a StepOutput receiver.");
            }
            if (isReceiver)
            {
                if (!detail::trim(step.socketName).empty())
                {
                    detail::addStep(report, Severity::Warning, i, step,
                                    "StepOutput spawns are world-anchored; the socket is ignored.");
                }
                if (detail::trim(step.sourceEventName).empty())
                {
                    detail::addStep(report, Severity::Error, i, step,
                                    "StepOutput binding has no source event name.");
                }
                const int src = step.sourceStepIndex;
                if (src < 0 || src >= static_cast<int>(sequence.steps.size()))
                {
                    detail::addStep(report, Severity::Error, i, step,
                                    "StepOutput binds to a missing source step (index " +
                                        std::to_string(src) + ").");
                }
                else if (src == i)
                {
                    detail::addStep(report, Severity::Error, i, step,
                                    "StepOutput binds to itself (self-dependency).");
                }
                else
                {
                    const VFXSequenceStep& source = sequence.steps[static_cast<size_t>(src)];
                    if (source.kind != VFXStepKind::VFX)
                    {
                        detail::addStep(report, Severity::Error, i, step,
                                        "source step " + std::to_string(src) +
                                            " is not a VFX step; only VFX steps emit particle events.");
                    }
                    else if (detail::trim(source.outputEventName).empty())
                    {
                        detail::addStep(report, Severity::Error, i, step,
                                        "source step " + std::to_string(src) + " publishes no named output.");
                    }
                    else if (source.outputEventName != step.sourceEventName)
                    {
                        detail::addStep(report, Severity::Error, i, step,
                                        "source step " + std::to_string(src) +
                                            " does not publish an event named '" + step.sourceEventName + "'.");
                    }
                    outputEdges[i] = src; // for the post-loop cycle pass
                }
                if (step.eventConsumption == VFXEventConsumption::EveryEvent && step.eventBudget == 0)
                {
                    detail::addStep(report, Severity::Warning, i, step,
                                    "EveryEvent consumption with an event budget of 0 never spawns.");
                }
                if (step.inheritNormal)
                {
                    detail::addStep(report, Severity::Info, i, step,
                                    "normal inheritance has no producer yet and is ignored this version.");
                }
            }
        }

        for (const auto& [cue, steps] : exactCueSteps)
        {
            if (steps.size() > 1)
            {
                detail::add(report, Severity::Info, steps.front(),
                            "Cue '" + cue + "' fans out to " + std::to_string(steps.size()) + " steps.");
            }
        }

        // VK-1497 — a variant group with a single member always plays; the author probably
        // intended two or more mutually-exclusive variants.
        for (const auto& [group, members] : variantGroups)
        {
            if (members.size() == 1)
            {
                detail::add(report, Severity::Info, members.front(),
                            "Variant group " + std::to_string(group) +
                                " has a single member; it always plays.");
            }
        }

        // VK-1524 — dependency-cycle detection over receiver->source edges. Each receiver has at
        // most one outgoing edge (a functional graph), so a cycle is found by walking each chain
        // and revisiting a node already on the current path. Bipartite-by-construction today (a
        // source is Time/Cue, a receiver is StepOutput, and dual-role is an Error above), so this
        // is defensive insurance that also surfaces a clear "cycle" diagnostic if the model relaxes.
        {
            std::unordered_set<int> settled;
            for (const auto& [start, _] : outputEdges)
            {
                if (settled.count(start))
                    continue;
                std::unordered_set<int> path;
                std::vector<int> chain;
                int node = start;
                while (outputEdges.count(node) && !path.count(node) && !settled.count(node))
                {
                    path.insert(node);
                    chain.push_back(node);
                    node = outputEdges.at(node);
                }
                if (outputEdges.count(node) && path.count(node))
                {
                    detail::addStep(report, Severity::Error, node,
                                    sequence.steps[static_cast<size_t>(node)],
                                    "is part of a StepOutput dependency cycle.");
                }
                for (int n : chain)
                    settled.insert(n);
            }
        }

        for (const auto& [normalizedCue, steps] : normalizedCueSteps)
        {
            (void)normalizedCue;
            if (steps.size() <= 1)
                continue;

            std::unordered_set<std::string> exactValues;
            for (int stepIndex : steps)
                exactValues.insert(sequence.steps[static_cast<size_t>(stepIndex)].cueName);

            if (exactValues.size() > 1)
            {
                detail::add(report, Severity::Warning, steps.front(),
                            "Cues differing only by case or whitespace will not fire together.");
            }
        }

        // VK-1451 — one-shot event markers.
        for (size_t m = 0; m < sequence.eventMarkers.size(); ++m)
        {
            const VFXSequenceEventMarker& marker = sequence.eventMarkers[m];
            if (marker.time < 0.0f)
            {
                detail::add(report, Severity::Warning, -1,
                            "Event marker " + std::to_string(m) + " has a negative time.");
            }
            if (marker.cueName.empty())
            {
                detail::add(report, Severity::Warning, -1,
                            "Event marker " + std::to_string(m) + " has an empty cue name.");
            }
        }

        // VK-1498 — stableLoop only has an effect when there is VK-1497 variety to keep stable.
        if (sequence.stableLoop)
        {
            bool hasVariety = !variantGroups.empty();
            for (const auto& step : sequence.steps)
            {
                if (step.probability < 1.0f)
                {
                    hasVariety = true;
                    break;
                }
            }
            if (!hasVariety)
            {
                detail::add(report, Severity::Info, -1,
                            "Stable Loop is set but no step has probability < 1 or a variant group; "
                            "it has no effect.");
            }
        }

        return report;
    }
}
