#pragma once
#include "nfd/FileDialog.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace windows
{
	// Export Game dialog (File > Export Game...): collects the output directory
	// and export options, then fires ExportGameCommand. Progress is shown by the
	// separate ExportProgressWindow, which listens for the export notifications.
	class ExportGameWindow
	{
	private:
		bool visible = false;
		nfd::FileDialog fileDialog;

		std::string outputDirectory;
		bool cleanBuild = false;
		bool verifyIntegrity = true;
		bool buildScripts = true;
		bool stripUnreferencedAssets = false;
		std::string alwaysIncludeText; // one glob per line, parsed on export

		std::string gameName;
		std::string gameVersion;

		// Plugins discovered next to the editor; ship defaults to the
		// descriptor's global enabled flag, the checkbox overrides it.
		struct PluginRow
		{
			std::string name;
			std::string version;
			uint32_t apiVersion = 0;
			bool defaultEnabled = true;
			bool ship = true;
		};
		std::vector<PluginRow> plugins;

		void loadFromPreferences();
		void saveToPreferences() const;
		void refreshPluginList();
		void startExport();

	public:
		void draw();
		void show();
	};
}
