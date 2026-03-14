#pragma once

#include "AssetLifecycleManager.hpp"
#include "AssetTypes.hpp"
#include "../scene/Entity.hpp"
#include "../components/Components.hpp"

namespace resource {

	inline void acquireEntityAssets(scene::Entity& entity, AssetLifecycleManager& lifecycle)
	{
		if (entity.hasComponent<components::MeshComponent>())
		{
			const auto& mesh = entity.getComponent<components::MeshComponent>();
			if (!mesh.meshPath.empty()) lifecycle.acquire(mesh.meshPath, AssetType::Mesh);
			if (!mesh.animatorPath.empty()) lifecycle.acquire(mesh.animatorPath, AssetType::Animator);
		}

		if (entity.hasComponent<components::MaterialComponent>())
		{
			const auto& mat = entity.getComponent<components::MaterialComponent>();
			if (!mat.defaultMaterial.empty()) lifecycle.acquire(mat.defaultMaterial, AssetType::Material);
			for (const auto& [name, path] : mat.subMeshMaterials)
			{
				if (!path.empty()) lifecycle.acquire(path, AssetType::Material);
			}
		}

		if (entity.hasComponent<components::AudioSource2DComponent>())
		{
			const auto& audio = entity.getComponent<components::AudioSource2DComponent>();
			if (!audio.audioFilePath.empty()) lifecycle.acquire(audio.audioFilePath, AssetType::Audio);
		}

		if (entity.hasComponent<components::AudioSource3DComponent>())
		{
			const auto& audio = entity.getComponent<components::AudioSource3DComponent>();
			if (!audio.audioFilePath.empty()) lifecycle.acquire(audio.audioFilePath, AssetType::Audio);
		}

		if (entity.hasComponent<components::VFXComponent>())
		{
			const auto& vfx = entity.getComponent<components::VFXComponent>();
			if (!vfx.vfxPath.empty()) lifecycle.acquire(vfx.vfxPath, AssetType::VFX);
		}

		if (entity.hasComponent<components::AnimatorComponent>())
		{
			const auto& anim = entity.getComponent<components::AnimatorComponent>();
			if (!anim.animatorPath.empty()) lifecycle.acquire(anim.animatorPath, AssetType::Animator);
		}

		if (entity.hasComponent<components::DecalComponent>())
		{
			const auto& decal = entity.getComponent<components::DecalComponent>();
			if (!decal.albedoTexture.empty()) lifecycle.acquire(decal.albedoTexture, AssetType::Texture);
			if (!decal.normalTexture.empty()) lifecycle.acquire(decal.normalTexture, AssetType::Texture);
			if (!decal.ormTexture.empty()) lifecycle.acquire(decal.ormTexture, AssetType::Texture);
		}
	}

	inline void releaseEntityAssets(scene::Entity& entity, AssetLifecycleManager& lifecycle)
	{
		if (entity.hasComponent<components::MeshComponent>())
		{
			const auto& mesh = entity.getComponent<components::MeshComponent>();
			if (!mesh.meshPath.empty()) lifecycle.release(mesh.meshPath);
			if (!mesh.animatorPath.empty()) lifecycle.release(mesh.animatorPath);
		}

		if (entity.hasComponent<components::MaterialComponent>())
		{
			const auto& mat = entity.getComponent<components::MaterialComponent>();
			if (!mat.defaultMaterial.empty()) lifecycle.release(mat.defaultMaterial);
			for (const auto& [name, path] : mat.subMeshMaterials)
			{
				if (!path.empty()) lifecycle.release(path);
			}
		}

		if (entity.hasComponent<components::AudioSource2DComponent>())
		{
			const auto& audio = entity.getComponent<components::AudioSource2DComponent>();
			if (!audio.audioFilePath.empty()) lifecycle.release(audio.audioFilePath);
		}

		if (entity.hasComponent<components::AudioSource3DComponent>())
		{
			const auto& audio = entity.getComponent<components::AudioSource3DComponent>();
			if (!audio.audioFilePath.empty()) lifecycle.release(audio.audioFilePath);
		}

		if (entity.hasComponent<components::VFXComponent>())
		{
			const auto& vfx = entity.getComponent<components::VFXComponent>();
			if (!vfx.vfxPath.empty()) lifecycle.release(vfx.vfxPath);
		}

		if (entity.hasComponent<components::AnimatorComponent>())
		{
			const auto& anim = entity.getComponent<components::AnimatorComponent>();
			if (!anim.animatorPath.empty()) lifecycle.release(anim.animatorPath);
		}

		if (entity.hasComponent<components::DecalComponent>())
		{
			const auto& decal = entity.getComponent<components::DecalComponent>();
			if (!decal.albedoTexture.empty()) lifecycle.release(decal.albedoTexture);
			if (!decal.normalTexture.empty()) lifecycle.release(decal.normalTexture);
			if (!decal.ormTexture.empty()) lifecycle.release(decal.ormTexture);
		}
	}

}
