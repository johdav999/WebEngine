// Browser authoring adapter; all mutations go through portable EditorSession commands.
namespace {
std::vector<std::shared_ptr<const PhysicalSurface>> physicsCatalog;
std::vector<std::shared_ptr<const CollisionShape>> physicsShapes;
EM_JS(void, physics_editor_begin,(int available,int terrain,int x,int z,int debug,const char* target),{window.__physicsEditor={debug:!!debug,available:!!available,terrain:!!terrain,x,z,target:UTF8ToString(target),surfaces:[],shapes:[],assets:[]};});
EM_JS(void, physics_editor_surface,(const char* id,float friction,float restitution,float grip,float rolling),{window.__physicsEditor.surfaces.push({id:UTF8ToString(id),friction,restitution,grip,rolling});});
EM_JS(void, physics_editor_asset,(int type),{window.__physicsEditor.assets.push(type);});
EM_JS(void, physics_editor_shape,(const char* path,int type,int children,int triangles,int surface,const float* values),{window.__physicsEditor.shapes.push({path:UTF8ToString(path),type,children,triangles,surface,values:Array.from(new Float32Array(HEAPU8.buffer,values,9))});});
EM_JS(void, physics_editor_end,(),{window.EnginePhysicsEditor?.inspect(window.__physicsEditor);});
ColliderComponent* physicsCollider(){auto* node=session?session->selected():nullptr;if(auto* c=dynamic_cast<ColliderComponent*>(node))return c;auto* actor=dynamic_cast<Actor*>(node);if(!actor)if(auto* c=dynamic_cast<Component*>(node))actor=c->owner();return actor?actor->getComponent<ColliderComponent>():nullptr;}
std::vector<unsigned> physicsPath(const char* text){std::vector<unsigned> result;if(!text||!*text)return result;std::string value(text);std::size_t at=0;while(at<value.size()){std::size_t end;const auto n=std::stoul(value.substr(at),&end);if(n>=32||!end||result.size()>=8)throw std::invalid_argument("Invalid compound path");result.push_back(unsigned(n));at+=end;if(at<value.size()){if(value[at++]!='.'||at==value.size())throw std::invalid_argument("Invalid compound path");}}return result;}
std::shared_ptr<const CollisionShape> shapeAt(std::shared_ptr<const CollisionShape> shape,const std::vector<unsigned>& path){for(auto index:path){if(index>=shape->children().size())throw std::invalid_argument("Stale compound path");shape=shape->children()[index].shape;}return shape;}
std::shared_ptr<const CollisionShape> replaceAt(std::shared_ptr<const CollisionShape> root,const std::vector<unsigned>& path,std::shared_ptr<const CollisionShape> value,std::size_t at=0){if(at==path.size())return value;auto children=root->children();if(path[at]>=children.size())throw std::invalid_argument("Stale compound path");children[path[at]].shape=replaceAt(children[path[at]].shape,path,std::move(value),at+1);return CollisionShape::compound(children)->withSurfaces(root->surface());}
int surfaceIndex(const std::shared_ptr<const PhysicalSurface>& value){auto found=std::find(physicsCatalog.begin(),physicsCatalog.end(),value);return found==physicsCatalog.end()?-1:int(found-physicsCatalog.begin());}
std::shared_ptr<const PhysicalSurface> catalogSurface(int index){if(index< -1||index>=int(physicsCatalog.size()))throw std::invalid_argument("Stale physical surface selection");return index<0?nullptr:physicsCatalog[index];}
void reportPhysicsAuthoring(){
    if(!session||!document)return;const auto used=SceneArchive::collectAssets(*document);physicsCatalog.clear();physicsShapes.clear();
    auto append=[](auto& to,const auto& from){for(const auto& resource:from)if(resource&&std::find(to.begin(),to.end(),resource)==to.end())to.push_back(resource);};
    append(physicsCatalog,session->assets().physicalSurfaces);append(physicsCatalog,used.physicalSurfaces);append(physicsShapes,session->assets().collisionShapes);append(physicsShapes,used.collisionShapes);
    auto* terrain=session->terrainActor();auto* collider=physicsCollider();const auto asset=terrain?terrain->getComponents<TerrainComponent>().front()->asset():nullptr;
    physics_editor_begin(collider||terrain,bool(terrain),asset?asset->data().samplesX-1:0,asset?asset->data().samplesZ-1:0,document->physicsIfCreated()&&document->physicsIfCreated()->debugDrawEnabled(),collider?collider->savedId().c_str():"");
    for(const auto& surface:physicsCatalog)physics_editor_surface(surface->id.c_str(),surface->friction,surface->restitution,surface->tyreGrip,surface->rollingResistance);
    for(const auto& shape:physicsShapes)physics_editor_asset(int(shape->type()));
    if(collider){
        auto walk=[&](auto&& self,const CollisionShape& shape,const std::string& path,const Transform& transform,std::shared_ptr<const PhysicalSurface> assigned)->void{
            auto h=shape.halfExtents();const float values[]={transform.position.x,transform.position.y,transform.position.z,transform.rotation.x,transform.rotation.y,transform.rotation.z,h.x,h.y,h.z};physics_editor_shape(path.c_str(),int(shape.type()),shape.children().size(),shape.indices().size()/3,surfaceIndex(assigned),values);
            for(std::size_t i=0;i<shape.children().size();++i){const auto& c=shape.children()[i];self(self,*c.shape,path.empty()?std::to_string(i):path+"."+std::to_string(i),c.transform,c.surface);}
        };walk(walk,*collider->shape(),"",{},collider->surface());
    }
    physics_editor_end();
}
bool assignPhysicsSurface(int scope,const char* path,int first,int count,std::shared_ptr<const PhysicalSurface> surface){
    if(scope==4||scope==5){auto* actor=session->terrainActor();if(!actor)return false;const auto& data=actor->getComponents<TerrainComponent>().front()->asset()->data();auto cells=data.cellSurfaces;if(scope==4)return session->setTerrainPhysicalSurfaces(std::move(surface),std::move(cells));
        const auto total=std::size_t(data.samplesX-1)*(data.samplesZ-1);if(first<0||count<1||std::uint64_t(first)+count>total)throw std::invalid_argument("Terrain physical cell range is invalid");if(cells.empty())cells.resize(total);for(int i=0;i<count;++i)cells[first+i]=surface;return session->setTerrainPhysicalSurfaces(data.physicalSurface,std::move(cells));}
    auto* c=physicsCollider();if(!c)return false;const auto id=c->savedId();if(session->selected()!=c)session->select(id);
    if(scope==0)return session->setCollider(c->shape(),std::move(surface));
    auto indices=physicsPath(path);auto root=c->shape(),target=shapeAt(root,indices);
    if(scope==1)target=target->withSurfaces(std::move(surface),target->triangleSurfaces());
    else if(scope==2){if(indices.empty())throw std::invalid_argument("Choose a compound child path");const auto child=indices.back();indices.pop_back();target=shapeAt(root,indices);auto children=target->children();children[child].surface=std::move(surface);target=CollisionShape::compound(children)->withSurfaces(target->surface());}
    else if(scope==3){if(target->type()!=CollisionShapeType::TriangleMesh||first<0||count<1||std::uint64_t(first)+count>target->indices().size()/3)throw std::invalid_argument("Physical triangle range is invalid");auto triangles=target->triangleSurfaces();if(triangles.empty())triangles.resize(target->indices().size()/3);for(int i=0;i<count;++i)triangles[first+i]=surface;target=target->withSurfaces(target->surface(),std::move(triangles));}
    else throw std::invalid_argument("Unknown physical binding scope");
    return session->setCollider(replaceAt(root,indices,std::move(target)),c->surface());
}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_editor_physics_surface(int operation,int index,int scope,const char* path,int first,int count,const char* id,float friction,float restitution,float grip,float rolling){return command([&]{
    if(operation==0||operation==2){auto surface=std::make_shared<PhysicalSurface>();surface->id=id?id:"";surface->friction=friction;surface->restitution=restitution;surface->tyreGrip=grip;surface->rollingResistance=rolling;surface->validate();if(operation==0){AssetLibrary library;library.physicalSurfaces.push_back(surface);return session->importAssets(library);}return assignPhysicsSurface(scope,path,first,count,std::move(surface));}
    return assignPhysicsSurface(scope,path,first,count,catalogSurface(index));
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_physics_shape(int operation,const char* path,int type,const float* values){return command([&]{
    auto* c=physicsCollider();if(!c||!values)return false;for(int i=0;i<9;++i)if(!std::isfinite(values[i]))throw std::invalid_argument("Nonfinite shape input");if(session->selected()!=c)session->select(c->savedId());
    auto indices=physicsPath(path);auto root=c->shape(),target=shapeAt(root,indices);Transform t;t.position={values[0],values[1],values[2]};t.rotation={values[3],values[4],values[5]};
    auto primitive=[&]()->std::shared_ptr<const CollisionShape>{if(type==0)return CollisionShape::box({values[6],values[7],values[8]});if(type==1)return CollisionShape::sphere(values[6]);if(type==2)return CollisionShape::capsule(values[7],values[6]);if(type>=10&&type-10<int(physicsShapes.size()))return physicsShapes[type-10];throw std::invalid_argument("Choose a primitive or collision asset");};
    if(operation==0){const CompoundChild child{target,{},{}};target=CollisionShape::compound({&child,1});}
    else if(operation==1){auto children=target->children();if(target->type()!=CollisionShapeType::Compound)throw std::invalid_argument("Select a compound parent");children.push_back({primitive(),t,{}});target=CollisionShape::compound(children)->withSurfaces(target->surface());}
    else if(operation==2||operation==3){if(indices.empty())throw std::invalid_argument("Choose a child path");const auto child=indices.back();indices.pop_back();target=shapeAt(root,indices);auto children=target->children();if(operation==2)children.erase(children.begin()+child);else children[child].transform=t;target=CollisionShape::compound(children)->withSurfaces(target->surface());}
    else if(operation==4)target=primitive();else return false;
    return session->setCollider(replaceAt(root,indices,std::move(target)),c->surface());
},true);}
EMSCRIPTEN_KEEPALIVE int engine_editor_physics_debug(int enabled){if(!session||play.playing())return 0;document->physics().setDebugDrawEnabled(enabled!=0);return 1;}
}
