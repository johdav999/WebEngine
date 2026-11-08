#include "engine/rpg/runtime.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace r = engine::rpg;
namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(r::ErrorCode expected, F action) {
    try { action(); } catch (const r::Error& e) { require(e.code() == expected, "Wrong RPG error code"); return; }
    throw std::runtime_error("Expected RPG rejection");
}
template<class F> void rejectsModule(engine::ModuleErrorCode expected, F action) {
    try { action(); } catch (const engine::ModuleError& e) { require(e.code() == expected, "Wrong module error code"); return; }
    throw std::runtime_error("Expected module rejection");
}
engine::ModuleRegistration<r::Bindings> policy() {
    engine::ModuleRegistration<r::Bindings> result;
    result.descriptor.id = "test.rules";
    result.descriptor.dependencies.push_back({"webengine.rpg", {}});
    result.install = [](r::Bindings& bindings) {
        bindings.addDefinitionHandler(r::ContentId("test.definition"), {1, [](const r::Definition&) {}});
    };
    return result;
}
r::CatalogDefinition content() {
    return {r::ContentId("test.catalog"), r::ContentRevision(2), {
        {r::ContentId("test.z"), r::ContentId("test.definition"), 1},
        {r::ContentId("test.a"), r::ContentId("test.definition"), 1}}};
}
void identities() {
    static_assert(!std::is_convertible_v<std::string, r::ContentId>);
    static_assert(!std::is_convertible_v<std::uint64_t, r::SessionId>);
    static_assert(!std::is_copy_constructible_v<r::Runtime>);
    static_assert(!std::is_move_constructible_v<r::Runtime>);
    for (const auto* key : {"", "plain", ".a", "a.", "a..b", "A.b", "a/b", "a. b", "a.\xC3\xA9"})
        rejects(r::ErrorCode::InvalidId, [&] { r::ContentId id(key); });
    require(r::ContentId("studio.some-key_2").value() == "studio.some-key_2", "Valid key changed");
    r::ContentId maximum("a." + std::string(126, 'b'));
    rejects(r::ErrorCode::Limit, [] { r::ContentId id("a." + std::string(127, 'b')); });
    rejects(r::ErrorCode::InvalidVersion, [] { r::ContentRevision revision(0); });
    rejects(r::ErrorCode::InvalidSession, [] { r::SessionId id(0); });
    r::EntityIds ids(r::SessionId(1), std::numeric_limits<std::uint64_t>::max());
    require(ids.allocate().value == std::numeric_limits<std::uint64_t>::max(), "Last identity unavailable");
    rejects(r::ErrorCode::ExhaustedIdentity, [&] { ids.allocate(); });
    require(ids.next() == 0, "Exhaustion wrapped identity cursor");
}
void compositionAndCatalog() {
    r::Composition composition;
    auto rules = policy();
    const std::array registrations{rules, r::moduleRegistration()};
    auto stable = composition.replace(registrations);
    auto catalog = r::Catalog::create(content(), stable);
    require(catalog->definitions()[0].id.value() == "test.a", "Catalogue order is not canonical");
    require(catalog->find(r::ContentId("test.z")) != nullptr, "Definition lookup failed");
    require(catalog->find(r::ContentId("test.absent")) == nullptr, "Missing definition unexpectedly found");
    require(catalog->revision().value() == 2, "Content revision changed");
    rejectsModule(engine::ModuleErrorCode::DuplicateModule, [&] {
        const std::array duplicates{r::moduleRegistration(), r::moduleRegistration()};
        composition.replace(duplicates);
    });
    rejectsModule(engine::ModuleErrorCode::MissingDependency, [&] { composition.replace({&rules, 1}); });
    auto incompatible = rules;
    incompatible.descriptor.dependencies[0].versions = {{2, 0, 0}, {3, 0, 0}};
    rejectsModule(engine::ModuleErrorCode::IncompatibleVersion, [&] {
        const std::array entries{r::moduleRegistration(), incompatible};
        composition.replace(entries);
    });
    auto duplicate = rules;
    duplicate.descriptor.id = "test.duplicate";
    rejects(r::ErrorCode::DuplicateHandler, [&] {
        const std::array entries{r::moduleRegistration(), rules, duplicate};
        composition.replace(entries);
    });
    auto throwing = rules;
    throwing.install = [](r::Bindings& bindings) {
        bindings.addDefinitionHandler(r::ContentId("test.partial"), {1, [](const r::Definition&) {}});
        throw std::runtime_error("installer failed");
    };
    try {
        const std::array entries{r::moduleRegistration(), throwing};
        composition.replace(entries);
        throw std::logic_error("Throwing installer succeeded");
    } catch (const std::runtime_error& e) { require(std::string(e.what()) == "installer failed", "Installer error lost"); }
    rejectsModule(engine::ModuleErrorCode::ReentrantInstall, [&] {
        auto recursive = rules;
        recursive.install = [&](r::Bindings&) { composition.replace({}); };
        const std::array entries{r::moduleRegistration(), recursive};
        composition.replace(entries);
    });
    require(composition.snapshot() == stable, "Failed replacement published partial bindings");
    require(stable->registry().find(r::ContentId("test.partial")) == nullptr, "Installer leaked staged state");
    rejects(r::ErrorCode::MissingModule, [&] { r::Catalog::create(content(), {}); });
    r::Composition empty;
    rejects(r::ErrorCode::MissingModule, [&] { r::Catalog::create(content(), empty.replace({})); });
    auto bad = content();
    bad.definitions.push_back(bad.definitions[0]);
    rejects(r::ErrorCode::DuplicateDefinition, [&] { r::Catalog::create(bad, stable); });
    bad = content();
    bad.definitions[0].handler = r::ContentId("test.absent");
    rejects(r::ErrorCode::MissingHandler, [&] { r::Catalog::create(bad, stable); });
    bad = content();
    bad.definitions[0].handlerVersion = 2;
    rejects(r::ErrorCode::HandlerVersionMismatch, [&] { r::Catalog::create(bad, stable); });
    bad = content();
    bad.definitions.clear();
    for (std::size_t i = 0; i < r::Limits::definitions; ++i)
        bad.definitions.push_back({r::ContentId("test.entry" + std::to_string(i)), r::ContentId("test.definition"), 1});
    require(r::Catalog::create(bad, stable)->definitions().size() == r::Limits::definitions, "Definition bound rejected");
    bad.definitions.push_back({r::ContentId("test.extra"), r::ContentId("test.definition"), 1});
    rejects(r::ErrorCode::Limit, [&] { r::Catalog::create(bad, stable); });
    require(catalog->definitions().size() == 2, "Rejected catalogue altered prior catalogue");
    r::Runtime first(catalog, r::SessionId(1)), second(catalog, r::SessionId(2));
    const auto a = first.allocateEntityId();
    first.allocateEntityId();
    const auto b = second.allocateEntityId();
    require(a.value == b.value && a != b, "Namespaces failed to distinguish entity IDs");
    require(first.nextEntity() == 3 && second.nextEntity() == 2, "Sessions share mutable state");
    composition.replace({});
    stable.reset();
    catalog.reset();
    require(first.catalog().find(r::ContentId("test.a")) != nullptr, "Retained session lost catalogue");
    rejects(r::ErrorCode::MissingModule, [] { r::Runtime invalid({}, r::SessionId(9)); });
}
void handlerValidation() {
    r::Bindings bindings;
    rejects(r::ErrorCode::InvalidVersion, [&] {
        bindings.addDefinitionHandler(r::ContentId("test.handler"), {0, [](const r::Definition&) {}});
    });
    rejects(r::ErrorCode::MissingValidator, [&] { bindings.addDefinitionHandler(r::ContentId("test.handler"), {1, {}}); });
    require(bindings.size() == 0, "Invalid handler partially inserted");
    for (std::size_t i = 0; i < r::Limits::handlers; ++i)
        bindings.addDefinitionHandler(r::ContentId("test.handler" + std::to_string(i)), {1, [](const r::Definition&) {}});
    rejects(r::ErrorCode::Limit, [&] {
        bindings.addDefinitionHandler(r::ContentId("test.extra"), {1, [](const r::Definition&) {}});
    });
    require(bindings.size() == r::Limits::handlers, "Limit rejection changed registry");
    r::Composition composition;
    auto rules = policy();
    rules.install = [](r::Bindings& target) {
        target.addDefinitionHandler(r::ContentId("test.definition"), {1, [](const r::Definition&) {
            throw std::runtime_error("invalid authored definition");
        }});
    };
    const std::array entries{r::moduleRegistration(), rules};
    auto snapshot = composition.replace(entries);
    try {
        r::Catalog::create(content(), snapshot);
        throw std::logic_error("Validator did not reject");
    } catch (const std::runtime_error& e) {
        require(std::string(e.what()) == "invalid authored definition", "Validator error lost");
    }
    require(composition.snapshot() == snapshot, "Catalogue failure changed bindings");
}
struct Lifetime {
    std::vector<int>* order;
    int marker;
    ~Lifetime() { order->push_back(marker); }
};
void lifetime() {
    std::vector<int> order;
    std::weak_ptr<Lifetime> weakProvider;
    std::shared_ptr<const r::Catalog> catalog;
    {
        auto provider = std::make_shared<Lifetime>();
        provider->order = &order; provider->marker = 2;
        weakProvider = provider;
        auto rules = policy();
        rules.install = [provider, &order](r::Bindings& bindings) {
            auto validator = std::make_shared<Lifetime>();
            validator->order = &order; validator->marker = 1;
            bindings.addDefinitionHandler(r::ContentId("test.definition"), {1,
                [validator, weak = std::weak_ptr<Lifetime>(provider)](const r::Definition&) {
                    if (weak.expired()) throw std::runtime_error("Provider released too early");
                }});
        };
        r::Composition composition;
        const std::array entries{r::moduleRegistration(), rules};
        catalog = r::Catalog::create(content(), composition.replace(entries));
    }
    require(!weakProvider.expired() && order.empty(), "Catalogue did not retain provider");
    {
        r::Runtime runtime(catalog, r::SessionId(99));
        catalog.reset();
        require(!weakProvider.expired(), "Session did not retain provider");
        auto another = r::Catalog::create(content(), runtime.catalog().bindings());
        require(another->definitions().size() == 2, "Retained validator unusable");
    }
    require(weakProvider.expired(), "Provider leaked after final owner");
    require(order == std::vector<int>({1, 2}), "Registry must die before provider captures");
}
}
int main() {
    try {
        identities(); compositionAndCatalog(); handlerValidation(); lifetime();
        std::cout << "RPG foundation: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RPG foundation failure: " << error.what() << '\n';
        return 1;
    }
}
