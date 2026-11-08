fn fogGridTexel(x:i32,y:i32,slice:i32) -> vec4f {
    let packed=fogGrid[u32(((slice-1)*36+y)*64+x)];
    return vec4f(unpack2x16float(packed.x),unpack2x16float(packed.y));
}
fn fogGridSlice(uv:vec2f,slice:f32) -> vec4f {
    if(slice<1) {return vec4f(0,0,0,1);}
    let texel=clamp(uv*vec2f(64,36)-0.5,vec2f(0),vec2f(63,35));
    let low=vec2i(floor(texel));
    let high=min(low+1,vec2i(63,35));
    let t=fract(texel);
    let index=i32(slice);
    let a=mix(fogGridTexel(low.x,low.y,index),fogGridTexel(high.x,low.y,index),t.x);
    let b=mix(fogGridTexel(low.x,high.y,index),fogGridTexel(high.x,high.y,index),t.x);
    return mix(a,b,t.y);
}
fn fogGridMedium(p:vec3f,origin:vec3f) -> vec4f {
    let f=frame.fog;
    let delta=p-origin;
    let distance=length(delta);
    let direction=delta/max(distance,1e-6);
    let slab=fogRayRange(f,origin,direction,f.volumeMaxDistance.w);
    if(slab.x>=slab.y || distance<=slab.x) {
        let t=exp(-fogDepth(f,origin,p,f.height.x));
        return vec4f(f.colorDensity.rgb*(1-t),t);
    }
    let clip=frame.viewProjection*vec4f(p,1);
    if(clip.w<=0) {return fogVolumeSegment(origin,p,0);}
    let uv=clip.xy/clip.w*vec2f(0.5,-0.5)+0.5;
    let slice=clamp((distance-slab.x)/(slab.y-slab.x),0,1)*16;
    let nearSlice=floor(slice);
    let medium=mix(fogGridSlice(uv,nearSlice),fogGridSlice(uv,min(16,nearSlice+1)),fract(slice));
    let front=exp(-fogDepth(f,origin,origin+direction*slab.x,f.height.x));
    let tail=exp(-fogDepth(f,origin+direction*min(distance,slab.y),p,max(0.0,f.height.x-slab.y)));
    let haze=f.colorDensity.rgb*((1-front)+front*medium.a*(1-tail));
    return vec4f(haze+front*medium.rgb,front*medium.a*tail);
}
fn fogVolumeApply(color:vec3f,p:vec3f,additive:bool) -> vec3f {
    let f=frame.fog;
    if(f.volumeMinSteps.w<=0) {return fogApply(f,color,p,additive);}
    let origin=fogOrigin(f,p);
    var medium:vec4f;
    if(f.volumeOptions.y>0.5) {medium=fogGridMedium(p,origin);}
    else {medium=fogVolumeSegment(origin,p,0);}
    return color*medium.a+select(medium.rgb,vec3f(0),additive);
}

override cloudDepthEnabled:bool=false;
override cloudShadowsEnabled:bool=false;
// The cloud prefix follows the fog prefix in the same storage binding.
fn cloudRange(origin:vec3f,d:vec3f) -> vec2f {
    let c=frame.cloud;var enter=0.0;var leave=c.layer.z;
    if(abs(d.y)<0.00001) {if(origin.y<=c.layer.x || origin.y>=c.layer.x+c.layer.y) {return vec2f(0);}}
    else {let a=(c.layer.x-origin.y)/d.y;let b=(c.layer.x+c.layer.y-origin.y)/d.y;enter=max(0.0,min(a,b));leave=min(leave,max(a,b));}
    return vec2f(enter,leave);
}
fn cloudGridNode(x:i32,y:i32,slice:i32) -> vec4f {
    let value=fogGrid[36864u+u32(((slice-1)*36+y)*64+x)];return vec4f(unpack2x16float(value.x),unpack2x16float(value.y));
}
fn cloudGridSlice(uv:vec2f,slice:i32) -> vec4f {
    if(slice<1) {return vec4f(0,0,0,1);}
    let p=clamp(uv*vec2f(64,36)-0.5,vec2f(0),vec2f(63,35));let lo=vec2i(floor(p));let hi=min(lo+1,vec2i(63,35));let t=fract(p);
    return mix(mix(cloudGridNode(lo.x,lo.y,slice),cloudGridNode(hi.x,lo.y,slice),t.x),mix(cloudGridNode(lo.x,hi.y,slice),cloudGridNode(hi.x,hi.y,slice),t.x),t.y);
}
fn cloudGridMedium(p:vec3f) -> vec4f {
    if(!cloudDepthEnabled || frame.cloud.layer.w<0.5) {return vec4f(0,0,0,1);}
    let clip=frame.viewProjection*vec4f(p,1);if(clip.w<=0) {return vec4f(0,0,0,1);}
    let ndc=clip.xy/clip.w;let uv=ndc*vec2f(0.5,-0.5)+0.5;
    var origin=frame.cloud.eye.xyz;
    if(frame.cloud.eye.w<0.5) {
        let a=frame.inverseViewProjection*vec4f(ndc,0,1);let b=frame.inverseViewProjection*vec4f(0,0,0,1);
        origin+=a.xyz/a.w-b.xyz/b.w;
    }
    let delta=p-origin;let distance=length(delta);let d=delta/max(distance,0.00001);let slab=cloudRange(origin,d);
    if(slab.y<=slab.x || distance<=slab.x) {return vec4f(0,0,0,1);}
    let node=clamp((distance-slab.x)/(slab.y-slab.x),0,1)*32.0;let lo=i32(floor(node));
    return mix(cloudGridSlice(uv,lo),cloudGridSlice(uv,min(32,lo+1)),fract(node));
}
fn cloudApply(color:vec3f,p:vec3f,additive:bool) -> vec3f {
    let medium=cloudGridMedium(p);return color*medium.a+select(medium.rgb,vec3f(0),additive);
}
fn cloudSurfaceShadow(p:vec3f,l:vec3f) -> f32 {
    let c=frame.cloud;if(!cloudShadowsEnabled || c.sun.w<=0 || p.y>=c.layer.x || dot(l,c.sun.xyz)<0.995) {return 1;}
    let entry=p.xz+c.sun.xz*((c.layer.x-p.y)/c.sun.y);
    let uv=(entry-c.shadow.xy)/(2.0*c.shadow.z)+0.5;
    if(any(uv<=vec2f(0)) || any(uv>=vec2f(1))) {return 1;}
    let fade=1.0-smoothstep(0.8,1.0,max(abs(uv.x*2-1),abs(uv.y*2-1)));
    let size=vec2i(textureDimensions(shadowMaps));let texel=clamp(uv*vec2f(size)-0.5,vec2f(0),vec2f(size-1));let lo=vec2i(floor(texel));let hi=min(lo+1,size-1);let f=fract(texel);
    let a=mix(textureLoad(shadowMaps,lo,4,0),textureLoad(shadowMaps,vec2i(hi.x,lo.y),4,0),f.x);
    let b=mix(textureLoad(shadowMaps,vec2i(lo.x,hi.y),4,0),textureLoad(shadowMaps,hi,4,0),f.x);
    return mix(1.0,mix(a,b,f.y),c.sun.w*fade);
}

