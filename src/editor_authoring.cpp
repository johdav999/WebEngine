#include "engine/editor_authoring.hpp"
#include "engine/editor_ai.hpp"
#include "engine/editor_animation.hpp"
#include "engine/editor_prefab.hpp"
#include "engine/editor_cinematic.hpp"
#include "engine/effect_archive.hpp"
#include "engine/effect_presets.hpp"
#include "engine/skeletal_mesh_component.hpp"
#include <charconv>
#include <deque>
#include <sstream>

namespace engine {
namespace {
double numeric(TextInput* field) {
    if(field->composing())throw std::invalid_argument("Finish text composition first");
    const auto& s=field->text();double n=0;auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),n);
    if(e!=std::errc{}||p!=s.data()+s.size()||!std::isfinite(n)||std::abs(n)>1e6)throw std::invalid_argument("Enter a finite number within +/-1000000");return n;
}
std::size_t index(TextInput* f,std::size_t count) {const auto n=numeric(f);if(n<0||n>=double(count)||n!=std::floor(n))throw std::invalid_argument("Index is outside the document");return std::size_t(n);}
std::vector<std::byte> bytes(std::string_view s){const auto b=std::as_bytes(std::span(s.data(),s.size()));return {b.begin(),b.end()};}
std::string text(std::span<const std::byte> b){return {reinterpret_cast<const char*>(b.data()),b.size()};}
// Small retained graph/timeline overview. Uses the same clipped draw list and
// pointer routing on native and browser; document commands remain in the owner.
class DocumentOverview final:public Widget {
public:
    struct Item {std::string label;float start=0,end=1;};
    std::vector<Item> items;bool timeline=false;
    std::function<void(std::size_t)> select;
protected:
    bool interactive()const override{return true;}
    void onPaint(GuiPainter& p) override {
        p.fill({0,0,p.size().x,p.size().y},{.035f,.055f,.08f,1});
        for(std::size_t i=0;i<items.size()&&i<12;++i){const float x=timeline?140+items[i].start*650:12+float(i%4)*200;const float y=timeline?8+float(i)*23:10+float(i/4)*43;const float w=timeline?std::max(3.f,(items[i].end-items[i].start)*650):186;p.fill({x,y,w,timeline?18.f:34.f},{.12f,.32f,.42f,1});auto length=std::min(items[i].label.size(),std::size_t(timeline?16:22));while(length<items[i].label.size()&&length>0&&(static_cast<unsigned char>(items[i].label[length])&0xc0)==0x80)--length;p.text(items[i].label.substr(0,length),{timeline?6:x+5,y+4},{.9f,.96f,1,1});}
    }
    GuiPointerResult onPointer(const GuiPointerEvent& e)override{if(e.action==GuiPointer::Up&&e.inside&&select){const int i=timeline?int((e.position.y-8)/23):int((e.position.y-10)/43)*4+int((e.position.x-12)/200);if(i>=0&&std::size_t(i)<items.size())select(std::size_t(i));}return GuiPointerResult::Handled;}
};
}
struct EditorAuthoringPanel::Impl {
    GuiSystem& gui;Scene& scene;EditorSession& editor;Panel* root;TextLabel* message;
    std::array<ScrollPanel*,6> pages{};std::vector<Button*> buttons;std::vector<TextInput*> fields;
    std::deque<std::function<void()>> commands;std::function<void(bool)> file;
    EditorAuthoringArea area=EditorAuthoringArea::Terrain;bool editable=true;
    std::string status="Authoring tools. Scene Undo/Redo and document Undo/Redo are separate.";
    std::unique_ptr<AiGraphEditor> ai;
    std::unique_ptr<AnimationLibraryEditor> animation;
    std::unique_ptr<PrefabAssetEditor> prefab;
    std::unique_ptr<CinematicSequenceEditor> cinematic;
    std::shared_ptr<const PrefabAsset> nested;
    std::shared_ptr<const EffectAsset> effect;
    std::vector<std::shared_ptr<const EffectAsset>> fxHistory;std::size_t fxCursor=0;
    std::shared_ptr<const EffectAsset> savedEffect;bool prefabUnsaved=false;std::uint32_t prefabRevision=1;
    TextLabel *aiSummary,*animSummary,*prefabSummary,*fxSummary,*cineSummary;
    DocumentOverview *aiOverview=nullptr,*animationOverview=nullptr,*cinematicOverview=nullptr;
    TextInput *tx,*tz,*radius,*strength,*height,*brushMode;
    TextInput *aiIndex,*aiName,*aiBehavior,*aiFrom,*aiTo,*aiCondition,*aiDelay;
    TextInput *clipIndex,*clipLabel,*clipSpeed,*clipLoop,*animFrom,*animTo,*animFade;
    TextInput *prefabNode,*prefabName,*prefabX,*prefabY,*prefabZ;
    TextInput *emitter,*rate,*lifeMin,*lifeMax,*sizeMin,*sizeMax;
    TextInput *trackIndex,*trackType,*trackName,*trackResource,*trackStart,*trackEnd,*keyTime;
    Impl(GuiSystem& g,Scene& s,EditorSession& e):gui(g),scene(s),editor(e) {
        StateMachineData graph;graph.states={{0,"Idle","idle",{80,80}}};ai=std::make_unique<AiGraphEditor>(StateMachineDefinition::create(graph));
        cinematic=std::make_unique<CinematicSequenceEditor>(CinematicSequence::create({}));
        effect=campfireEffect();savedEffect=effect;fxHistory={effect};
        root=&gui.root().add<Panel>();root->setClipChildren(true);root->setSize({900,650});
        const std::array<const char*,6> names{"Terrain","Animation","AI","Prefab","FX","Cinematic"};
        for(int i=0;i<6;++i){button(*root,names[i],{float(8+i*120),8},{114,32},[this,i]{area=EditorAuthoringArea(i);show();});pages[i]=&root->add<ScrollPanel>();pages[i]->setPosition({8,92});pages[i]->setSize({884,548});pages[i]->setPadding(10);pages[i]->setLayout(VerticalLayout{7});}
        button(*root,"Close tools",{738,8},{150,32},[this]{root->setVisible(false);});
        message=&root->add<TextLabel>();message->setPosition({12,48});message->setSize({870,38});message->setWrap(true,false);message->setLiveRegion(true);
        terrain();animations();graphs();prefabs();effects();cinematics();show();root->setVisible(false);refresh();
    }
    ~Impl(){commands.clear();for(auto* b:buttons)b->onClick({});for(auto* f:fields){f->onSubmit({});f->onChange({});}for(auto* v:{aiOverview,animationOverview,cinematicOverview})if(v)v->select={};root->setVisible(false);}
    Button& button(Widget& p,std::string name,Vec2 position,Vec2 size,std::function<void()> action){auto& b=p.add<Button>(name);b.setAccessibleName(name);b.setPosition(position);b.setSize(size);b.onClick([this,action=std::move(action)]{if(commands.size()<32)commands.push_back(action);else status="Authoring command queue full";});buttons.push_back(&b);return b;}
    void action(Widget& p,std::string name,std::function<void()> f){button(p,std::move(name),{},{820,32},std::move(f));}
    TextInput* field(Widget& p,std::string name,std::string value){auto& row=p.add<Panel>();row.setSize({820,34});row.style.background={0,0,0,0};auto& label=row.add<TextLabel>(name);label.setPosition({4,5});label.setSize({410,26});auto& f=row.add<TextInput>();f.setPosition({420,0});f.setSize({390,32});f.setAccessibleName(name);f.setText(value);fields.push_back(&f);return &f;}
    TextLabel* summary(Widget& p){auto& l=p.add<TextLabel>();l.setSize({820,100});l.setWrap(true,false);return &l;}
    void files(Widget& p){action(p,"Open / replace tool document",[this]{if(file)file(false);else throw std::runtime_error("No file adapter attached");});action(p,"Save tool document",[this]{if(file)file(true);else throw std::runtime_error("No file adapter attached");});action(p,"Document undo",[this]{history(false);});action(p,"Document redo",[this]{history(true);});}
    void show(){for(int i=0;i<6;++i)pages[i]->setVisible(i==int(area));}
    void terrain(){auto& p=*pages[0];
        action(p,"Create terrain",[this]{TerrainData d;d.samplesX=d.samplesZ=17;d.chunkCells=16;d.lodLevels=1;d.heights.resize(289);if(!editor.createTerrain(d,{-8,0,-8}))throw std::runtime_error("Terrain creation rejected");});
        brushMode=field(p,"Brush mode (0 raise, 1 flatten, 2 smooth, 3 paint, 4 hole, 5 fill)","0");tx=field(p,"Brush local X","8");tz=field(p,"Brush local Z","8");radius=field(p,"Brush radius","3");strength=field(p,"Brush strength","1");height=field(p,"Flatten height / paint layer","0");
        action(p,"Stamp terrain",[this]{TerrainBrush b;b.mode=TerrainBrushMode(index(brushMode,6));b.center={float(numeric(tx)),float(numeric(tz))};b.radius=float(numeric(radius));b.strength=float(numeric(strength));b.targetHeight=float(numeric(height));if(b.mode==TerrainBrushMode::Paint)b.layer=std::uint32_t(index(height,4));b.validate();if(!editor.beginTerrainStroke())throw std::runtime_error("Select an editable terrain actor or chunk");try{editor.stampTerrain(b);if(!editor.commitTerrainStroke())throw std::runtime_error("Stroke made no change");}catch(...){editor.cancel();throw;}});
        action(p,"Scene undo",[this]{editor.undo();});action(p,"Scene redo",[this]{editor.redo();});
    }
    void animations(){auto& p=*pages[1];animationOverview=&p.add<DocumentOverview>();animationOverview->setSize({820,146});animationOverview->setAccessibleName("Animation state overview");animSummary=summary(p);
        action(p,"Capture selected animation library",[this]{auto* m=editor.selectedSkeletalMesh();if(!m)throw std::runtime_error("Select a skeletal mesh");animation=std::make_unique<AnimationLibraryEditor>(AnimationLibrary::fromSkeleton(m->skeleton().skeleton()));});
        clipIndex=field(p,"Animation clip index","0");clipLabel=field(p,"Clip label","Clip");clipSpeed=field(p,"Clip speed","1");clipLoop=field(p,"Clip loop (0 or 1)","1");
        action(p,"Read animation clip",[this]{needAnimation();const auto& c=animation->library()->data().clips.at(index(clipIndex,animation->library()->data().clips.size()));clipLabel->setText(c.label);clipSpeed->setText(std::to_string(c.speed));clipLoop->setText(c.loop?"1":"0");});
        action(p,"Apply animation clip",[this]{needAnimation();auto d=animation->library()->data();const auto i=index(clipIndex,d.clips.size());d.clips[i].label=clipLabel->text();d.clips[i].speed=numeric(clipSpeed);d.clips[i].loop=index(clipLoop,2)!=0;animation->edit(AnimationLibrary::create(std::move(d)));});
        action(p,"Add animation state for clip",[this]{needAnimation();auto d=animation->library()->data();const auto i=index(clipIndex,d.clips.size());d.controller.states.push_back({"State "+std::to_string(d.controller.states.size()),i,index(clipLoop,2)!=0,numeric(clipSpeed),{}});d.positions.push_back({float(d.positions.size()*160),80});animation->edit(AnimationLibrary::create(std::move(d)));});
        animFrom=field(p,"Animation transition from state index","0");animTo=field(p,"Animation transition to state index","0");animFade=field(p,"Animation transition fade seconds","0.2");
        action(p,"Add animation transition",[this]{needAnimation();auto d=animation->library()->data();d.controller.transitions.push_back({index(animFrom,d.controller.states.size()),index(animTo,d.controller.states.size()),numeric(animFade),1,{}});animation->edit(AnimationLibrary::create(std::move(d)));});
        action(p,"Assign animation library to selection",[this]{needAnimation();if(!editor.setAnimationLibrary(animation->library()))throw std::runtime_error("Select a compatible skeletal mesh");});files(p);
    }
    void needAnimation(){if(!animation)throw std::runtime_error("Open a .weanim or capture a selected skeletal mesh first");}
    void graphs(){auto& p=*pages[2];aiOverview=&p.add<DocumentOverview>();aiOverview->setSize({820,146});aiOverview->setAccessibleName("AI state overview");aiSummary=summary(p);aiIndex=field(p,"AI state index","0");aiName=field(p,"AI state name","Idle");aiBehavior=field(p,"AI behavior binding","idle");aiOverview->select=[this](std::size_t i){aiIndex->setText(std::to_string(i));const auto& s=ai->definition()->data().states.at(i);aiName->setText(s.name);aiBehavior->setText(s.behavior);};
        action(p,"Read AI state",[this]{const auto& s=ai->definition()->data().states.at(index(aiIndex,ai->definition()->data().states.size()));aiName->setText(s.name);aiBehavior->setText(s.behavior);});
        action(p,"Apply AI state",[this]{auto d=ai->definition()->data();auto& s=d.states.at(index(aiIndex,d.states.size()));s.name=aiName->text();s.behavior=aiBehavior->text();ai->edit(std::move(d));});
        action(p,"Add AI state",[this]{auto d=ai->definition()->data();std::uint32_t id=0;for(const auto& s:d.states)id=std::max(id,s.id+1);d.states.push_back({id,aiName->text(),aiBehavior->text(),{float(d.states.size()*160),80}});ai->edit(std::move(d));aiIndex->setText(std::to_string(ai->definition()->data().states.size()-1));});
        aiFrom=field(p,"AI transition from state index","0");aiTo=field(p,"AI transition to state index","0");aiCondition=field(p,"AI transition condition binding","always");aiDelay=field(p,"AI transition minimum seconds","1");
        action(p,"Add AI transition",[this]{auto d=ai->definition()->data();std::uint32_t id=0;for(const auto& t:d.transitions)id=std::max(id,t.id+1);d.transitions.push_back({id,d.states[index(aiFrom,d.states.size())].id,d.states[index(aiTo,d.states.size())].id,aiCondition->text(),numeric(aiDelay)});ai->edit(std::move(d));});
        action(p,"Remove last AI transition",[this]{auto d=ai->definition()->data();if(!d.transitions.empty()){d.transitions.pop_back();ai->edit(std::move(d));}});
        action(p,"Assign AI graph to selection",[this]{if(!editor.setAiController(AiControllerSettings::defaults(ai->definition(),*AiBindingRegistry::builtins())))throw std::runtime_error("Select a compatible actor");});files(p);
    }
    void prefabs(){auto& p=*pages[3];prefabSummary=summary(p);
        action(p,"Capture selected prefab",[this]{auto* a=dynamic_cast<Actor*>(editor.selected());if(!a)throw std::runtime_error("Select an actor");prefab=std::make_unique<PrefabAssetEditor>(PrefabAsset::capture(*a,a->name));prefabUnsaved=true;prefabRevision=prefab->source()->revision();});
        prefabNode=field(p,"Prefab definition node index","0");prefabName=field(p,"Prefab node name","Prefab");prefabX=field(p,"Prefab node position X","0");prefabY=field(p,"Prefab node position Y","0");prefabZ=field(p,"Prefab node position Z","0");
        action(p,"Read prefab node",[this]{auto& n=selectPrefabNode();prefabName->setText(n.name);prefabX->setText(std::to_string(n.transform.position.x));prefabY->setText(std::to_string(n.transform.position.y));prefabZ->setText(std::to_string(n.transform.position.z));});
        action(p,"Apply prefab node position",[this]{auto& n=selectPrefabNode();auto t=n.transform;t.position={float(numeric(prefabX)),float(numeric(prefabY)),float(numeric(prefabZ))};if(!prefab->session().setTransform(t))throw std::runtime_error("Prefab transform rejected");});
        action(p,"Rename prefab node",[this]{selectPrefabNode();if(!prefab->session().setProperty("node.name",prefabName->text()))throw std::runtime_error("Prefab rename rejected");});
        action(p,"Place linked prefab",[this]{needPrefab();const auto candidate=prefabSnapshot();if(!editor.placePrefab(candidate))throw std::runtime_error("Prefab placement rejected");prefabRevision=candidate->revision();});
        action(p,"Use selected prefab as nested source",[this]{auto* a=dynamic_cast<Actor*>(editor.selected());if(!a)throw std::runtime_error("Select an actor");nested=PrefabAsset::capture(*a,a->name);});
        action(p,"Nest source in prefab",[this]{needPrefab();if(!nested)throw std::runtime_error("Capture a nested source first");prefab->nestPrefab(nested,{2,0,0});});
        action(p,"Publish prefab to scene instances",[this]{needPrefab();const auto candidate=prefabSnapshot();const std::array batch{candidate};if(!editor.updatePrefabs(batch))throw std::runtime_error("No matching instances, or conflicting source revision");prefabRevision=candidate->revision();});files(p);
    }
    std::shared_ptr<const PrefabAsset> prefabSnapshot(){needPrefab();return PrefabAsset::create(prefabDefinition(prefab->definition()),prefab->source()->name(),prefab->source()->id(),prefabRevision+1,prefab->source()->base());}
    void needPrefab(){if(!prefab)throw std::runtime_error("Open or capture a prefab first");}
    Node& selectPrefabNode(){needPrefab();std::vector<Node*> nodes;std::function<void(Node&)> visit=[&](Node& n){nodes.push_back(&n);for(const auto& c:n.children())visit(*c);};visit(prefab->definition());auto* n=nodes.at(index(prefabNode,nodes.size()));prefab->session().select(n->savedId());return *n;}
    void effectEdit(std::shared_ptr<const EffectAsset> a){a->validate();auto h=fxHistory;h.resize(fxCursor+1);h.push_back(a);if(h.size()>33)h.erase(h.begin());fxHistory=std::move(h);fxCursor=fxHistory.size()-1;effect=std::move(a);}
    void effects(){auto& p=*pages[4];fxSummary=summary(p);
        action(p,"Capture selected FX",[this]{auto* e=editor.selectedEffect();if(!e)throw std::runtime_error("Select an effect actor/component");effectEdit(std::make_shared<EffectAsset>(e->asset()));});
        action(p,"Campfire preset",[this]{effectEdit(campfireEffect());});action(p,"Explosion preset",[this]{effectEdit(explosionEffect());});
        emitter=field(p,"FX emitter index","0");rate=field(p,"FX spawn rate","30");lifeMin=field(p,"FX lifetime minimum","1");lifeMax=field(p,"FX lifetime maximum","2");sizeMin=field(p,"FX size at birth","0.1");sizeMax=field(p,"FX size at death","0.3");
        action(p,"Read FX emitter",[this]{const auto& d=*effect->emitters.at(index(emitter,effect->emitters.size())).definition;rate->setText(std::to_string(d.spawnRate));lifeMin->setText(std::to_string(d.lifetimeMin));lifeMax->setText(std::to_string(d.lifetimeMax));sizeMin->setText(std::to_string(d.size.front().value));sizeMax->setText(std::to_string(d.size.back().value));});
        action(p,"Apply FX emitter",[this]{auto a=std::make_shared<EffectAsset>(*effect);const auto i=index(emitter,a->emitters.size());auto d=std::make_shared<ParticleEmitterDefinition>(*a->emitters[i].definition);d->spawnRate=float(numeric(rate));d->lifetimeMin=float(numeric(lifeMin));d->lifetimeMax=float(numeric(lifeMax));d->size={{0,float(numeric(sizeMin))},{1,float(numeric(sizeMax))}};a->emitters[i].definition=d;effectEdit(a);});
        action(p,"Place FX",[this]{if(!editor.createEffect(effect))throw std::runtime_error("Effect placement rejected");});action(p,"Assign FX to selection",[this]{if(!editor.setEffectAsset(effect))throw std::runtime_error("Select an editable effect");});files(p);
    }
    void cinematics(){auto& p=*pages[5];cinematicOverview=&p.add<DocumentOverview>();cinematicOverview->timeline=true;cinematicOverview->setSize({820,150});cinematicOverview->setAccessibleName("Cinematic timeline");cineSummary=summary(p);trackIndex=field(p,"Cinematic track index","0");cinematicOverview->select=[this](std::size_t i){trackIndex->setText(std::to_string(i));};trackType=field(p,"Track type (0 transform, 1 camera, 2 animation, 3 audio, 4 FX)","0");trackName=field(p,"Track name","Track");trackResource=field(p,"Track resource (clip name or audio URI)","");trackStart=field(p,"Track start seconds","0");trackEnd=field(p,"Track end seconds","8");keyTime=field(p,"Key / sample time seconds","0");
        action(p,"Add cinematic track for selection",[this]{auto d=cinematic->asset()->data();CinematicTrack t;t.id="track-"+Node().savedId();t.name=trackName->text();t.type=CinematicTrackType(index(trackType,5));if(t.type!=CinematicTrackType::Audio){if(!editor.selected())throw std::runtime_error("Select the track target first");t.target=editor.selected()->savedId();}t.resource=trackResource->text();t.start=numeric(trackStart);t.end=numeric(trackEnd);if(t.type==CinematicTrackType::Transform||t.type==CinematicTrackType::Camera){t.keys={{t.start,editor.selected()->transform},{t.end,editor.selected()->transform}};}d.tracks.push_back(t);cinematic->replace(std::move(d));trackIndex->setText(std::to_string(cinematic->asset()->data().tracks.size()-1));});
        action(p,"Record selected pose at time",[this]{auto d=cinematic->asset()->data();auto& t=d.tracks.at(index(trackIndex,d.tracks.size()));if(t.type!=CinematicTrackType::Transform&&t.type!=CinematicTrackType::Camera)throw std::runtime_error("Choose a transform/camera track");auto* n=scene.findSavedNode(t.target);if(!n)throw std::runtime_error("Track target is missing");CinematicKey k;k.time=numeric(keyTime);k.transform=n->transform;if(auto* c=dynamic_cast<CameraComponent*>(n))k.lens=c->projectionMode()==ProjectionMode::Perspective?c->verticalFov():c->orthographicHeight();auto it=std::lower_bound(t.keys.begin(),t.keys.end(),k.time,[](const auto& a,double b){return a.time<b;});if(it!=t.keys.end()&&it->time==k.time)*it=k;else t.keys.insert(it,k);cinematic->replace(std::move(d));});
        action(p,"Apply sampled track pose",[this]{const auto i=index(trackIndex,cinematic->asset()->data().tracks.size());const auto& t=cinematic->asset()->data().tracks[i];if(t.type!=CinematicTrackType::Transform&&t.type!=CinematicTrackType::Camera)throw std::runtime_error("Choose a transform/camera track");const auto k=cinematic->asset()->sample(i,numeric(keyTime));if(!editor.select(t.target)||!editor.setTransform(k.transform))throw std::runtime_error("Target missing or pose rejected");});
        action(p,"Remove cinematic track",[this]{auto d=cinematic->asset()->data();d.tracks.erase(d.tracks.begin()+index(trackIndex,d.tracks.size()));cinematic->replace(std::move(d));});files(p);
    }
    void history(bool redo){switch(area){case EditorAuthoringArea::AI:if(redo)ai->redo();else ai->undo();break;case EditorAuthoringArea::Animation:needAnimation();if(redo)animation->redo();else animation->undo();break;case EditorAuthoringArea::Prefab:needPrefab();if(redo)prefab->session().redo();else prefab->session().undo();break;case EditorAuthoringArea::Effects:if(redo){if(fxCursor+1<fxHistory.size())++fxCursor;}else if(fxCursor)--fxCursor;effect=fxHistory[fxCursor];break;case EditorAuthoringArea::Cinematic:if(redo)cinematic->redo();else cinematic->undo();break;default:break;}}
    bool dirty()const{return ai->dirty()||(animation&&animation->dirty())||(prefab&&(prefabUnsaved||prefab->session().dirty()))||effect!=savedEffect||cinematic->dirty();}
    void refresh(){
        aiOverview->items.clear();for(const auto& s:ai->definition()->data().states)aiOverview->items.push_back({s.name});
        animationOverview->items.clear();if(animation)for(const auto& s:animation->library()->data().controller.states)animationOverview->items.push_back({s.name});
        cinematicOverview->setSize({820,std::max(150.f,float(std::min<std::size_t>(12,cinematic->asset()->data().tracks.size()))*23+16)});cinematicOverview->items.clear();for(const auto& t:cinematic->asset()->data().tracks)cinematicOverview->items.push_back({t.name,float(t.start/cinematic->asset()->data().duration),float(t.end/cinematic->asset()->data().duration)});
        std::ostringstream a;a<<"AI graph: "<<ai->definition()->data().name<<"\n";for(const auto& s:ai->definition()->data().states){a<<s.id<<": "<<s.name<<" ["<<s.behavior<<"]  ";if(a.tellp()>500)break;}a<<"\n"<<ai->definition()->data().transitions.size()<<" transitions";aiSummary->setText(a.str());
        std::ostringstream m;if(animation){const auto& d=animation->library()->data();m<<d.name<<" — "<<d.clips.size()<<" clips, "<<d.controller.states.size()<<" states, "<<d.controller.transitions.size()<<" transitions\n";for(std::size_t i=0;i<d.clips.size()&&i<8;++i)m<<i<<": "<<d.clips[i].label<<"  ";}else m<<"Open .weanim or select an imported skeletal mesh. Changes stay in this document until assigned.";animSummary->setText(m.str());
        std::ostringstream p;if(prefab){p<<prefab->source()->name()<<" (independent definition)\n";std::size_t i=0;std::function<void(const Node&)> visit=[&](const Node& n){if(i<10)p<<i<<": "<<n.name<<"  ";++i;for(const auto& c:n.children())visit(*c);};visit(prefab->definition());p<<"\nNested source: "<<(nested?nested->name():"none");}else p<<"Open .weprefab or capture an actor. Definition edits, nesting and publication use the existing prefab commands.";prefabSummary->setText(p.str());
        fxSummary->setText(effect->name+" — "+std::to_string(effect->emitters.size())+" emitters. Edit an immutable candidate, then Place or Assign. Use scene Play to preview.");
        std::ostringstream c;c<<cinematic->asset()->data().name<<" — "<<cinematic->asset()->data().duration<<" seconds\n";for(std::size_t i=0;i<cinematic->asset()->data().tracks.size()&&i<6;++i){const auto& t=cinematic->asset()->data().tracks[i];c<<i<<": "<<t.name<<" ["<<int(t.type)<<"] "<<t.keys.size()<<" keys  ";}cineSummary->setText(c.str());
        message->setText(status+(dirty()?"  * Unsaved tool documents":""));
    }
};
EditorAuthoringPanel::EditorAuthoringPanel(GuiSystem& g,Scene& s,EditorSession& e):impl_(std::make_unique<Impl>(g,s,e)){}
EditorAuthoringPanel::~EditorAuthoringPanel()=default;
void EditorAuthoringPanel::setVisible(bool v){impl_->root->setVisible(v);}
bool EditorAuthoringPanel::visible()const{return impl_->root->visible();}
void EditorAuthoringPanel::setBounds(GuiRect r){impl_->root->setPosition({r.x,r.y});impl_->root->setSize({r.width,r.height});for(auto* p:impl_->pages)p->setSize({std::max(1.f,r.width-16),std::max(1.f,r.height-100)});}
void EditorAuthoringPanel::update(bool editable){editable=editable&&!impl_->editor.transforming();impl_->editable=editable;impl_->root->setEnabled(editable);if(!editable){impl_->commands.clear();return;}while(!impl_->commands.empty()){auto f=std::move(impl_->commands.front());impl_->commands.pop_front();try{f();impl_->status="Authoring command complete";}catch(const std::exception& e){impl_->status=e.what();}}impl_->refresh();}
bool EditorAuthoringPanel::dirty()const{return impl_->dirty();}
EditorAuthoringArea EditorAuthoringPanel::area()const{return impl_->area;}
std::string EditorAuthoringPanel::extension()const{switch(area()){case EditorAuthoringArea::AI:return "weai";case EditorAuthoringArea::Animation:return "weanim";case EditorAuthoringArea::Prefab:return "weprefab";case EditorAuthoringArea::Effects:return "wefx";case EditorAuthoringArea::Cinematic:return "wecin";default:throw std::logic_error("Terrain is saved in the scene archive");}}
std::vector<std::byte> EditorAuthoringPanel::saveDocument()const{switch(area()){case EditorAuthoringArea::AI:return bytes(StateMachineArchive::save(*impl_->ai->definition()));case EditorAuthoringArea::Animation:impl_->needAnimation();return AnimationLibraryArchive::save(*impl_->animation->library());case EditorAuthoringArea::Prefab:impl_->needPrefab();return PrefabAssetArchive::save(impl_->prefabSnapshot());case EditorAuthoringArea::Effects:return EffectArchive::save(impl_->effect);case EditorAuthoringArea::Cinematic:return bytes(CinematicSequenceArchive::save(*impl_->cinematic->asset()));default:throw std::logic_error("Save terrain with the scene");}}
void EditorAuthoringPanel::openDocument(std::span<const std::byte> b,bool discard){if(!impl_->editable)throw std::logic_error("Stop Play before opening tool documents");if(b.size()>64*1024*1024)throw std::invalid_argument("Tool document exceeds 64 MiB");if(dirty()&&!discard)throw std::logic_error("Unsaved tool documents; explicitly replace to discard the current area");switch(area()){case EditorAuthoringArea::AI:impl_->ai=std::make_unique<AiGraphEditor>(StateMachineArchive::load(text(b)));break;case EditorAuthoringArea::Animation:impl_->animation=std::make_unique<AnimationLibraryEditor>(AnimationLibraryArchive::load(b));break;case EditorAuthoringArea::Prefab:impl_->prefab=std::make_unique<PrefabAssetEditor>(PrefabAssetArchive::load(b));impl_->prefabUnsaved=false;impl_->prefabRevision=impl_->prefab->source()->revision();break;case EditorAuthoringArea::Effects:{auto a=EffectArchive::load(b);std::vector h{a};impl_->fxHistory=std::move(h);impl_->fxCursor=0;impl_->effect=impl_->savedEffect=a;break;}case EditorAuthoringArea::Cinematic:impl_->cinematic=std::make_unique<CinematicSequenceEditor>(CinematicSequenceArchive::load(text(b)));break;default:throw std::logic_error("Open terrain through the scene archive");}impl_->refresh();}
void EditorAuthoringPanel::markSaved(){switch(area()){case EditorAuthoringArea::AI:impl_->ai->markSaved();break;case EditorAuthoringArea::Animation:impl_->needAnimation();impl_->animation->markSaved();break;case EditorAuthoringArea::Prefab:impl_->needPrefab();impl_->prefab->session().markSaved();impl_->prefabUnsaved=false;break;case EditorAuthoringArea::Effects:impl_->savedEffect=impl_->effect;break;case EditorAuthoringArea::Cinematic:impl_->cinematic->markSaved();break;default:break;}}
const std::string& EditorAuthoringPanel::status()const{return impl_->status;}
void EditorAuthoringPanel::onFile(std::function<void(bool)> f){impl_->file=std::move(f);}
}
