#include "SceneSerialization.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    json SceneSerialization::serializeSocketAttachment(const components::SocketAttachmentComponent& attachment)
    {
        json j;
        j["parentEntityName"] = attachment.parentEntityName;
        j["socketName"] = attachment.socketName;
        j["isActive"] = attachment.isActive;
        return j;
    }

    void SceneSerialization::deserializeSocketAttachment(const json& j, components::SocketAttachmentComponent& attachment)
    {
        attachment.parentEntityName = j.value("parentEntityName", std::string(""));
        attachment.parentEntity = entt::null;
        attachment.socketName = j.value("socketName", "");
        attachment.isActive = j.value("isActive", true);
        attachment.cachedSocketIndex = -1;
        attachment.needsParentResolution = !attachment.parentEntityName.empty();
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
