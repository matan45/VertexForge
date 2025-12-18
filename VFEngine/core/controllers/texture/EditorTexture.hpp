#pragma once
#include <memory>
#include <vector>

namespace core {
    class Texture;
}

namespace dto
{
    class EditorTexture
    {
    private:
        std::unique_ptr<core::Texture> texture;
        int width;
        int height;
        int numbersOfChannels;
        int mipLevels;

    public:
        explicit EditorTexture(std::unique_ptr<core::Texture> texture);
        ~EditorTexture();
        void* getDescriptorSet() const;
        std::vector<void*> getMipDescriptorSets() const;

        int getWidth() const
        {
            return width;
        }

        int getHeight() const
        {
            return height;
        }

        int getNumbersOfChannels() const
        {
            return numbersOfChannels;
        }

        int getMipLevels() const
        {
            return mipLevels;
        }
    };
}
