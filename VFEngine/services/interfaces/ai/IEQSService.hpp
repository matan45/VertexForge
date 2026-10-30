#pragma once

namespace services
{
    class IEQSService
    {
    public:
        virtual ~IEQSService() = default;

        virtual void registerEventHandlers() = 0;

        // The EQS frame task, every frame (VK-1660): runs queued queries within the frame budget only while
        // `simulate` (Play with game time running), and draws the debug overlay in every mode.
        virtual void update(float deltaTime, bool simulate) = 0;
    };
}
