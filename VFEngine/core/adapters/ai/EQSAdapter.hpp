#pragma once
#include "../../../services/providers/ai/IEQSProvider.hpp"
#include "../../../utilities/eqs/EQSQueryLibrary.hpp"
#include "../../../utilities/eqs/EQSScheduler.hpp"
#include <mutex>
#include <string>
#include <unordered_set>

namespace services
{
    class IPhysicsProvider;
    class INavmeshProvider;
}

namespace core
{
    // VK-1660: the engine side of EQS. Templates are read by name from assets/eqs/<Name>.vfEQSQuery through
    // the VFS (EQSQueryLibrary), queued queries run in the EQS frame task (EQSScheduler), and generators and
    // tests reach the engine through the providers built here: the navmesh, physics line of sight and the
    // scene's named entities.
    class EQSAdapter : public services::IEQSProvider
    {
    public:
        EQSAdapter(services::IPhysicsProvider* physicsProvider,
                   services::INavmeshProvider* navmeshProvider);
        ~EQSAdapter() override;

        // === IEQSProvider ===
        eqs::EQSQueryHandle submitQuery(const std::string& queryName, const eqs::EQSContext& context) override;
        eqs::EQSResult getResult(eqs::EQSQueryHandle handle) const override;
        void releaseQuery(eqs::EQSQueryHandle handle) override;
        void releaseAll() override;
        void update(float budgetMs) override;
        eqs::EQSDebugResult runQueryNow(const std::string& queryName, const eqs::EQSContext& context) override;
        services::EQSQueryInfo lookupQuery(const std::string& queryName) override;
        void invalidateQuery(const std::string& queryName) override;

    private:
        // The absolute path of assets/eqs/<queryName>.vfEQSQuery in the current project.
        static std::string templatePath(const std::string& queryName);
        eqs::EQSProviderRefs buildProviderRefs() const;
        // Logs `message` the first time `key` is seen since invalidateQuery / releaseAll.
        void warnOnce(const std::string& key, const std::string& message);

        services::IPhysicsProvider* physicsProvider;
        services::INavmeshProvider* navmeshProvider;
        eqs::EQSProviderRefs providers;
        eqs::EQSScheduler scheduler;
        eqs::EQSQueryLibrary library;

        std::mutex warningMutex;
        std::unordered_set<std::string> warnedKeys;
    };
}
