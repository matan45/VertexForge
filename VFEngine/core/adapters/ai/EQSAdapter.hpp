#pragma once
#include "../../../services/providers/ai/IEQSProvider.hpp"
#include "../../../utilities/eqs/EQSQueryEngine.hpp"
#include <unordered_map>
#include <memory>
#include <string>

namespace services
{
    class IPhysicsProvider;
    class INavmeshProvider;
}

namespace core
{
    class EQSAdapter : public services::IEQSProvider
    {
    public:
        EQSAdapter(services::IPhysicsProvider* physicsProvider,
                   services::INavmeshProvider* navmeshProvider);
        ~EQSAdapter() override;

        // === IEQSProvider ===
        void registerQuery(const std::string& name, const eqs::EQSQueryDef& def) override;
        eqs::EQSQueryHandle submitQuery(const std::string& queryName, const eqs::EQSContext& context) override;
        eqs::EQSResult getResult(eqs::EQSQueryHandle handle) const override;
        void cancelQuery(eqs::EQSQueryHandle handle) override;
        void update(float frameBudgetMs = 2.0f) override;

    private:
        void registerBuiltInQueries();
        eqs::EQSProviderRefs buildProviderRefs() const;

        services::IPhysicsProvider* physicsProvider;
        services::INavmeshProvider* navmeshProvider;
        eqs::EQSQueryEngine engine;
        std::unordered_map<std::string, eqs::EQSQueryDef> queryDefs;
    };
}
