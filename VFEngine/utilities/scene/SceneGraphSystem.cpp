#include "SceneGraphSystem.hpp"
#include "../components/MediaComponents.hpp"
#include "../threading/JobSystem.hpp"

namespace scene {
	SceneGraphSystem::SceneGraphSystem() : root{ Entity("Root") }
	{
	}

	entt::entity SceneGraphSystem::addChild(Entity& parent,Entity& child) const
	{
		if (!parent.isAlive() || !child.isAlive()) {
			vfLogError("addChild: parent or child entity is not alive.");
			return entt::null;
		}

		parent.addChildren(child);
		markTransformDirty(child);

		return child.getHandle();
	}

	void SceneGraphSystem::removeEntity(Entity& entity)
	{
		if (!entity.isAlive() || entity == root) {
			vfLogError("Cannot remove the root entity or an invalid entity.");
			return;
		}

		auto children = entity.getChildren();
		for (auto& child : children) {
			if (child.isAlive()) {
				removeEntity(child);
			}
		}

		Entity parent = entity.getParent();
		if (parent.isAlive()) {
			parent.removeChildren(entity);
		}

		EntityRegistry::getRegistry().destroy(entity.getHandle());
	}

	void SceneGraphSystem::clearScene()
	{
		if (!root.isValid()) {
			vfLogError("Root entity is invalid, cannot clear scene.");
			return;
		}

		auto children = root.getChildren();
		for (auto& child : children) {
			removeEntity(child);
		}

		root.removeAllOptionalComponents();

		vfLogInfo("Scene cleared successfully.");
	}

	void SceneGraphSystem::moveEntity(Entity& entity, Entity& newParent) const
	{
		if (isDescendant(entity, newParent)) {
			vfLogError("Invalid entity or parent.");
			return;
		}

		Entity oldParent = entity.getParent();
		if (oldParent.isValid()) {
			oldParent.removeChildren(entity);
		}

		newParent.addChildren(entity);
		markTransformDirty(entity);
	}

	std::vector<scene::Entity> SceneGraphSystem::findAllEntitiesByName(std::string_view name) const
	{
		std::vector<Entity> foundEntities;

		auto view = EntityRegistry::getRegistry().view<components::NameComponent>();

		for (auto entityHandle : view) {
			const auto& entityName = view.get<components::NameComponent>(entityHandle).name;
			if (entityName == name) {
				foundEntities.emplace_back(entityHandle);
			}
		}

		return foundEntities;
	}

	void SceneGraphSystem::updateWorldTransforms()
	{
		if (!root.isAlive()) {
			return;
		}
		glm::mat4 identityMatrix(1.0f);

		// Parallelize top-level children: each subtree is independent
		auto children = root.getChildren();
		if (children.size() > 1)
		{
			std::vector<std::future<void>> futures;
			futures.reserve(children.size());

			for (auto& child : children)
			{
				if (child.isAlive())
				{
					futures.push_back(threading::JobSystem::instance().submit(
						[this, child, identityMatrix]() mutable
						{
							updateChildWorldTransforms(child, identityMatrix);
						}, threading::JobPriority::HIGH
					));
				}
			}

			for (auto& f : futures)
			{
				f.get();
			}
		}
		else
		{
			updateChildWorldTransforms(root, identityMatrix);
		}
	}

	void SceneGraphSystem::updateCamera() const
	{
		auto view = EntityRegistry::getRegistry().view<components::CameraComponent, components::TransformComponent>();

		for (auto entityHandle : view) {
			auto entity = Entity(entityHandle);
			auto& camera = entity.getComponent<components::CameraComponent>();

			if (entity.hasComponent<components::WorldTransformComponent>()) {
				const auto& worldTransform = entity.getComponent<components::WorldTransformComponent>();
				camera.updateViewMatrixFromWorld(worldTransform.worldMatrix);
			} else {
				const auto& transform = entity.getComponent<components::TransformComponent>();
				camera.updateViewMatrix(transform.position, transform.rotation);
			}
		}
	}

	void SceneGraphSystem::markTransformDirty(Entity& entity) const
	{
		markTransformDirtyRecursive(entity);
	}

	void SceneGraphSystem::markTransformDirtyRecursive(Entity& entity) const
	{
		if (!entity.isAlive()) return;
		if (entity.hasComponent<components::TransformComponent>()) {
			entity.getComponent<components::TransformComponent>().isDirty = true;
		}
		for (auto& child : entity.getChildren()) {
			if (child.isAlive()) {
				markTransformDirtyRecursive(child);
			}
		}
	}

	void SceneGraphSystem::updateChildWorldTransforms(Entity& entity, const glm::mat4& parentWorldTransform)
	{
		if (!entity.isAlive()) {
			return;
		}
		if (!entity.hasComponent<components::TransformComponent>()) {
			return;
		}

		// Skip entities whose transform is driven by the socket attachment system
		if (entity.hasComponent<components::SocketAttachmentComponent>()) {
			const auto& attachment = entity.getComponent<components::SocketAttachmentComponent>();
			if (attachment.isActive && attachment.parentEntity != entt::null) {
				glm::mat4 worldMatrix = parentWorldTransform;
				if (entity.hasComponent<components::WorldTransformComponent>()) {
					worldMatrix = entity.getComponent<components::WorldTransformComponent>().worldMatrix;
				}
				for (auto& child : entity.getChildren()) {
					if (child.isAlive()) {
						updateChildWorldTransforms(child, worldMatrix);
					}
				}
				return;
			}
		}

		auto& transform = entity.getComponent<components::TransformComponent>();
		glm::mat4 worldMatrix;

		if (transform.isDirty) {
			worldMatrix = parentWorldTransform * transform.getMatrix();
			entity.addOrReplaceComponent<components::WorldTransformComponent>().worldMatrix = worldMatrix;
			transform.isDirty = false;
		}
		else if (entity.hasComponent<components::WorldTransformComponent>()) {
			worldMatrix = entity.getComponent<components::WorldTransformComponent>().worldMatrix;
		}
		else {
			worldMatrix = parentWorldTransform * transform.getMatrix();
			entity.addOrReplaceComponent<components::WorldTransformComponent>().worldMatrix = worldMatrix;
		}

		for (auto& child : entity.getChildren()) {
			if (child.isAlive()) {
				updateChildWorldTransforms(child, worldMatrix);
			}
		}
	}

	bool SceneGraphSystem::isDescendant(scene::Entity& parent, scene::Entity& child) const
	{
		if (!parent.isValid() || !child.isValid()) {
			return false;
		}

		for (auto& childEntity : parent.getChildren()) {
			if (childEntity == child || isDescendant(childEntity, child)) {
				return true;
			}
		}

		return false;
	}

}
