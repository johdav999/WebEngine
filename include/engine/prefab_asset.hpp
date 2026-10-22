#pragma once
#include "engine/actor.hpp"
#include <map>
#include <span>

namespace engine {
struct PrefabBinding { std::string key,node;bool operator==(const PrefabBinding&) const=default; };
class PrefabAsset;
// Inert instance provenance, not a simulation behaviour or global asset registry.
class PrefabInstanceComponent final : public Component {
public:
    PrefabInstanceComponent(std::shared_ptr<const PrefabAsset> asset,std::vector<PrefabBinding> bindings={});
    std::string_view typeName() const noexcept override {return "PrefabInstanceComponent";}
    const std::shared_ptr<const PrefabAsset>& asset() const {return asset_;}
    const std::vector<PrefabBinding>& bindings() const {return bindings_;}
    void configure(std::shared_ptr<const PrefabAsset>,std::vector<PrefabBinding>);
    void validate() const override;
private:
    std::shared_ptr<const PrefabAsset> asset_;
    std::vector<PrefabBinding> bindings_;
};
class PrefabAsset : public std::enable_shared_from_this<PrefabAsset> {
public:
    // Definition-local saved IDs are stable keys. Clone/capture shares immutable resources.
    static std::shared_ptr<const PrefabAsset> create(std::unique_ptr<Actor> definition,std::string name,
        std::string id={},std::uint32_t revision=1,std::shared_ptr<const PrefabAsset> base={});
    static std::shared_ptr<const PrefabAsset> capture(const Actor&,std::string name,std::string id={},
        std::uint32_t revision=1,std::shared_ptr<const PrefabAsset> base={});
    const Actor& definition() const {return *definition_;}
    const std::string& id() const {return id_;}
    const std::string& name() const {return name_;}
    std::uint32_t revision() const {return revision_;}
    const std::shared_ptr<const PrefabAsset>& base() const {return base_;}
    std::vector<std::shared_ptr<const PrefabAsset>> dependencies() const; // Direct base/nested immutable revisions.
    // Detached complete actor. Optional old identities preserve saved IDs on update;
    // ordinary scene attachment recreates runtime handles.
    std::unique_ptr<Actor> instantiate(std::span<const PrefabBinding> identities={}) const;
private:
    PrefabAsset()=default;
    std::shared_ptr<const Actor> definition_;
    std::shared_ptr<const PrefabAsset> base_;
    std::string id_,name_;
    std::uint32_t revision_=1;
};
// Returns a detached canonical definition; strips a root instance link and restores
// definition keys. Nested linked instances and their scoped bindings survive.
std::unique_ptr<Actor> prefabDefinition(const Actor&);
class PrefabAssetArchive {
public:
    static std::vector<std::byte> save(std::shared_ptr<const PrefabAsset>);
    static std::shared_ptr<const PrefabAsset> load(std::span<const std::byte>);
};
}
