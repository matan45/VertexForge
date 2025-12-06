#include "TextureController.hpp"
#include "../core/VulkanContext.hpp"

namespace controllers {

	std::unique_ptr<core::Texture> TextureController::createTexture()
	{
		return std::make_unique<core::Texture>(*core::VulkanContext::getDevice());
	}
}
