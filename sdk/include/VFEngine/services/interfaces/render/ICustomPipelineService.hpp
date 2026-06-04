#pragma once

namespace services {

    class ICustomPipelineService {
    public:
        virtual ~ICustomPipelineService() = default;
        virtual void registerEventHandlers() = 0;
    };

}
