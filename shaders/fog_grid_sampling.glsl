uniform mat4 fogViewProjection;
vec4 fogGridSlice(vec2 uv,float slice) {
    if(slice<1.0) return vec4(0,0,0,1);
    vec2 point=clamp(uv*vec2(64,36),vec2(0.5),vec2(63.5,35.5));
    vec2 atlasUv=(point+vec2(0,(slice-1.0)*36.0))/vec2(textureSize(fogGrid,0));
    return textureLod(fogGrid,atlasUv,0.0);
}
vec4 fogGridMedium(vec3 p,vec3 origin,vec4 clip) {
    vec3 delta=p-origin;
    float distance=length(delta);
    vec3 direction=delta/max(distance,1e-6);
    vec2 slab=fogRayRange(origin,direction,fog.volumeMaxDistance.w);
    if(slab.x>=slab.y || distance<=slab.x) {
        float t=exp(-fogDepth(fog,origin,p,fog.height.x));
        return vec4(fog.colorDensity.rgb*(1.0-t),t);
    }
    vec2 uv=clip.xy/clip.w*0.5+0.5;
    float slice=clamp((distance-slab.x)/(slab.y-slab.x),0.0,1.0)*16.0;
    float nearSlice=floor(slice);
    vec4 medium=mix(fogGridSlice(uv,nearSlice),fogGridSlice(uv,min(16.0,nearSlice+1.0)),fract(slice));
    float front=exp(-fogDepth(fog,origin,origin+direction*slab.x,fog.height.x));
    float tail=exp(-fogDepth(fog,origin+direction*min(distance,slab.y),p,max(0.0,fog.height.x-slab.y)));
    vec3 haze=fog.colorDensity.rgb*((1.0-front)+front*medium.a*(1.0-tail));
    return vec4(haze+front*medium.rgb,front*medium.a*tail);
}
vec3 fogVolumeApply(vec3 color,vec3 p,bool additive) {
    if(fog.volumeMinSteps.w<=0.0) return fogApply(fog,color,p,additive);
    vec3 origin=fogOrigin(fog,p);
    vec4 clip=fogViewProjection*vec4(p,1);
    vec4 medium=fog.volumeOptions.y>0.5 && clip.w>0.0 ? fogGridMedium(p,origin,clip) : fogVolumeSegment(origin,p,0.0);
    return color*medium.a+(additive ? vec3(0) : medium.rgb);
}
