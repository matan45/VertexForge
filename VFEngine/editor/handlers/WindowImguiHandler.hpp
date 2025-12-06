#pragma once


namespace handlers {
	class WindowImguiHandler
	{
	public:
		WindowImguiHandler() = default;
		~WindowImguiHandler() = default;

		void init();
		void cleanUp() const;
	};
}
