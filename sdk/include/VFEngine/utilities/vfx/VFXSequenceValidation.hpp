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

        for (int i = 0; i < static_cast<int>(sequence.steps.size()); ++i)
        {
            const VFXSequenceStep& step = sequence.steps[static_cast<size_t>(i)];

            if (!step.vfxRef.isValid())
            {
                detail::addStep(report, Severity::Error, i, step, "missing .vfVFX reference.");
            }
            else if (context.checkRefResolvable && step.vfxRef.resolve().empty())
            {
                detail::addStep(report, Severity::Warning, i, step,
                                ".vfVFX reference does not currently resolve.");
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

            detail::validateOverrides(report, step.overrides, i, step);
        }

        for (const auto& [cue, steps] : exactCueSteps)
        {
            if (steps.size() > 1)
            {
                detail::add(report, Severity::Info, steps.front(),
                            "Cue '" + cue + "' fans out to " + std::to_string(steps.size()) + " steps.");
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

        return report;
    }
}
