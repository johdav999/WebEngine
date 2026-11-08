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
    float4 lightingZones[4],lightingZoneRotations;
    float4 reflectionProbe[3];
    CloudData frameCloud;
};
cbuffer Object : register(b1) {
    column_major float4x4 model, normalMatrix;
    float4 tint, surface, emissionCutoff, uvTransform;
    float4 options; // unlit, alpha mode, double sided, receives shadows
    float4 maps; // normal presence, nine packed two-bit wrap modes, fog mode, layered
    float4 layerColor[4],layerUv[4],weightUv,layerOptions,layerDetail[4];
    float4 wave,waterDetail,waterHorizon,waterZenith,transmission,absorptionFoam,foamColor;
    float4 advanced[9];
};
cbuffer Screen : register(b2) { float4 screen; };
cbuffer ShadowPass : register(b3) { column_major float4x4 shadowMatrix; };
Texture2D baseImage : register(t0);
Texture2D surfaceImage : register(t1);
Texture2D normalImage : register(t2);
Texture2D occlusionImage : register(t3);
Texture2D emissiveImage : register(t4);
Texture2DArray<float> shadowImages : register(t5);
TextureCubeArray specularEnvironment : register(t6);
TextureCubeArray diffuseEnvironment : register(t7);
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
#include "desktop_fog_volume.hlsli"
#include "desktop_fog_grid.hlsli"
#include "desktop_diffuse_gi.hlsli"
#include "desktop_fx.hlsli"
#include "desktop_layers.hlsli"
#include "desktop_water_surface.hlsli"
MeshOutput surfaceVertexFor(float3 position,float3 normal,float3 tangent,float3 bitangent,float2 uv,column_major float4x4 objectModel,column_major float4x4 objectNormal,float4 objectTint,float4 objectFlags) {
    if(options.x>1.5)waterVertex(position,normal,tangent,bitangent);
    if(advanced[5].w>0) {
        const float3 direction=normalize(advanced[6].xyz),q=float3(.73,.19,.51)*advanced[6].w;
        const float phase=dot(position,q)+waterClock.x*advanced[7].x;
        const float3 gradient=q*(advanced[5].w*cos(phase));
        position+=direction*(advanced[5].w*sin(phase));
        tangent+=direction*dot(gradient,tangent);bitangent+=direction*dot(gradient,bitangent);
        const float3 warped=cross(tangent,bitangent);normal=safeNormal(warped*(dot(warped,normal)<0?-1:1),normal);
    }
    MeshOutput output;output.instanceColor=objectTint;output.instanceFlags=objectFlags;const float4 world=mul(objectModel,float4(position,1));
    output.position=mul(viewProjection,world);output.world=world.xyz;
    const float3 n=safeNormal(mul(objectNormal,float4(normal,0)).xyz,float3(0,1,0));
    const float3 wt=mul(objectModel,float4(tangent,0)).xyz;
    const float3 wb=mul(objectModel,float4(bitangent,0)).xyz;
    const float3 t=safeNormal(wt-n*dot(n,wt),float3(1,0,0));
    output.normal=n;output.tangent=float4(t,dot(cross(n,t),wb)<0?-1:1);
    output.uv=(uv+advanced[5].yz*waterClock.x)*uvTransform.xy+uvTransform.zw;output.planeNormal=safeNormal(mul(objectNormal,float4(0,1,0,0)).xyz,float3(0,1,0));
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
float surfaceLightVisibility(uint index,MeshOutput input,float3 n,float3 l,Light light,float attenuation) {
    float visibility=shadowVisibility(index,input,n,l);
    if(maps.z>.5&&options.w>.5&&light.positionType.w<.5&&attenuation>0&&light.colorIntensity.w>0)
        visibility*=cloudSurfaceShadow(input.world,l);
    if(visibility<=0||attenuation<=0||light.colorIntensity.w<=0||maps.z<.5||
       frameFog.volumeMinSteps.w<=0||(uint(frameFog.volumeOptions.w)&1u)==0u)return visibility;

    const float distance=light.positionType.w>.5?length(light.positionType.xyz-input.world):1e20;
    if(distance<=1e-6)return visibility;
    return visibility*fogSpotTransmission(index,frameFog,input.world,safeNormal(l,n),distance);
}
float3 environmentDirection(float3 d,float rotation) {const float c=cos(rotation),s=sin(rotation);return float3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);}
float lightingZoneWeight(float3 world,uint index) {
    const float4 lower=lightingZones[2*index];
    if(lower.w==0)return 0;
    const float3 edge=min(world-lower.xyz,lightingZones[2*index+1].xyz-world);
    return smoothstep(0,lower.w,min(edge.x,min(edge.y,edge.z)));
}
static float4 surfaceAdvanced[9];
void loadAdvanced(float2 uv) {
    [unroll]for(uint i=0;i<9;++i)surfaceAdvanced[i]=advanced[i];
    if(advanced[8].x>.5) {
        const float4 packed=sampleMap(layerDetail0,5,uv);
        surfaceAdvanced[0].x*=packed.r;surfaceAdvanced[0].y*=packed.g;surfaceAdvanced[0].z*=packed.b;surfaceAdvanced[1].x*=packed.a;
    }
    if(advanced[8].z>.5) {
        uint width,height;layerDetail2.GetDimensions(width,height);const float2 inset=.5/float2(width,height);const uint wraps=uint(advanced[7].z);
        const float2 q=float2((wraps&1)?saturate(uv.x):frac(uv.x),(wraps&2)?saturate(uv.y):frac(uv.y));const float x=clamp(q.x,inset.x,1-inset.x);
        const float4 sheen=layerDetail2.SampleLevel(clampClamp,float2(x,clamp(q.y*.5,inset.y,.5-inset.y)),0);
        const float4 subsurface=layerDetail2.SampleLevel(clampClamp,float2(x,clamp(.5+q.y*.5,.5+inset.y,1-inset.y)),0);
        surfaceAdvanced[4].xyz*=sheen.rgb;surfaceAdvanced[2].w*=sheen.a;surfaceAdvanced[3].xyz*=subsurface.rgb;surfaceAdvanced[1].z*=subsurface.a;
    }
    if(advanced[8].w>.5) {
        const float4 packed=sampleMap(layerDetail3,8,uv);const float2 direction=packed.rg*2-1;
        surfaceAdvanced[3].w*=packed.b;surfaceAdvanced[5].x*=packed.a;
        if(dot(direction,direction)>1e-6)surfaceAdvanced[4].w+=atan2(direction.y,direction.x);
    }
}
float3 coatNormal(float2 uv,float3 n,float3 t,float3 b) {
    if(advanced[8].y<.5)return n;
    const float3 sampled=sampleMap(layerDetail1,6,uv).xyz*2-1;
    return safeNormal(t*sampled.x*advanced[7].y*(uvTransform.x<0?-1:1)+b*sampled.y*advanced[7].y*(uvTransform.y<0?-1:1)+n*sampled.z,n);
}
float3 directBrdf(float3 n,float3 v,float3 l,float3 coatN,float3 tangent,float3 base,float rough,float metal,float3 f0) {
    const float3 h=safeNormal(v+l,n);const float nv=max(dot(n,v),.001),nl=max(dot(n,l),0),nh=max(dot(n,h),0),vh=max(dot(v,h),0);
    const float a=rough*rough,a2=a*a,denom=nh*nh*(a2-1)+1;float d=a2/(3.14159265*denom*denom);
    if(abs(surfaceAdvanced[3].w)>.0001) {
        const float3 axis=safeNormal(tangent-n*dot(tangent,n),float3(1,0,0)),bitangent=cross(n,axis);
        const float angle=surfaceAdvanced[4].w;const float3 t=axis*cos(angle)+bitangent*sin(angle),b=cross(n,t);
        float aspect=sqrt(max(.1,1-.9*abs(surfaceAdvanced[3].w)));if(surfaceAdvanced[3].w<0)aspect=1/aspect;
        const float ax=max(.002,a/aspect),ay=max(.002,a*aspect),ellipse=pow(dot(h,t)/ax,2)+pow(dot(h,b)/ay,2)+nh*nh;
        d=1/max(3.14159265*ax*ay*ellipse*ellipse,1e-7);
    }
    const float k=(rough+1)*(rough+1)/8,g=(nv/(nv*(1-k)+k))*(nl/max(nl*(1-k)+k,.001));
    const float3 f=f0+(1-f0)*pow(saturate(1-vh),5),specular=d*g*f/max(4*nv*nl,.001);
    const float coatNh=max(dot(coatN,h),0),coatNl=max(dot(coatN,l),0),coatNv=max(dot(coatN,v),.001);
    const float coat=surfaceAdvanced[0].x,coatRough=max(.045,surfaceAdvanced[0].y),ca=pow(coatRough,4),cd=coatNh*coatNh*(ca-1)+1,cf=.04+.96*pow(1-vh,5);
    const float ck=(coatRough+1)*(coatRough+1)/8,cg=(coatNv/(coatNv*(1-ck)+ck))*(coatNl/max(coatNl*(1-ck)+ck,.001));
    const float coatLobe=coat*ca/max(3.14159265*cd*cd,1e-7)*cg*cf/max(4*coatNv,.001);
    const float sss=surfaceAdvanced[1].z,wrapped=saturate((dot(n,l)+.5)/1.5)+max(-dot(n,l),0)*.5;
    const float3 diffuse=(1-f)*(1-metal)*base/3.14159265;
    const float3 sheen=surfaceAdvanced[4].xyz*surfaceAdvanced[1].w*pow(1-vh,lerp(1,5,surfaceAdvanced[2].w))*nl;
    return ((diffuse*(1-surfaceAdvanced[0].z)*(1-sss)+specular)*nl+diffuse*surfaceAdvanced[3].xyz*sss*wrapped+sheen)*(1-coat*cf)+coatLobe*max(dot(coatN,l),0);
}
float4 projectedReflectionProbe(float3 world,float3 ray){
    const float4 lower=reflectionProbe[0],upper=reflectionProbe[1];
    if(lower.w<=0)return 0;
    const float3 distanceToFace=min(world-lower.xyz,upper.xyz-world);
    const float edge=min(distanceToFace.x,min(distanceToFace.y,distanceToFace.z));
    const float weight=smoothstep(0,lower.w,edge);
    if(weight<=0)return 0;
    const float3 target=float3(ray.x>=0?upper.x:lower.x,ray.y>=0?upper.y:lower.y,ray.z>=0?upper.z:lower.z);
    const float3 divisor=float3(abs(ray.x)>1e-6?ray.x:1,abs(ray.y)>1e-6?ray.y:1,abs(ray.z)>1e-6?ray.z:1);
    const float3 intersection=(target-world)/divisor;
    const float3 exit=float3(abs(ray.x)>1e-6?intersection.x:1e20,abs(ray.y)>1e-6?intersection.y:1e20,abs(ray.z)>1e-6?intersection.z:1e20);
    const float distance=min(exit.x,min(exit.y,exit.z));
    return float4(world+ray*distance-reflectionProbe[2].xyz,weight);
}
#include "desktop_screen_reflections.hlsli"
float3 glassComposite(MeshOutput input,float3 normal,float3 display,float3 v,float nv) {
    const float ior=surfaceAdvanced[0].w,distance=surfaceAdvanced[1].x;
    const float3 direction=refract(-v,normal,1/ior);
    float3 transmitted=specularEnvironment.SampleLevel(environmentSampler,float4(environmentDirection(direction,environmentOptions.x),0),surface.x*environmentOptions.y).rgb*cameraEnvironment.w;
    if(outputOptions.x<.5)transmitted=transmitted/(1+transmitted);
    if(input.instanceFlags.w>.5&&waterExtent.z>.5&&reflectionOptions.y<.5) {
        const float2 lo=waterViewport.xy+.5,hi=waterViewport.xy+waterViewport.zw-.5,base=clamp(input.position.xy,lo,hi);
        const float2 bend=mul(viewProjection,float4(normal,0)).xy*float2(1,-1)*(1-1/ior)*min(max(distance,.05),2)*.03;
        float2 pixel=clamp(base+bend*waterViewport.zw,lo,hi);
        const float depth=sceneWaterDepth.Load(int3(int2(pixel),0));if(depth<=input.position.z+.000001)pixel=base;
        transmitted=linearDisplay(sceneWaterColor.SampleLevel(clampClamp,pixel/waterExtent.xy,0).rgb);
    }
    float3 attenuation=1;if(surfaceAdvanced[1].y>0)attenuation=pow(max(surfaceAdvanced[2].xyz,.0001),distance/surfaceAdvanced[1].y);
    const float f0=pow((ior-1)/(ior+1),2)*(surfaceAdvanced[5].x*2),f=f0+(1-f0)*pow(1-saturate(nv),5);
    return lerp(display,transmitted*attenuation,surfaceAdvanced[0].z*(1-f));
}
float4 meshPS(MeshOutput input,bool front:SV_IsFrontFace):SV_Target {
    loadAdvanced(input.uv);
    const bool layered=maps.w>.5;LayerSurface layer=(LayerSurface)0;if(layered)layer=layerSurface(input);
    const float4 texel=layered?layer.color:sampleMap(baseImage,0,input.uv);
    const float4 packedSurface=layered?float4(1,layer.roughness,1,1):sampleMap(surfaceImage,1,input.uv);
    const float3 normalSample=layered?float3(0,0,1):sampleMap(normalImage,2,input.uv).xyz*2-1;
    const float aoSample=layered?1:sampleMap(occlusionImage,3,input.uv).r;
    const float3 emission=emissionCutoff.xyz*(layered?float3(1,1,1):sampleMap(emissiveImage,4,input.uv).rgb);
    const float alpha=texel.a*input.instanceColor.a;if(reflectionOptions.y>.5&&dot(reflectionPlane,float4(input.world,1))<.002)discard;
    if(options.y==1&&alpha<emissionCutoff.w)discard;
    const float3 base=max(input.instanceColor.rgb*texel.rgb,0);
    if(options.x>.5&&options.x<1.5){float3 color=base+emission;if(maps.z>.5)color=cloudApply(color,input.world,maps.z>1.5);float3 display=surfaceFog(color,input);return float4(outputOptions.x>.5?display:srgb(display),options.y<2?1:alpha);}
    const bool water=options.x>1.5;
    float3 n=safeNormal(input.normal,float3(0,1,0));const float3 t=safeNormal(input.tangent.xyz-n*dot(n,input.tangent.xyz),float3(1,0,0)),b=cross(n,t)*input.tangent.w;
    if(layered)n=layer.normal;
    if(!layered&&(uint(maps.x)&1)!=0) {
        const float3 mapped=safeNormal(float3(normalSample.xy*surface.z,normalSample.z),float3(0,0,1));
        n=safeNormal(t*mapped.x*(uvTransform.x<0?-1:1)+b*mapped.y*(uvTransform.y<0?-1:1)+n*mapped.z,n);
    }
    float4 interaction=0;
    if(water){
        const float2 waterUv=input.uv*waterDetail.z;const float time=waterClock.x*wave.z;
        const float3 rippleA=sampleMap(normalImage,2,waterUv+float2(.021,.013)*time).xyz*2-1;
        const float3 rippleB=sampleMap(normalImage,2,waterUv*1.37+float2(-.017,.019)*time).xyz*2-1;
        const float2 r=(rippleA.xy+rippleB.xy)*surface.z+.12*float2(sin(waterUv.x*6.2831853+time),cos(waterUv.y*8.1-time*1.3));
        n=safeNormal(input.normal+(t*r.x+b*r.y)*waterDetail.w,input.normal);
        if(maps.z>.5&&interactionOptions.x>0)interaction=interactionSurface(input.world,input.planeNormal);
        n=safeNormal(n-interaction.xyz*.12,n);
    }
    if(options.z>.5&&!front)n=-n;
    const float3 geometric=safeNormal(input.normal,float3(0,1,0))*(options.z>.5&&!front?-1:1);
    const float3 coatN=coatNormal(input.uv,geometric,t,b);
    const float rough=clamp(surface.x*packedSurface.g,.045,1),metal=saturate(surface.y*packedSurface.b),ao=lerp(1,aoSample,surface.w);
    const float dielectric=pow((surfaceAdvanced[0].w-1)/(surfaceAdvanced[0].w+1),2)*(surfaceAdvanced[5].x*2);
    const float3 v=safeNormal(cameraEnvironment.xyz-input.world,n),f0=lerp(dielectric.xxx,base,metal);const float nv=max(dot(n,v),.001);
    float3 radiance=0;
    if(!water) {
        for(uint i=0;i<uint(ambientCount.w);i++) {
            const Light light=lights[i];float3 l=safeNormal(-light.directionRange.xyz,n);float attenuation=1;
            if(light.positionType.w>.5) {
                const float3 delta=light.positionType.xyz-input.world;const float distance=max(length(delta),.001);l=delta/distance;
                attenuation=pow(saturate(1-distance/max(light.directionRange.w,.001)),2);
                if(light.positionType.w>1.5)attenuation*=smoothstep(light.cone.y,light.cone.x,dot(light.directionRange.xyz,-l));
            }
            radiance+=directBrdf(n,v,l,coatN,t,base,rough,metal,f0)*light.colorIntensity.rgb*light.colorIntensity.w*attenuation*surfaceLightVisibility(i,input,n,l,light,attenuation);
        }
    }
    const float3 reflectionDirection=reflect(-v,n);
    const float environmentLod=rough*environmentOptions.y;
    const float3 irradiance=diffuseEnvironment.SampleLevel(environmentSampler,float4(environmentDirection(n,environmentOptions.x),0),0).rgb;
    const float3 reflection=specularEnvironment.SampleLevel(environmentSampler,float4(environmentDirection(reflectionDirection,environmentOptions.x),0),environmentLod).rgb;
    const float2 integrated=brdfLut.SampleLevel(environmentSampler,float2(nv,rough),0).rg;
    const float3 f=f0+(max(1-rough,f0)-f0)*pow(1-nv,5),diffuse=(1-f)*(1-metal)*base*(1-surfaceAdvanced[0].z);
    float3 ssrFallbackSpecular=reflection*cameraEnvironment.w;
    float3 giFallbackDiffuse=irradiance*cameraEnvironment.w;
    float3 indirect=ao*(diffuse*(giFallbackDiffuse+ambientCount.rgb)+reflection*(f0*integrated.x+integrated.y)*cameraEnvironment.w);
    if(!water&&(lightingZones[0].w>0||lightingZones[2].w>0)) {
        const float2 weights=float2(lightingZoneWeight(input.world,0),lightingZoneWeight(input.world,1));
        const float total=weights.x+weights.y;
        float3 localDiffuse=irradiance*(cameraEnvironment.w*max(0,1-total));
        float3 localSpecular=reflection*(cameraEnvironment.w*max(0,1-total));
        for(uint i=0;i<2;++i) {
            if(weights[i]>0) {
                const float weight=weights[i]*lightingZones[2*i+1].w/max(1,total);
                const float rotation=lightingZoneRotations[i];
                localDiffuse+=diffuseEnvironment.SampleLevel(environmentSampler,float4(environmentDirection(n,rotation),i+1),0).rgb*weight;
                localSpecular+=specularEnvironment.SampleLevel(environmentSampler,float4(environmentDirection(reflectionDirection,rotation),i+1),environmentLod).rgb*weight;
            }
        }
        ssrFallbackSpecular=localSpecular;
        giFallbackDiffuse=localDiffuse;
        indirect=ao*(diffuse*(localDiffuse+ambientCount.rgb)+localSpecular*(f0*integrated.x+integrated.y));
    }
    if(!water && maps.z>.5) {
        const float4 projected=projectedReflectionProbe(input.world,reflectionDirection);
        if(projected.w>0){
            const float3 cache=specularEnvironment.SampleLevel(environmentSampler,float4(projected.xyz,reflectionProbe[2].w),environmentLod).rgb*reflectionProbe[1].w;
            const float3 replacement=lerp(ssrFallbackSpecular,cache,projected.w);
            indirect+=ao*(replacement-ssrFallbackSpecular)*(f0*integrated.x+integrated.y);
            ssrFallbackSpecular=replacement;
        }
    }
    if(!water && maps.z>.5 && metal<1) {
        const float4 gi=sampleDiffuseGi(input.world,n);
        if(gi.a>0)indirect+=ao*diffuse*(gi.rgb-giFallbackDiffuse)*gi.a;
    }
    if(!water) {
        const float coatF=.04+.96*pow(1-nv,5),coat=surfaceAdvanced[0].x;
        const float3 coatReflection=specularEnvironment.SampleLevel(environmentSampler,float4(environmentDirection(reflect(-v,coatN),environmentOptions.x),0),surfaceAdvanced[0].y*environmentOptions.y).rgb*cameraEnvironment.w;
        indirect=indirect*(1-coat*coatF)+coatReflection*(coat*coatF);
        indirect+=ao*(giFallbackDiffuse+ambientCount.rgb)*base*surfaceAdvanced[3].xyz*surfaceAdvanced[1].z*.25;
        indirect+=ssrFallbackSpecular*surfaceAdvanced[4].xyz*surfaceAdvanced[1].w*pow(1-nv,lerp(1,5,surfaceAdvanced[2].w));
    }
    radiance+=indirect;
    if(outputOptions.y>.5){
        const float4 hit=traceScreenReflection(input,n,reflectionDirection,rough);
        radiance+=ao*(hit.rgb-ssrFallbackSpecular)*hit.a*(f0*integrated.x+integrated.y);
    }
    if(water){
        const float3 reflected=reflect(-v,n),sky=lerp(waterHorizon.rgb,waterZenith.rgb,saturate(reflected.y));
        const float3 reflectedColor=cameraEnvironment.w>0?reflection*cameraEnvironment.w:sky;
        const float fresnel=.02+.98*pow(1-nv,5);float3 waterLight=base*(ambientCount.rgb+.12);
        for(uint j=0;j<uint(ambientCount.w);j++){
            const Light light=lights[j];float3 l=safeNormal(-light.directionRange.xyz,n);float attenuation=1;
            if(light.positionType.w>.5){const float3 delta=light.positionType.xyz-input.world;const float distance=max(length(delta),.001);l=delta/distance;attenuation=pow(saturate(1-distance/max(light.directionRange.w,.001)),2);if(light.positionType.w>1.5)attenuation*=smoothstep(light.cone.y,light.cone.x,dot(light.directionRange.xyz,-l));}
            const float3 h=safeNormal(v+l,n);const float highlight=pow(max(dot(n,h),0),lerp(256.,8.,rough))*fresnel*waterHorizon.w;
            waterLight+=(base*max(dot(n,l),0)*.25+highlight)*light.colorIntensity.rgb*light.colorIntensity.w*attenuation*surfaceLightVisibility(j,input,n,l,light,attenuation);
        }
        radiance=waterLight*(1-fresnel*waterHorizon.w)+reflectedColor*fresnel*waterHorizon.w;
    }
    const float3 combined=maps.z>.5?cloudApply(radiance+emission,input.world,maps.z>1.5):radiance+emission;
    float3 display=outputOptions.x>.5?combined:combined/(1+combined);
    display=surfaceFog(display,input);
    if(water){
        if(reflectionOptions.x>.5&&input.planarSurface>.5&&foamColor.w>0){
            const float4 clip=mul(reflectionViewProjection,float4(input.world,1));const float2 uv=clip.xy/max(clip.w,.0001)*float2(.5,-.5)+.5;
            const float2 distorted=uv+n.xz*.008*(1-rough);const bool inside=clip.w>0&&all(distorted>0)&&all(distorted<1)&&clip.z>=0&&clip.z<=clip.w;
            const float2 pixel=clamp(waterViewport.xy+distorted*waterViewport.zw,waterViewport.xy+.5,waterViewport.xy+waterViewport.zw-.5)/waterExtent.xy;
            const float4 reflected=planarReflection.SampleLevel(clampClamp,pixel,0);const float weight=inside?foamColor.w*waterHorizon.w*(.02+.98*pow(1-nv,5))*reflected.a:0;
            display=lerp(display,linearDisplay(reflected.rgb),weight);
        }
        display=waterComposite(input,n,base,display,waterClock.x*wave.z,nv)+fogVolumeApply(cloudApply(min(abs(interaction.w)*.18,.25).xxx,input.world,true),input.world,true);
    }
    if(!water&&surfaceAdvanced[0].z>0)display=glassComposite(input,n,display,v,nv);
    return float4(outputOptions.x>.5?display:srgb(display),options.y<2?1:alpha);
}
struct GuiInput {float2 position:POSITION;float2 uv:TEXCOORD;float4 color:COLOR;};
struct GuiOutput {float4 position:SV_Position;float2 uv:TEXCOORD;float4 color:COLOR;};
GuiOutput guiVS(GuiInput input) {GuiOutput output;output.position=float4(input.position.x/screen.x*2-1,1-input.position.y/screen.y*2,0,1);output.uv=input.uv;output.color=input.color;return output;}
float4 guiPS(GuiOutput input):SV_Target {const float4 color=baseImage.Sample(clampClamp,input.uv)*input.color;return float4(color.rgb*color.a,color.a);}
#include "desktop_environment.hlsli"
#include "desktop_water_passes.hlsli"
