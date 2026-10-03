#pragma once


namespace editor {
	class EditorMcpHost;
}

namespace handlers {
	class WindowImguiHandler
	{
	public:
		WindowImguiHandler() = default;
		~WindowImguiHandler() = default;

		// mcpHost: shown in the status bar and preferences window; may be null.
		void init(editor::EditorMcpHost* mcpHost);
		void cleanUp() const;
	};
}
