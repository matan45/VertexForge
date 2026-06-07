#pragma once

namespace services {

    class IPluginTextureService {
    public:
        virtual ~IPluginTextureService() = default;
        virtual void registerEventHandlers() = 0;
    };

}
