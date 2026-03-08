#pragma once

#include "../EventTypes.hpp"
#include "../../providers/render/IBillboardRenderProvider.hpp"
#include <string>

namespace events::render
{
    struct SetBillboardRenderingEnabledCommand : ICommand<>
    {
        bool enabled = true;
        std::string_view getName() const override { return "SetBillboardRenderingEnabledCommand"; }
    };

    struct SetBillboardMaxDistanceCommand : ICommand<>
    {
        float distance = 500.0f;
        std::string_view getName() const override { return "SetBillboardMaxDistanceCommand"; }
    };

    struct GetBillboardStatsQuery : IQuery<services::BillboardRenderStats>
    {
        std::string_view getName() const override { return "GetBillboardStatsQuery"; }
    };

    struct BakeImposterCommand : ICommand<services::ImposterBakeResult>
    {
        std::string meshPath;
        std::string outputPath;
        std::string_view getName() const override { return "BakeImposterCommand"; }
    };
}
