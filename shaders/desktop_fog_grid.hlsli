Texture2D<float4> fogGrid : register(t24);
float4 fogGridSlice(float2 uv,float slice) {
    if(slice<1.0) return float4(0,0,0,1);
    float2 texel=clamp(uv*float2(64,36),float2(.5,.5),float2(63.5,35.5));
    uint width,height;fogGrid.GetDimensions(width,height);
    float2 atlasUv=(texel+float2(0,(slice-1.0)*36.0))/float2(width,height);
    return fogGrid.SampleLevel(clampClamp,atlasUv,0);
}
float4 fogGridMedium(float3 p,float3 origin) {
    float3 delta=p-origin;
    float distance=length(delta);
    float3 direction=delta/max(distance,1e-6);
    float2 slab=fogRayRange(frameFog,origin,direction,frameFog.volumeMaxDistance.w);
    if(slab.x>=slab.y || distance<=slab.x) {
        float t=exp(-fogDepth(frameFog,origin,p,frameFog.height.x));
        return float4(frameFog.colorDensity.rgb*(1.0-t),t);
    }
    float4 clip=mul(viewProjection,float4(p,1));
    if(clip.w<=0.0) return fogVolumeSegment(origin,p,0.0);
    float2 uv=clip.xy/clip.w*float2(.5,-.5)+.5;
    float slice=clamp((distance-slab.x)/(slab.y-slab.x),0.0,1.0)*16.0;
    float nearSlice=floor(slice);
    float4 medium=lerp(fogGridSlice(uv,nearSlice),fogGridSlice(uv,min(16.0,nearSlice+1.0)),frac(slice));
    float front=exp(-fogDepth(frameFog,origin,origin+direction*slab.x,frameFog.height.x));
    float tail=exp(-fogDepth(frameFog,origin+direction*min(distance,slab.y),p,max(0.0,frameFog.height.x-slab.y)));
    float3 haze=frameFog.colorDensity.rgb*((1.0-front)+front*medium.a*(1.0-tail));
    return float4(haze+front*medium.rgb,front*medium.a*tail);
}
float3 fogVolumeApply(float3 color,float3 p,bool additive) {
    if(frameFog.volumeMinSteps.w<=0.0) return fogApply(frameFog,color,p,additive);
    float3 origin=(frameFog.eye.w>.5?p-frameFog.forward.xyz*(dot(p-frameFog.eye.xyz,frameFog.forward.xyz)-frameFog.forward.w):frameFog.eye.xyz);
    float4 medium=frameFog.volumeOptions.y>0.5 ? fogGridMedium(p,origin) : fogVolumeSegment(origin,p,0.0);
    return color*medium.a+(additive ? 0 : medium.rgb);
}

