#include "EditorTexture.hpp"
#include "TextureController.hpp"

namespace dto
{
    EditorTexture::EditorTexture(std::unique_ptr<core::Texture> texture): texture{std::move(texture)}
    {
        width = this->texture->getImageData().width;
        height = this->texture->getImageData().height;
        numbersOfChannels = this->texture->getImageData().numbersOfChannels;
        mipLevels = this->texture->getImageData().mipLevels;
    }

    EditorTexture::~EditorTexture() = default;

    void* dto::EditorTexture::getDescriptorSet() const
    {
        return texture->getDescriptorSet();
    }

    std::vector<void*> dto::EditorTexture::getMipDescriptorSets() const
    {
        const auto& mipSets = texture->getMipDescriptorSets();
        std::vector<void*> result;
        result.reserve(mipSets.size());
        for (const auto& set : mipSets)
        {
            result.push_back(set);
        }
        return result;
    }
}
