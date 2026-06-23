#pragma once

namespace services {

	class ICpuMemoryService {
	public:
		virtual ~ICpuMemoryService() = default;
		virtual void registerEventHandlers() = 0;
		virtual void update(float deltaTime) = 0;
	};

}
