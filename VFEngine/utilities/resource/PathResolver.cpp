#include "PathResolver.hpp"
#include "../print/Log.hpp"

namespace resource
{
	void PathResolver::initialize()
	{
		if (initialized) return;
		initialized = true;

		// Detect exported build: either loose shaders directory or .vfpak archive
		exportedBuild = std::filesystem::exists("resources/shaders");

		if (!exportedBuild)
		{
			// Check for .vfpak archive (shaders are packed inside)
			std::error_code ec;
			for (const auto& entry : std::filesystem::directory_iterator(".", ec))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".vfpak")
				{
					exportedBuild = true;
					break;
				}
			}
		}

		if (exportedBuild)
		{
			vfLogInfo("PathResolver: Detected exported build mode");
		}
	}

	std::string PathResolver::resolveEnginePath(const std::string& path)
	{
		if (!exportedBuild) return path;

		const std::string devPrefix = "../../resources/";
		if (path.size() > devPrefix.size() &&
			path.compare(0, devPrefix.size(), devPrefix) == 0)
		{
			return "resources/" + path.substr(devPrefix.size());
		}

		return path;
	}
}
