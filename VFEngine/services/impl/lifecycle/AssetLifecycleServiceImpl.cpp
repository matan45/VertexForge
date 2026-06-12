#include "AssetLifecycleServiceImpl.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include "resource/AssetLifecycleHelpers.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/lifecycle/AssetLifecycleEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "asset/AssetRef.hpp"

namespace services {

	AssetLifecycleServiceImpl::AssetLifecycleServiceImpl()
	{
		setupReleaseCallback();
	}

	AssetLifecycleServiceImpl::~AssetLifecycleServiceImpl()
	{
		auto& dispatcher = events::EventDispatcher::instance();
		if (sceneClearedSubscription.isValid())
		{
			dispatcher.unsubscribe(sceneClearedSubscription);
		}
		if (entityDeletedSubscription.isValid())
		{
			dispatcher.unsubscribe(entityDeletedSubscription);
		}
		if (sceneLoadedSubscription.isValid())
		{
			dispatcher.unsubscribe(sceneLoadedSubscription);
		}
	}

	void AssetLifecycleServiceImpl::setupReleaseCallback()
	{
		resource::AssetLifecycleManager::instance().setReleaseCallback(
			[](const asset::AssetGUID& guid, resource::AssetType type)
			{
				events::lifecycle::AssetReleaseReadyNotification notification;
				notification.path = asset::AssetRef::fromGUID(guid).resolve();
				notification.type = type;
				events::EventDispatcher::instance().publish(notification);
			});
	}

	void AssetLifecycleServiceImpl::registerEventHandlers()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		dispatcher.registerCommandHandler<events::lifecycle::ForceReleaseAssetCommand>(
			[](const events::lifecycle::ForceReleaseAssetCommand& cmd)
			{
				resource::AssetLifecycleManager::instance().forceRelease(asset::AssetRef::fromPath(std::string(cmd.path)).getGUID());
			});

		dispatcher.registerCommandHandler<events::lifecycle::SetMemoryBudgetCommand>(
			[](const events::lifecycle::SetMemoryBudgetCommand& cmd)
			{
				auto& lifecycle = resource::AssetLifecycleManager::instance();
				auto config = lifecycle.getMemoryBudget();
				config.totalBudgetBytes = cmd.totalBudgetBytes;
				lifecycle.setMemoryBudget(config);
			});

		dispatcher.registerQueryHandler<events::lifecycle::QueryMemoryBudgetQuery>(
			[](const events::lifecycle::QueryMemoryBudgetQuery&)
			{
				auto& lifecycle = resource::AssetLifecycleManager::instance();
				events::lifecycle::MemoryBudgetStatus status;
				status.totalBudgetBytes = lifecycle.getMemoryBudget().totalBudgetBytes;
				status.trackedBytes = lifecycle.getTotalTrackedBytes();
				status.overBudget = lifecycle.isOverBudget();
				return status;
			});

		dispatcher.registerQueryHandler<events::lifecycle::QueryAssetStatsQuery>(
			[](const events::lifecycle::QueryAssetStatsQuery&)
			{
				return resource::AssetLifecycleManager::instance().getAllAssets();
			});

		dispatcher.registerQueryHandler<events::lifecycle::QueryPendingReleasesQuery>(
			[](const events::lifecycle::QueryPendingReleasesQuery&)
			{
				return resource::AssetLifecycleManager::instance().getPendingReleases();
			});

		dispatcher.registerQueryHandler<events::lifecycle::QueryAssetEntryQuery>(
			[](const events::lifecycle::QueryAssetEntryQuery& query)
			{
				return resource::AssetLifecycleManager::instance().getAssetEntry(asset::AssetRef::fromPath(std::string(query.path)).getGUID());
			});

		sceneClearedSubscription = dispatcher.subscribe<events::scene::SceneClearedNotification>(
			[](const events::scene::SceneClearedNotification&)
			{
				resource::AssetLifecycleManager::instance().clear();
			});

		sceneLoadedSubscription = dispatcher.subscribe<events::scene::SceneLoadedNotification>(
			[this](const events::scene::SceneLoadedNotification&)
			{
				acquireAllEntityAssets();
			});

		entityDeletedSubscription = dispatcher.subscribe<events::scene::EntityDeletedNotification>(
			[](const events::scene::EntityDeletedNotification& notification)
			{
				auto& registry = scene::EntityRegistry::getRegistry();
				auto enttEntity = internal::fromHandle(notification.entity);
				if (!registry.valid(enttEntity)) return;

				auto& lifecycle = resource::AssetLifecycleManager::instance();
				scene::Entity entity(enttEntity);
				resource::releaseEntityAssets(entity, lifecycle);
			});
	}

	void AssetLifecycleServiceImpl::acquireAllEntityAssets()
	{
		auto& registry = scene::EntityRegistry::getRegistry();
		auto& lifecycle = resource::AssetLifecycleManager::instance();

		auto view = registry.view<components::UUIDComponent>();
		for (auto entity : view)
		{
			scene::Entity sceneEntity(entity);
			resource::acquireEntityAssets(sceneEntity, lifecycle);
		}
	}

	void AssetLifecycleServiceImpl::update(float deltaTime)
	{
		resource::AssetLifecycleManager::instance().tick(deltaTime);
	}

}
