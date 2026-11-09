#pragma once

#include "../../data/EQSData.hpp"
#include "../../events/ai/EQSAuthoringEvents.hpp"
#include <string>

namespace services
{
    // VK-1660 - answers events::ai::LoadEQSQueryAssetQuery and SaveEQSQueryAssetCommand: the strict-validated,
    // hash-guarded, atomic write path the MCP eqs_* tools use for .vfEQSQuery templates. Reads the file on
    // every call; holds no state besides its handler registration. Editor only.
    class EQSAuthoringService
    {
    public:
        EQSAuthoringService() = default;
        ~EQSAuthoringService();

        EQSAuthoringService(const EQSAuthoringService&) = delete;
        EQSAuthoringService& operator=(const EQSAuthoringService&) = delete;

        void registerEventHandlers();
        void unregisterEventHandlers();

        static EQSAssetLoadResult loadAsset(const std::string& path);
        static EQSAssetSaveResult saveAsset(const ::events::ai::SaveEQSQueryAssetCommand& cmd);

    private:
        bool handlersRegistered = false;
    };
}
