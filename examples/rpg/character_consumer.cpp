#include "characters.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        namespace r = engine::rpg;
        for (bool alternate : {false, true}) {
            auto catalog = example::characterCatalog(example::characterDefinitions(alternate));
            r::Runtime runtime(catalog, r::SessionId(alternate ? 20 : 10));
            auto creation = runtime.prepare(runtime.command(r::ContentId("example.input"), 1,
                {r::CreateCharacter{r::ContentId(alternate ? "example.operator" : "example.scholar")}}));
            const auto entity = creation.results().created.at(0);
            if (!runtime.commit(std::move(creation))) throw std::runtime_error("Creation failed");
            auto award = runtime.prepare(runtime.command(r::ContentId("example.input"), 2,
                {r::AddExperience{entity, {alternate ? 11 : 35, r::ContentId("example.practice"), 1}}}));
            if (!runtime.commit(std::move(award))) throw std::runtime_error("Award failed");
            const auto& character = runtime.state().characters.at(entity);
            if (character.level != 2 || character.unlocks.size() != 2) throw std::runtime_error("Unexpected advancement");
            std::cout << character.definition.value() << ": xp=" << character.experience << " level=2 unlocks=2\n";
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
