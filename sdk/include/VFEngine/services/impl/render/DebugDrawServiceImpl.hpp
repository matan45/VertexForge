#pragma once

#include "../../interfaces/render/IDebugDrawService.hpp"

namespace services
{
    class IDebugDrawProvider;

    class DebugDrawServiceImpl : public IDebugDrawService
    {
    private:
        IDebugDrawProvider* debugDrawProvider;

    public:
        explicit DebugDrawServiceImpl(IDebugDrawProvider* provider);
        ~DebugDrawServiceImpl() override = default;

        void registerEventHandlers() override;
    };
}
