// Included by the animation browser adapter; transient import workbench state.
namespace engine {
namespace {
EM_JS(void,retargetBrowserReport,(const char* profile,const char* info),{
    window.EngineEditorAnimation?.retarget(UTF8ToString(profile),JSON.parse(UTF8ToString(info)));
});
void retargetSnapshot(){
    std::ostringstream o;o.imbue(std::locale::classic());o<<"{\"loaded\":"<<(retargetSource?"true":"false")<<",\"joints\":[";
    if(retargetSource)for(std::size_t i=0;i<retargetSource->joints.size();++i){if(i)o<<',';jsonString(o,retargetSource->joints[i].name);}
    o<<"],\"clips\":"<<(retargetSource?retargetSource->clips.size():0)<<",\"error\":";
    std::string error;if(retargetSource&&document)try{retargetProfile.validate(*retargetSource,*document->library()->data().skeleton);}catch(const std::exception& e){error=e.what();}
    jsonString(o,error);o<<'}';const auto profile=retargetProfileText(retargetProfile),info=o.str();retargetBrowserReport(profile.c_str(),info.c_str());
}
}
extern "C" {
EMSCRIPTEN_KEEPALIVE int engine_editor_retarget_file(const void* data,int size,int kind){return perform([&]{
    if(!data||size<1||size>64*1024*1024)throw std::invalid_argument("Retarget source exceeds 64 MiB");
    const auto bytes=std::span(static_cast<const std::byte*>(data),std::size_t(size));std::shared_ptr<const Skeleton> candidate;
    if(kind==0)candidate=AnimationLibraryArchive::load(bytes)->data().skeleton;
    else {auto mesh=kind==1?GlbImporter::loadMemory(bytes):kind==2?FbxImporter::loadMemory(bytes):nullptr;if(mesh)candidate=mesh->skeleton;}
    if(!candidate||candidate->clips.empty())throw std::invalid_argument("Choose an animated FBX, GLB or .weanim source");
    auto profile=matchAnimationJoints(*candidate,*document->library()->data().skeleton);
    if(!profile.joints.empty())try{profile.translationScale=estimateRetargetScale(*candidate,*document->library()->data().skeleton,profile);}catch(const std::invalid_argument&){/* Keep suggestions for manual repair. */}
    retargetSource=std::move(candidate);retargetProfile=std::move(profile);
});}
EMSCRIPTEN_KEEPALIVE int engine_editor_retarget_profile(const char* data,int size){return perform([&]{
    if(!retargetSource||!data||size<0||size>256*1024)throw std::invalid_argument("Load a retarget source first");
    auto candidate=parseRetargetProfile(std::string_view(data,std::size_t(size)));
    retargetProfile=std::move(candidate);
});}
EMSCRIPTEN_KEEPALIVE int engine_editor_retarget_action(int action){return perform([&]{
    if(action==0){retargetSource.reset();retargetProfile={};return;}
    if(!retargetSource)throw std::logic_error("Load a retarget source first");
    if(action==1){const auto value=estimateRetargetScale(*retargetSource,*document->library()->data().skeleton,retargetProfile);retargetProfile.translationScale=value;}
    else if(action==2)document->edit(document->library()->appendRetargeted(*retargetSource,retargetProfile));
    else throw std::invalid_argument("Unknown retarget action");
});}
}
}
