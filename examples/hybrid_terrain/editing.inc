// Included inside Demo: application-owned entrance policy and bounded history.
Snapshot snapshot() const{return {terrain,cavePart->mesh,entrance,manualHoles,hasPond()?pondPart->mesh:nullptr,hasPond()?waterRegion(*pondPart):std::nullopt,hasPond()?pondPart->transform.position.y:0,shapedEntrance};}
void stopWalking(){moving=false;motor->stop();motor->teleport({0,.05f,-7});}
TerrainMeshHoleSettings holeSettings()const{TerrainMeshHoleSettings settings;settings.meshToTerrain.m[12]=16;settings.meshToTerrain.m[14]=12;return settings;}
std::vector<std::uint8_t> meshHoleFootprint(Entrance e,bool shaped=true)const{
    return buildTerrainMeshHoleMask(*terrain,*caveMesh(e,terrain->data().material,shaped),holeSettings()).footprint;
}
std::vector<std::uint8_t> combinedMask(Entrance e,const std::vector<std::uint8_t>& manual,bool shaped=true) const{
    auto mask=manual;const auto owned=meshHoleFootprint(e,shaped);for(std::size_t i=0;i<mask.size();++i)mask[i]=mask[i]||owned[i]||cliffFootprint[i];return mask;
}
void publish(const Snapshot& next){
    validate(next.entrance);
    // Validate mesh and prepare its physics before publishing either resource.
    auto prepared=makeMeshTerrainActor("Prepared cave",next.mesh,true);
    const auto shape=prepared->getComponent<ColliderComponent>()->shape();
    TerrainEdit edit{terrain,next.terrain,{},false,false,false,true};
    for(unsigned i=0;i<terrain->chunkCount();++i)edit.affectedChunks.push_back(i);
    auto previous=snapshot();auto previousShape=caveCollider->shape();auto nextManual=next.manual;
    applyTerrainEdit(*groundActor,edit);cavePart->mesh=next.mesh;caveCollider->setShape(shape);
    try{applyPond(next.pond,next.region,next.waterLevel);world->synchronize();}catch(...){applyPond(previous.pond,previous.region,previous.waterLevel);std::swap(edit.before,edit.after);applyTerrainEdit(*groundActor,edit);cavePart->mesh=previous.mesh;caveCollider->setShape(previousShape);world->synchronize();throw;}
    terrain=next.terrain;entrance=next.entrance;shapedEntrance=next.shapedEntrance;manualHoles.swap(nextManual);blend(blending);refreshEntrancePresentation();
}
void remember(Snapshot before){
    auto after=snapshot();if(before.terrain==after.terrain&&before.mesh==after.mesh&&before.manual==after.manual)return;
    history.resize(historyPosition);history.push_back({std::move(before),std::move(after)});
    if(history.size()>32)history.erase(history.begin());historyPosition=history.size();
}
void editEntrance(Entrance next){
    if(stroke)throw std::invalid_argument("Finish the brush stroke first");validate(next);if(next==entrance&&shapedEntrance)return;
    history.reserve(33);auto before=snapshot();
    auto edit=terrain->withHoles(combinedMask(next,manualHoles));
    auto prepared=prepareCave(*edit.after,next);
    Snapshot after{edit.after,prepared.mesh,next,manualHoles,prepared.pond,prepared.region,prepared.waterLevel};publish(after);stopWalking();remember(std::move(before));
}
bool undo(){if(stroke||!historyPosition)return false;publish(history[historyPosition-1].before);--historyPosition;stopWalking();return true;}
bool redo(){if(stroke||historyPosition==history.size())return false;publish(history[historyPosition].after);++historyPosition;stopWalking();return true;}
void beginStroke(){if(stroke||tool==0)return;history.reserve(33);stroke=snapshot();lastStamp.reset();stopWalking();}
void stamp(Vec2 local){
    if(!stroke)return;
    // Entrance mask belongs to the entrance controls; brush edits affect the surrounding ground.
    auto nextManual=manualHoles;
    const auto from=lastStamp.value_or(local);const float dx=local.x-from.x,dz=local.y-from.y;
    const int steps=std::clamp(int(std::ceil(std::sqrt(dx*dx+dz*dz)/(.35f*brushRadius))),1,128);
    const auto owned=meshHoleFootprint(entrance);
    for(int step=1;step<=steps;++step){const Vec2 p{from.x+dx*step/steps,from.y+dz*step/steps};
        for(int z=0;z<32;++z)for(int x=0;x<32;++x){
            if(cliffFootprint[z*32+x])continue;
            if(owned[z*32+x])continue;
            const float a=x+.5f-p.x,b=z+.5f-p.y;if(a*a+b*b<=brushRadius*brushRadius)nextManual[z*32+x]=tool==1;
        }
    }
    const auto edit=terrain->withHoles(combinedMask(entrance,nextManual));
    if(edit.changed()){auto prepared=prepareCave(*edit.after,entrance);publish({edit.after,prepared.mesh,entrance,nextManual,prepared.pond,prepared.region,prepared.waterLevel});}
    manualHoles.swap(nextManual);lastStamp=local;
}
void endStroke(bool cancel=false){if(!stroke)return;auto before=*stroke;if(cancel)publish(before);else remember(std::move(before));stroke.reset();lastStamp.reset();}
void setTool(int mode){if(mode<0||mode>2)throw std::invalid_argument("Invalid brush tool");endStroke(true);tool=mode;}
void pointer(int phase,float x,float y,float width,float height){
    if(phase==3){endStroke(true);return;}if(!tool)return;
    if(phase==0)beginStroke();
    if(stroke&&width>0&&height>0&&x>=0&&y>=0&&x<=width&&y<=height){
        const auto ray=camera->screenPointToRay({x,y},{0,0,width,height});
        if(auto hit=terrainRaycast(*groundActor,ray.origin,ray.direction,200,true)){
            const auto p=transformPoint(inverseAffine(groundActor->worldMatrix()),hit->position);stamp({p.x,p.z});
        }else lastStamp.reset();
    }
    if(phase==2)endStroke();
}
int paintedCells() const{return int(std::count(manualHoles.begin(),manualHoles.end(),std::uint8_t(1)));}
