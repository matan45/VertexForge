#pragma once
#include "EntityRegistry.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"

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
			if (!isValid()) {
				vfLogError("Trying to add a component to an invalid entity.");
			}
			T& component = EntityRegistry::getRegistry().emplace<T>(entityHandle, std::forward<Args>(args)...);
			return component;
		}

		// Add or replace a component
		template<typename T, typename... Args>
		T& addOrReplaceComponent(Args&&... args) {
			if (!isValid()) {
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

		// Check if the entity has a specific component
		template<typename T>
		bool hasComponent() const {
			return EntityRegistry::getRegistry().all_of<T>(entityHandle);
		}

		// Remove a component
		template<typename T>
		void removeComponent() {
			if (!isValid()) {
				vfLogError("Trying to remove a component from an invalid entity.");
			}
			EntityRegistry::getRegistry().remove<T>(entityHandle);
		}

		// Check if the entity is valid (i.e., not null)
		bool isValid() const {
			return entityHandle != entt::null;
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
		std::string getName() {
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

		// Get parent entity
		Entity getParent() {
			if (hasComponent<components::ParentComponent>()) {
				entt::entity parentHandle = getComponent<components::ParentComponent>().parent;
				return Entity(parentHandle);
			}
			return Entity(); // Return an invalid entity if no parent exists
		}

		// Get children entities
		std::vector<Entity> getChildren() {
			std::vector<Entity> childEntities;

			if (hasComponent<components::ChildrenComponent>()) {
				auto& childrenHandles = getComponent<components::ChildrenComponent>().children;
				for (auto& childHandle : childrenHandles) {
					childEntities.emplace_back(childHandle);
				}
			}

			return childEntities;
		}
		
		void removeAllOptionalComponents() {
			if (!isValid()) {
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


