#pragma once
#include "EntityRegistry.hpp"
#include "../components/Components.hpp"
#include "../print/Log.hpp"

#include <entt/entt.hpp>
#include <string>
#include <vector>


namespace scene {
	class Entity
	{
	private:
		entt::entity entityHandle{ entt::null };  // Entity handle

	public:
		explicit Entity(entt::entity handle)
			: entityHandle(handle) {}

		explicit Entity(const std::string& name)
		{
			entityHandle = EntityRegistry::getRegistry().create();
			addComponent<components::UUIDComponent>();     // Generate unique ID
			addComponent<components::NameComponent>(name); // Set the name on creation
			addComponent<components::TransformComponent>();
		}

		// Default constructor creates an invalid entity
		Entity() = default;
		Entity(const Entity& other) = default;  // Let the compiler generate copy constructor
		Entity(Entity&&) = default;             // Let the compiler generate move constructor
		Entity& operator=(const Entity& other) = default; // Let the compiler generate copy assignment
		Entity& operator=(Entity&& other) = default;      // Let the compiler generate move assignment
		~Entity() = default;

		// Add a component to the entity
		template<typename T, typename... Args>
		T& addComponent(Args&&... args) {
			if (!isAlive()) {
				vfLogError("Trying to add a component to an invalid entity.");
			}
			T& component = EntityRegistry::getRegistry().emplace<T>(entityHandle, std::forward<Args>(args)...);
			return component;
		}

		// Add or replace a component
		template<typename T, typename... Args>
		T& addOrReplaceComponent(Args&&... args) {
			if (!isAlive()) {
				vfLogError("Trying to add or replace a component in an invalid entity.");
			}
			T& component = EntityRegistry::getRegistry().emplace_or_replace<T>(entityHandle, std::forward<Args>(args)...);
			return component;
		}

		// Get a component
		template<typename T>
		T& getComponent() {
			if (!hasComponent<T>()) {
				vfLogError("Entity does not have the requested component.");
			}
			return EntityRegistry::getRegistry().get<T>(entityHandle);
		}

		// Get a component (const version)
		template<typename T>
		const T& getComponent() const {
			if (!hasComponent<T>()) {
				vfLogError("Entity does not have the requested component.");
			}
			return EntityRegistry::getRegistry().get<T>(entityHandle);
		}

		// Check if the entity has a specific component
		template<typename T>
		bool hasComponent() const {
			if (!isAlive()) {
				return false;
			}
			return EntityRegistry::getRegistry().all_of<T>(entityHandle);
		}

		// Remove a component
		template<typename T>
		void removeComponent() {
			if (!isAlive()) {
				vfLogError("Trying to remove a component from an invalid entity.");
				return;
			}
			EntityRegistry::getRegistry().remove<T>(entityHandle);
		}

		// Check if the entity handle is non-null (does NOT check registry liveness)
		bool isValid() const {
			return entityHandle != entt::null;
		}

		// Check if the entity is alive in the registry (safe to access components)
		bool isAlive() const {
			return entityHandle != entt::null && EntityRegistry::getRegistry().valid(entityHandle);
		}

		// Get the entity handle
		entt::entity getHandle() const {
			return entityHandle;
		}

		// Entity comparison operators
		bool operator==(const Entity& other) const {
			return entityHandle == other.entityHandle;
		}

		bool operator!=(const Entity& other) const {
			return !(*this == other);
		}

		// Get the entity name
		std::string getName() const {
			if (hasComponent<components::NameComponent>()) {
				return getComponent<components::NameComponent>().name; // Access the 'name' field
			}
			else {
				return "Unnamed Entity"; // Return a default name if the entity doesn't have a Name component
			}
		}

		// Set the entity name
		void setName(std::string_view newName) {
			addOrReplaceComponent<components::NameComponent>(std::string(newName));
		}

		// Get the entity UUID
		uuid::UUID getUUID() const {
			if (hasComponent<components::UUIDComponent>()) {
				const auto& component = getComponent<components::UUIDComponent>();
				return component.id;
			}
			return uuid::UUID::invalid();
		}

		// Add camera component with billboard icon
		components::CameraComponent& addCameraComponent() {
			auto& camera = addOrReplaceComponent<components::CameraComponent>();
			auto& billboard = addOrReplaceComponent<components::BillboardComponent>();
			billboard.iconType = components::BillboardIconType::Camera;
			return camera;
		}

		// Add a child entity
		void addChildren(Entity& child) {
			// Add to the current entity's ChildrenComponent
			if (hasComponent<components::ChildrenComponent>()) {
				auto& childrenComponent = getComponent<components::ChildrenComponent>();
				childrenComponent.children.push_back(child.getHandle());
			}
			else
			{
				auto& childrenComponent = addOrReplaceComponent<components::ChildrenComponent>();
				childrenComponent.children.push_back(child.getHandle());
			}

			child.addOrReplaceComponent<components::ParentComponent>().parent = this->entityHandle;
		}

		// Remove a child entity
		void removeChildren(Entity& child) {
			if (!hasComponent<components::ChildrenComponent>()) {
				vfLogError("This entity does not have any children to remove.");
				return;
			}

			auto& childrenComponent = getComponent<components::ChildrenComponent>();
			auto new_end = std::remove(childrenComponent.children.begin(), childrenComponent.children.end(), child.getHandle());
			childrenComponent.children.erase(new_end, childrenComponent.children.end());

			// Remove the child's ParentComponent
			if (child.hasComponent<components::ParentComponent>()) {
				child.removeComponent<components::ParentComponent>();
			}
		}

		// Get parent entity (returns invalid entity if parent was destroyed)
		Entity getParent() {
			if (hasComponent<components::ParentComponent>()) {
				entt::entity parentHandle = getComponent<components::ParentComponent>().parent;
				if (EntityRegistry::getRegistry().valid(parentHandle)) {
					return Entity(parentHandle);
				}
			}
			return Entity(); // Return an invalid entity if no parent exists
		}

		// Get children entities (filters out destroyed entities)
		std::vector<Entity> getChildren() const {
			std::vector<Entity> childEntities;

			if (hasComponent<components::ChildrenComponent>()) {
				auto& registry = EntityRegistry::getRegistry();
				const auto& childrenHandles = getComponent<components::ChildrenComponent>().children;
				for (const auto& childHandle : childrenHandles) {
					if (registry.valid(childHandle)) {
						childEntities.emplace_back(childHandle);
					}
				}
			}

			return childEntities;
		}

		void removeAllOptionalComponents() {
			if (!isAlive()) {
				vfLogError("Trying to remove components from an invalid entity.");
				return;
			}

			removeOptionalComponentsImpl(components::OptionalComponents{});
		}

	private:
		template<typename... Ts>
		void removeOptionalComponentsImpl(entt::type_list<Ts...>) {
			(tryRemoveComponent<Ts>(), ...);
		}

		template<typename T>
		void tryRemoveComponent() {
			if (hasComponent<T>()) {
				removeComponent<T>();
			}
		}
	};
}

