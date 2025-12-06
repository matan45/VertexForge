#include "EditorTexture.hpp"
#include "TextureController.hpp"

namespace dto
{
    EditorTexture::EditorTexture(std::unique_ptr<core::Texture> texture): texture{std::move(texture)}
    {
        width = this->texture->getImageData().width;
        height = this->texture->getImageData().height;
        numbersOfChannels = this->texture->getImageData().numbersOfChannels;
    }

    EditorTexture::~EditorTexture() = default;

    void* dto::EditorTexture::getDescriptorSet() const
    {
        return texture->getDescriptorSet();
    }
}
