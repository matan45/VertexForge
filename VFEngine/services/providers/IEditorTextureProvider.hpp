#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace services {

    
    struct EditorTextureData {
        void* descriptorSet = nullptr;  // ImGui descriptor set for rendering (all mips with trilinear)
        int width = 0;
        int height = 0;
        int channels = 0;
        int mipLevels = 1;
        std::vector<void*> mipDescriptorSets;  // Per-mip level descriptors for preview
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
