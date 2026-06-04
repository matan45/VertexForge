#pragma once
#include <string>
#include <vector>
#include "resource/AssetTypes.hpp"

namespace services {

	class IAssetLifecycleService {
	public:
		virtual ~IAssetLifecycleService() = default;
		virtual void registerEventHandlers() = 0;
		virtual void update(float deltaTime) = 0;
	};

}
