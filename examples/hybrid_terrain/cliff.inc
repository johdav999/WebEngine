// Demo-owned fixed cliff, independent of cave/paint history. Ordinary scene
// archives retain its geometry/collider; its rectangular collar recovers ownership.
void bindCliff(bool expected){
    cliffPart=dynamic_cast<MeshComponent*>(world->findSavedNode("hybrid-cliff"));
    check(bool(cliffPart)==expected,"Checkpoint cliff metadata and geometry disagree");
    if(!cliffPart)return;
    check(cliffPart->mesh&&cliffPart->mesh->lodRecipe&&cliffPart->parent(),"Checkpoint cliff mesh is incomplete");
    auto* actor=dynamic_cast<Actor*>(cliffPart->parent());check(actor&&actor->getComponent<ColliderComponent>(),"Checkpoint cliff collision missing");
    check(actor->transform.position.x==-16&&actor->transform.position.y==0&&actor->transform.position.z==-12,"Checkpoint cliff/terrain placement mismatch");
    const auto b=cliffPart->mesh->localBounds();
    check(b.minimum.x>=0&&b.minimum.z>=0&&b.maximum.x<=32&&b.maximum.z<=32&&b.maximum.x>b.minimum.x&&b.maximum.z>b.minimum.z&&
        b.minimum.x==std::floor(b.minimum.x)&&b.minimum.z==std::floor(b.minimum.z)&&b.maximum.x==std::floor(b.maximum.x)&&b.maximum.z==std::floor(b.maximum.z),"Checkpoint cliff collar is invalid");
    for(int z=int(b.minimum.z);z<int(b.maximum.z);++z)for(int x=int(b.minimum.x);x<int(b.maximum.x);++x)cliffFootprint[z*32+x]=1;
}
void verifyCliff(Scene& scene)const{
    auto* part=dynamic_cast<MeshComponent*>(scene.findSavedNode("hybrid-cliff"));if(!part){check(!cliffPart,"Saved cliff disappeared");return;}
    const auto b=part->mesh->localBounds();
    for(float offset:{-.03f,-.001f,.001f,.03f}){
        for(float x=b.minimum.x+.37f;x<b.maximum.x;x+=1)for(float z:{b.minimum.z+offset,b.maximum.z+offset}){
            // A user-painted hole beside the collar intentionally has no ground.
            if((z<b.minimum.z||z>b.maximum.z)&&!terrainGround(scene,x-16,z-12))continue;
            auto hit=scene.physics().raycast({x-16,12,z-12},{0,-1,0},20);
            const float gz=std::floor(z),h=height(gz-12)+(height(gz+1-12)-height(gz-12))*(z-gz);
            check(hit&&std::abs(hit->point.y-h)<.003f,"Cliff upper/lower collar collision gap at "+std::to_string(x-16)+","+std::to_string(z-12)+" expected "+std::to_string(h)+" hit "+(hit?std::to_string(hit->point.y):"none"));}
        for(float z=b.minimum.z+.37f;z<b.maximum.z;z+=1)for(float x:{b.minimum.x+offset,b.maximum.x+offset}){
            if(x<0||x>32)continue;
            if((x<b.minimum.x||x>b.maximum.x)&&!terrainGround(scene,x-16,z-12))continue;
            // Compare against the original triangular heightfield interpolation.
            const float gz=std::floor(z),h=height(gz-12)+(height(gz+1-12)-height(gz-12))*(z-gz);
            auto hit=scene.physics().raycast({x-16,12,z-12},{0,-1,0},20);
            check(hit&&std::abs(hit->point.y-h)<.003f,"Cliff open-end collar collision gap");}
    }
    auto face=scene.physics().raycast({-10.75f,2.5f,-2},{0,0,1},4);
    check(face&&std::abs(face->normal.y)<.3f,"Steep cliff face collision missing");
}
