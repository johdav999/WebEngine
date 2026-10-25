#include "engine/rpg/runtime.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace r = engine::rpg;
int main() {
    try {
        engine::ModuleRegistration<r::Bindings> policy;
        policy.descriptor.id = "example.rpg-definitions";
        policy.descriptor.dependencies.push_back({"webengine.rpg", {}});
        policy.install = [](r::Bindings& bindings) {
            bindings.addDefinitionHandler(r::ContentId("example.marker"), {1, [](const r::Definition& definition) {
                if (!definition.id.value().starts_with("example."))
                    throw std::invalid_argument("Sample definitions require example namespace");
            }});
        };
        r::Composition composition;
        const std::array registrations{policy, r::moduleRegistration()};
        auto snapshot = composition.replace(registrations);
        auto catalog = r::Catalog::create({r::ContentId("example.catalog"), r::ContentRevision(7),
            {{r::ContentId("example.neutral"), r::ContentId("example.marker"), 1}}}, snapshot);
        r::Runtime first(catalog, r::SessionId(10));
        r::Runtime second(catalog, r::SessionId(20));
        snapshot.reset();
        catalog.reset();
        composition.replace({}); // Existing sessions retain the previous providers/catalogue.
        auto a = first.allocateEntityId();
        auto b = first.allocateEntityId();
        auto c = second.allocateEntityId();
        if (a.value != 1 || b.value != 2 || c.value != 1 || a == c ||
            first.nextEntity() != 3 || second.nextEntity() != 2 ||
            !first.catalog().find(r::ContentId("example.neutral")))
            throw std::runtime_error("Independent retained sessions failed");
        std::cout << "RPG R1: sessions 10:[1,2] 20:[1]; content revision 7; retained catalogue OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
