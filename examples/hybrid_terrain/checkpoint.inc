// Application metadata complements ordinary scene data. Load into a separate Demo
// and bind the host before releasing the old scene. History and gestures reset.
std::vector<std::byte> checkpointMetadata() const {
    check(!stroke,"Finish or cancel the brush stroke before saving");
    std::ostringstream out;out<<std::setprecision(9)<<(shapedEntrance?"HYBRID11 ":hasLargeGrotto()?"HYBRID10 ":cliffPart?"HYBRID9 ":hasPond()?"HYBRID8 ":hasCurves()?"HYBRID7 ":hasSystem()?"HYBRID6 ":"HYBRID5 ")<<entrance.halfWidth<<' '<<entrance.depth<<' '<<entrance.ceiling<<' '
        <<view<<' '<<cameraDetail<<' '<<shadowDetail<<' '<<blending<<' '<<brushRadius<<' ';
    for(auto h:manualHoles)out<<char('0'+h);
    const auto text=out.str();return {reinterpret_cast<const std::byte*>(text.data()),reinterpret_cast<const std::byte*>(text.data()+text.size())};
}
std::vector<std::byte> saveCheckpoint() const {
    const auto metadata=checkpointMetadata();const RenderView renderView{camera};
    return SceneArchive::save(*world,{&renderView,1},{},metadata);
}
void bindCheckpoint(SceneDocument& doc) {
    check(doc.scene&&doc.applicationData.size()<2048,"Invalid hybrid checkpoint metadata");
    std::istringstream input(std::string(reinterpret_cast<const char*>(doc.applicationData.data()),doc.applicationData.size()));
    std::string magic,mask,extra;int blendValue=-1;
    check(bool(input>>magic>>entrance.halfWidth>>entrance.depth>>entrance.ceiling>>view>>cameraDetail>>shadowDetail>>blendValue>>brushRadius>>mask),"Incomplete hybrid checkpoint");
    validate(entrance);check((magic=="HYBRID5"||magic=="HYBRID6"||magic=="HYBRID7"||magic=="HYBRID8"||(magic=="HYBRID9"||(magic=="HYBRID10"||magic=="HYBRID11")))&&!(input>>extra)&&mask.size()==1024&&view>=0&&view<=((magic=="HYBRID10"||magic=="HYBRID11")?11:magic=="HYBRID9"?10:magic=="HYBRID8"?9:magic!="HYBRID5"?6:4)&&
        cameraDetail>=-1&&cameraDetail<=3&&shadowDetail>=-1&&shadowDetail<=3&&(blendValue==0||blendValue==1)&&
        std::isfinite(brushRadius)&&brushRadius>=.5f&&brushRadius<=4,"Invalid hybrid checkpoint settings");
    for(std::size_t i=0;i<mask.size();++i){check(mask[i]=='0'||mask[i]=='1',"Invalid manual hole mask");manualHoles[i]=mask[i]-'0';}
    shapedEntrance=magic=="HYBRID11";world=doc.scene.get();
    groundActor=dynamic_cast<Actor*>(world->findSavedNode("hybrid-ground"));
    cavePart=dynamic_cast<MeshComponent*>(world->findSavedNode("hybrid-cave"));
    caveCollider=dynamic_cast<ColliderComponent*>(world->findSavedNode("hybrid-collision"));
    walker=dynamic_cast<Actor*>(world->findSavedNode("hybrid-walker"));
    camera=dynamic_cast<CameraComponent*>(world->findSavedNode("hybrid-camera"));
    lantern=dynamic_cast<LightComponent*>(world->findSavedNode("hybrid-lantern"));
    check(groundActor&&cavePart&&cavePart->mesh&&caveCollider&&walker&&camera&&lantern,"Checkpoint is missing a required scene node");
    auto* ground=groundActor->getComponent<TerrainComponent>();motor=walker->getComponent<CharacterMotorComponent>();
    check(ground&&ground->asset()&&motor,"Checkpoint terrain or explorer is missing");terrain=ground->asset();
    // Older caves upgraded through entrance editing need not own the optional
    // cliff sample. HYBRID9 required it; HYBRID10 describes the grotto/basin.
    bindCliff(magic=="HYBRID9"||((magic=="HYBRID10"||magic=="HYBRID11")&&world->findSavedNode("hybrid-cliff")));
    check(terrain->data().samplesX==33&&terrain->data().samplesZ==33&&terrain->data().holes==combinedMask(entrance,manualHoles,shapedEntrance),"Checkpoint mask and entrance metadata disagree");
    check(cavePart->mesh->lodRecipe.has_value(),"Checkpoint has no cave LOD recipe");
    if(magic=="HYBRID6"||magic=="HYBRID7"||magic=="HYBRID8"||(magic=="HYBRID9"||(magic=="HYBRID10"||magic=="HYBRID11")))check(hasSystem(),"Checkpoint is missing deeper chambers");
    if(magic=="HYBRID7"||magic=="HYBRID8"||(magic=="HYBRID9"||(magic=="HYBRID10"||magic=="HYBRID11")))check(hasCurves(),"Checkpoint is missing curved cave layout");
    pondPart=dynamic_cast<MeshComponent*>(world->findSavedNode("hybrid-pond"));
    if(magic=="HYBRID8"||(magic=="HYBRID9"||(magic=="HYBRID10"||magic=="HYBRID11")))check(hasPond(),"Checkpoint pond is missing");verifyPond();if((magic=="HYBRID10"||magic=="HYBRID11")){check(hasLargeGrotto(),"Checkpoint enlarged grotto is missing");verifyBasin();}
    systemDecor();
    // Loading does not call the seam builder or replace any saved collision.
    verifySeams(*world);auto ceiling=world->physics().raycast({0,1,3},{0,1,0},6);
    check(ceiling&&std::abs(ceiling->point.y-entrance.ceiling)<.01f,"Checkpoint ceiling mismatch");
    moving=false;motor->stop();coarse=cameraDetail>0;blending=blendValue!=0;tool=0;
}
