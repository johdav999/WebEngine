struct Fog { vec4 colorDensity; vec4 height; vec4 eye; vec4 forward; };
uniform Fog fog;
float fogDepth(Fog f,vec3 origin,vec3 end,float start) {
    float distance=length(end-origin);
    if(f.colorDensity.w<=0.0 || distance<=start) return 0.0;
    float y=mix(origin.y,end.y,start/max(distance,1e-6));
    float a=-f.height.z*(y-f.height.y),b=-f.height.z*(end.y-f.height.y),delta=abs(b-a);
    float ratio=delta<0.001 ? 1.0-delta*0.5+delta*delta/6.0 : (1.0-exp(-delta))/max(delta,1e-6);
    return min(80.0,f.colorDensity.w*(distance-start)*exp(clamp(max(a,b),-40.0,40.0))*ratio);
}
vec3 fogOrigin(Fog f,vec3 p) {
    return f.eye.w>0.5 ? p-f.forward.xyz*(dot(p-f.eye.xyz,f.forward.xyz)-f.forward.w) : f.eye.xyz;
}
vec3 fogApply(Fog f,vec3 color,vec3 p,bool additive) {
    float t=exp(-fogDepth(f,fogOrigin(f,p),p,f.height.x));
    return mix(additive ? vec3(0) : f.colorDensity.rgb,color,t);
}
