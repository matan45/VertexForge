#include "EditorTextureController.hpp"
#include "TextureController.hpp"
#include "resource/Types.hpp"

namespace controllers {

	std::unique_ptr<dto::EditorTexture> EditorTextureController::loadTexture(std::string_view path)
	{
		auto texture = TextureController::createTexture();
		texture->loadTextureFromFile(path);
		return std::make_unique<dto::EditorTexture>(std::move(texture));
	}

	std::unique_ptr<dto::EditorTexture> EditorTextureController::loadTextureFromData(const resource::TextureData& textureData)
	{
		auto texture = TextureController::createTexture();
		texture->loadTextureFromData(textureData);
		return std::make_unique<dto::EditorTexture>(std::move(texture));
	}

	std::unique_ptr<dto::EditorTexture> EditorTextureController::loadHdrTextureFromData(const resource::HDRData& hdrData)
	{
		auto texture = TextureController::createTexture();
		texture->loadHDRFromData(hdrData, true);
		return std::make_unique<dto::EditorTexture>(std::move(texture));
	}
}
