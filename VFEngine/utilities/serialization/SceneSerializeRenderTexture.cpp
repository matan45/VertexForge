#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"
#include "../scene/EntityRegistry.hpp"
#include <unordered_map>

namespace {
    using json = nlohmann::json;

    void readVec4(const json& j, const std::string& key, glm::vec4& out) {
        if (j.contains(key) && j[key].is_array() && j[key].size() >= 4) {
            out = glm::vec4(j[key][0].get<float>(), j[key][1].get<float>(),
                           j[key][2].get<float>(), j[key][3].get<float>());
        }
    }

    json writeVec4(const glm::vec4& v) {
        return json::array({v.x, v.y, v.z, v.w});
    }
}

namespace serialization
{
    std::string SceneSerialization::updateModeToString(rendertexture::UpdateMode mode)
    {
        switch (mode)
        {
        case rendertexture::UpdateMode::EveryFrame: return "everyFrame";
        case rendertexture::UpdateMode::OnDemand: return "onDemand";
        case rendertexture::UpdateMode::FixedInterval: return "fixedInterval";
        default: return "everyFrame";
        }
    }

    rendertexture::UpdateMode SceneSerialization::stringToUpdateMode(const std::string& str)
    {
        if (str == "everyFrame") return rendertexture::UpdateMode::EveryFrame;
        if (str == "onDemand") return rendertexture::UpdateMode::OnDemand;
        if (str == "fixedInterval") return rendertexture::UpdateMode::FixedInterval;
        return rendertexture::UpdateMode::EveryFrame;
    }

    json SceneSerialization::serializeRenderTexture(const components::RenderTextureComponent& rtt)
    {
        json j;
        j["width"] = rtt.width;
        j["height"] = rtt.height;
        j["updateMode"] = updateModeToString(rtt.updateMode);
        j["fixedIntervalSeconds"] = rtt.fixedIntervalSeconds;
        j["clearColor"] = writeVec4(rtt.clearColor);
        j["priority"] = rtt.priority;
        j["enabled"] = rtt.enabled;
        return j;
    }

    void SceneSerialization::deserializeRenderTexture(const json& j, components::RenderTextureComponent& rtt)
    {
        rtt.width = j.value("width", 512u);
        rtt.height = j.value("height", 512u);
        rtt.updateMode = stringToUpdateMode(j.value("updateMode", "everyFrame"));
        rtt.fixedIntervalSeconds = j.value("fixedIntervalSeconds", 1.0f / 30.0f);
        readVec4(j, "clearColor", rtt.clearColor);
        rtt.priority = j.value("priority", 0u);
        rtt.enabled = j.value("enabled", true);
        // textureId is runtime-only, not serialized
        rtt.textureId = rendertexture::INVALID_RENDER_TEXTURE_ID;
    }

    void SceneSerialization::resolveRenderTextureSourceNames()
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // Build name → entity map for entities that have RenderTextureComponent
        std::unordered_map<std::string, entt::entity> rttEntityMap;
        auto rttView = registry.view<components::RenderTextureComponent, components::NameComponent>();
        for (auto entity : rttView)
        {
            const auto& name = rttView.get<components::NameComponent>(entity).name;
            if (!name.empty())
            {
                rttEntityMap[name] = entity;
            }
        }

        if (rttEntityMap.empty())
            return;

        // Resolve UIImageComponent references
        auto uiImageView = registry.view<components::UIImageComponent>();
        for (auto entity : uiImageView)
        {
            auto& image = uiImageView.get<components::UIImageComponent>(entity);
            if (!image.renderTextureSourceName.empty() && image.renderTextureSource == entt::null)
            {
                auto it = rttEntityMap.find(image.renderTextureSourceName);
                if (it != rttEntityMap.end())
                {
                    image.renderTextureSource = it->second;
                }
            }
        }

        // Resolve BillboardComponent references
        auto bbView = registry.view<components::BillboardComponent>();
        for (auto entity : bbView)
        {
            auto& billboard = bbView.get<components::BillboardComponent>(entity);
            if (!billboard.renderTextureSourceName.empty() && billboard.renderTextureSource == entt::null)
            {
                auto it = rttEntityMap.find(billboard.renderTextureSourceName);
                if (it != rttEntityMap.end())
                {
                    billboard.renderTextureSource = it->second;
                }
            }
        }
    }
}
