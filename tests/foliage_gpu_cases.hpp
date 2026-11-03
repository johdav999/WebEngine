void gpuCases() {
    auto mesh=blade();auto bend=MeshBend::create(mesh,std::array<float,3>{0,0,1});
    check(bend->vertices()[2][0]==1&&bend->vertices()[0][0]==0,"Authored bend weights lost");
    check(std::abs(bend->vertices()[2][2]-.5f)<1e-5f,"Bend surface gradient incorrect");
    rejects([&]{MeshBend::create(mesh,std::array<float,1>{1});});
    auto wind=std::make_shared<MeshWindState>();wind->configure(.25,FoliageWind{{1,0},.3f,1,0});
    const auto revision=wind->revision();wind->configure(.25,FoliageWind{{1,0},.3f,1,0});check(wind->revision()==revision,"Unchanged wind revised state");
    rejects([&]{wind->configure(-1,{});});check(wind->revision()==revision,"Invalid wind published state");
    Scene generic;auto& actor=generic.spawn("Instanced props");actor.addComponent<InteractionComponent>("Pick");
    auto& bulk=actor.addComponent<MeshInstancesComponent>("Blades",mesh);bulk.setBend(bend,wind);
    MeshInstance a;a.key=17;a.transform.position={3,0,0};MeshInstance b;b.key=23;b.transform.position={-3,0,0};
    auto data=MeshInstanceSet::create(mesh,{a,b});bulk.setInstances(data);
    rejects([&]{bulk.setInstances(MeshInstanceSet::create(mesh,{a,a}));});check(bulk.instances()==data,"Invalid instance publication changed data");
    generic.synchronize();SelectionQuery query;query.geometry=SelectionGeometry::Triangles;
    const auto hit=generic.selection().resolveRay({{3.1f,.5f,2},{0,0,-1}},query);
    check(hit&&hit->surface==bulk.id()&&hit->instanceKey==17,"Instance triangle picking lost stable local key");
    RenderFrame frame;frame.collect(generic);check(frame.sceneFrame().meshes.size()==2,"Bulk source projected once per instance");
    check(frame.batches(RenderLayer::World).size()==1&&frame.batches(RenderLayer::World)[0].instanceCount==2,"GPU wind broke instance batching");
    frame.prepareObjects();const auto savedData=std::vector<std::byte>(frame.objectData().begin(),frame.objectData().end());
    wind->configure(.75,FoliageWind{{1,0},.3f,1,0});frame.collect(generic);frame.prepareObjects();
    check(frame.changedObjects()==0&&std::equal(savedData.begin(),savedData.end(),frame.objectData().begin()),"Wind uploaded unchanged instance transforms");
    for(const auto& draw:frame.sceneFrame().meshes) {
        check(draw.bend==bend&&draw.wind==wind&&!draw.morphPose,"Bulk wind generated a CPU vertex stream");
        for(std::size_t i=0;i<mesh->vertices.size();++i) {
            const float w=bend->vertices()[i][0];const auto p=transformPoint(draw.model,mesh->vertices[i].position)+wind->displacement(draw.windPhase)*(w*w);
            const auto local=transformPoint(inverseAffine(draw.model),p);
            check(local.x>=draw.sourceBounds.minimum.x-1e-5f&&local.x<=draw.sourceBounds.maximum.x+1e-5f,"Wind escaped conservative bounds");
        }
    }
    frame.prepareShadow(0,{Mat4::identity(),256,256},false,false,true);check(!frame.shadowBatches(0).empty(),"Bulk wind missing from shadow pass");
    const auto loaded=SceneArchive::load(SceneArchive::save(generic));check(loaded.scene->collect().meshes.empty(),"Runtime instance projection serialized as source content");
    // Fixed seed and one 100x100 cell: exactly 10,000 globally anchored candidates.
    auto definitionData=definition();definitionData.cellSize=100;definitionData.maxCandidatesPerCell=12000;definitionData.maxInstancesPerCell=10000;
    auto& species=definitionData.species[0];species.density=1;species.minimumSpacing=.1f;species.details.resize(1);species.drawDistance=500;
    auto catalog=FoliageCatalog::create(definitionData);FoliageStreamSettings settings;settings.visualRadius=0;settings.hysteresisCells=0;
    Scene scene;FoliageStream stream(scene,catalog,flat,settings);stream.update({50,50},{50,50});stream.present({50,50},.25);
    check(stream.stats().instances==10000&&stream.stats().bulkComponents==1&&stream.stats().deformedVertices==0,"Dense GPU fixture did not use one bulk source");
    frame.collect(scene);frame.prepareObjects();check(frame.sceneFrame().meshes.size()==10000&&frame.batches(RenderLayer::World).size()==1,"Dense foliage did not batch");
    const auto first=frame.sceneFrame().meshes.front();const auto id=stream.placement(first.component,first.instanceKey);check(bool(id)&&!stream.placement(first.component),"Ambiguous bulk picking key accepted");
    const auto* component=dynamic_cast<const MeshInstancesComponent*>(scene.findComponent(first.component));const auto instances=component->instances();
    stream.present({50,50},.75);frame.collect(scene);frame.prepareObjects();check(frame.changedObjects()==0&&component->instances()==instances,"Wind-only tick rebuilt placements");
    const auto windRevision=first.wind->revision();stream.present({50,50},.75);check(first.wind->revision()==windRevision,"Paused GPU wind advanced");
    stream.remove(*id);check(stream.stats().instances==9999&&!stream.placement(first.component,first.instanceKey),"GPU harvest retained removed projection");const auto saved=stream.saveRemoved();
    stream.update({500,500},{50,50});stream.update({50,50},{50,50});stream.present({50,50},.75);check(stream.stats().instances==9999,"Harvest regenerated during GPU cell re-entry");
    rejects([&]{stream.loadRemoved("WEFO 99 1 1 0");});check(stream.saveRemoved()==saved,"Invalid bulk save changed overrides");
    stream.clear();settings.rendering=FoliageRendering::CpuMeshes;FoliageStream cpu(scene,catalog,flat,settings);cpu.loadRemoved(saved);cpu.update({50,50},{50,50});cpu.present({50,50},.75);
    check(cpu.stats().instances==9999&&cpu.stats().deformedVertices>0&&cpu.saveRemoved()==saved,"CPU reference changed durable source identity");cpu.clear();
    std::cout<<"GPU foliage: gradients, immutable sets, picking keys, conservative bounds, batching, static uploads, pause, 10000 clumps and CPU/save parity PASS\n";
}
