#pragma once
#include <memory>
#include <shared_mutex>
#include <typeindex>
#include <unordered_map>
#include <stdexcept>
#include <string>

namespace services {

    /**
     * Service Locator - provides dependency injection for services
     *
     * Usage:
     *   ServiceLocator::instance().registerService<ISceneService>(sceneServiceImpl);
     *   auto service = TRY_RESOLVE_SERVICE(ISceneService);
     *   if (service) { service->doSomething(); }
     *
     * Thread Safety:
     * - All methods are thread-safe
     * - tryGet() returns shared_ptr that keeps the service alive for duration of use
     *
     * Lifetime Contract:
     * - Services must be registered before consumers call tryGet()
     * - Services should only be unregistered during shutdown when no consumers are active
     */
    class ServiceLocator {
    public:
        static ServiceLocator& instance();

        // Prevent copying
        ServiceLocator(const ServiceLocator&) = delete;
        ServiceLocator& operator=(const ServiceLocator&) = delete;

        // Register a service implementation
        template<typename TInterface>
        void registerService(std::shared_ptr<TInterface> service);

        // Check if a service is registered
        template<typename TInterface>
        bool hasService() const;

        // Try get a service (returns nullptr if not found)
        // Thread-safe: keeps service alive for duration of use
        template<typename TInterface>
        std::shared_ptr<TInterface> tryGet();

        // Unregister a specific service
        template<typename TInterface>
        void unregisterService();

        // Clear all registered services (call during shutdown)
        void clear();

    private:
        ServiceLocator() = default;
        ~ServiceLocator() = default;

        std::unordered_map<std::type_index, std::shared_ptr<void>> services;
        mutable std::shared_mutex mutex;
    };

    // Template implementations

    template<typename TInterface>
    void ServiceLocator::registerService(std::shared_ptr<TInterface> service) {
        std::unique_lock lock(mutex);
        services[std::type_index(typeid(TInterface))] = std::move(service);
    }

    template<typename TInterface>
    bool ServiceLocator::hasService() const {
        std::shared_lock lock(mutex);
        return services.find(std::type_index(typeid(TInterface))) != services.end();
    }

    template<typename TInterface>
    std::shared_ptr<TInterface> ServiceLocator::tryGet() {
        std::shared_lock lock(mutex);

        auto it = services.find(std::type_index(typeid(TInterface)));
        if (it == services.end()) {
            return nullptr;
        }

        return std::static_pointer_cast<TInterface>(it->second);
    }

    template<typename TInterface>
    void ServiceLocator::unregisterService() {
        std::unique_lock lock(mutex);
        services.erase(std::type_index(typeid(TInterface)));
    }

    // Convenience macro for resolving services
    // Returns shared_ptr or nullptr if not found - thread-safe
    #define TRY_RESOLVE_SERVICE(Type) services::ServiceLocator::instance().tryGet<Type>()

}
