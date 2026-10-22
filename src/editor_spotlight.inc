namespace {
EM_JS(void,spotlight_editor_projection,(const float* points,int count),{
    window.EngineSpotlightEditor?.projection(Array.from(new Float32Array(HEAPU8.buffer,points,count)));
});
void projectSpotlightAuthoring(){
    if(!session||!viewCamera||play.playing()||fxPreview.active()){spotlight_editor_projection(nullptr,0);return;}
    const auto* light=dynamic_cast<const LightComponent*>(session->selected());
    if(!light)if(const auto* actor=dynamic_cast<const Actor*>(session->selected()))light=actor->getComponent<LightComponent>();
    if(!light||light->type!=LightType::Spot||!light->isActiveInHierarchy()){spotlight_editor_projection(nullptr,0);return;}
    std::vector<float> points;const auto vp=viewport();
    try{for(const auto& line:spotlightOutline(*light)){
        const auto a=viewCamera->worldToScreen(line.begin,vp),b=viewCamera->worldToScreen(line.end,vp);
        if(a.inFront&&b.inFront&&a.depth>=0&&a.depth<=1&&b.depth>=0&&b.depth<=1)
            points.insert(points.end(),{a.pixel.x,a.pixel.y,b.pixel.x,b.pixel.y});
    }}catch(const std::exception&){points.clear();}
    spotlight_editor_projection(points.data(),int(points.size()));
}
}
