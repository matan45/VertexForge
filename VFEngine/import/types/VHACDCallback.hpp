#pragma once

#ifndef ENABLE_VHACD_IMPLEMENTATION
#define ENABLE_VHACD_IMPLEMENTATION 0
#endif
#include <VHACD.h>

#include <functional>
#include <atomic>
#include <string>
#include <string_view>

namespace types
{
    // Progress callback: (overallProgress 0-1, stageName, operationName)
    using VHACDProgressCallback = std::function<void(
        float overallProgress,
        std::string_view stage,
        std::string_view operation
    )>;

    /**
     * Adapter class implementing V-HACD's IUserCallback interface.
     * Provides progress forwarding and cancellation support for convex decomposition.
     */
    class VHACDCallback : public VHACD::IVHACD::IUserCallback
    {
    public:
        explicit VHACDCallback(VHACDProgressCallback callback = nullptr)
            : m_callback(std::move(callback))
        {
        }

        /**
         * Called by V-HACD to notify progress.
         * @param overallProgress Total progress from 0-100%
         * @param stageProgress Progress of the current stage 0-100%
         * @param stage Text description of the current stage
         * @param operation Text description of current operation
         */
        void Update(const double overallProgress,
                    const double /*stageProgress*/,
                    const char* const stage,
                    const char* operation) override
        {
            if (m_callback)
            {
                // Convert from 0-100 to 0-1 range
                m_callback(
                    static_cast<float>(overallProgress / 100.0),
                    stage ? stage : "",
                    operation ? operation : ""
                );
            }
        }

        /**
         * Called by V-HACD when decomposition is complete.
         */
        void NotifyVHACDComplete() override
        {
            m_complete.store(true);
            if (m_callback)
            {
                m_callback(1.0f, "Complete", "Decomposition finished");
            }
        }

        bool isComplete() const { return m_complete.load(); }

        void requestCancel() { m_cancelRequested.store(true); }
        bool isCancelRequested() const { return m_cancelRequested.load(); }

    private:
        VHACDProgressCallback m_callback;
        std::atomic<bool> m_complete{false};
        std::atomic<bool> m_cancelRequested{false};
    };
}
