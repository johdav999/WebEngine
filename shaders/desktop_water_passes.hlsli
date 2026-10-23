// Snapshot shaders average color and select the nearest opaque depth at 4x MSAA.
Texture2D captureColor : register(t19);
Texture2D<float> captureDepth : register(t20);
Texture2DMS<float4,4> captureColorMs : register(t21);
Texture2DMS<float,4> captureDepthMs : register(t22);
struct WaterCaptureOutput {float4 color:SV_Target0;float depth:SV_Target1;};
WaterCaptureOutput waterCapturePS(float4 pixel:SV_Position){WaterCaptureOutput o;o.color=captureColor.Load(int3(int2(pixel.xy),0));o.depth=captureDepth.Load(int3(int2(pixel.xy),0));return o;}
WaterCaptureOutput waterCaptureMsPS(float4 pixel:SV_Position){WaterCaptureOutput o;o.color=0;o.depth=1;[unroll]for(uint i=0;i<4;i++){o.color+=captureColorMs.Load(int2(pixel.xy),i)*.25;o.depth=min(o.depth,captureDepthMs.Load(int2(pixel.xy),i));}return o;}
cbuffer Underwater : register(b6){
    column_major float4x4 underwaterInverse,waterWorldToLocal;
    float4 underwaterViewport,underwaterEye,waterMinimum,waterMaximum,underwaterAbsorption,underwaterColorDensity,underwaterOptions,caustics;
    float4 waterRegionPlanes[16];
};
float3 underwaterPoint(float2 pixel,float depth){const float2 ndc=(pixel-underwaterViewport.xy)/underwaterViewport.zw*float2(2,-2)+float2(-1,1);const float4 p=mul(underwaterInverse,float4(ndc,depth,1));return p.xyz/p.w;}
float2 waterSlab(float p,float d,float lo,float hi,float2 interval){if(abs(d)<1e-7)return p>=lo&&p<=hi?interval:float2(1,0);const float a=(lo-p)/d,b=(hi-p)/d;return float2(max(interval.x,min(a,b)),min(interval.y,max(a,b)));}
float distanceInWater(float3 start,float3 end){
    const float3 origin=mul(waterWorldToLocal,float4(start,1)).xyz,delta=mul(waterWorldToLocal,float4(end-start,0)).xyz;
    float2 interval=waterSlab(origin.x,delta.x,waterMinimum.x,waterMaximum.x,float2(0,1));interval=waterSlab(origin.z,delta.z,waterMinimum.z,waterMaximum.z,interval);
    if(underwaterOptions.z>0){interval=waterSlab(origin.y,delta.y,waterMinimum.y,0,interval);
        [loop]for(uint i=0;i<16;i++){if(i>=uint(underwaterOptions.z))break;const float4 e=waterRegionPlanes[i];const float p=dot(e.xy,origin.xz)+e.z,d=dot(e.xy,delta.xz);
            if(abs(d)<1e-7){if(p<0)return 0;}else if(d>0)interval.x=max(interval.x,-p/d);else interval.y=min(interval.y,-p/d);}}
    if(abs(delta.y)<1e-7){if(origin.y>0)return 0;}else if(delta.y>0)interval.y=min(interval.y,-origin.y/delta.y);else interval.x=max(interval.x,-origin.y/delta.y);
    return min(underwaterOptions.x,max(0,interval.y-interval.x)*length(end-start));
}
bool inWaterRegion(float3 p){if(underwaterOptions.z<=0)return true;if(p.y<waterMinimum.y)return false;
    [loop]for(uint i=0;i<16;i++){if(i>=uint(underwaterOptions.z))break;const float4 e=waterRegionPlanes[i];if(dot(e.xy,p.xz)+e.z<0)return false;}return true;}
float causticPattern(float2 p,float time){
    const float2 cell=floor(p),f=frac(p);float2 nearest=float2(8,8);
    [unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++){
        const float2 g=float2(x,y),seed=cell+g,phase=float2(dot(seed,float2(127.1,311.7)),dot(seed,float2(269.5,183.3)));
        const float2 offset=.5+.32*sin(phase+time*float2(.8,1.1));const float d=length(g+offset-f);
        if(d<nearest.x)nearest=float2(d,nearest.x);else nearest.y=min(nearest.y,d);
    }return 1-smoothstep(.025,.14,nearest.y-nearest.x);
}
float4 underwaterPS(float4 pixel:SV_Position):SV_Target{
    const float4 source=sceneWaterColor.Load(int3(int2(pixel.xy),0));const float depth=sceneWaterDepth.Load(int3(int2(pixel.xy),0));
    const float3 end=underwaterPoint(pixel.xy,depth),surfaceNormal=cross(ddx(end),ddy(end)),local=mul(waterWorldToLocal,float4(end,1)).xyz;
    const float3 planeNormal=float3(waterWorldToLocal[1][0],waterWorldToLocal[1][1],waterWorldToLocal[1][2]);const float planeLength=length(planeNormal),below=-local.y/planeLength;
    const float facing=abs(dot(surfaceNormal,planeNormal))/(max(length(surfaceNormal),1e-8)*planeLength);float light=0;
    if(inWaterRegion(local)&&caustics.x>0&&depth<.999999&&local.y < -underwaterOptions.w&&local.x>=waterMinimum.x&&local.x<=waterMaximum.x&&local.z>=waterMinimum.z&&local.z<=waterMaximum.z){
        const float2 units=float2(length(waterWorldToLocal[0].xyz),length(waterWorldToLocal[2].xyz));
        light=causticPattern(local.xz/units*caustics.y,caustics.z)*caustics.x*(1-smoothstep(0,caustics.w,below))*smoothstep(.15,.75,facing)*underwaterAbsorption.w;
    }
    const float3 start=underwaterOptions.y>.5?underwaterPoint(pixel.xy,0):underwaterEye.xyz;
    const float path=distanceInWater(start,end)*underwaterAbsorption.w,transmission=exp(-underwaterColorDensity.w*path);
    const float3 result=linearDisplay(source.rgb)*(1+light)*exp(-underwaterAbsorption.xyz*path)*transmission+underwaterColorDensity.xyz*(1-transmission);
    return float4(outputOptions.x>.5?result:srgb(result),source.a);
}
