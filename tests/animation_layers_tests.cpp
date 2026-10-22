#include "engine/animation_component.hpp"
#include "engine/editor_animation.hpp"
#include "engine/editor.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include <iostream>
#include <limits>
using namespace engine;
namespace {
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
bool near(double a,double b){return std::abs(a-b)<1e-4;}
template<class F>void rejects(F f){bool threw=false;try{f();}catch(const std::exception&){threw=true;}check(threw,"Expected invalid input rejection");}
std::shared_ptr<Skeleton> rig(){
    auto s=std::make_shared<Skeleton>();s->joints={{"Root",-1,{}},{"Leg",0,{}},{"Arm",0,{}}};
    s->bindings={{},{0,Mat4::identity()},{1,Mat4::identity()},{2,Mat4::identity()}};
    for(int i=0;i<5;++i){
        AnimationClip c;c.name="Clip "+std::to_string(i);c.duration=i==2?2:1;c.frameCount=2;c.frames.resize(6);
        c.frames[3].translation.x=float(i==1?2:i==2?8:0);
        c.frames[4].translation.y=float(i==1?2:i==2?4:0);
        c.frames[2].translation.z=c.frames[5].translation.z=float(i==3?4:i==4?2:0);
        if(i==1||i==2)c.markers={{c.duration/2,"step",""}};
        s->clips.push_back(c);
    }s->validate();return s;
}
std::shared_ptr<AnimationControllerDefinition> graph(){
    auto d=std::make_shared<AnimationControllerDefinition>();
    d->parameters={{"speed",AnimationParameterType::Number,0.0},{"sideways",AnimationParameterType::Number,0.0},{"carry",AnimationParameterType::Number,0.0}};
    d->states={{"Locomotion",0,true,1,{},{}}};
    d->states[0].tree=AnimationBlendTree{0,{},{{0,0,0},{1,1,0},{2,2,0}},true};
    AnimationLayer layer;layer.name="Carry";layer.motion.clip=3;layer.mask={0,0,1};layer.weightParameter=2;
    d->layers.push_back(layer);return d;
}
float coordinate(const SkeletonComponent& s,int bone,int element){return s.skinPose()->bones[bone].transform.m[element];}
void weights(){
    auto d=graph();auto& t=*d->states[0].tree;auto w=animationBlendWeights(t,.5);
    check(near(w.values[0],.5)&&near(w.values[1],.5)&&w.values[2]==0,"1D interpolates adjacent idle/walk samples");
    w=animationBlendWeights(t,1.5);check(w.values[0]==0&&near(w.values[1],.5)&&near(w.values[2],.5),"Walk/run interpolation");
    w=animationBlendWeights(t,std::numeric_limits<double>::max());check(w.values[2]==1,"Finite extreme speed clamps safely");
    std::swap(t.points[0],t.points[2]);w=animationBlendWeights(t,0);check(w.values[2]==1,"Unsorted sample points work");
    t={0,1,{{0,0,0},{1,1,0},{2,-1,0},{3,0,1},{4,0,-1}},true};
    w=animationBlendWeights(t,0,0);check(w.values[0]==1,"2D exact center");
    w=animationBlendWeights(t,.5,.5);float sum=0;for(auto v:w.values)sum+=v;check(near(sum,1)&&w.values[1]>0&&w.values[3]>0,"2D diagonal direction");
    for(int x=-20;x<=20;++x)for(int y=-20;y<=20;++y){w=animationBlendWeights(t,x*.1,y*.1);sum=0;for(auto v:w.values){check(std::isfinite(v)&&v>=0,"2D weights finite/nonnegative");sum+=v;}check(near(sum,1),"2D weights normalize outside hull");}
    t.points.push_back(t.points[0]);rejects([&]{d->validate(*rig());});
}
void playback(){
    SkeletonComponent s;s.setSkeleton(rig());AnimationComponent p("Player",s);auto d=graph();p.setController(d);
    p.setParameter("speed",1.5);p.setParameter("carry",1.0);p.update(1);
    check(near(coordinate(s,1,12),2.5),"Synchronized walk/run root at half cycle");
    check(near(coordinate(s,2,13),1.5)&&near(coordinate(s,3,14),4),"Carry affects arm while legs continue");
    check(p.events().size()==2&&near(p.events()[0].time,1)&&near(p.events()[1].time,1),"Phase synchronized source markers use common clock");
    check(near(p.events()[0].weight,.5)&&near(p.events()[1].weight,.5),"Marker source contributions");
    p.setParameter("carry",.5);p.update(0);check(near(coordinate(s,3,14),2)&&near(p.time(),1),"Weight updates on zero tick without reset");
    p.pause();const auto layerTime=p.layerTime("Carry");p.update(1);check(near(p.layerTime("Carry"),layerTime),"Pause freezes layer clocks");
    p.resume();p.update(.1);p.restartLayer("Carry");check(near(p.layerTime("Carry"),0)&&near(p.time(),1.1),"Restart affects only selected layer");
    p.enableRootMotion();p.consumeOutput();p.update(.2);check(near(p.consumeRootMotion().translation.x,.5),"Tree root movement uses synchronized clip intervals");
    check(near(coordinate(s,1,12),0),"Base root remains anchored");
    p.setParameter("carry",1.0);p.update(.1);check(near(p.consumeRootMotion().translation.x,.25),"Overlay cannot inject root movement");
    auto invalid=std::make_shared<AnimationControllerDefinition>(*d);invalid->layers[0].mask={1};
    const auto time=p.time();rejects([&]{p.setController(invalid);});check(p.controller()==d&&p.time()==time,"Invalid replacement preserves playback");
    invalid=std::make_shared<AnimationControllerDefinition>(*d);invalid->layers[0].motion.speed=std::numeric_limits<double>::max();p.setController(invalid);
    rejects([&]{p.update(2);});check(p.time()==0&&p.layerTime("Carry")==0,"Layer clock overflow is atomic");
    p.setController(d);s.setSkeleton(rig());p.update(0);check(!p.controller()&&!p.rootMotionEnabled(),"Skeleton replacement cancels layers");
}
void additiveAndTransitions(){
    auto sAsset=rig();SkeletonComponent s;s.setSkeleton(sAsset);AnimationComponent p("Player",s);
    auto d=graph();d->layers[0].weightParameter.reset();d->layers[0].weight=.5;
    auto look=d->layers[0];look.name="Look";look.mode=AnimationLayerMode::Additive;look.motion.clip=4;look.weight=1;
    d->layers.push_back(look);p.setController(d);check(near(coordinate(s,3,14),4),"Override then additive combines pose");
    p.crossFade(0,.5);check(!p.controller()&&near(coordinate(s,3,14),4),"Manual crossfade starts from the visible layered pose");
    p.update(.25);check(near(coordinate(s,3,14),2),"Manual crossfade smoothly removes the overlay");
    d=std::make_shared<AnimationControllerDefinition>(*d);d->layers[1].referenceClip=3;
    p.setController(d);check(near(coordinate(s,3,14),0),"Additive reference clip subtraction");
    d=graph();d->layers[0].weightParameter.reset();d->states.push_back({"Action",1,false,1,{},{}});
    d->transitions={{0,1,.5,{},{{0,AnimationComparison::Greater,1.0}}}};p.setController(d);p.update(.3);p.setParameter("speed",2.0);p.update(.2);
    check(near(p.layerTime("Carry"),.5)&&near(coordinate(s,3,14),4),"Transition preserves overlay clock and does not double fade overlays");
    p.update(2);const auto before=p.layerTime("Carry");p.update(.2);
    check(p.state()==AnimationState::Paused&&near(p.layerTime("Carry"),before+.2),"Layers continue over a completed base pose");
    p.pause();p.update(.2);check(near(p.layerTime("Carry"),before+.2),"Explicit pause freezes completed-base overlays");
    auto mask=animationSubtreeMask(*sAsset,2,.4f);check(mask.size()==3&&near(mask[2],.4)&&mask[0]==0&&mask[1]==0,"Subtree mask isolates arm");
    d=graph();d->layers[0].mask.clear();d->layers[0].motion.clip=1;d->layers[0].weightParameter.reset();p.setController(d);p.enableRootMotion();p.update(.5);
    check(near(coordinate(s,1,12),0),"Extraction protects locomotion ancestor chain from all-joint layers");
    auto rotated=rig();
    for(auto index:{2,5}){auto& pose=rotated->clips[4].frames[index];pose.rotation={0,0,float(std::sqrt(.5)),float(std::sqrt(.5))};pose.scale={2,2,2};}
    s.setSkeleton(rotated);d=graph();d->layers[0].mode=AnimationLayerMode::Additive;d->layers[0].motion.clip=4;d->layers[0].weight=.5;d->layers[0].weightParameter.reset();
    p.setController(d);const auto& bone=s.skinPose()->bones[3].transform;
    check(near(bone.m[0],1.5*std::sqrt(.5))&&near(bone.m[1],1.5*std::sqrt(.5)),"Additive rotation takes shortest arc and scale multiplies weighted ratio");
    d=graph();d->states[0].tree->parameterY=1;d->states[0].tree->points={{3,-1,0},{4,1,0}};
    p.setController(d);p.update(0);check(near(coordinate(s,3,14),3),"2D sources contribute to actual joint pose");
    auto noSync=graph();noSync->states[0].tree->synchronizePhase=false;p.setController(noSync);p.setParameter("speed",1.5);p.update(.5);
    check(near(coordinate(s,1,12),1.5),"Unsynchronized tree retains independent clip seconds");
    auto invalid=graph();invalid->states[0].blend=AnimationBlend{1,0,0,1};rejects([&]{p.setController(invalid);});
    invalid=graph();invalid->layers[0].weightParameter=100;rejects([&]{p.setController(invalid);});
    invalid=graph();invalid->layers[0].mask[1]=std::numeric_limits<float>::quiet_NaN();rejects([&]{p.setController(invalid);});
    auto reflected=rig();for(auto index:{2,5})reflected->clips[4].frames[index].scale={-1,-1,-1};
    s.setSkeleton(reflected);p.play(0);p.update(.1);const auto beforePose=coordinate(s,3,14);
    invalid=graph();invalid->layers[0].mode=AnimationLayerMode::Additive;invalid->layers[0].motion.clip=4;invalid->layers[0].weight=.5;invalid->layers[0].weightParameter.reset();
    rejects([&]{p.setController(invalid);});check(!p.controller()&&near(p.time(),.1)&&coordinate(s,3,14)==beforePose,"Invalid composed pose rejects before controller publication");
}
void persistence(){
    auto geometry=std::make_shared<Mesh>(*Mesh::cube());geometry->skeleton=rig();Scene scene("Layers");auto& actor=scene.addChild<Actor>("Character");auto& mesh=actor.addComponent<SkeletalMeshComponent>("Skin",geometry);
    mesh.animation().setController(graph());mesh.animation().setParameter("speed",1.5);mesh.animation().setParameter("carry",.75);scene.update(.3);
    const auto bytes=SceneArchive::save(scene);auto restored=SceneArchive::load(bytes);auto* copy=dynamic_cast<SkeletalMeshComponent*>(restored.scene->findSavedNode(mesh.savedId()));
    check(copy&&near(copy->animation().layerTime("Carry"),.3)&&copy->animation().controller()->states[0].tree.has_value(),"Scene retains tree and layer clocks");
    check(SceneArchive::save(*restored.scene)==bytes,"Scene exact archive roundtrip");
    scene.update(.2);restored.scene->update(.2);check(near(coordinate(mesh.skeleton(),3,14),coordinate(copy->skeleton(),3,14)),"Restored pose continues identically");
    auto library=AnimationLibrary::fromSkeleton(geometry->skeleton,graph());const auto libBytes=AnimationLibraryArchive::save(*library);
    auto loaded=AnimationLibraryArchive::load(libBytes);check(AnimationLibraryArchive::metadata(library->data())==AnimationLibraryArchive::metadata(loaded->data()),"Library retains all composition metadata");
    EditorSession editor(scene);editor.select(mesh.savedId());const auto before=SceneArchive::save(scene);check(editor.setAnimationLibrary(loaded),"Apply layered library");
    const auto applied=SceneArchive::save(scene);check(editor.undo()&&SceneArchive::save(scene)==before,"Undo restores layer runtime and buffers");check(editor.redo()&&SceneArchive::save(scene)==applied,"Redo restores composed state");
    auto target=std::make_shared<Skeleton>(*geometry->skeleton);std::swap(target->joints[1],target->joints[2]);for(auto& c:target->clips)for(std::size_t f=0;f<c.frameCount;++f)std::swap(c.frames[f*3+1],c.frames[f*3+2]);
    auto otherGeometry=std::make_shared<Mesh>(*geometry);otherGeometry->skeleton=target;SkeletalMeshComponent other("Reordered rig",otherGeometry);loaded->apply(other);
    check(other.animation().controller()->layers[0].mask[1]==1&&other.animation().controller()->layers[0].mask[2]==0,"Library maps masks by joint identity");
    auto old=AnimationLibraryArchive::metadata(AnimationLibrary::fromSkeleton(geometry->skeleton)->data());
    old.replace(5,1,"1");old.resize(old.size()-6); // Remove the state-tree, layer and IK suffixes.
    auto legacy=AnimationLibrary::create(AnimationLibraryArchive::parseMetadata(old,geometry->skeleton));check(legacy->controller()->layers.empty()&&!legacy->controller()->states[0].tree,"Legacy metadata defaults composition off");
}
}
int main(){try{weights();playback();additiveAndTransitions();persistence();std::cout<<"Animation layers and blend trees passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
