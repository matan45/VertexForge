#pragma once
#include "../../interfaces/memory/ICpuMemoryService.hpp"

namespace services {

	class CpuMemoryServiceImpl : public ICpuMemoryService {
	public:
		CpuMemoryServiceImpl() = default;
		~CpuMemoryServiceImpl() override = default;

		void registerEventHandlers() override;
		void update(float deltaTime) override;
	};

}
