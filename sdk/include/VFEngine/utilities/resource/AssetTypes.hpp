#pragma once
#include "../asset/AssetGUID.hpp"
#include <cstdint>
#include <string>
#include <chrono>

namespace resource {

	enum class AssetType : uint8_t
	{
		Texture = 0,
		Mesh = 1,
		Audio = 2,
		Animation = 3,
		Animator = 4,
		Material = 5,
		MaterialInstance = 6,
		PhysicsShape = 7,
		VFX = 8,
		Script = 9,
		HDR = 10,
		Font = 11,
		Skeleton = 12,
		Navmesh = 13,
		InputMapping = 14,
		Terrain = 15,
		TerrainMaterial = 16,
		BehaviorTree = 17,
		World = 18,
		Scene = 19,
		Theme = 20,
		Prefab = 21,
		HumanoidRig = 22,
		RetargetMap = 23,
		COUNT
	};

	inline const char* assetTypeName(AssetType type) {
		switch (type) {
		case AssetType::Texture:          return "Texture";
		case AssetType::Mesh:             return "Mesh";
		case AssetType::Audio:            return "Audio";
		case AssetType::Animation:        return "Animation";
		case AssetType::Animator:         return "Animator";
		case AssetType::Material:         return "Material";
		case AssetType::MaterialInstance:  return "MaterialInstance";
		case AssetType::PhysicsShape:     return "PhysicsShape";
		case AssetType::VFX:              return "VFX";
		case AssetType::Script:           return "Script";
		case AssetType::HDR:              return "HDR";
		case AssetType::Font:             return "Font";
		case AssetType::Skeleton:         return "Skeleton";
		case AssetType::Navmesh:          return "Navmesh";
		case AssetType::InputMapping:     return "InputMapping";
		case AssetType::Terrain:          return "Terrain";
		case AssetType::TerrainMaterial:  return "TerrainMaterial";
		case AssetType::BehaviorTree:     return "BehaviorTree";
		case AssetType::World:            return "World";
		case AssetType::Scene:            return "Scene";
		case AssetType::Theme:            return "Theme";
		case AssetType::Prefab:           return "Prefab";
		case AssetType::HumanoidRig:      return "HumanoidRig";
		case AssetType::RetargetMap:      return "RetargetMap";
		default:                          return "Unknown";
		}
	}

	enum class AssetState : uint8_t
	{
		Active,
		PendingRelease,
		Released
	};

	struct AssetEntry
	{
		asset::AssetGUID guid;
		AssetType type = AssetType::Texture;
		AssetState state = AssetState::Active;
		uint32_t refCount = 0;
		size_t estimatedMemoryBytes = 0;
		float graceTimeRemaining = 0.0f;
	};

}
