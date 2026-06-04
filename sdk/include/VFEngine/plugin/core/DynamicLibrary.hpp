#pragma once
#include <string>
#include <filesystem>

namespace plugin {

    class DynamicLibrary
    {
    public:
        explicit DynamicLibrary(const std::filesystem::path& path);
        ~DynamicLibrary();

        DynamicLibrary(const DynamicLibrary&) = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;

        DynamicLibrary(DynamicLibrary&& other) noexcept;
        DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

        bool isLoaded() const;
        void* getSymbol(const std::string& name) const;

        template<typename T>
        T getFunction(const std::string& name) const
        {
            return reinterpret_cast<T>(getSymbol(name));
        }

        const std::filesystem::path& getPath() const;

    private:
        void* handle = nullptr;
        std::filesystem::path libraryPath;
    };

}
