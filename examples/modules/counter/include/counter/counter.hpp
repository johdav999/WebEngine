#pragma once
#include "engine/modules.hpp"
#include <map>

namespace counter_example {
struct Counter {
    int value=0;
    void add() { if(value==1000000)throw std::overflow_error("Counter limit");++value; }
};
struct Registry {
    std::map<std::string,std::function<Counter()>> factories;
    void add(std::string id,std::function<Counter()> factory) {
        if(!factory||!factories.emplace(std::move(id),std::move(factory)).second)
            throw std::invalid_argument("Duplicate or empty counter factory");
    }
};
engine::ModuleRegistration<Registry> registration();
using Composition=engine::ModuleComposition<Registry>;
// The snapshot precedes state in lifetime: instances keep their provider alive.
struct Session {
    Composition::Handle installed;
    Counter counter;
    explicit Session(Composition::Handle snapshot):installed(std::move(snapshot)),
        counter(installed->registry().factories.at("example.counter")()) {}
};
struct Demo {
    Composition modules;
    std::optional<Session> first,second;
    Demo();
    void add(int session) { (session==0?first:second)->counter.add(); }
    void reset(int session) { (session==0?first:second).emplace(modules.snapshot()); }
    bool rejectsBrokenReplacement();
};
}
