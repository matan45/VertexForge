#pragma once

namespace services {

    class IPostProcessEffectService {
    public:
        virtual ~IPostProcessEffectService() = default;
        virtual void registerEventHandlers() = 0;
    };

}
