#pragma once
#include <string>
#include <filesystem>

namespace resource
{
	class PathResolver
	{
	private:
		static inline bool exportedBuild = false;
		static inline bool initialized = false;
	public:
		static void initialize();

		// Resolves engine resource paths.
		// In development, returns path as-is.
		// In exported builds, rewrites "../../resources/" prefix to "resources/".
		static std::string resolveEnginePath(const std::string& path);

		static bool isExportedMode() { return exportedBuild; }

	
	};
}
