#pragma once
#include "../EventTypes.hpp"
#include "resource/ResourceLoadTypes.hpp"
#include "resource/CancellationToken.hpp"
#include "asset/AssetGUID.hpp"
#include <string_view>

namespace events::resource
{
	struct SetResourcePriorityCommand : ICommand<bool>
	{
		asset::AssetGUID guid;
		float priority = 0.0f;

		std::string_view getName() const override { return "SetResourcePriority"; }
	};

	struct CancelResourceLoadCommand : ICommand<bool>
	{
		asset::AssetGUID guid;

		std::string_view getName() const override { return "CancelResourceLoad"; }
	};

	struct GetResourceSchedulerStatsQuery : IQuery<::resource::SchedulerStats>
	{
		std::string_view getName() const override { return "GetResourceSchedulerStats"; }
	};
}
