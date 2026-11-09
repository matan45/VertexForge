#include "VFXAssetSummaryService.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vfx/VFXAssetSummaryEvents.hpp"
#include "asset/AssetDatabase.hpp"
#include "vfx/VFXAsset.hpp"
#include "vfx/VFXBurstTypes.hpp"
#include "vfx/VFXEventTypes.hpp"
#include "vfx/VFXModifierConfigLoader.hpp"
#include "vfx/VFXParameterRegistry.hpp"
#include "vfx/VFXSequenceAsset.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <variant>

namespace services
{
    namespace
    {
        // VFXEmitterConfigLoader::getPropertyValue: a missing key or a value of another type reads
        // as the default.
        template <typename T>
        T nodeValue(const vfx::VFXNode& node, const std::string& name, T defaultValue)
        {
            auto it = node.properties.find(name);
            if (it == node.properties.end())
                return defaultValue;
            if (const auto* value = std::get_if<T>(&it->second.value))
                return *value;
            return defaultValue;
        }

        bool endsWithNoCase(const std::string& value, std::string_view suffix)
        {
            if (value.size() < suffix.size())
                return false;
            return std::equal(suffix.begin(), suffix.end(),
                              value.end() - static_cast<std::ptrdiff_t>(suffix.size()),
                              [](char a, char b)
                              {
                                  return std::tolower(static_cast<unsigned char>(a)) ==
                                         std::tolower(static_cast<unsigned char>(b));
                              });
        }

        const char* eventTypeName(vfx::VFXEventType type)
        {
            switch (type)
            {
            case vfx::VFXEventType::OnSpawn:             return "spawn";
            case vfx::VFXEventType::OnDeath:             return "death";
            case vfx::VFXEventType::OnCollision:         return "collision";
            case vfx::VFXEventType::OnLifetimeThreshold: return "lifetimeThreshold";
            default:                                     return "spawn";
            }
        }

        const char* stepKindName(vfx::VFXStepKind kind)
        {
            switch (kind)
            {
            case vfx::VFXStepKind::Sound:     return "sound";
            case vfx::VFXStepKind::ScriptCue: return "scriptCue";
            default:                          return "vfx";
            }
        }

        void summarizeEffect(const std::string& path, VFXAssetSummary& summary)
        {
            auto data = vfx::VFXAsset::load(path);
            if (!data)
            {
                summary.error = "The effect file cannot be read or parsed (see logs_read)";
                return;
            }
            summary.name = data->name;

            // The renderer reads the first Emitter node, like this.
            const vfx::VFXNode* emitter = data->graph.findEmitterNode();
            if (!emitter)
            {
                summary.error = "The effect graph has no Emitter node";
                return;
            }

            VFXEffectSummary effect;
            effect.spawnRate = nodeValue<float>(*emitter, "spawnRate", vfx::EmitterDefaults::SPAWN_RATE);
            effect.lifetime = nodeValue<float>(*emitter, "lifetime", vfx::EmitterDefaults::LIFETIME);
            effect.startSize = nodeValue<float>(*emitter, "startSize", vfx::EmitterDefaults::START_SIZE);
            effect.startSpeed = nodeValue<float>(*emitter, "startSpeed", vfx::EmitterDefaults::START_SPEED);
            effect.startColor = nodeValue<glm::vec4>(*emitter, "startColor", glm::vec4(1.0f));
            effect.looping = nodeValue<bool>(*emitter, "looping", vfx::EmitterDefaults::LOOPING);
            effect.burstCount = static_cast<uint32_t>(vfx::loadBurstsFromNode(*emitter).size());

            // VK-1472: the blendMode string wins; legacy assets only carry the additiveBlend bool.
            const std::string blendMode = nodeValue<std::string>(*emitter, "blendMode", std::string());
            effect.blendMode = blendMode.empty()
                ? vfx::blendModeFromLegacy(nodeValue<bool>(*emitter, "additiveBlend", vfx::EmitterDefaults::ADDITIVE_BLEND))
                : vfx::stringToBlendMode(blendMode);
            effect.sortOrder = std::clamp(nodeValue<int32_t>(*emitter, "sortOrder", vfx::EmitterDefaults::SORT_ORDER),
                                          vfx::kMinSortOrder, vfx::kMaxSortOrder);
            effect.renderMode = std::clamp(nodeValue<int32_t>(*emitter, "renderMode", vfx::EmitterDefaults::RENDER_MODE),
                                           0, 4);
            effect.emitsLight = nodeValue<bool>(*emitter, "lightEmissionEnabled",
                                                vfx::EmitterDefaults::LIGHT_EMISSION_ENABLED);
            effect.texture = nodeValue<std::string>(*emitter, "texture", std::string());

            // A ColorOverLifetime node replaces the particle colour every frame. The first one in the
            // emitter chain counts, as in the LUT the GPU sim samples (VFXLUTBaker::bake).
            effect.birthColor = effect.startColor;
            effect.deathColor = effect.startColor;
            const vfx::VFXModifierChain modifiers = vfx::VFXModifierConfigLoader::fromGraph(data->graph);
            for (const auto& modifier : modifiers.modifiers)
            {
                if (const auto* colorOverLifetime = std::get_if<vfx::ColorOverLifetimeConfig>(&modifier))
                {
                    effect.colorOverLifetime = true;
                    effect.birthColor = colorOverLifetime->gradient.evaluate(0.0f);
                    effect.deathColor = colorOverLifetime->gradient.evaluate(1.0f);
                    break;
                }
            }

            const vfx::VFXEventConfig eventConfig = vfx::loadEventConfigFromNode(*emitter);
            for (uint32_t i = 0; i < vfx::VFX_EVENT_TYPE_COUNT; ++i)
            {
                const vfx::VFXEventTypeConfig& typeConfig = eventConfig.types[i];
                if (!typeConfig.enabled)
                    continue;

                VFXEventSummary eventSummary;
                eventSummary.type = eventTypeName(static_cast<vfx::VFXEventType>(i));
                eventSummary.notify = typeConfig.notify;
                eventSummary.subEffect = typeConfig.vfxPath;
                effect.events.push_back(std::move(eventSummary));
            }

            summary.effect = std::move(effect);
            summary.valid = true;
        }

