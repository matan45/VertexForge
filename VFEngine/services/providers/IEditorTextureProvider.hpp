#pragma once
#include <string>
#include <string_view>

namespace services {

    
    struct EditorTextureData {
        void* descriptorSet = nullptr;  // ImGui descriptor set for rendering
        int width = 0;
        int height = 0;
        int channels = 0;
        bool valid = false;
    };

   
    class IEditorTextureProvider {
    public:
        virtual ~IEditorTextureProvider() = default;
        
        virtual EditorTextureData loadTexture(std::string_view path) = 0;
        
        virtual EditorTextureData loadHdrTexture(std::string_view path) = 0;
        
        virtual void releaseTexture(void* descriptorSet) = 0;
    };

}
