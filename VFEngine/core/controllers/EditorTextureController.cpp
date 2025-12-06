#include "EditorTextureController.hpp"
#include "TextureController.hpp"

namespace controllers {

	std::unique_ptr<dto::EditorTexture> EditorTextureController::loadTexture(std::string_view path)
	{
		auto texture = TextureController::createTexture();
		texture->loadTextureFromFile(path);
		return std::make_unique<dto::EditorTexture>(std::move(texture));
	}

	std::unique_ptr<dto::EditorTexture> EditorTextureController::loadHdrTexture(std::string_view path)
	{
		auto texture = TextureController::createTexture();
		texture->loadHDRFromFile(path);
		return std::make_unique<dto::EditorTexture>(std::move(texture));
	}
}
