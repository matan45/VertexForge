#include "SceneSerialization.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    json SceneSerialization::serializeSocketAttachment(const components::SocketAttachmentComponent& attachment)
    {
        json j;
        // Store parent entity UUID rather than raw entt handle for scene persistence
        // The parent entity's UUID will be resolved during deserialization
        j["parentEntityId"] = static_cast<uint64_t>(attachment.parentEntity);
        j["socketName"] = attachment.socketName;
        j["isActive"] = attachment.isActive;
        return j;
    }

    void SceneSerialization::deserializeSocketAttachment(const json& j, components::SocketAttachmentComponent& attachment)
    {
        // Store the raw entity ID - will be resolved after all entities are loaded
        attachment.parentEntity = static_cast<entt::entity>(j.value("parentEntityId", static_cast<uint64_t>(entt::null)));
        attachment.socketName = j.value("socketName", "");
        attachment.isActive = j.value("isActive", true);
        attachment.cachedSocketIndex = -1; // Will be resolved at runtime
    }

    json SceneSerialization::serializeSocketOverride(const components::SocketOverrideComponent& override)
    {
        json j;

        if (!override.additionalSockets.empty())
        {
            json additionalJson = json::array();
            for (const auto& socket : override.additionalSockets)
            {
                json socketJ;
                socketJ["name"] = socket.name;
                socketJ["targetBoneName"] = socket.targetBoneName;
                socketJ["localPosition"] = json::array({socket.localPosition.x, socket.localPosition.y, socket.localPosition.z});
                socketJ["localRotation"] = json::array({socket.localRotation.w, socket.localRotation.x, socket.localRotation.y, socket.localRotation.z});
                socketJ["localScale"] = json::array({socket.localScale.x, socket.localScale.y, socket.localScale.z});
                additionalJson.push_back(socketJ);
            }
            j["additionalSockets"] = additionalJson;
        }

        if (!override.overriddenSockets.empty())
        {
            json overriddenJson = json::array();
            for (const auto& socket : override.overriddenSockets)
            {
                json socketJ;
                socketJ["name"] = socket.name;
                socketJ["targetBoneName"] = socket.targetBoneName;
                socketJ["localPosition"] = json::array({socket.localPosition.x, socket.localPosition.y, socket.localPosition.z});
                socketJ["localRotation"] = json::array({socket.localRotation.w, socket.localRotation.x, socket.localRotation.y, socket.localRotation.z});
                socketJ["localScale"] = json::array({socket.localScale.x, socket.localScale.y, socket.localScale.z});
                overriddenJson.push_back(socketJ);
            }
            j["overriddenSockets"] = overriddenJson;
        }

        return j;
    }

    namespace
    {
        animator::SocketDefinition deserializeSocketDef(const json& socketJ)
        {
            animator::SocketDefinition socket;
            socket.name = socketJ.value("name", "");
            socket.targetBoneName = socketJ.value("targetBoneName", "");

            if (socketJ.contains("localPosition") && socketJ["localPosition"].is_array() && socketJ["localPosition"].size() >= 3)
            {
                socket.localPosition.x = socketJ["localPosition"][0].get<float>();
                socket.localPosition.y = socketJ["localPosition"][1].get<float>();
                socket.localPosition.z = socketJ["localPosition"][2].get<float>();
            }

            if (socketJ.contains("localRotation") && socketJ["localRotation"].is_array() && socketJ["localRotation"].size() >= 4)
            {
                socket.localRotation.w = socketJ["localRotation"][0].get<float>();
                socket.localRotation.x = socketJ["localRotation"][1].get<float>();
                socket.localRotation.y = socketJ["localRotation"][2].get<float>();
                socket.localRotation.z = socketJ["localRotation"][3].get<float>();
            }

            if (socketJ.contains("localScale") && socketJ["localScale"].is_array() && socketJ["localScale"].size() >= 3)
            {
                socket.localScale.x = socketJ["localScale"][0].get<float>();
                socket.localScale.y = socketJ["localScale"][1].get<float>();
                socket.localScale.z = socketJ["localScale"][2].get<float>();
            }

            return socket;
        }
    }

    void SceneSerialization::deserializeSocketOverride(const json& j, components::SocketOverrideComponent& override)
    {
        if (j.contains("additionalSockets") && j["additionalSockets"].is_array())
        {
            for (const auto& socketJ : j["additionalSockets"])
            {
                override.additionalSockets.push_back(deserializeSocketDef(socketJ));
            }
        }

        if (j.contains("overriddenSockets") && j["overriddenSockets"].is_array())
        {
            for (const auto& socketJ : j["overriddenSockets"])
            {
                override.overriddenSockets.push_back(deserializeSocketDef(socketJ));
            }
        }
    }
}
