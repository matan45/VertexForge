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
		VFXSequence = 24,
		// Generic sentinel for plugin-registered asset types (VK-1449). The
		// precise identity is a string typeId carried alongside (see
		// asset::AssetTypeRegistry / AssetMetadata::pluginTypeId); all plugin
		// asset types funnel through this single enum value. Appended before
		// COUNT — both persistence paths store AssetType by name, never by the
		// raw integer, so the renumbered COUNT is backward-compatible.
		PluginAsset = 25,
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
		case AssetType::VFXSequence:      return "VFXSequence";
		case AssetType::PluginAsset:      return "PluginAsset";
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
