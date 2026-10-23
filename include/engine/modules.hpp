#pragma once
#include <compare>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine {
struct ModuleVersion {
    std::uint32_t major=1, minor=0, patch=0;
    auto operator<=>(const ModuleVersion&) const = default;
};
// Inclusive minimum, exclusive maximum. This is an API range, not a binary ABI.
struct ModuleVersionRange {
    ModuleVersion minimum{1,0,0}, maximum{2,0,0};
    bool contains(ModuleVersion v) const noexcept { return minimum<=v && v<maximum; }
};
inline constexpr ModuleVersion moduleCompositionApiVersion{1,0,0};
struct ModuleDependency { std::string id; ModuleVersionRange versions; };
struct ModuleSchema { std::string id; ModuleVersion version; };
struct ModuleDescriptor {
    std::string id;
    ModuleVersion version;
    ModuleVersionRange engineApi;
    std::vector<ModuleDependency> dependencies;
    std::vector<ModuleSchema> schemas;
    std::vector<std::string> requiredCapabilities;
};
struct ModuleHost {
    ModuleVersion engineApi=moduleCompositionApiVersion;
    std::vector<std::string> capabilities;
};
struct ModuleLimits {
    static constexpr std::size_t modules=128, entriesPerModule=64, keyBytes=128;
};
enum class ModuleErrorCode {
    Limit, InvalidDescriptor, DuplicateModule, MissingDependency, IncompatibleVersion,
    DependencyCycle, DuplicateSchema, MissingCapability, MissingInstaller, ReentrantInstall
};
class ModuleError : public std::invalid_argument {
public:
    ModuleError(ModuleErrorCode code,std::string module,std::string message)
        :std::invalid_argument(std::move(message)),code_(code),module_(std::move(module)) {}
    ModuleErrorCode code() const noexcept { return code_; }
    const std::string& module() const noexcept { return module_; }
private:
    ModuleErrorCode code_;
    std::string module_;
};
// Pure validation; returns input indices in dependency order, lexical ties by ID.
std::vector<std::size_t> validateModules(std::span<const ModuleDescriptor>,const ModuleHost& = {});

// Registry is an application-defined typed builder. It must be default constructible;
// validate/freeze any domain-specific invariants in the optional finalizer.
template<class Registry> struct ModuleRegistration {
    ModuleDescriptor descriptor;
    std::function<void(Registry&)> install;
};

template<class Registry> class ModuleSnapshot {
public:
    const Registry& registry() const noexcept { return *registry_; }
    std::span<const ModuleDescriptor> descriptors() const noexcept { return descriptors_; }
    ModuleSnapshot(const ModuleSnapshot&)=delete;
    ModuleSnapshot& operator=(const ModuleSnapshot&)=delete;
    ~ModuleSnapshot() noexcept {
        // Registry factories may borrow providers retained by installer captures.
        registry_.reset();
        while(!providers_.empty()) providers_.pop_back();
    }
private:
    template<class> friend class ModuleComposition;
    ModuleSnapshot()=default;
    std::vector<ModuleDescriptor> descriptors_;
    std::vector<ModuleRegistration<Registry>> providers_;
    std::optional<Registry> registry_;
};

// Owner-thread only; installation is synchronous and non-reentrant. Callbacks must
// mutate only the staged registry. No global registration, runtime scheduler or unload.
template<class Registry> class ModuleComposition {
public:
    using Snapshot=ModuleSnapshot<Registry>;
    using Handle=std::shared_ptr<const Snapshot>;
    ModuleComposition()=default;
    ModuleComposition(const ModuleComposition&)=delete;
    ModuleComposition& operator=(const ModuleComposition&)=delete;
    Handle snapshot() const noexcept { return current_; }
    Handle replace(std::span<const ModuleRegistration<Registry>> registrations,
                   const ModuleHost& host={},const std::function<void(const Registry&)>& finalize={}) {
        if(installing_) throw ModuleError(ModuleErrorCode::ReentrantInstall,"","Reentrant module installation");
        struct Guard { bool& flag; explicit Guard(bool& f):flag(f){flag=true;} ~Guard(){flag=false;} } guard(installing_);
        if(registrations.size()>ModuleLimits::modules)
            throw ModuleError(ModuleErrorCode::Limit,"","Too many modules");
        std::vector<ModuleDescriptor> descriptions;
        descriptions.reserve(registrations.size());
        for(const auto& entry:registrations) {
            if(!entry.install) throw ModuleError(ModuleErrorCode::MissingInstaller,entry.descriptor.id,"Missing installer: "+entry.descriptor.id);
            descriptions.push_back(entry.descriptor);
        }
        const auto order=validateModules(descriptions,host);
        auto candidate=std::shared_ptr<Snapshot>(new Snapshot);
        candidate->providers_.reserve(order.size());
        candidate->descriptors_.reserve(order.size());
        // Copy all providers before invoking any installer. Reverse release also
        // applies if a provider copy, registry construction or installer throws.
        for(auto i:order) {
            candidate->providers_.push_back(registrations[i]);
            candidate->descriptors_.push_back(descriptions[i]);
        }
        candidate->registry_.emplace();
        for(const auto& entry:candidate->providers_) entry.install(*candidate->registry_);
        if(finalize) finalize(*candidate->registry_);
        current_=candidate;
        return candidate;
    }
private:
    bool installing_=false;
    Handle current_;
};
} // namespace engine
