#pragma once
#include <string>
#include <string_view>
#include <cstdint>

namespace services {

    /**
     * @brief Data structure representing a loaded editor texture.
     *
     * Used to return texture information from the provider without
     * exposing Core implementation details.
     */
    struct EditorTextureData {
        void* descriptorSet = nullptr;  // ImGui descriptor set for rendering
        int width = 0;
        int height = 0;
        int channels = 0;
        bool valid = false;
    };

    /**
     * @brief Provider interface for editor texture loading operations.
     *
     * This interface abstracts the Core module's EditorTextureController,
     * allowing Services to load textures for UI display without depending on Core.
     * Core implements this interface via an adapter class.
     */
    class IEditorTextureProvider {
    public:
        virtual ~IEditorTextureProvider() = default;

        /**
         * @brief Load a standard texture (PNG, JPG, etc.) for editor display.
         * @param path Path to the texture file
         * @return EditorTextureData with texture information, or invalid data on failure
         */
        virtual EditorTextureData loadTexture(std::string_view path) = 0;

        /**
         * @brief Load an HDR texture for editor display.
         * @param path Path to the HDR texture file
         * @return EditorTextureData with texture information, or invalid data on failure
         */
        virtual EditorTextureData loadHdrTexture(std::string_view path) = 0;

        /**
         * @brief Release a previously loaded texture.
         * @param descriptorSet The descriptor set handle returned from load operations
         */
        virtual void releaseTexture(void* descriptorSet) = 0;
    };

}
