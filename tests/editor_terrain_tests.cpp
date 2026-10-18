#include "engine/editor.hpp"
#include "engine/editor_play.hpp"
#include "engine/rigid_body_component.hpp"
#include <iostream>
#include <stdexcept>
using namespace engine;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
TerrainData data(){TerrainData d;d.samplesX=d.samplesZ=33;d.chunkCells=16;d.heights.resize(33*33);auto m=std::make_shared<Material>();m->layers.enabled=true;d.material=m;return d;}
int main(){try{
    Scene scene;EditorSession editor(scene,16);
    check(editor.createTerrain(data(),{-16,0,-16},true),"Create terrain");auto* actor=editor.terrainActor();check(actor,"Select created terrain");
    const auto id=actor->savedId();auto* chunk=actor->getComponents<TerrainComponent>().front();const auto handle=chunk->id();const auto source=chunk->asset();
    check(editor.undoCount()==1&&editor.dirty(),"Creation has history");editor.markSaved();
    TerrainBrush brush;brush.center={16,16};brush.radius=6;brush.strength=1;
    check(editor.beginTerrainStroke(),"Begin stroke");check(!editor.beginTransform(),"One gesture owner");
    check(editor.stampTerrain(brush)&&editor.stampTerrain(brush),"Multiple previews");
    check(!editor.dirty()&&editor.undoCount()==1&&editor.transforming(),"Previews are not committed history");
    check(terrainGround(*actor,0,0)->position.y>1.9f,"Queries use sculpted source");
    const auto collisionHit=scene.physics().raycast({0,20,0},{0,-1,0},30);check(collisionHit&&std::abs(collisionHit->point.y-2)<.001f,"Sculpted collision matches source height");
    check(editor.commitTerrainStroke()&&editor.undoCount()==2&&editor.dirty(),"Whole stroke commits once");
    const auto raised=chunk->asset();check(chunk->id()==handle,"Chunk handles survive editing");
    check(editor.undo()&&chunk->asset()==source&&!editor.dirty(),"Undo restores source and saved checkpoint");
    check(editor.redo()&&chunk->asset()==raised,"Redo restores final version");
    check(editor.beginTerrainStroke()&&editor.stampTerrain(brush)&&editor.cancel(),"Cancel stroke");check(chunk->asset()==raised&&editor.undoCount()==2,"Cancel restores exact previous source");
    brush.mode=TerrainBrushMode::Paint;brush.layer=2;brush.strength=1;
    const auto geometry=chunk->mesh;const auto shape=actor->getComponent<ColliderComponent>()->shape();
    check(editor.beginTerrainStroke()&&editor.stampTerrain(brush)&&editor.commitTerrainStroke(),"Paint stroke");
    check(chunk->mesh==geometry&&actor->getComponent<ColliderComponent>()->shape()==shape,"Painting shares geometry and collider");
    check(chunk->asset()->data().material->layers.weights!=nullptr,"Painting publishes weights");check(editor.undo()&&chunk->asset()==raised,"Paint undo restores source");
    auto texture=std::make_shared<Texture>();texture->width=texture->height=2;texture->pixels.resize(16,255);texture->name="Terrain layer test";
    const auto bindingSource=chunk->asset();const auto bindingGeometry=chunk->mesh;const auto bindingShape=actor->getComponent<ColliderComponent>()->shape();const auto bindingCount=editor.undoCount();
    auto otherInstance=makeTerrainActor("Shared source instance",bindingSource);auto* sharedInstance=otherInstance.get();otherInstance->transform.position={100,0,0};scene.adoptChild(std::move(otherInstance));
    check(editor.setTerrainLayerTexture(0,texture),"Assign layer texture");const auto textured=chunk->asset();
    check(textured->data().material->layers.layers[0].texture==texture&&textured->chunks()==bindingSource->chunks(),"Source stores texture while sharing all chunk meshes");
    check(chunk->mesh==bindingGeometry&&actor->getComponent<ColliderComponent>()->shape()==bindingShape&&chunk->id()==handle,"Texture assignment preserves geometry collision and handles");
    for(const auto* part:actor->getComponents<TerrainComponent>())check(part->resolvedMaterial(0).layers.layers[0].texture==texture,"Every chunk receives the layer texture");
    check(sharedInstance->getComponent<TerrainComponent>()->asset()==bindingSource&&!sharedInstance->getComponent<TerrainComponent>()->resolvedMaterial(0).layers.layers[0].texture,"Another instance sharing the old source remains unchanged");
    check(editor.undoCount()==bindingCount+1&&editor.setTerrainLayerTexture(0,texture)&&editor.undoCount()==bindingCount+1,"Assignment is one command; repeated assignment is a no-op");
    check(editor.undo()&&chunk->asset()==bindingSource&&editor.redo()&&chunk->asset()==textured,"Texture undo and redo restore exact versions");
    brush.mode=TerrainBrushMode::Paint;brush.layer=1;brush.strength=1;check(editor.beginTerrainStroke()&&editor.stampTerrain(brush)&&editor.commitTerrainStroke(),"Paint with assigned textures");
    const auto weights=chunk->asset()->data().material->layers.weights;
    check(chunk->asset()->data().material->layers.layers[0].texture==texture,"Painting retains assigned textures");
    check(editor.setTerrainLayerTexture(2,texture)&&chunk->asset()->data().material->layers.weights==weights,"Assigning a different layer retains painted weights");
    auto invalid=std::make_shared<Texture>();bool badTexture=false;const auto validSource=chunk->asset();try{editor.setTerrainLayerTexture(3,invalid);}catch(const std::invalid_argument&){badTexture=true;}
    check(badTexture&&chunk->asset()==validSource,"Invalid texture preserves published source");check(!editor.setTerrainLayerTexture(4,texture),"Invalid layer rejected");
    check(editor.setTerrainLayerTexture(0,nullptr)&&!chunk->asset()->data().material->layers.layers[0].texture&&editor.undo(),"Clearing a texture is reversible");
    auto texturedSave=SceneArchive::load(SceneArchive::save(scene));auto* savedTerrain=dynamic_cast<Actor*>(texturedSave.scene->findSavedNode(id));
    check(savedTerrain->getComponent<TerrainComponent>()->asset()->data().material->layers.layers[0].texture->pixels==texture->pixels,"Textures survive scene archive roundtrip");
    // Return to the earlier sculpted version before continuing the height-brush cases.
    check(editor.undo()&&editor.undo()&&editor.undo(),"Undo layer edits and paint in one ordered history");check(chunk->asset()==raised,"Layer history restores original sculpted source");
    brush.mode=TerrainBrushMode::Raise;brush.strength=-.5f;
    check(editor.beginTerrainStroke()&&editor.stampTerrain(brush)&&editor.commitTerrainStroke(),"Lower and branch history");check(editor.redoCount()==0,"Branch clears redo");
    brush.mode=TerrainBrushMode::Flatten;brush.targetHeight=.25f;brush.strength=1;
    check(editor.beginTerrainStroke()&&editor.stampTerrain(brush)&&editor.commitTerrainStroke(),"Flatten");check(std::abs(terrainGround(*actor,0,0)->position.y-.25f)<.001,"Flatten target height");
    brush.mode=TerrainBrushMode::Smooth;brush.strength=.5f;check(editor.beginTerrainStroke()&&editor.stampTerrain(brush)&&editor.commitTerrainStroke(),"Smooth");
    const auto final=chunk->asset();check(editor.beginTerrainStroke(),"Begin invalid input test");brush.radius=-1;bool rejected=false;try{editor.stampTerrain(brush);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected&&chunk->asset()==final,"Invalid stamp preserves valid source");editor.cancel();brush.radius=6;
    check(editor.select(chunk->savedId())&&editor.terrainActor()==actor,"Selecting a chunk resolves owning terrain");
    const auto bytes=SceneArchive::save(scene);auto loaded=SceneArchive::load(bytes);auto* restored=dynamic_cast<Actor*>(loaded.scene->findSavedNode(id));
    check(restored&&restored->getComponent<TerrainComponent>()->asset()->data().heights==final->data().heights,"Scene roundtrip retains edits");
    check(editor.beginTerrainStroke()&&editor.stampTerrain(brush),"Begin external change guard");actor->name="External terrain";check(!editor.cancel(),"Cancel refuses external changes");check(actor->name=="External terrain","External changes preserved");
    Scene other;auto paged=TerrainAsset::create(data(),false);auto candidate=makeTerrainActor("Stream owned",paged);auto* raw=candidate.get();other.adoptChild(std::move(candidate));EditorSession streaming(other);streaming.select(raw->savedId());check(!streaming.beginTerrainStroke(),"Source-only stream-owned instances reject editing");
    Scene undoScene;EditorSession creation(undoScene);creation.createTerrain(data());auto created=creation.selected()->savedId();check(creation.undo()&&!undoScene.findSavedNode(created)&&creation.redo()&&undoScene.findSavedNode(created),"Creation undo and redo retain saved identity");
    std::cout<<"Editor terrain creation, stroke/history, collision, paint, cancellation, guards and archives passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
