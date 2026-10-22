#include "engine/modules.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace engine {
namespace {
[[noreturn]] void fail(ModuleErrorCode code,const std::string& id,const std::string& message) {
    throw ModuleError(code,id,message+(id.empty()?"":" ["+id+"]"));
}
bool key(const std::string& value) {
    if(value.empty()||value.size()>ModuleLimits::keyBytes||value.front()=='.'||value.back()=='.') return false;
    bool dot=false,previousDot=false;
    for(unsigned char c:value) {
        if(c=='.') { if(previousDot)return false; dot=true;previousDot=true; }
        else { if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;previousDot=false; }
    }
    return dot;
}
void range(const ModuleVersionRange& r,const std::string& id) {
    if(!(r.minimum<r.maximum))fail(ModuleErrorCode::InvalidDescriptor,id,"Empty or reversed API version range");
}
void bounded(std::size_t size,const std::string& id) {
    if(size>ModuleLimits::entriesPerModule)fail(ModuleErrorCode::Limit,id,"Too many descriptor entries");
}
void validKey(const std::string& value,const std::string& owner) {
    if(!key(value))fail(ModuleErrorCode::InvalidDescriptor,owner,"Invalid namespaced key: "+value.substr(0,ModuleLimits::keyBytes));
}
}
std::vector<std::size_t> validateModules(std::span<const ModuleDescriptor> input,const ModuleHost& host) {
    if(input.size()>ModuleLimits::modules)fail(ModuleErrorCode::Limit,"","Too many modules");
    bounded(host.capabilities.size(),"");
    std::set<std::string> capabilities;
    for(const auto& c:host.capabilities) {
        validKey(c,"");
        if(!capabilities.insert(c).second)fail(ModuleErrorCode::InvalidDescriptor,"","Duplicate host capability");
    }
    std::map<std::string,std::size_t> indices;
    std::set<std::string> schemas;
    for(std::size_t i=0;i<input.size();++i) {
        const auto& d=input[i];validKey(d.id,d.id);range(d.engineApi,d.id);
        if(!indices.emplace(d.id,i).second)fail(ModuleErrorCode::DuplicateModule,d.id,"Duplicate module ID");
        if(!d.engineApi.contains(host.engineApi))fail(ModuleErrorCode::IncompatibleVersion,d.id,"Unsupported module-composition engine API");
        bounded(d.dependencies.size(),d.id);bounded(d.schemas.size(),d.id);bounded(d.requiredCapabilities.size(),d.id);
        std::set<std::string> deps,caps;
        for(const auto& dep:d.dependencies) {
            validKey(dep.id,d.id);range(dep.versions,d.id);
            if(!deps.insert(dep.id).second)fail(ModuleErrorCode::InvalidDescriptor,d.id,"Duplicate dependency: "+dep.id);
        }
        for(const auto& schema:d.schemas) {
            validKey(schema.id,d.id);
            if(!schemas.insert(schema.id).second)fail(ModuleErrorCode::DuplicateSchema,d.id,"Duplicate schema provider: "+schema.id);
        }
        for(const auto& c:d.requiredCapabilities) {
            validKey(c,d.id);
            if(!caps.insert(c).second)fail(ModuleErrorCode::InvalidDescriptor,d.id,"Duplicate required capability");
            if(!capabilities.contains(c))fail(ModuleErrorCode::MissingCapability,d.id,"Missing host capability: "+c);
        }
    }
    for(const auto& d:input)for(const auto& dep:d.dependencies) {
        const auto found=indices.find(dep.id);
        if(found==indices.end())fail(ModuleErrorCode::MissingDependency,d.id,"Missing dependency: "+dep.id);
        if(!dep.versions.contains(input[found->second].version))fail(ModuleErrorCode::IncompatibleVersion,d.id,"Incompatible dependency: "+dep.id);
    }
    std::vector<std::size_t> result;result.reserve(input.size());
    std::vector<bool> emitted(input.size(),false);
    while(result.size()<input.size()) {
        bool progress=false;
        for(const auto& [id,i]:indices) {
            if(emitted[i])continue;
            const auto& deps=input[i].dependencies;
            if(!std::all_of(deps.begin(),deps.end(),[&](const auto& d){return emitted[indices.at(d.id)];}))continue;
            emitted[i]=true;result.push_back(i);progress=true;break;
        }
        if(!progress) {
            for(const auto& [id,i]:indices)if(!emitted[i])fail(ModuleErrorCode::DependencyCycle,id,"Dependency cycle or dependency blocked by a cycle");
        }
    }
    return result;
}
} // namespace engine
