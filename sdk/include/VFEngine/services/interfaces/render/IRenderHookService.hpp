#pragma once

namespace services {

    class IRenderHookService {
    public:
        virtual ~IRenderHookService() = default;
        virtual void registerEventHandlers() = 0;
    };

}
