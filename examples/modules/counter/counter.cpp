#include "counter/counter.hpp"
namespace counter_example {
engine::ModuleRegistration<Registry> registration() {
    engine::ModuleDescriptor d;d.id="example.counter";
    d.schemas.push_back({"example.counter.factory",{1,0,0}});
    return {std::move(d),[](Registry& r){r.add("example.counter",[]{return Counter{};});}};
}
Demo::Demo() {
    const auto entry=registration();modules.replace({&entry,1});
    first.emplace(modules.snapshot());second.emplace(modules.snapshot());
}
bool Demo::rejectsBrokenReplacement() {
    const auto before=modules.snapshot();auto bad=registration();
    bad.descriptor.dependencies.push_back({"example.missing",{}});
    try { modules.replace({&bad,1}); } catch(const engine::ModuleError& e) {
        return e.code()==engine::ModuleErrorCode::MissingDependency&&modules.snapshot()==before;
    }
    return false;
}
}
