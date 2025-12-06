#pragma once

#include "../core/Texture.hpp"
#include <memory>

namespace controllers {
	class TextureController
	{
	public:
		static std::unique_ptr<core::Texture> createTexture();
	};
}


