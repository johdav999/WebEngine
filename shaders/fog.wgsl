struct Fog { colorDensity:vec4f, height:vec4f, eye:vec4f, forward:vec4f };
// Stable analytic average of exp(-falloff * (height-baseHeight)).
fn fogDepth(f:Fog,origin:vec3f,end:vec3f,start:f32) -> f32 {
    let distance=length(end-origin);
    if(f.colorDensity.w<=0 || distance<=start) {return 0;}
    let y=mix(origin.y,end.y,start/max(distance,1e-6));
    let a=-f.height.z*(y-f.height.y);let b=-f.height.z*(end.y-f.height.y);
    let delta=abs(b-a);
    let ratio=select((1-exp(-delta))/max(delta,1e-6),1-delta*0.5+delta*delta/6,delta<0.001);
    return min(80.0,f.colorDensity.w*(distance-start)*exp(clamp(max(a,b),-40,40))*ratio);
}
fn fogOrigin(f:Fog,p:vec3f) -> vec3f {
    return select(f.eye.xyz,p-f.forward.xyz*(dot(p-f.eye.xyz,f.forward.xyz)-f.forward.w),f.eye.w>0.5);
}
fn fogApply(f:Fog,color:vec3f,p:vec3f,additive:bool) -> vec3f {
    let t=exp(-fogDepth(f,fogOrigin(f,p),p,f.height.x));
    return mix(select(f.colorDensity.rgb,vec3f(0),additive),color,t);
}
