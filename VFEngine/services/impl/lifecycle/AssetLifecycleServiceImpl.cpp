#include "AssetLifecycleServiceImpl.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/lifecycle/AssetLifecycleEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"

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
			[](const std::string& path, resource::AssetType type)
			{
				events::lifecycle::AssetReleaseReadyNotification notification;
				notification.path = path;
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
				resource::AssetLifecycleManager::instance().forceRelease(cmd.path);
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
				return resource::AssetLifecycleManager::instance().getAssetEntry(query.path);
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

				if (entity.hasComponent<components::MeshComponent>())
				{
					auto& mesh = entity.getComponent<components::MeshComponent>();
					if (!mesh.meshPath.empty()) lifecycle.release(mesh.meshPath);
					if (!mesh.animatorPath.empty()) lifecycle.release(mesh.animatorPath);
				}

				if (entity.hasComponent<components::MaterialComponent>())
				{
					auto& mat = entity.getComponent<components::MaterialComponent>();
					if (!mat.defaultMaterial.empty()) lifecycle.release(mat.defaultMaterial);
					for (const auto& [name, path] : mat.subMeshMaterials)
					{
						if (!path.empty()) lifecycle.release(path);
					}
				}

				if (entity.hasComponent<components::AudioSource2DComponent>())
				{
					auto& audio = entity.getComponent<components::AudioSource2DComponent>();
					if (!audio.audioFilePath.empty()) lifecycle.release(audio.audioFilePath);
				}

				if (entity.hasComponent<components::AudioSource3DComponent>())
				{
					auto& audio = entity.getComponent<components::AudioSource3DComponent>();
					if (!audio.audioFilePath.empty()) lifecycle.release(audio.audioFilePath);
				}

				if (entity.hasComponent<components::VFXComponent>())
				{
					auto& vfx = entity.getComponent<components::VFXComponent>();
					if (!vfx.vfxPath.empty()) lifecycle.release(vfx.vfxPath);
				}

				if (entity.hasComponent<components::AnimatorComponent>())
				{
					auto& anim = entity.getComponent<components::AnimatorComponent>();
					if (!anim.animatorPath.empty()) lifecycle.release(anim.animatorPath);
				}
			});
	}

	void AssetLifecycleServiceImpl::acquireAllEntityAssets()
	{
		auto& registry = scene::EntityRegistry::getRegistry();
		auto& lifecycle = resource::AssetLifecycleManager::instance();

		auto meshView = registry.view<components::MeshComponent>();
		for (auto entity : meshView)
		{
			auto& mesh = meshView.get<components::MeshComponent>(entity);
			if (!mesh.meshPath.empty()) lifecycle.acquire(mesh.meshPath, resource::AssetType::Mesh);
			if (!mesh.animatorPath.empty()) lifecycle.acquire(mesh.animatorPath, resource::AssetType::Animator);
		}

		auto matView = registry.view<components::MaterialComponent>();
		for (auto entity : matView)
		{
			auto& mat = matView.get<components::MaterialComponent>(entity);
			if (!mat.defaultMaterial.empty()) lifecycle.acquire(mat.defaultMaterial, resource::AssetType::Material);
			for (const auto& [name, path] : mat.subMeshMaterials)
			{
				if (!path.empty()) lifecycle.acquire(path, resource::AssetType::Material);
			}
		}

		auto audio2dView = registry.view<components::AudioSource2DComponent>();
		for (auto entity : audio2dView)
		{
			auto& audio = audio2dView.get<components::AudioSource2DComponent>(entity);
			if (!audio.audioFilePath.empty()) lifecycle.acquire(audio.audioFilePath, resource::AssetType::Audio);
		}

		auto audio3dView = registry.view<components::AudioSource3DComponent>();
		for (auto entity : audio3dView)
		{
			auto& audio = audio3dView.get<components::AudioSource3DComponent>(entity);
			if (!audio.audioFilePath.empty()) lifecycle.acquire(audio.audioFilePath, resource::AssetType::Audio);
		}

		auto vfxView = registry.view<components::VFXComponent>();
		for (auto entity : vfxView)
		{
			auto& vfx = vfxView.get<components::VFXComponent>(entity);
			if (!vfx.vfxPath.empty()) lifecycle.acquire(vfx.vfxPath, resource::AssetType::VFX);
		}

		auto animView = registry.view<components::AnimatorComponent>();
		for (auto entity : animView)
		{
			auto& anim = animView.get<components::AnimatorComponent>(entity);
			if (!anim.animatorPath.empty()) lifecycle.acquire(anim.animatorPath, resource::AssetType::Animator);
		}
	}

	void AssetLifecycleServiceImpl::update(float deltaTime)
	{
		resource::AssetLifecycleManager::instance().tick(deltaTime);
	}

}
