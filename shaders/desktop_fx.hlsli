// Uses the portable FxFrame instance/vertex payloads and particles.wgsl math.
cbuffer Fx : register(b7) { float4 fxRight,fxUp,fxTexture; };
struct ParticleInput {float4 positionSize:POSITION;float4 color:COLOR;float4 ageRotation:TEXCOORD0;float4 atlas:TEXCOORD1;float4 parameters:TEXCOORD2;float4 planeRight:TEXCOORD3;float4 planeUp:TEXCOORD4;};
struct RibbonInput {float4 positionAdditive:POSITION;float4 color:COLOR;float4 uvSoft:TEXCOORD0;};
struct FxOutput {float4 position:SV_Position;float3 world:POSITION;float2 uv:TEXCOORD0;float4 color:COLOR;nointerpolation float3 parameters:TEXCOORD1;};
FxOutput particleVS(ParticleInput p,uint vertex:SV_VertexID) {
    const float2 corners[6]={float2(-.5,-.5),float2(.5,-.5),float2(-.5,.5),float2(-.5,.5),float2(.5,-.5),float2(.5,.5)};
    const float2 corner=corners[vertex],shaped=corner*float2(p.positionSize.w,p.positionSize.w+p.ageRotation.w);
    const float c=cos(p.ageRotation.y),s=sin(p.ageRotation.y);
    const float2 rotated=float2(c*shaped.x-s*shaped.y,s*shaped.x+c*shaped.y);
    FxOutput o;o.world=p.positionSize.xyz+(p.parameters.y>.5?p.planeRight.xyz:fxRight.xyz)*rotated.x+(p.parameters.y>.5?p.planeUp.xyz:fxUp.xyz)*rotated.y;
    o.position=mul(viewProjection,float4(o.world,1));
    const float age=saturate(p.ageRotation.x),phase=age>=1?1:frac(age*p.atlas.w);
    const uint frame=min(uint(phase*p.atlas.z),uint(p.atlas.z)-1);
    const float2 cell=float2(frame%uint(p.atlas.x),frame/uint(p.atlas.x)),local=float2(corner.x+.5,.5-corner.y);
    o.uv=(cell+local)/p.atlas.xy+.5/fxTexture.xy*(1-2*local);
    o.color=p.color;o.parameters=float3(p.ageRotation.z,p.parameters.x,0);return o;
}
FxOutput ribbonVS(RibbonInput p) {
    FxOutput o;o.world=p.positionAdditive.xyz;o.position=mul(viewProjection,float4(o.world,1));o.uv=p.uvSoft.xy;
    o.color=p.color;o.parameters=float3(p.positionAdditive.w,p.uvSoft.z,1);return o;
}
float4 fxPS(FxOutput i):SV_Target {
    const float2 uv=i.parameters.z>.5?float2(frac(i.uv.x),i.uv.y):i.uv;
    float4 texel;
    const uint wrap=uint(fxTexture.z);
    if(wrap==1)texel=baseImage.SampleLevel(clampRepeat,uv,0);
    else if(wrap==2)texel=baseImage.SampleLevel(repeatClamp,uv,0);
    else if(wrap==3)texel=baseImage.SampleLevel(clampClamp,uv,0);
    else texel=baseImage.SampleLevel(repeatRepeat,uv,0);
    float fade=1;
    if(i.parameters.y>0) {
        const float2 pixel=clamp(i.position.xy,waterViewport.xy+.5,waterViewport.xy+waterViewport.zw-.5);
        const float depth=sceneWaterDepth.Load(int3(int2(pixel),0));
        if(depth<.999999) {
            const float2 ndc=(pixel-waterViewport.xy)/waterViewport.zw*float2(2,-2)+float2(-1,1);
            const float4 opaquePosition=mul(inverseViewProjection,float4(ndc,depth,1));
            fade=saturate(dot(opaquePosition.xyz/opaquePosition.w-i.world,viewForward.xyz)/i.parameters.y);
        }
    }
    const float alpha=texel.a*i.color.a*fade;float3 color=texel.rgb*i.color.rgb;
    if(outputOptions.x<.5)color=color/(1+color);
    color=fogApply(frameFog,color,i.world,i.parameters.x>.5);
    if(outputOptions.x<.5)color=srgb(color);
    return float4(color*alpha,alpha);
}
