#ifdef VOLUMETRIC_CLOUDS
struct CloudData {vec4 layer,eye,sun,shadow;};
uniform CloudData cloud;
uniform mat4 cloudViewProjection,cloudInverseVp;
uniform vec4 cloudViewport;
vec4 cloudGridSlice(vec2 uv,float slice) {
    if(slice<1.0) return vec4(0,0,0,1);
    vec2 point=clamp(uv*vec2(64,36),vec2(.5),vec2(63.5,35.5));
    return textureLod(fogGrid,(point+vec2(64,(slice-1.0)*36.0))/vec2(textureSize(fogGrid,0)),0.0);
}
vec4 cloudGridMedium(vec3 p) {
    if(cloud.layer.w<.5) return vec4(0,0,0,1);
    vec4 clip=cloudViewProjection*vec4(p,1);
    if(clip.w<=0.0) return vec4(0,0,0,1);
    vec2 ndc=clip.xy/clip.w,uv=ndc*.5+.5;
    vec3 origin=cloud.eye.xyz;
    if(cloud.eye.w<.5) {
        vec4 a=cloudInverseVp*vec4(ndc,0,1),b=cloudInverseVp*vec4(0,0,0,1);
        origin+=a.xyz/a.w-b.xyz/b.w;
    }
    vec3 delta=p-origin;float distance=length(delta);vec3 d=delta/max(distance,.00001);
    float enter=0.0,leave=cloud.layer.z;
    if(abs(d.y)<.00001) {
        if(origin.y<=cloud.layer.x || origin.y>=cloud.layer.x+cloud.layer.y) return vec4(0,0,0,1);
    } else {
        float a=(cloud.layer.x-origin.y)/d.y,b=(cloud.layer.x+cloud.layer.y-origin.y)/d.y;
        enter=max(0.0,min(a,b));leave=min(leave,max(a,b));
    }
    if(leave<=enter || distance<=enter) return vec4(0,0,0,1);
    float node=clamp((distance-enter)/(leave-enter),0.0,1.0)*32.0,lo=floor(node);
    return mix(cloudGridSlice(uv,lo),cloudGridSlice(uv,min(32.0,lo+1.0)),fract(node));
}
vec3 cloudApply(vec3 color,vec3 p,bool additive) {
    vec4 medium=cloudGridMedium(p);return color*medium.a+(additive?vec3(0):medium.rgb);
}
float cloudSurfaceShadow(vec3 p,vec3 l) {
    if(cloud.sun.w<=0.0 || p.y>=cloud.layer.x || dot(l,cloud.sun.xyz)<.995) return 1.0;
    vec2 entry=p.xz+cloud.sun.xz*((cloud.layer.x-p.y)/cloud.sun.y);
    vec2 uv=(entry-cloud.shadow.xy)/(2.0*cloud.shadow.z)+.5;
    if(any(lessThanEqual(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1)))) return 1.0;
    float fade=1.0-smoothstep(.8,1.0,max(abs(uv.x*2.0-1.0),abs(uv.y*2.0-1.0)));
    float lo=0.0,hi=1.0;
    for(int i=0;i<8;++i) {
        float middle=(lo+hi)*.5;
        if(texture(shadowMaps,vec4(uv,4,middle))>.5) lo=middle;else hi=middle;
    }
    float transmission=lo>=255.0/256.0?1.0:hi<=1.0/256.0?0.0:(lo+hi)*.5;
    return mix(1.0,transmission,cloud.sun.w*fade);
}
#endif
