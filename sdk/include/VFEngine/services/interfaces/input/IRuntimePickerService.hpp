#pragma once

namespace services
{
    // Runtime screen-ray picking service. Registers CQRS query handlers
    // (ScreenToWorldRay / PickTerrain / PickEntity) backed by an IRuntimePickerProvider.
    class IRuntimePickerService
    {
    public:
        virtual ~IRuntimePickerService() = default;

        virtual void registerEventHandlers() = 0;
    };
}
