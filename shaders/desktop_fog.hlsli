// Matches the portable FogUniform and fog.wgsl analytic integration.
struct Fog {float4 colorDensity,height,eye,forward;};
float fogDepth(Fog f,float3 origin,float3 endpoint,float start) {
    const float distance=length(endpoint-origin);
    if(f.colorDensity.w<=0||distance<=start)return 0;
    const float y=lerp(origin.y,endpoint.y,start/max(distance,1e-6));
    const float a=-f.height.z*(y-f.height.y),b=-f.height.z*(endpoint.y-f.height.y),delta=abs(b-a);
    const float ratio=delta<.001?1-delta*.5+delta*delta/6:(1-exp(-delta))/max(delta,1e-6);
    return min(80,f.colorDensity.w*(distance-start)*exp(clamp(max(a,b),-40,40))*ratio);
}
float3 fogApply(Fog f,float3 color,float3 p,bool additive) {
    const float3 origin=f.eye.w>.5?p-f.forward.xyz*(dot(p-f.eye.xyz,f.forward.xyz)-f.forward.w):f.eye.xyz;
    const float t=exp(-fogDepth(f,origin,p,f.height.x));
    return lerp(additive?float3(0,0,0):f.colorDensity.rgb,color,t);
}
