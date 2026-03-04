#include "print/Log.hpp"
#include "DynamicLibrary.hpp"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
#else
    #include <dlfcn.h>
#endif

namespace plugin {

    DynamicLibrary::DynamicLibrary(const std::filesystem::path& path)
        : libraryPath(path)
    {
#ifdef _WIN32
        handle = LoadLibraryW(path.wstring().c_str());
        if (!handle) {
            DWORD error = GetLastError();
            vfLogError("Failed to load library '{}': error code {}", path.string(), error);
        }
#else
        handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            vfLogError("Failed to load library '{}': {}", path.string(), dlerror());
        }
#endif
    }

    DynamicLibrary::~DynamicLibrary()
    {
        if (handle) {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(handle));
#else
            dlclose(handle);
#endif
            handle = nullptr;
        }
    }

    DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
        : handle(other.handle)
        , libraryPath(std::move(other.libraryPath))
    {
        other.handle = nullptr;
    }

    DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
    {
        if (this != &other) {
            if (handle) {
#ifdef _WIN32
                FreeLibrary(static_cast<HMODULE>(handle));
#else
                dlclose(handle);
#endif
            }
            handle = other.handle;
            libraryPath = std::move(other.libraryPath);
            other.handle = nullptr;
        }
        return *this;
    }

    bool DynamicLibrary::isLoaded() const
    {
        return handle != nullptr;
    }

    void* DynamicLibrary::getSymbol(const std::string& name) const
    {
        if (!handle) {
            return nullptr;
        }

#ifdef _WIN32
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name.c_str()));
#else
        return dlsym(handle, name.c_str());
#endif
    }

    const std::filesystem::path& DynamicLibrary::getPath() const
    {
        return libraryPath;
    }

}
