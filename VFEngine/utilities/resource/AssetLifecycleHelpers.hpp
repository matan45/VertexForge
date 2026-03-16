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
			if (mesh.meshRef.isValid()) lifecycle.acquire(mesh.meshRef.getGUID(), AssetType::Mesh);
			if (mesh.animatorRef.isValid()) lifecycle.acquire(mesh.animatorRef.getGUID(), AssetType::Animator);
		}

		if (entity.hasComponent<components::MaterialComponent>())
		{
			const auto& mat = entity.getComponent<components::MaterialComponent>();
			if (mat.defaultMaterialRef.isValid()) lifecycle.acquire(mat.defaultMaterialRef.getGUID(), AssetType::Material);
			for (const auto& [name, ref] : mat.subMeshMaterials)
			{
				if (ref.isValid()) lifecycle.acquire(ref.getGUID(), AssetType::Material);
			}
		}

		if (entity.hasComponent<components::AudioSource2DComponent>())
		{
			const auto& audio = entity.getComponent<components::AudioSource2DComponent>();
			if (audio.audioRef.isValid()) lifecycle.acquire(audio.audioRef.getGUID(), AssetType::Audio);
		}

		if (entity.hasComponent<components::AudioSource3DComponent>())
		{
			const auto& audio = entity.getComponent<components::AudioSource3DComponent>();
			if (audio.audioRef.isValid()) lifecycle.acquire(audio.audioRef.getGUID(), AssetType::Audio);
		}

		if (entity.hasComponent<components::VFXComponent>())
		{
			const auto& vfx = entity.getComponent<components::VFXComponent>();
			if (vfx.vfxRef.isValid()) lifecycle.acquire(vfx.vfxRef.getGUID(), AssetType::VFX);
		}

		if (entity.hasComponent<components::AnimatorComponent>())
		{
			const auto& anim = entity.getComponent<components::AnimatorComponent>();
			if (anim.animatorRef.isValid()) lifecycle.acquire(anim.animatorRef.getGUID(), AssetType::Animator);
		}

		if (entity.hasComponent<components::DecalComponent>())
		{
			const auto& decal = entity.getComponent<components::DecalComponent>();
			if (decal.albedoTextureRef.isValid()) lifecycle.acquire(decal.albedoTextureRef.getGUID(), AssetType::Texture);
			if (decal.normalTextureRef.isValid()) lifecycle.acquire(decal.normalTextureRef.getGUID(), AssetType::Texture);
			if (decal.ormTextureRef.isValid()) lifecycle.acquire(decal.ormTextureRef.getGUID(), AssetType::Texture);
		}
	}

	inline void releaseEntityAssets(scene::Entity& entity, AssetLifecycleManager& lifecycle)
	{
		if (entity.hasComponent<components::MeshComponent>())
		{
			const auto& mesh = entity.getComponent<components::MeshComponent>();
			if (mesh.meshRef.isValid()) lifecycle.release(mesh.meshRef.getGUID());
			if (mesh.animatorRef.isValid()) lifecycle.release(mesh.animatorRef.getGUID());
		}

		if (entity.hasComponent<components::MaterialComponent>())
		{
			const auto& mat = entity.getComponent<components::MaterialComponent>();
			if (mat.defaultMaterialRef.isValid()) lifecycle.release(mat.defaultMaterialRef.getGUID());
			for (const auto& [name, ref] : mat.subMeshMaterials)
			{
				if (ref.isValid()) lifecycle.release(ref.getGUID());
			}
		}

		if (entity.hasComponent<components::AudioSource2DComponent>())
		{
			const auto& audio = entity.getComponent<components::AudioSource2DComponent>();
			if (audio.audioRef.isValid()) lifecycle.release(audio.audioRef.getGUID());
		}

		if (entity.hasComponent<components::AudioSource3DComponent>())
		{
			const auto& audio = entity.getComponent<components::AudioSource3DComponent>();
			if (audio.audioRef.isValid()) lifecycle.release(audio.audioRef.getGUID());
		}

		if (entity.hasComponent<components::VFXComponent>())
		{
			const auto& vfx = entity.getComponent<components::VFXComponent>();
			if (vfx.vfxRef.isValid()) lifecycle.release(vfx.vfxRef.getGUID());
		}

		if (entity.hasComponent<components::AnimatorComponent>())
		{
			const auto& anim = entity.getComponent<components::AnimatorComponent>();
			if (anim.animatorRef.isValid()) lifecycle.release(anim.animatorRef.getGUID());
		}

		if (entity.hasComponent<components::DecalComponent>())
		{
			const auto& decal = entity.getComponent<components::DecalComponent>();
			if (decal.albedoTextureRef.isValid()) lifecycle.release(decal.albedoTextureRef.getGUID());
			if (decal.normalTextureRef.isValid()) lifecycle.release(decal.normalTextureRef.getGUID());
			if (decal.ormTextureRef.isValid()) lifecycle.release(decal.ormTextureRef.getGUID());
		}
	}

}
