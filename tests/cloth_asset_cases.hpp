#pragma once
#include "engine/cloth_asset.hpp"
#include "engine/glb_importer.hpp"
#include "cloth_asset_fixture.hpp"
#include <bit>

inline void clothAssetCases() {
    using namespace engine;
    auto driver=GlbImporter::loadMemory(std::as_bytes(std::span(clothDriverFixture)));
    auto bytes=std::as_bytes(std::span(clothAssetFixture));
    auto asset=loadClothAsset(bytes,driver->skeleton);
    check(asset.mesh->skeleton==driver->skeleton,"Imported garment must share exact driver rig");
    check(asset.mesh->vertices.size()==4&&asset.cloth->links.size()==6,"Exported topology changed");
    check(asset.cloth->maxDistance[0]==0&&asset.cloth->maxDistance[2]==1,"Painted pin/distance changed");
    check(asset.cloth->inverseMass[0]==1&&asset.cloth->inverseMass[2]==20,"Painted mass changed");
    // The GLB importer may reorder/split vertices; compare geometry by position,
    // and compare bind-deformed positions, never raw binding indices.
    for(const auto& v:asset.mesh->vertices) {
        bool found=false;for(const auto& d:driver->vertices)if(dot(v.position-d.position,v.position-d.position)<1e-10f)found=true;
        check(found,"Garment and character GLB use different bind spaces");
    }
    auto scene=std::make_unique<Scene>();auto& component=scene->spawn("Exported").addComponent<SkeletalMeshComponent>("Driver",driver);
    for(std::size_t i=0;i<asset.mesh->vertices.size();++i) {
        Vec3 p{};const auto& skin=asset.mesh->skin[i];const auto original=asset.mesh->vertices[i].position;
        for(unsigned j=0;j<4;++j)p=p+transformPoint(component.skinPose()->bones[skin.joints[j]].transform,original)*skin.weights[j];
        check(dot(p-original,p-original)<1e-8f,"Exported garment moves in bind pose");
    }
    ClothingCatalog catalog;catalog.skeleton=driver->skeleton;catalog.items={{"cape","cape",asset.mesh,0,asset.cloth}};
    ClothingInstance instance(component.id(),catalog);instance.apply(*scene,{{"cape"}});
    check(instance.advanceCloth(*scene,1.0/60),"Exported garment did not simulate");
    check(instance.clothStats(*scene).steps==2,"Exported garment fixed steps incorrect");
    for(std::size_t n=0;n<bytes.size();++n)rejects([&]{loadClothAsset(bytes.first(n),driver->skeleton);});
    auto badRig=std::make_shared<Skeleton>(*driver->skeleton);
    for(auto& j:badRig->joints)if(j.name=="Root")j.bindPose.translation.x+=.1f;
    rejects([&]{loadClothAsset(bytes,badRig);});
    badRig=std::make_shared<Skeleton>(*driver->skeleton);
    for(std::size_t b=1;b<badRig->bindings.size();++b)badRig->bindings[b].inverseBind.m[12]+=.1f;
    rejects([&]{loadClothAsset(bytes,badRig);});
    auto corrupted=std::vector<std::byte>(bytes.begin(),bytes.end());corrupted.push_back(std::byte{0});
    rejects([&]{loadClothAsset(corrupted,driver->skeleton);});
    corrupted[4]=std::byte{2};rejects([&]{loadClothAsset(corrupted,driver->skeleton);});
    auto word=[&](std::size_t at){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(std::to_integer<unsigned char>(bytes[at+i]))<<(8*i);return v;};
    auto put=[&](std::size_t at,std::uint32_t v){for(unsigned i=0;i<4;++i)corrupted[at+i]=std::byte((v>>(8*i))&255);};
    corrupted.assign(bytes.begin(),bytes.end());put(12,1025); // Untrusted allocation budget.
    rejects([&]{loadClothAsset(corrupted,driver->skeleton);});
    std::size_t vertexOffset=56;for(unsigned i=0;i<word(8);++i)vertexOffset+=4+word(vertexOffset)+4+64;
    corrupted.assign(bytes.begin(),bytes.end());
    // Vertex 3 UV folds over the other triangle without changing geometric topology.
    put(vertexOffset+3*72+24,std::bit_cast<std::uint32_t>(0.f));
    put(vertexOffset+3*72+28,std::bit_cast<std::uint32_t>(0.f));
    rejects([&]{loadClothAsset(corrupted,driver->skeleton);});
    auto reordered=std::make_shared<Skeleton>(*driver->skeleton);
    std::reverse(reordered->bindings.begin()+1,reordered->bindings.end());
    auto remapped=loadClothAsset(bytes,reordered);
    check(remapped.mesh->skeleton==reordered,"Equivalent reordered palette must be remapped");
    check(instance.selection().size()==1,"Rejected import changed live outfit");
    std::cout<<"Cloth asset PASS: Blender/GLB rig compatibility, topology, painted groups, simulation, truncation and rig rejection\n";
}