        void summarizeCombo(const std::string& path, VFXAssetSummary& summary)
        {
            auto data = vfx::VFXSequenceAsset::load(path);
            if (!data)
            {
                summary.error = "The combo file cannot be read or parsed (see logs_read)";
                return;
            }
            summary.name = data->name;

            VFXComboSummary combo;
            combo.playbackRate = data->playbackRate;
            const auto addCue = [&combo](const std::string& cue)
            {
                if (!cue.empty() && std::find(combo.cues.begin(), combo.cues.end(), cue) == combo.cues.end())
                    combo.cues.push_back(cue);
            };

            combo.steps.reserve(data->steps.size());
            for (const auto& step : data->steps)
            {
                VFXComboStepSummary stepSummary;
                stepSummary.label = step.label;
                stepSummary.kind = stepKindName(step.kind);
                if (step.kind == vfx::VFXStepKind::VFX)
                    stepSummary.effectPath = step.vfxRef.resolve();
                stepSummary.startTime = step.startTime;
                stepSummary.cue = step.cueName;
                stepSummary.loop = step.loop;
                combo.steps.push_back(std::move(stepSummary));
                addCue(step.cueName);
            }

            // Step cues first, then timeline-marker cues, then the cues ScriptCue steps publish.
            for (const auto& marker : data->eventMarkers)
                addCue(marker.cueName);
            for (const auto& step : data->steps)
            {
                if (step.kind == vfx::VFXStepKind::ScriptCue)
                    addCue(step.emitCueName);
            }

            summary.combo = std::move(combo);
            summary.valid = true;
        }
    }

    VFXAssetSummaryService::~VFXAssetSummaryService()
    {
        unregisterEventHandlers();
    }

    void VFXAssetSummaryService::registerEventHandlers()
    {
        if (handlersRegistered)
            return;
        handlersRegistered = true;

        ::events::EventDispatcher::instance().registerQueryHandler<events::vfxassets::GetVFXAssetSummariesQuery>(
            [](const events::vfxassets::GetVFXAssetSummariesQuery& query)
            {
                return VFXAssetSummaryService::summarizeAssets(query.paths);
            });
    }

    void VFXAssetSummaryService::unregisterEventHandlers()
    {
        if (!handlersRegistered)
            return;
        handlersRegistered = false;

        ::events::EventDispatcher::instance().unregisterQueryHandler<events::vfxassets::GetVFXAssetSummariesQuery>();
    }

    VFXAssetSummary VFXAssetSummaryService::summarizeAsset(const std::string& path)
    {
        VFXAssetSummary summary;
        summary.path = path;
        if (path.empty())
        {
            summary.error = "Empty path";
            return summary;
        }

        const std::string resolved = asset::AssetDatabase::instance().resolveAssetPath(path);
        summary.isCombo = endsWithNoCase(resolved, ".vfVFXSequence");

        try
        {
            std::error_code ec;
            if (!std::filesystem::is_regular_file(std::filesystem::path(resolved), ec))
            {
                summary.error = "File not found: " + resolved;
                return summary;
            }

            if (summary.isCombo)
                summarizeCombo(resolved, summary);
            else
                summarizeEffect(resolved, summary);
        }
        catch (const std::exception& e)
        {
            // One unreadable file must not fail the whole batch.
            summary.valid = false;
            summary.error = std::string("Cannot summarize the file: ") + e.what();
            summary.effect.reset();
            summary.combo.reset();
        }
        return summary;
    }

    std::vector<VFXAssetSummary> VFXAssetSummaryService::summarizeAssets(const std::vector<std::string>& paths)
    {
        std::vector<VFXAssetSummary> summaries;
        summaries.reserve(paths.size());
        for (const auto& path : paths)
            summaries.push_back(summarizeAsset(path));
        return summaries;
    }
}
