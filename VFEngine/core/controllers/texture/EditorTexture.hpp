#pragma once
#include <memory>

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

    public:
        explicit EditorTexture(std::unique_ptr<core::Texture> texture);
        ~EditorTexture();
        void* getDescriptorSet() const;

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
    };
}