float4 fogGridPS(float4 pixel:SV_Position) : SV_Target {
    const float slice=floor(pixel.y/36);
    const float2 uv=float2(pixel.x,pixel.y-slice*36)/float2(64,36);
    const float4 unprojected=mul(inverseViewProjection,float4(uv*float2(2,-2)+float2(-1,1),1,1));
    const float3 endpoint=unprojected.xyz/unprojected.w;
    const float3 origin=frameFog.eye.w>.5?endpoint-frameFog.forward.xyz*(dot(endpoint-frameFog.eye.xyz,frameFog.forward.xyz)-frameFog.forward.w):frameFog.eye.xyz;
    const float3 direction=normalize(endpoint-origin);
    const float2 slab=fogRayRange(frameFog,origin,direction,frameFog.volumeMaxDistance.w);
    if(slab.x>=slab.y) return float4(0,0,0,1);
    const int substeps=int(ceil(frameFog.volumeMinSteps.w/16));
    const int steps=(int(slice)+1)*substeps;
    const float ds=(slab.y-slab.x)/(16*substeps);
    const int4 indices=fogVolumeLightIndices();
    // Grid production is used only for a single box.
    const float2 ranges[4]={float2(0,0),float2(0,0),float2(0,0),float2(0,0)};
    float3 scatter=0;
    float transmission=1;
    for(int i=0;i<steps && transmission>0.0001;i++) {
        const float4 segment=fogVolumeStep(frameFog,origin,direction,slab.x+i*ds,ds,frameFog.height.x,indices,1,ranges);
        scatter+=transmission*segment.rgb;
        transmission*=segment.a;
    }
    return float4(min(scatter,65504),transmission);
}
float2 cloudRange(float3 origin,float3 d) {
    const CloudData c=frameCloud;
    float enter=0.0,leave=c.layer.z;
    if(abs(d.y)<.00001) {
        if(origin.y<=c.layer.x||origin.y>=c.layer.x+c.layer.y)return 0;
    } else {
        const float a=(c.layer.x-origin.y)/d.y,b=(c.layer.x+c.layer.y-origin.y)/d.y;
        enter=max(0.0,min(a,b));leave=min(leave,max(a,b));
    }
    return float2(enter,leave);
}
float4 cloudGridSlice(float2 uv,float slice) {
    if(slice<1.0)return float4(0,0,0,1);
    const float2 texel=clamp(uv*float2(64,36),float2(.5,.5),float2(63.5,35.5));
    uint width,height;fogGrid.GetDimensions(width,height);
    return fogGrid.SampleLevel(clampClamp,(texel+float2(0,576+(slice-1.0)*36.0))/float2(width,height),0);
}
float4 cloudGridMedium(float3 p) {
    if(frameCloud.layer.w<.5)return float4(0,0,0,1);
    const float4 clip=mul(viewProjection,float4(p,1));
    if(clip.w<=0)return float4(0,0,0,1);
    const float2 ndc=clip.xy/clip.w,uv=ndc*float2(.5,-.5)+.5;
    float3 origin=frameCloud.eye.xyz;
    if(frameCloud.eye.w<.5) {
        const float4 a=mul(inverseViewProjection,float4(ndc,0,1)),b=mul(inverseViewProjection,float4(0,0,0,1));
        origin+=a.xyz/a.w-b.xyz/b.w;
    }
    const float3 delta=p-origin;
    const float distance=length(delta);
    const float2 slab=cloudRange(origin,delta/max(distance,.00001));
    if(slab.y<=slab.x||distance<=slab.x)return float4(0,0,0,1);
    const float node=saturate((distance-slab.x)/(slab.y-slab.x))*32.0,lo=floor(node);
    return lerp(cloudGridSlice(uv,lo),cloudGridSlice(uv,min(32.0,lo+1.0)),frac(node));
}
float3 cloudApply(float3 color,float3 p,bool additive) {
    const float4 medium=cloudGridMedium(p);
    return color*medium.a+(additive?0:medium.rgb);
}
float cloudSurfaceShadow(float3 p,float3 l) {
    const CloudData c=frameCloud;
    if(c.sun.w<=0||p.y>=c.layer.x||dot(l,c.sun.xyz)<.995)return 1;
    const float2 entry=p.xz+c.sun.xz*((c.layer.x-p.y)/c.sun.y);
    const float2 uv=(entry-c.shadow.xy)/(2.0*c.shadow.z)+.5;
    if(any(uv<=0)||any(uv>=1))return 1;
    const float fade=1.0-smoothstep(.8,1.0,max(abs(uv.x*2-1),abs(uv.y*2-1)));
    uint width,height,layers;shadowImages.GetDimensions(width,height,layers);
    const float2 texel=clamp(uv*float2(width,height)-.5,float2(0,0),float2(width-1,height-1));
    const int2 lo=int2(floor(texel)),hi=min(lo+1,int2(width-1,height-1));
    const float2 f=frac(texel);
    const float a=lerp(shadowImages.Load(int4(lo,4,0)),shadowImages.Load(int4(hi.x,lo.y,4,0)),f.x);
    const float b=lerp(shadowImages.Load(int4(lo.x,hi.y,4,0)),shadowImages.Load(int4(hi,4,0)),f.x);
    return lerp(1.0,lerp(a,b,f.y),c.sun.w*fade);
}
