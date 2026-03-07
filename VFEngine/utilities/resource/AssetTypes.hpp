#pragma once
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
		std::string path;
		AssetType type = AssetType::Texture;
		AssetState state = AssetState::Active;
		uint32_t refCount = 0;
		size_t estimatedMemoryBytes = 0;
		float graceTimeRemaining = 0.0f;
	};

}
