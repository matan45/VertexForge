#pragma once

#include "../../interfaces/input/IRuntimePickerService.hpp"

namespace services
{
    class IRuntimePickerProvider;

    class RuntimePickerServiceImpl : public IRuntimePickerService
    {
    public:
        explicit RuntimePickerServiceImpl(IRuntimePickerProvider* provider);

        void registerEventHandlers() override;

    private:
        IRuntimePickerProvider* pickerProvider;
    };
}
