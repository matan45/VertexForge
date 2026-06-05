#pragma once
#include <string>
#include <memory>
#include "texture/EditorTexture.hpp"

namespace resource
{
	struct TextureData;
	struct HDRData;
}

namespace controllers {
	class EditorTextureController
	{
	public:
		// Sync loading (for small UI resources like icon atlases)
		static std::unique_ptr<dto::EditorTexture> loadTexture(std::string_view path);

		// Load from pre-loaded data (for async loading)
		static std::unique_ptr<dto::EditorTexture> loadTextureFromData(const resource::TextureData& textureData);
		static std::unique_ptr<dto::EditorTexture> loadHdrTextureFromData(const resource::HDRData& hdrData);
	};
}


