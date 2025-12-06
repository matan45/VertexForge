#pragma once
#include <string>
#include <memory>
#include "texture/EditorTexture.hpp"

namespace controllers {
	class EditorTextureController
	{
	public:
		static std::unique_ptr<dto::EditorTexture> loadTexture(std::string_view path);
		static std::unique_ptr<dto::EditorTexture> loadHdrTexture(std::string_view path);
	};
}


