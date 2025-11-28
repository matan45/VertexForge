#include "SceneGraphSystem.hpp"

namespace scene {
	SceneGraphSystem::SceneGraphSystem() : root{ Entity("Root") }
	{
	}

	entt::entity SceneGraphSystem::addChild(Entity& parent,Entity& child) const
	{
		if (!parent.isValid() || !child.isValid()) {
			vfLogError("Invalid parent or child entity.");
			return entt::null;
		}

		parent.addChildren(child);
		markTransformDirty(child);  // Mark child's transform as dirty

		return child.getHandle();
	}

	void SceneGraphSystem::removeEntity(Entity& entity)
	{
		if (!entity.isValid() || entity == root) {
			vfLogError("Cannot remove the root entity or an invalid entity.");
			return;
		}
		if (!entity.isValid()) {
			vfLogError("Invalid entity.");
			return;
		}

		// Recursively remove children
		for (auto& child : entity.getChildren()) {
			removeEntity(child);
		}

		// Remove from parent's children list
		Entity parent = entity.getParent();
		if (parent.isValid()) {
			parent.removeChildren(entity);
		}

		// Destroy entity in the registry
		EntityRegistry::getRegistry().destroy(entity.getHandle());
	}

	void SceneGraphSystem::moveEntity(Entity& entity, Entity& newParent) const
	{
		if (isDescendant(entity, newParent)) {
			vfLogError("Invalid entity or parent.");
			return;
		}

		// Remove from old parent and add to new parent
		Entity oldParent = entity.getParent();
		if (oldParent.isValid()) {
			oldParent.removeChildren(entity);
		}

		newParent.addChildren(entity);
		markTransformDirty(entity);  // Mark the entity's transform as dirty
	}

	std::vector<scene::Entity> SceneGraphSystem::findAllEntitiesByName(std::string_view name) const
	{
		std::vector<Entity> foundEntities;

		auto view = EntityRegistry::getRegistry().view<components::NameComponent>(); // Create a view of entities with a Name component

		for (auto entityHandle : view) {
			const auto& entityName = view.get<components::NameComponent>(entityHandle).name;
			if (entityName == name) {
				foundEntities.emplace_back(entityHandle); // Wrap the entt::entity handle in an Entity object
			}
		}

		return foundEntities;
	}

	void SceneGraphSystem::updateWorldTransforms()
	{
		glm::mat4 identityMatrix(1.0f); // Start with an identity matrix for the root
		updateChildWorldTransforms(root, identityMatrix); // Begin updating from the root entity
	}

	void SceneGraphSystem::updateCamera() const
	{
		auto view = EntityRegistry::getRegistry().view<components::CameraComponent, components::TransformComponent>();

		for (auto entityHandle : view) {
			auto entity = Entity(entityHandle);
			auto& camera = entity.getComponent<components::CameraComponent>();
			const auto& transform = entity.getComponent<components::TransformComponent>();

			camera.updateViewMatrix(transform.position, transform.rotation);
		}
	}

	void SceneGraphSystem::markTransformDirty(Entity& entity) const
	{
		if (entity.hasComponent<components::TransformComponent>()) {
			auto& transform = entity.getComponent<components::TransformComponent>();
			transform.isDirty = true;
		}
	}

	void SceneGraphSystem::updateChildWorldTransforms(Entity& entity, const glm::mat4& parentWorldTransform)
	{
		auto& transform = entity.getComponent<components::TransformComponent>();

		// Check if the transform or any ancestor's transform is dirty
		if (transform.isDirty) {
			// Calculate the new world transform by combining with the parent's world transform
			glm::mat4 worldMatrix = parentWorldTransform * transform.GetMatrix();

			// Update or replace the WorldTransform component
			entity.addOrReplaceComponent<components::WorldTransformComponent>().worldMatrix = worldMatrix;

			// Mark the transform as clean
			transform.isDirty = false;

			// Recursively update the children
			for (auto& child : entity.getChildren()) {
				updateChildWorldTransforms(child, worldMatrix);
			}
		}
		else {
			// If not dirty, simply propagate the current parent's world transform to children
			for (auto& child : entity.getChildren()) {
				updateChildWorldTransforms(child, parentWorldTransform);
			}
		}
	}

	bool SceneGraphSystem::isDescendant(scene::Entity& parent, scene::Entity& child) const
	{
		if (!parent.isValid() || !child.isValid()) {
			return false;
		}

		// Recursively check if any of the parent's children is the child or one of its descendants
		for (auto& childEntity : parent.getChildren()) {
			if (childEntity == child || isDescendant(childEntity, child)) {
				return true;
			}
		}

		return false;
	}

}