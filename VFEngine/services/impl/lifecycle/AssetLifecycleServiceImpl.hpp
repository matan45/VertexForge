#pragma once
#include "../../interfaces/lifecycle/IAssetLifecycleService.hpp"
#include "../../events/EventTypes.hpp"

namespace services {

	class AssetLifecycleServiceImpl : public IAssetLifecycleService {
	public:
		AssetLifecycleServiceImpl();
		~AssetLifecycleServiceImpl() override;

		void registerEventHandlers() override;
		void update(float deltaTime) override;

	private:
		void setupReleaseCallback();

		events::SubscriptionToken sceneClearedSubscription;
		events::SubscriptionToken entityDeletedSubscription;
	};

}
