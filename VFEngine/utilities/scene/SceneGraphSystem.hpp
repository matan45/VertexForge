#pragma once
#include "Entity.hpp"
#include "../types/PhysicsTypes.hpp"
#include "../types/AudioTypes.hpp"
#include "../types/RenderSettings.hpp"

namespace scene {

	class SceneGraphSystem
	{
	private:
		Entity root;
		types::PhysicsSettings physicsSettings = types::PhysicsSettings::createDefault();
		types::AudioSettings audioSettings = types::AudioSettings::createDefault();
		types::RenderSettings renderSettings = types::RenderSettings::createDefault();
	public:
		explicit SceneGraphSystem();
		~SceneGraphSystem() = default;

		entt::entity addChild(Entity& parent,Entity& child) const;
		void removeEntity(Entity& entity);
		void clearScene();

		void moveEntity(Entity& entity, Entity& newParent) const;

		std::vector<Entity> findAllEntitiesByName(std::string_view name) const;

		void updateWorldTransforms();
		void updateCamera() const;
		void markTransformDirty(Entity& entity) const;

		Entity& GetRoot() {
			return root;
		}

		types::PhysicsSettings& getPhysicsSettings() {
			return physicsSettings;
		}

		const types::PhysicsSettings& getPhysicsSettings() const {
			return physicsSettings;
		}

		void setPhysicsSettings(const types::PhysicsSettings& settings) {
			physicsSettings = settings;
		}

		types::AudioSettings& getAudioSettings() {
			return audioSettings;
		}

		const types::AudioSettings& getAudioSettings() const {
			return audioSettings;
		}

		void setAudioSettings(const types::AudioSettings& settings) {
			audioSettings = settings;
		}

		types::RenderSettings& getRenderSettings() {
			return renderSettings;
		}

		const types::RenderSettings& getRenderSettings() const {
			return renderSettings;
		}

		void setRenderSettings(const types::RenderSettings& settings) {
			renderSettings = settings;
		}

	private:
		void markTransformDirtyRecursive(Entity& entity) const;
		void updateChildWorldTransforms(Entity& parent, const glm::mat4& parentWorldTransform);
		bool isDescendant(scene::Entity& parent, scene::Entity& child) const;
	};

}
