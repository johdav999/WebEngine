#include "engine/modules.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>

using namespace engine;
namespace {
int checks=0;
void check(bool value,const char* message) { ++checks;if(!value)throw std::runtime_error(message); }
template<class F> void rejects(ModuleErrorCode code,F action) {
    try { action(); } catch(const ModuleError& e) {check(e.code()==code,"Wrong diagnostic code");check(*e.what(),"Empty diagnostic");return;}
    throw std::runtime_error("Expected module rejection");
}
ModuleDescriptor descriptor(std::string id) { ModuleDescriptor d;d.id=std::move(id);return d; }
struct Registry { std::map<std::string,std::function<int()>> factories; };
using Composition=ModuleComposition<Registry>;
using Entry=ModuleRegistration<Registry>;
Entry entry(std::string id) { auto d=descriptor(id);return {d,[id](Registry& r){r.factories.emplace(id,[]{return 7;});}}; }
struct Provider {
    std::vector<int>* released;
    int id;
    ~Provider() {released->push_back(id);}
};
void descriptors() {
    std::vector<ModuleDescriptor> ds{descriptor("sample.z"),descriptor("sample.a"),descriptor("sample.b")};
    ds[1].dependencies.push_back({"sample.z",{}});
    check(validateModules(ds)==std::vector<std::size_t>({2,0,1}),"Dependency ordering");
    std::reverse(ds.begin(),ds.end());auto order=validateModules(ds);std::string names;
    for(auto i:order)names+=ds[i].id;
    check(names=="sample.bsample.zsample.a","Input order changes install order");
    auto invalid=ds;invalid.push_back(ds[0]);rejects(ModuleErrorCode::DuplicateModule,[&]{validateModules(invalid);});
    invalid=ds;invalid[0].dependencies.push_back({"sample.missing",{}});rejects(ModuleErrorCode::MissingDependency,[&]{validateModules(invalid);});
    invalid=ds;invalid[2].dependencies.push_back({"sample.a",{}});rejects(ModuleErrorCode::DependencyCycle,[&]{validateModules(invalid);});
    invalid={descriptor("sample.self")};invalid[0].dependencies.push_back({"sample.self",{}});rejects(ModuleErrorCode::DependencyCycle,[&]{validateModules(invalid);});
    invalid=ds;invalid[2].version={2,0,0};rejects(ModuleErrorCode::IncompatibleVersion,[&]{validateModules(invalid);});
    invalid=ds;invalid[0].engineApi.minimum={2,0,0};invalid[0].engineApi.maximum={3,0,0};rejects(ModuleErrorCode::IncompatibleVersion,[&]{validateModules(invalid);});
    invalid=ds;invalid[0].engineApi.maximum=invalid[0].engineApi.minimum;rejects(ModuleErrorCode::InvalidDescriptor,[&]{validateModules(invalid);});
    for(auto bad:{"", "unnamespaced", "UPPER.bad", ".leading", "trailing.", "double..dot", "bad/slash.name"}) {
        invalid={descriptor(bad)};rejects(ModuleErrorCode::InvalidDescriptor,[&]{validateModules(invalid);});
    }
    invalid={descriptor(std::string(129,'a')+".x")};rejects(ModuleErrorCode::InvalidDescriptor,[&]{validateModules(invalid);});
    invalid=ds;invalid[0].schemas.push_back({"sample.schema",{}});invalid[1].schemas.push_back({"sample.schema",{2,0,0}});
    rejects(ModuleErrorCode::DuplicateSchema,[&]{validateModules(invalid);});
    invalid=ds;invalid[0].requiredCapabilities={"host.storage"};rejects(ModuleErrorCode::MissingCapability,[&]{validateModules(invalid);});
    check(validateModules(invalid,{{1,0,0},{"host.storage"}}).size()==3,"Available capability rejected");
    invalid[0].requiredCapabilities.push_back("host.storage");rejects(ModuleErrorCode::InvalidDescriptor,[&]{validateModules(invalid,{{1,0,0},{"host.storage"}});});
    rejects(ModuleErrorCode::InvalidDescriptor,[&]{validateModules({},{{1,0,0},{"host.storage","host.storage"}});});
    invalid=ds;invalid[1].dependencies.push_back(invalid[1].dependencies[0]);rejects(ModuleErrorCode::InvalidDescriptor,[&]{validateModules(invalid);});
    invalid.assign(129,descriptor("sample.limit"));rejects(ModuleErrorCode::Limit,[&]{validateModules(invalid);});
    invalid={descriptor("sample.limit")};invalid[0].schemas.resize(65);rejects(ModuleErrorCode::Limit,[&]{validateModules(invalid);});
    check(validateModules({}).empty(),"Empty composition rejected");
    ModuleVersionRange r{{1,2,3},{2,0,0}};
    check(r.contains({1,2,3})&&r.contains({1,9,99})&&!r.contains({1,2,2})&&!r.contains({2,0,0}),"Version range edges");
}
void transactions() {
    Composition c;check(!c.snapshot(),"Unexpected initial snapshot");
    auto a=entry("sample.a");const auto original=c.replace({&a,1});
    auto noInstaller=a;noInstaller.install={};rejects(ModuleErrorCode::MissingInstaller,[&]{c.replace({&noInstaller,1});});
    int invoked=0;auto bad=a;bad.descriptor.dependencies.push_back({"sample.missing",{}});bad.install=[&](Registry&){++invoked;};
    rejects(ModuleErrorCode::MissingDependency,[&]{c.replace({&bad,1});});check(invoked==0,"Installer ran before validation");
    bad=a;bad.install=[](Registry& r){r.factories.emplace("sample.partial",[]{return 9;});throw std::runtime_error("install failure");};
    try{c.replace({&bad,1});throw std::logic_error("unexpected success");}catch(const std::runtime_error& e){check(std::string(e.what())=="install failure","Wrong exception");}
    check(c.snapshot()==original&&original->registry().factories.size()==1,"Failure published partial registry");
    try{c.replace({&a,1},{},[](const Registry&){throw std::runtime_error("finalize failure");});throw std::logic_error("unexpected success");}
    catch(const std::runtime_error& e){check(std::string(e.what())=="finalize failure","Finalizer failure lost");}
    check(c.snapshot()==original,"Finalizer failure replaced registry");
    bad=a;bad.install=[&](Registry&){c.replace({});};rejects(ModuleErrorCode::ReentrantInstall,[&]{c.replace({&bad,1});});
    check(c.snapshot()==original,"Reentrant install replaced registry");
    c.replace({&a,1});check(c.snapshot()!=original,"Guard did not reset after exception");
    auto retained=c.snapshot();c.replace({});
    check(retained->registry().factories.at("sample.a")()==7,"Old snapshot was invalidated by replacement");
    check(c.snapshot()->registry().factories.empty(),"Empty replacement did not install");
}
void lifetime() {
    std::vector<int> released;
    Composition::Handle survivor;
    {
        Composition c;
        auto a=std::make_shared<Provider>(Provider{&released,1});released.clear();
        auto b=std::make_shared<Provider>(Provider{&released,2});released.clear();
        std::weak_ptr<Provider> weak=a;
        auto ea=entry("sample.a"),eb=entry("sample.b");eb.descriptor.dependencies.push_back({"sample.a",{}});
        ea.install=[a](Registry& r){r.factories.emplace("sample.a",[p=a.get()]{return p->id;});};
        eb.install=[b](Registry& r){r.factories.emplace("sample.b",[p=b.get()]{return p->id;});};
        std::vector<Entry> inputs{eb,ea};survivor=c.replace(inputs);
        inputs.clear();ea.install={};eb.install={};a.reset();b.reset();
        check(!weak.expired()&&released.empty(),"Provider lost while snapshot lives");
        c.replace({});check(survivor->registry().factories.at("sample.a")()==1,"Old session lost provider");
    }
    check(released.empty(),"Composition destruction invalidated session");survivor.reset();
    check(released==std::vector<int>({2,1}),"Providers not released in reverse dependency order");
    // Failure destroys registry values before installer captures, even before publication.
    std::vector<std::string> log;
    struct Record {std::vector<std::string>* log;std::string text;~Record(){log->push_back(text);}};
    auto capture=std::make_shared<Record>(Record{&log,"provider"});log.clear();
    Composition c;auto a=entry("sample.a");
    a.install=[capture,&log](Registry& r){auto object=std::make_shared<Record>(Record{&log,"registry"});log.clear();r.factories.emplace("sample.a",[object]{return 1;});throw std::runtime_error("fail");};
    try{c.replace({&a,1});}catch(const std::runtime_error&){}
    check(log==std::vector<std::string>({"registry"}),"Staged registry did not unwind");a.install={};capture.reset();
    check(log==std::vector<std::string>({"registry","provider"}),"Registry/provider teardown order wrong");
}
}
int main(){try{descriptors();transactions();lifetime();std::cout<<"modules: "<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"modules: "<<e.what()<<'\n';return 1;}}
