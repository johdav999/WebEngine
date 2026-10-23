// D3D12 material, deformation and lighting contract mirrors mesh.wgsl.
#include "desktop_fog.hlsli"
struct Light { float4 positionType, directionRange, colorIntensity, cone; };
struct Shadow { column_major float4x4 matrix; float4 options, selection; };
cbuffer Frame : register(b0) {
    column_major float4x4 viewProjection;
    float4 ambientCount; Light lights[8];
    float4 cameraEnvironment, environmentOptions;
    Shadow shadows[4]; float4 viewForward;Fog frameFog;float4 outputOptions;
    float4 waterClock;column_major float4x4 inverseViewProjection;float4 waterViewport,waterExtent;
    column_major float4x4 reflectionViewProjection;float4 reflectionPlane,reflectionOptions,interactionOptions,interactionData[64];
};
cbuffer Object : register(b1) {
    column_major float4x4 model, normalMatrix;
    float4 tint, surface, emissionCutoff, uvTransform;
    float4 options; // unlit, alpha mode, double sided, receives shadows
    float4 maps; // normal presence, nine packed two-bit wrap modes, fog mode, layered
    float4 layerColor[4],layerUv[4],weightUv,layerOptions,layerDetail[4];
    float4 wave,waterDetail,waterHorizon,waterZenith,transmission,absorptionFoam,foamColor;
};
cbuffer Screen : register(b2) { float4 screen; };
cbuffer ShadowPass : register(b3) { column_major float4x4 shadowMatrix; };
Texture2D baseImage : register(t0);
Texture2D surfaceImage : register(t1);
Texture2D normalImage : register(t2);
Texture2D occlusionImage : register(t3);
Texture2D emissiveImage : register(t4);
Texture2DArray<float> shadowImages : register(t5);
TextureCube specularEnvironment : register(t6);
TextureCube diffuseEnvironment : register(t7);
Texture2D brdfLut : register(t8);
Texture2D layerDetail0 : register(t12);
Texture2D layerDetail1 : register(t13);
Texture2D layerDetail2 : register(t14);
Texture2D layerDetail3 : register(t15);
Texture2D sceneWaterColor : register(t16);
Texture2D<float> sceneWaterDepth : register(t17);
Texture2D planarReflection : register(t18);
struct Bone { column_major float4x4 transform,normal; };
StructuredBuffer<Bone> bones : register(t11);
SamplerState repeatRepeat : register(s0);
SamplerState clampRepeat : register(s1);
SamplerState repeatClamp : register(s2);
SamplerState clampClamp : register(s3);
SamplerState environmentSampler : register(s4);
SamplerComparisonState shadowSampler : register(s5);
float3 safeNormal(float3 v,float3 fallback) {return dot(v,v)>1e-12?v*rsqrt(max(dot(v,v),1e-12)):fallback;}
float4 sampleMap(Texture2D image,uint slot,float2 uv) {
    const uint wrap=(uint(maps.y)>>(slot*2))&3;
    if(wrap==1)return image.Sample(clampRepeat,uv);
    if(wrap==2)return image.Sample(repeatClamp,uv);
    if(wrap==3)return image.Sample(clampClamp,uv);
    return image.Sample(repeatRepeat,uv);
}
float3 srgb(float3 v) {
    v=max(v,0);
    return float3(v.x<=.0031308?v.x*12.92:1.055*pow(v.x,1.0/2.4)-.055,
                  v.y<=.0031308?v.y*12.92:1.055*pow(v.y,1.0/2.4)-.055,
                  v.z<=.0031308?v.z*12.92:1.055*pow(v.z,1.0/2.4)-.055);
}
struct MeshInput {float3 position:POSITION;float3 normal:NORMAL;float2 uv:TEXCOORD;float4 tangent:TANGENT;};
struct SkinInput {float3 position:POSITION;float3 normal:NORMAL;float2 uv:TEXCOORD;float4 tangent:TANGENT;uint4 joints:BLENDINDICES;float4 weights:BLENDWEIGHT;};
struct MeshOutput {float4 position:SV_Position;float3 world:POSITION;float3 normal:NORMAL;float2 uv:TEXCOORD;float4 tangent:TANGENT;float3 planeNormal:TEXCOORD1;nointerpolation float planarSurface:TEXCOORD2;nointerpolation float4 instanceColor:COLOR;nointerpolation float4 instanceFlags:TEXCOORD3;};
#include "desktop_fx.hlsli"
#include "desktop_layers.hlsli"
#include "desktop_water_surface.hlsli"
MeshOutput surfaceVertexFor(float3 position,float3 normal,float3 tangent,float3 bitangent,float2 uv,column_major float4x4 objectModel,column_major float4x4 objectNormal,float4 objectTint,float4 objectFlags) {
    if(options.x>1.5)waterVertex(position,normal,tangent,bitangent);
    MeshOutput output;output.instanceColor=objectTint;output.instanceFlags=objectFlags;const float4 world=mul(objectModel,float4(position,1));
    output.position=mul(viewProjection,world);output.world=world.xyz;
    const float3 n=safeNormal(mul(objectNormal,float4(normal,0)).xyz,float3(0,1,0));
    const float3 wt=mul(objectModel,float4(tangent,0)).xyz;
    const float3 wb=mul(objectModel,float4(bitangent,0)).xyz;
    const float3 t=safeNormal(wt-n*dot(n,wt),float3(1,0,0));
    output.normal=n;output.tangent=float4(t,dot(cross(n,t),wb)<0?-1:1);
    output.uv=uv*uvTransform.xy+uvTransform.zw;output.planeNormal=safeNormal(mul(objectNormal,float4(0,1,0,0)).xyz,float3(0,1,0));
    output.planarSurface=maps.z>.5&&abs(dot(reflectionPlane,mul(objectModel,float4(0,0,0,1))))<.01&&abs(dot(output.planeNormal,reflectionPlane.xyz))>.999?1:0;return output;
}
MeshOutput surfaceVertex(float3 position,float3 normal,float3 tangent,float3 bitangent,float2 uv){return surfaceVertexFor(position,normal,tangent,bitangent,uv,model,normalMatrix,tint,float4(options.w,0,0,maps.z));}
struct InstanceObject {column_major float4x4 model,normal;float4 color,flags;float4 padding[6];};
StructuredBuffer<InstanceObject> instanceObjects:register(t23);
MeshOutput instanceVS(MeshInput input,uint id:SV_InstanceID){const InstanceObject object=instanceObjects[id];return surfaceVertexFor(input.position,input.normal,input.tangent.xyz,cross(input.normal,input.tangent.xyz)*input.tangent.w,input.uv,object.model,object.normal,object.color,object.flags);}
MeshOutput instanceShadowVS(MeshInput input,uint id:SV_InstanceID){MeshOutput output=instanceVS(input,id);output.position=mul(shadowMatrix,float4(output.world,1));return output;}
MeshOutput meshVS(MeshInput input) {
    return surfaceVertex(input.position,input.normal,input.tangent.xyz,cross(input.normal,input.tangent.xyz)*input.tangent.w,input.uv);
}
MeshOutput skinFor(SkinInput input,column_major float4x4 objectModel,column_major float4x4 objectNormal,float4 objectTint,float4 objectFlags) {
    float3 p=0,n=0,t=0,b=0;const float3 bitangent=cross(input.normal,input.tangent.xyz)*input.tangent.w;
    [unroll]for(uint i=0;i<4;i++) {
        const Bone bone=bones[input.joints[i]];const float weight=input.weights[i];
        p+=weight*mul(bone.transform,float4(input.position,1)).xyz;
        n+=weight*mul(bone.normal,float4(input.normal,0)).xyz;
        t+=weight*mul(bone.transform,float4(input.tangent.xyz,0)).xyz;
        b+=weight*mul(bone.transform,float4(bitangent,0)).xyz;
    }
    return surfaceVertexFor(p,n,t,b,input.uv,objectModel,objectNormal,objectTint,objectFlags);
}
MeshOutput skinVS(SkinInput input){return skinFor(input,model,normalMatrix,tint,float4(options.w,0,0,maps.z));}
MeshOutput instanceSkinVS(SkinInput input,uint id:SV_InstanceID){const InstanceObject object=instanceObjects[id];return skinFor(input,object.model,object.normal,object.color,object.flags);}
MeshOutput instanceSkinShadowVS(SkinInput input,uint id:SV_InstanceID){MeshOutput output=instanceSkinVS(input,id);output.position=mul(shadowMatrix,float4(output.world,1));return output;}
MeshOutput shadowVS(MeshInput input) {
    MeshOutput output=surfaceVertex(input.position,input.normal,input.tangent.xyz,cross(input.normal,input.tangent.xyz)*input.tangent.w,input.uv);
    output.position=mul(shadowMatrix,float4(output.world,1));return output;
}
MeshOutput skinShadowVS(SkinInput input) {
    MeshOutput output=skinVS(input);output.position=mul(shadowMatrix,float4(output.world,1));return output;
}
void shadowPS(MeshOutput input) {
    const float alpha=(maps.w>.5?layerSurface(input).color.a:sampleMap(baseImage,0,input.uv).a)*input.instanceColor.a;
    if(options.y==1&&alpha<emissionCutoff.w)discard;
}
float sampleShadow(uint index,MeshOutput input,float3 n,float3 l) {
    const Shadow shadow=shadows[index];if(shadow.options.x<0||input.instanceFlags.x<.5)return 1;
    const float3 geometric=safeNormal(input.normal,n),offset=geometric*shadow.options.z*(1-saturate(dot(geometric,l)));
    const float4 clip=mul(shadow.matrix,float4(input.world+offset,1));if(clip.w<=0)return 1;
    const float3 ndc=clip.xyz/clip.w;const float2 uv=ndc.xy*float2(.5,-.5)+.5;
    if(any(uv<0)||any(uv>1)||ndc.z<0||ndc.z>1)return 1;
    const float reference=ndc.z-shadow.options.y;
    if(environmentOptions.w<2)return shadowImages.SampleCmpLevelZero(shadowSampler,float3(uv,shadow.options.x),reference);
    float visibility=0;
    [unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++)
        visibility+=shadowImages.SampleCmpLevelZero(shadowSampler,float3(uv+float2(x,y)*shadow.options.w,shadow.options.x),reference);
    return visibility/9;
}
float shadowVisibility(uint index,MeshOutput input,float3 n,float3 l) {
    const float depth=dot(input.world-cameraEnvironment.xyz,viewForward.xyz);
    for(uint j=0;j<4;j++) {
        const Shadow s=shadows[j];if(s.options.x<0||uint(s.selection.x)!=index)continue;
        if(s.selection.w>.5&&depth>s.selection.z)continue;
        float value=sampleShadow(j,input,n,l);
        if(s.selection.w>.5&&j<3) {
            const Shadow next=shadows[j+1];
            if(next.options.x>=0&&next.selection.x==s.selection.x)
                value=lerp(value,sampleShadow(j+1,input,n,l),smoothstep(lerp(s.selection.y,s.selection.z,.9),s.selection.z,depth));
        }
        return value;
    }
    return 1;
}
float3 environmentDirection(float3 d) {const float c=cos(environmentOptions.x),s=sin(environmentOptions.x);return float3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);}
float3 directBrdf(float3 n,float3 v,float3 l,float3 base,float rough,float metal,float3 f0) {
    const float3 h=safeNormal(v+l,n);const float nv=max(dot(n,v),.001),nl=max(dot(n,l),0),nh=max(dot(n,h),0),vh=max(dot(v,h),0);
    const float a=rough*rough,a2=a*a,denom=nh*nh*(a2-1)+1,d=a2/(3.14159265*denom*denom);
    const float k=(rough+1)*(rough+1)/8,g=(nv/(nv*(1-k)+k))*(nl/max(nl*(1-k)+k,.001));
    const float3 f=f0+(1-f0)*pow(saturate(1-vh),5),specular=d*g*f/max(4*nv*nl,.001);
    return ((1-f)*(1-metal)*base/3.14159265+specular)*nl;
}
float4 meshPS(MeshOutput input,bool front:SV_IsFrontFace):SV_Target {
    const bool layered=maps.w>.5;LayerSurface layer=(LayerSurface)0;if(layered)layer=layerSurface(input);
    const float4 texel=layered?layer.color:sampleMap(baseImage,0,input.uv);
    const float4 packedSurface=layered?float4(1,layer.roughness,1,1):sampleMap(surfaceImage,1,input.uv);
    const float3 normalSample=layered?float3(0,0,1):sampleMap(normalImage,2,input.uv).xyz*2-1;
    const float aoSample=layered?1:sampleMap(occlusionImage,3,input.uv).r;
    const float3 emission=emissionCutoff.xyz*(layered?float3(1,1,1):sampleMap(emissiveImage,4,input.uv).rgb);
    const float alpha=texel.a*input.instanceColor.a;if(reflectionOptions.y>.5&&dot(reflectionPlane,float4(input.world,1))<.002)discard;
    if(options.y==1&&alpha<emissionCutoff.w)discard;
    const float3 base=max(input.instanceColor.rgb*texel.rgb,0);
    if(options.x>.5&&options.x<1.5){float3 display=surfaceFog(base+emission,input);return float4(outputOptions.x>.5?display:srgb(display),options.y<2?1:alpha);}
    float3 n=safeNormal(input.normal,float3(0,1,0));const float3 t=safeNormal(input.tangent.xyz-n*dot(n,input.tangent.xyz),float3(1,0,0)),b=cross(n,t)*input.tangent.w;
    if(layered)n=layer.normal;
    if(!layered&&(uint(maps.x)&1)!=0) {
        const float3 mapped=safeNormal(float3(normalSample.xy*surface.z,normalSample.z),float3(0,0,1));
        n=safeNormal(t*mapped.x*(uvTransform.x<0?-1:1)+b*mapped.y*(uvTransform.y<0?-1:1)+n*mapped.z,n);
    }
    float4 interaction=0;
    if(options.x>1.5){
        const float2 waterUv=input.uv*waterDetail.z;const float time=waterClock.x*wave.z;
        const float3 rippleA=sampleMap(normalImage,2,waterUv+float2(.021,.013)*time).xyz*2-1;
        const float3 rippleB=sampleMap(normalImage,2,waterUv*1.37+float2(-.017,.019)*time).xyz*2-1;
        const float2 r=(rippleA.xy+rippleB.xy)*surface.z+.12*float2(sin(waterUv.x*6.2831853+time),cos(waterUv.y*8.1-time*1.3));
        n=safeNormal(input.normal+(t*r.x+b*r.y)*waterDetail.w,input.normal);
        if(maps.z>.5&&interactionOptions.x>0)interaction=interactionSurface(input.world,input.planeNormal);
        n=safeNormal(n-interaction.xyz*.12,n);
    }
    if(options.z>.5&&!front)n=-n;
    const float rough=clamp(surface.x*packedSurface.g,.045,1),metal=saturate(surface.y*packedSurface.b),ao=lerp(1,aoSample,surface.w);
    const float3 v=safeNormal(cameraEnvironment.xyz-input.world,n),f0=lerp(float3(.04,.04,.04),base,metal);const float nv=max(dot(n,v),.001);
    float3 radiance=0;
    for(uint i=0;i<uint(ambientCount.w);i++) {
        const Light light=lights[i];float3 l=safeNormal(-light.directionRange.xyz,n);float attenuation=1;
        if(light.positionType.w>.5) {
            const float3 delta=light.positionType.xyz-input.world;const float distance=max(length(delta),.001);l=delta/distance;
            attenuation=pow(saturate(1-distance/max(light.directionRange.w,.001)),2);
            if(light.positionType.w>1.5)attenuation*=smoothstep(light.cone.y,light.cone.x,dot(light.directionRange.xyz,-l));
        }
        radiance+=directBrdf(n,v,l,base,rough,metal,f0)*light.colorIntensity.rgb*light.colorIntensity.w*attenuation*shadowVisibility(i,input,n,l);
    }
    const float3 irradiance=diffuseEnvironment.SampleLevel(environmentSampler,environmentDirection(n),0).rgb;
    const float3 reflection=specularEnvironment.SampleLevel(environmentSampler,environmentDirection(reflect(-v,n)),rough*environmentOptions.y).rgb;
    const float2 integrated=brdfLut.SampleLevel(environmentSampler,float2(nv,rough),0).rg;
    const float3 f=f0+(max(1-rough,f0)-f0)*pow(1-nv,5),diffuse=(1-f)*(1-metal)*base;
    radiance+=ao*(diffuse*(irradiance*cameraEnvironment.w+ambientCount.rgb)+reflection*(f0*integrated.x+integrated.y)*cameraEnvironment.w);
    if(options.x>1.5){
        const float3 reflected=reflect(-v,n),sky=lerp(waterHorizon.rgb,waterZenith.rgb,saturate(reflected.y));
        const float3 reflectedColor=cameraEnvironment.w>0?reflection*cameraEnvironment.w:sky;
        const float fresnel=.02+.98*pow(1-nv,5);float3 waterLight=base*(ambientCount.rgb+.12);
        for(uint j=0;j<uint(ambientCount.w);j++){
            const Light light=lights[j];float3 l=safeNormal(-light.directionRange.xyz,n);float attenuation=1;
            if(light.positionType.w>.5){const float3 delta=light.positionType.xyz-input.world;const float distance=max(length(delta),.001);l=delta/distance;attenuation=pow(saturate(1-distance/max(light.directionRange.w,.001)),2);if(light.positionType.w>1.5)attenuation*=smoothstep(light.cone.y,light.cone.x,dot(light.directionRange.xyz,-l));}
            const float3 h=safeNormal(v+l,n);const float highlight=pow(max(dot(n,h),0),lerp(256.,8.,rough))*fresnel*waterHorizon.w;
            waterLight+=(base*max(dot(n,l),0)*.25+highlight)*light.colorIntensity.rgb*light.colorIntensity.w*attenuation*shadowVisibility(j,input,n,l);
        }
        radiance=waterLight*(1-fresnel*waterHorizon.w)+reflectedColor*fresnel*waterHorizon.w;
    }
    const float3 combined=radiance+emission;
    float3 display=outputOptions.x>.5?combined:combined/(1+combined);
    display=surfaceFog(display,input);
    if(options.x>1.5){
        if(reflectionOptions.x>.5&&input.planarSurface>.5&&foamColor.w>0){
            const float4 clip=mul(reflectionViewProjection,float4(input.world,1));const float2 uv=clip.xy/max(clip.w,.0001)*float2(.5,-.5)+.5;
            const float2 distorted=uv+n.xz*.008*(1-rough);const bool inside=clip.w>0&&all(distorted>0)&&all(distorted<1)&&clip.z>=0&&clip.z<=clip.w;
            const float2 pixel=clamp(waterViewport.xy+distorted*waterViewport.zw,waterViewport.xy+.5,waterViewport.xy+waterViewport.zw-.5)/waterExtent.xy;
            const float4 reflected=planarReflection.SampleLevel(clampClamp,pixel,0);const float weight=inside?foamColor.w*waterHorizon.w*(.02+.98*pow(1-nv,5))*reflected.a:0;
            display=lerp(display,linearDisplay(reflected.rgb),weight);
        }
        display=waterComposite(input,n,base,display,waterClock.x*wave.z,nv)+fogApply(frameFog,min(abs(interaction.w)*.18,.25).xxx,input.world,true);
    }
    return float4(outputOptions.x>.5?display:srgb(display),options.y<2?1:alpha);
}
struct GuiInput {float2 position:POSITION;float2 uv:TEXCOORD;float4 color:COLOR;};
struct GuiOutput {float4 position:SV_Position;float2 uv:TEXCOORD;float4 color:COLOR;};
GuiOutput guiVS(GuiInput input) {GuiOutput output;output.position=float4(input.position.x/screen.x*2-1,1-input.position.y/screen.y*2,0,1);output.uv=input.uv;output.color=input.color;return output;}
float4 guiPS(GuiOutput input):SV_Target {const float4 color=baseImage.Sample(clampClamp,input.uv)*input.color;return float4(color.rgb*color.a,color.a);}
#include "desktop_environment.hlsli"
#include "desktop_water_passes.hlsli"
