#pragma once
#include "imguiHandler/ImguiWindow.hpp"

namespace windows {
	class ConsoleLog : public controllers::imguiHandler::ImguiWindow
	{
	public:
		explicit ConsoleLog() = default;
		~ConsoleLog() override = default;

		void draw() override;
	};
}


