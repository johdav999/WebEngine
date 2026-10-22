#include "editor_browser.hpp"
#include "prefab_example.hpp"
#include "engine/editor_prefab.hpp"
#include "engine/mesh_component.hpp"
#include "engine/light_component.hpp"
#include <emscripten.h>
#include <sstream>
#include <iomanip>

namespace engine {
namespace {
std::vector<std::shared_ptr<const PrefabAsset>> catalog;
std::unique_ptr<PrefabAssetEditor> prefabDocument;
std::string prefabScene;
std::uint32_t prefabBindingSerial=0;
EM_JS(void,prefabReport,(const char* text),{window.EnginePrefabEditor?.report(JSON.parse(UTF8ToString(text)));});
EM_JS(void,prefabError,(const char* text),{window.EnginePrefabEditor?.message(UTF8ToString(text),true);});
EM_JS(void,prefabDownload,(const void* data,int size),{window.EnginePrefabEditor?.download(HEAPU8.slice(data,data+size));});
void quote(std::ostream& out,std::string_view text){out<<'"';for(unsigned char c:text){if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c<32)out<<"\\u00"<<"0123456789abcdef"[c>>4]<<"0123456789abcdef"[c&15];else out<<char(c);}out<<'"';}
void put(std::vector<std::shared_ptr<const PrefabAsset>>& list,std::shared_ptr<const PrefabAsset> asset){for(auto& a:list)if(a->id()==asset->id()){if(a->revision()<=asset->revision())a=std::move(asset);return;}if(list.size()>=64)throw std::invalid_argument("Prefab session supports at most 64 assets");list.push_back(std::move(asset));}
void recover(){auto* scene=browserEditorScene();if(!scene)return;if(prefabScene==scene->savedId())return;catalog.clear();prefabDocument.reset();prefabScene=scene->savedId();scene->visit([&](const Node& n,const Mat4&){if(auto* link=dynamic_cast<const PrefabInstanceComponent*>(&n)){std::vector<std::shared_ptr<const PrefabAsset>> chain;for(auto a=link->asset();a;a=a->base())chain.push_back(a);for(auto it=chain.rbegin();it!=chain.rend();++it)put(catalog,*it);}});}
bool allowed(){return browserEditorActive()&&browserEditorSession()&&!browserEditorPlaying()&&!browserEditorFxActive()&&!browserEditorRegionActive()&&!browserEditorSession()->transforming();}
void stash(){if(prefabDocument&&prefabDocument->session().dirty())put(catalog,prefabDocument->snapshot(prefabDocument->definition().name));}
void report(){
    recover();std::ostringstream out;out<<std::setprecision(17)<<"{\"scene\":";quote(out,prefabScene);out<<",\"binding\":"<<prefabBindingSerial<<",\"assets\":[";bool comma=false;for(const auto& a:catalog){if(comma)out<<',';comma=true;out<<"{\"id\":";quote(out,a->id());out<<",\"name\":";quote(out,a->name());out<<",\"revision\":"<<a->revision()<<",\"base\":";quote(out,a->base()?a->base()->id():"");out<<'}';}out<<"],\"document\":";
    if(!prefabDocument)out<<"null";else{auto& doc=*prefabDocument;auto& session=doc.session();auto* node=session.selected();out<<"{\"id\":";quote(out,doc.source()->id());out<<",\"name\":";quote(out,doc.definition().name);out<<",\"base\":";quote(out,doc.source()->base()?doc.source()->base()->name():"");out<<",\"undo\":"<<session.undoCount()<<",\"redo\":"<<session.redoCount()<<",\"dirty\":"<<(session.dirty()?"true":"false")<<",\"selected\":";quote(out,node?node->savedId():"");out<<",\"nodes\":[";comma=false;doc.definition().visit([&](const Node& n,const Mat4&){if(comma)out<<',';comma=true;out<<"{\"key\":";quote(out,n.savedId());out<<",\"name\":";quote(out,n.name);out<<",\"type\":";quote(out,dynamic_cast<const Component*>(&n)?static_cast<const Component&>(n).typeName():"Actor");out<<",\"protected\":"<<(n.isProtectedChild()?"true":"false")<<'}';});out<<"],\"properties\":[";comma=false;for(const auto& p:session.properties()){if(comma)out<<',';comma=true;out<<"{\"key\":";quote(out,p.key);out<<",\"label\":";quote(out,p.label);out<<",\"group\":";quote(out,p.group);out<<",\"type\":"<<int(p.type)<<",\"value\":";std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,std::string>)quote(out,v);else if constexpr(std::is_same_v<T,bool>)out<<(v?"true":"false");else out<<v;},p.value);out<<",\"choices\":[";for(std::size_t i=0;i<p.choices.size();++i){if(i)out<<',';quote(out,p.choices[i]);}out<<"]}";}out<<"],\"transform\":[";if(node){const auto& t=node->transform;const float values[]={t.position.x,t.position.y,t.position.z,t.rotation.x,t.rotation.y,t.rotation.z,t.scale.x,t.scale.y,t.scale.z};for(int i=0;i<9;++i){if(i)out<<',';out<<values[i];}}out<<"]}";}
    out<<",\"instance\":";auto* session=browserEditorSession();auto* root=session?session->selectedPrefabRoot():nullptr;if(!root)out<<"null";else{auto* link=root->getComponent<PrefabInstanceComponent>();out<<"{\"root\":";quote(out,root->savedId());out<<",\"asset\":";quote(out,link->asset()->id());out<<",\"name\":";quote(out,link->asset()->name());out<<",\"revision\":"<<link->asset()->revision()<<",\"overrides\":[";comma=false;for(auto& p:prefabOverrides(*root,*EditorPropertyRegistry::builtins())){if(comma)out<<',';comma=true;out<<"{\"key\":";quote(out,p.key);out<<",\"node\":";quote(out,p.node);out<<",\"field\":";quote(out,p.field);out<<'}';}out<<"]}";}out<<'}';const auto text=out.str();prefabReport(text.c_str());
}
template<class F>int perform(F fn){try{if(!allowed())throw std::logic_error("Enter edit mode and stop previews before editing prefabs");recover();fn();refreshBrowserEditor();report();return 1;}catch(const std::exception& e){prefabError(e.what());try{report();}catch(...){}return 0;}}
std::shared_ptr<const PrefabAsset> commitDocument(){if(!prefabDocument)throw std::logic_error("Open or capture a prefab first");auto next=prefabDocument->session().dirty()?prefabDocument->snapshot(prefabDocument->definition().name):prefabDocument->source();return next;}
void publish(){auto next=commitDocument();auto staged=catalog;put(staged,next);std::vector<std::shared_ptr<const PrefabAsset>> changed{next};
    for(auto& asset:staged)asset=refreshPrefabDependencies(asset,changed,*EditorPropertyRegistry::builtins());
    // Prepare every derived definition before one grouped scene publication.
    browserEditorSession()->updatePrefabs(changed);catalog=std::move(staged);prefabDocument=std::make_unique<PrefabAssetEditor>(next);
}
}
void invalidateBrowserPrefabBinding(){catalog.clear();prefabDocument.reset();prefabScene.clear();++prefabBindingSerial;}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_editor_prefab_refresh(){try{report();return 1;}catch(const std::exception& e){prefabError(e.what());return 0;}}
EMSCRIPTEN_KEEPALIVE int engine_editor_prefab_open(const char* id){return perform([&]{if(!id)throw std::invalid_argument("Missing prefab ID");stash();for(auto& a:catalog)if(a->id()==id){prefabDocument=std::make_unique<PrefabAssetEditor>(a);return;}throw std::invalid_argument("Unknown prefab asset");});}
EMSCRIPTEN_KEEPALIVE int engine_editor_prefab_select(const char* key){return perform([&]{if(!prefabDocument||!key||!prefabDocument->session().select(key))throw std::invalid_argument("Unknown prefab node");});}
EMSCRIPTEN_KEEPALIVE int engine_editor_prefab_nest(const char* id){return perform([&]{if(!prefabDocument||!id)throw std::invalid_argument("Open a parent prefab first");for(const auto& asset:catalog)if(asset->id()==id){if(!prefabDocument->nestPrefab(asset,{0,2,0}))throw std::invalid_argument("Cannot nest this prefab");return;}throw std::invalid_argument("Unknown nested prefab source");});}
EMSCRIPTEN_KEEPALIVE int engine_editor_prefab_nested_source(){return perform([&]{if(!prefabDocument)throw std::invalid_argument("Open a parent prefab first");auto* actor=prefabDocument->session().selectedPrefabRoot();if(!actor)throw std::invalid_argument("Select a nested instance or one of its children");auto asset=actor->getComponent<PrefabInstanceComponent>()->asset();stash();put(catalog,asset);prefabDocument=std::make_unique<PrefabAssetEditor>(asset);});}
EMSCRIPTEN_KEEPALIVE int engine_editor_prefab_action(int action){return perform([&]{
    auto* session=browserEditorSession();if(action==0){auto* actor=dynamic_cast<Actor*>(session->selected());if(!actor)throw std::invalid_argument("Select an actor to capture a prefab");stash();auto asset=PrefabAsset::capture(*actor,actor->name);if(!session->linkPrefab(asset))throw std::invalid_argument("Select an unlinked editable actor");put(catalog,asset);prefabDocument=std::make_unique<PrefabAssetEditor>(asset);}
    else if(action==1){auto asset=commitDocument();put(catalog,asset);if(!session->placePrefab(asset,{float(catalog.size()%3)*2,0,2}))throw std::invalid_argument("Could not place prefab");prefabDocument=std::make_unique<PrefabAssetEditor>(asset);}
    else if(action==2){auto base=commitDocument();put(catalog,base);auto definition=prefabDefinition(base->definition());definition->name=base->name()+" variant";auto variant=PrefabAsset::create(std::move(definition),base->name()+" variant",{},1,base);put(catalog,variant);prefabDocument=std::make_unique<PrefabAssetEditor>(variant);}
    else if(action==3)publish();
    else if(action==4){auto* root=session->selectedPrefabRoot();if(!root)throw std::invalid_argument("Select a linked prefab instance");auto asset=root->getComponent<PrefabInstanceComponent>()->asset();for(auto& a:catalog)if(a->id()==asset->id())asset=a;const std::array assets={asset};if(!session->updatePrefabs(assets,true))throw std::invalid_argument("Cannot revert this prefab instance");}
    else if(action==5){if(!session->unpackPrefab())throw std::invalid_argument("Select a prefab instance to unpack");}
    else if(action==6){stash();prefabDocument.reset();}
    else if(action==7||action==8){if(!prefabDocument)throw std::logic_error("No prefab document");if(action==7)prefabDocument->session().undo();else prefabDocument->session().redo();}
    else if(action==9){auto asset=commitDocument();auto bytes=PrefabAssetArchive::save(asset);prefabDownload(bytes.data(),int(bytes.size()));put(catalog,asset);prefabDocument=std::make_unique<PrefabAssetEditor>(asset);}
    else if(action==10){auto asset=makePrefabExample();put(catalog,asset);session->placePrefab(asset,{-2,1,0});session->placePrefab(asset,{2,1,0});prefabDocument=std::make_unique<PrefabAssetEditor>(asset);}
    else if(action>=11&&action<=13){if(!prefabDocument)throw std::logic_error("No prefab document");auto& edit=prefabDocument->session();bool ok=action==11?edit.addComponent("MeshComponent",Mesh::cube()):action==12?edit.addComponent("LightComponent"):edit.removeSelectedComponents();if(!ok)throw std::invalid_argument("Select an editable actor or component");}
    else throw std::invalid_argument("Unknown prefab action");
});}
EMSCRIPTEN_KEEPALIVE int engine_editor_prefab_property(const char* key,int type,double number,const char* text){return perform([&]{if(!prefabDocument||!key)throw std::invalid_argument("No prefab property selected");EditorValue value;if(type==int(EditorPropertyType::Text)){if(!text)throw std::invalid_argument("Missing text");value=std::string(text);}else if(type==int(EditorPropertyType::Boolean)){if(number!=0&&number!=1)throw std::invalid_argument("Invalid Boolean");value=number!=0;}else value=number;if(!prefabDocument->session().setProperty(key,value))throw std::invalid_argument("Prefab property is read-only");});}
EMSCRIPTEN_KEEPALIVE int engine_editor_prefab_transform(const float* values,int count){return perform([&]{if(!prefabDocument||!values||count!=9)throw std::invalid_argument("Invalid prefab transform");Transform t;t.position={values[0],values[1],values[2]};t.rotation={values[3],values[4],values[5]};t.scale={values[6],values[7],values[8]};if(!prefabDocument->session().setTransform(t))throw std::invalid_argument("Prefab transform is read-only");});}
EMSCRIPTEN_KEEPALIVE int engine_editor_prefab_file(const void* bytes,int count){return perform([&]{if(!bytes||count<1||count>64*1024*1024)throw std::invalid_argument("Prefab file exceeds 64 MiB");auto asset=PrefabAssetArchive::load(std::span(static_cast<const std::byte*>(bytes),std::size_t(count)));for(const auto& current:catalog)if(current->id()==asset->id()&&current->revision()>asset->revision())throw std::invalid_argument("This session already has a newer prefab revision");auto candidate=std::make_unique<PrefabAssetEditor>(asset);stash();auto staged=catalog;std::vector<std::shared_ptr<const PrefabAsset>> chain;for(auto a=asset;a;a=a->base())chain.push_back(a);for(auto it=chain.rbegin();it!=chain.rend();++it)put(staged,*it);catalog=std::move(staged);prefabDocument=std::move(candidate);});}
}
}
