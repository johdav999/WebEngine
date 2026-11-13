#include "engine/performance_timeline.hpp"
#include "webgl_renderer.hpp"
#include "screen_reflections.hpp"
#include "engine/cluster_stream.hpp"
#include "fog_grid.hpp"
#include "fog_render.hpp"
#include "cloud_render.hpp"
#include "diffuse_gi_gpu.hpp"
#include "browser_canvas.hpp"
#include "shaders.hpp"
#include <emscripten.h>
#include <cstddef>
#include <sstream>

EM_JS(void, gl_status, (const char* text,int error), { window.engineStatus(UTF8ToString(text),!!error); });
EM_JS(void, gl_report, (const char* json), { window.EngineQuality?.report(JSON.parse(UTF8ToString(json))); });
EM_JS(void, gl_performance, (const char* json), {
    const snapshot=JSON.parse(UTF8ToString(json));snapshot.build=Module.__webengineBuild;
    window.enginePerformance(snapshot);
});
EM_JS(void, gl_stats, (int meshes,int lights,int layers), { window.engineStats(meshes,lights,layers); });
EM_JS(int, gl_msaa4x_supported, (), {
    const gl=document.querySelector('#viewport').getContext('webgl2');
    // WebGL exposes the sample array, but not GLES's GL_NUM_SAMPLE_COUNTS query.
    // The opaque (alpha:false) default framebuffer is RGB8; resolve formats must match.
    return [gl.RGB8,gl.DEPTH_COMPONENT24].every(format =>
        Array.from(gl.getInternalformatParameter(gl.RENDERBUFFER,format,gl.SAMPLES) || []).includes(4));
});
EM_JS(int, gl_hdr, (), {
    return !!document.querySelector('#viewport').getContext('webgl2').getExtension('EXT_color_buffer_float');
});
EM_JS(int, gl_hdr_msaa, (), {
    const gl=document.querySelector('#viewport').getContext('webgl2');
    return Array.from(gl.getInternalformatParameter(gl.RENDERBUFFER,gl.RGBA16F,gl.SAMPLES)||[]).includes(4);
});
EM_JS(void, gl_lose, (), {
    const gl=Module.canvas.getContext('webgl2');const ext=gl?.getExtension('WEBGL_lose_context');
    if(ext) { ext.loseContext();setTimeout(()=>ext.restoreContext(),100); }
});

namespace engine {
namespace {
constexpr const char* meshVertex=R"(#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec4 ribbonColor;
layout(location=4) in vec4 tangent;
layout(location=5) in vec4 instanceModel0;
layout(location=6) in vec4 instanceModel1;
layout(location=7) in vec4 instanceModel2;
layout(location=8) in vec4 instanceModel3;
layout(location=9) in vec4 instanceNormal0;
layout(location=10) in vec4 instanceNormal1;
layout(location=11) in vec4 instanceNormal2;
layout(location=12) in vec4 instanceTint;
layout(location=13) in vec4 instanceFlags;
layout(location=14) in vec4 instancePlant;
layout(location=15) in vec4 bend;
uniform bool instanceMode,gpuWind;
uniform vec4 windDirection,windClock;
flat out float instanceShadow;
out vec4 vertexColor;
uniform mat4 model,normalMatrix,vp;
uniform bool water;
uniform vec4 reflectionPlane;
flat out float planarSurface;
uniform vec4 wave,detail;
uniform float waterTime;
uniform vec4 advanced[9];
out vec3 worldTangent,worldBitangent;
out vec3 planeNormal;
out vec3 worldPosition,worldNormal;
out vec2 texcoord;
void main() {
    mat4 objectModel=instanceMode?mat4(instanceModel0,instanceModel1,instanceModel2,instanceModel3):model;
    mat4 objectNormal=instanceMode?mat4(instanceNormal0,instanceNormal1,instanceNormal2,vec4(0,0,0,1)):normalMatrix;
    vertexColor=instanceMode?instanceTint:ribbonColor;instanceShadow=instanceMode?instanceFlags.x:1.0;
    planarSurface=abs(dot(reflectionPlane,objectModel[3]))<0.01 && abs(dot(normalize((objectNormal*vec4(0,1,0,0)).xyz),reflectionPlane.xyz))>0.999 ? 1.0 : 0.0;
    vec3 p=position,n=normal,t=tangent.xyz,b=cross(n,tangent.xyz)*tangent.w;
    if(water) {
        vec3 dx=vec3(1,0,0),dz=vec3(0,0,1);
        for(int i=0;i<3;i++) {
            float angle=float(i)*1.17,c=cos(angle),s=sin(angle);
            vec2 d=vec2(c*detail.x-s*detail.y,s*detail.x+c*detail.y);
            float weight=i==0 ? 0.55 : i==1 ? 0.3 : 0.15;
            float scale=i==0 ? 1.0 : i==1 ? 0.57 : 0.31;
            float k=6.2831853/(wave.y*scale),a=wave.x*weight;
            float q=min(wave.w,0.8/max(3.0*k*a,0.0001));
            float phase=k*dot(d,position.xz)-sqrt(9.81*k)*waterTime*wave.z;
            float sn=sin(phase),cs=cos(phase);
            p+=vec3(q*a*d.x*cs,a*sn,q*a*d.y*cs);
            dx+=vec3(-q*a*k*d.x*d.x*sn,a*k*d.x*cs,-q*a*k*d.x*d.y*sn);
            dz+=vec3(-q*a*k*d.x*d.y*sn,a*k*d.y*cs,-q*a*k*d.y*d.y*sn);
        }
        n=normalize(cross(dz,dx));t=dx;b=-dz;
    }
    if(advanced[5].w>0.0) {
        vec3 direction=normalize(advanced[6].xyz),q=vec3(.73,.19,.51)*advanced[6].w;
        float phase=dot(p,q)+waterTime*advanced[7].x;
        vec3 gradient=q*(advanced[5].w*cos(phase));
        p+=direction*(advanced[5].w*sin(phase));
        t+=direction*dot(gradient,t);b+=direction*dot(gradient,b);
        vec3 warped=cross(t,b);n=normalize(warped*(dot(warped,n)<0.0?-1.0:1.0));
    }
    vec4 world=objectModel*vec4(p,1.0);worldPosition=world.xyz;
    worldNormal=(objectNormal*vec4(n,0.0)).xyz;texcoord=uv+advanced[5].yz*waterTime;
    worldTangent=(objectModel*vec4(t,0.0)).xyz;worldBitangent=(objectModel*vec4(b,0.0)).xyz;planeNormal=normalize((objectNormal*vec4(0,1,0,0)).xyz);
    if(gpuWind) {
        float phase=instancePlant.x;
        float amount=windDirection.w*sin(windClock.x+phase)*(1.0-windClock.z+windClock.z*.5*(1.0+sin(windClock.y+phase*1.37)));
        vec3 offset=windDirection.xyz*amount,gradient=(objectNormal*vec4(bend.yzw*(2.0*bend.x),0)).xyz;
        float denominator=1.0+dot(gradient,offset),safeDenominator=abs(denominator)>=.05?denominator:denominator>=0.0?.05:-.05;
        worldNormal=normalize(worldNormal-gradient*(dot(offset,worldNormal)/safeDenominator));
        worldTangent+=offset*dot(gradient,worldTangent);worldBitangent+=offset*dot(gradient,worldBitangent);
        world.xyz+=offset*(bend.x*bend.x);worldPosition=world.xyz;
    }
    gl_Position=vp*world;
    gl_Position.z=2.0*gl_Position.z-gl_Position.w;
})";
constexpr const char* meshFragment=R"(#version 300 es
precision highp float;
// FOG_FUNCTIONS
uniform bool fogWorld,fogAdditive,hdrOutput;
in vec3 worldPosition,worldNormal;
in vec2 texcoord;
uniform sampler2D baseTexture;
uniform highp sampler2D waterNormalTexture;
uniform sampler2D layerOne,layerTwo,layerThree,layerWeights;
uniform bool layered;
uniform vec4 layerColor[4],layerUv[4],weightUv,layerOptions,layerDetail[4];
uniform sampler2D layerDetail0,layerDetail1,layerDetail2,layerDetail3;
uniform sampler2D planarReflection;
uniform mat4 reflectionVp;
uniform vec4 reflectionPlane;
uniform bool reflectionPass,planarActive,worldWater;
flat in float planarSurface;
uniform int interactionCount;
uniform vec4 interactionData[64];
uniform bool water;
uniform vec4 wave,detail,reflectionHorizon,reflectionZenith;
uniform float waterTime,roughness,normalScale;
uniform vec4 advanced[9];
uniform vec3 cameraPosition;
in vec3 worldTangent,worldBitangent,planeNormal;
uniform sampler2D sceneColor,sceneDepth;
uniform bool sceneWater;
uniform float particleSoftDistance;
uniform bool ribbon;
uniform bool instanceMode;
flat in float instanceShadow;
in vec4 vertexColor;
uniform mat4 inverseVp,vp;
uniform vec4 waterViewport,transmission,absorptionFoam,foamColor;
uniform vec2 waterExtent;
uniform vec3 viewForward;
uniform vec4 tint,uvTransform;
uniform vec3 emissive;
uniform bool unlit,masked,toneMapped,shadowPass,receivesShadow;
uniform highp sampler2DArray environmentAtlas;
#ifdef WEBENGINE_COMPACT_PBR
const bool compactPbr=true;
#else
const bool compactPbr=false;
#endif
uniform bool compactBaseSurface;
#ifdef WEBENGINE_SCREEN_REFLECTIONS
uniform bool screenReflectionPass;
#endif
uniform vec4 environmentRects[9],environmentOptions;
uniform vec4 lightingZones[4],lightingZoneRotations;
uniform vec4 reflectionProbe[3];
uniform float metallic;
// FOG_VOLUME
// FOG_GRID_SAMPLING
// CLOUD_GRID_SAMPLING
// DIFFUSE_GI
float sampleShadow(int j,vec3 l) {
    vec3 geometric=normalize(worldNormal);
    vec4 clip=shadowMatrix[j]*vec4(worldPosition+geometric*shadowOptions[j].z*(1.0-clamp(dot(geometric,l),0.0,1.0)),1);
    if(clip.w<=0.0)return 1.0;
    vec3 p=clip.xyz/clip.w;p.xy=p.xy*.5+.5;
    if(any(lessThan(p,vec3(0)))||any(greaterThan(p,vec3(1))))return 1.0;
    float result=0.0;
    if(shadowTaps==1)return texture(shadowMaps,vec4(p.xy,float(j),p.z-shadowOptions[j].y));
    for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++) result+=texture(shadowMaps,vec4(p.xy+vec2(x,y)*shadowOptions[j].w,float(j),p.z-shadowOptions[j].y));
    return result/9.0;
}
float visibility(int light,vec3 l) {
    if(!receivesShadow||instanceShadow<.5)return 1.0;
    float depth=dot(worldPosition-cameraPosition,viewForward);
    for(int j=0;j<4;j++) {
        if(j>=shadowCount)break;
        vec4 s=shadowSelection[j];if(int(s.x)!=light || (s.w>.5&&depth>s.z))continue;
        float result=sampleShadow(j,l);
        if(s.w>.5&&j+1<shadowCount&&shadowSelection[j+1].x==s.x) result=mix(result,sampleShadow(j+1,l),smoothstep(mix(s.y,s.z,.9),s.z,depth));
        return result;
    }
    return 1.0;
}
uniform float cutoff;
out vec4 outputColor;
vec4 surfaceFogMedium() {
    if(fog.colorDensity.w<=0.0 && fog.volumeMinSteps.w<=0.0) return vec4(0,0,0,1);
    bool reflected=fogWorld && reflectionPass;
    vec3 eye=fogOrigin(fog,worldPosition);
    vec3 hit=worldPosition;
    if(reflected) {
        vec3 n=reflectionPlane.xyz,reflectedForward=reflect(fog.forward.xyz,n);
        vec3 virtualEye=fog.eye.w>0.5 ? worldPosition-reflectedForward*(dot(worldPosition-cameraPosition,reflectedForward)-fog.forward.w) : cameraPosition;
        vec3 delta=worldPosition-virtualEye;
        float denom=dot(n,delta);
        float t=clamp(-dot(reflectionPlane,vec4(virtualEye,1))/(abs(denom)>1e-6 ? denom : 1e-6),0.0,1.0);
        hit=virtualEye+delta*t;
        eye=virtualEye-2.0*dot(reflectionPlane,vec4(virtualEye,1))*n;
    }
    if(fog.volumeMinSteps.w<=0.0) {
        float depth=fogDepth(fog,eye,hit,fog.height.x);
        if(reflected) depth+=fogDepth(fog,hit,worldPosition,max(0.0,fog.height.x-length(hit-eye)));
        float transmission=exp(-depth);
        return vec4(fog.colorDensity.rgb*(1.0-transmission),transmission);
    }
    if(!reflected && fog.volumeOptions.y>0.5) {
        vec4 clip=fogViewProjection*vec4(worldPosition,1);
        if(clip.w>0.0) return fogGridMedium(worldPosition,eye,clip);
    }
    vec4 medium=vec4(0,0,0,1);
    float traveled=0.0;
    vec3 origin=eye,endpoint=hit;
    int segments=reflected ? 2 : 1;
    // Use the same integration site for both reflected legs and direct fog.
    for(int segment=0;segment<segments;++segment) {
        vec4 leg=fogVolumeSegment(origin,endpoint,traveled);
        medium=vec4(medium.rgb+medium.a*leg.rgb,medium.a*leg.a);
        traveled=length(endpoint-origin);
        origin=endpoint;
        endpoint=worldPosition;
    }
    return medium;
}
vec3 scenePoint(vec2 pixel,float depth) {
    vec2 ndc=(pixel-waterViewport.xy)/waterViewport.zw*2.0-1.0;
    vec4 world=inverseVp*vec4(ndc,depth,1);return world.xyz/world.w;
}
vec3 surfaceFog(vec3 color,vec4 medium) {
    return color*medium.a+medium.rgb;
}
vec3 waterComposite(vec3 normal,vec3 body,vec3 display,vec2 uv,float time,float nv,vec4 medium) {
    if(!sceneWater) return display;
    vec2 lo=waterViewport.xy+0.5,hi=waterViewport.xy+waterViewport.zw-0.5;
    vec2 pixel=clamp(gl_FragCoord.xy,lo,hi);float depth=texelFetch(sceneDepth,ivec2(pixel),0).r;
    if(depth>=0.999999||depth<=gl_FragCoord.z+0.000001) return display;
    vec3 background=scenePoint(pixel,depth);float viewThickness=max(dot(background-worldPosition,viewForward),0.0);
    vec2 projectedNormal=(vp*vec4(normal,0)).xy;
    vec2 samplePixel=clamp(pixel+projectedNormal*waterViewport.zw*transmission.y*smoothstep(0.0,0.5,viewThickness),lo,hi);
    float sampleDepth=texelFetch(sceneDepth,ivec2(samplePixel),0).r;
    vec3 samplePoint=scenePoint(samplePixel,min(sampleDepth,0.999999));
    if(sampleDepth>=0.999999||dot(samplePoint-worldPosition,viewForward)<=0.001||dot(samplePoint-worldPosition,normalize(planeNormal))>0.01) {samplePixel=pixel;samplePoint=background;}
    vec3 ray=normalize(scenePoint(pixel,0.99)-scenePoint(pixel,0.0));
    float thickness=clamp(dot(samplePoint-worldPosition,viewForward)/max(dot(ray,viewForward),0.1),0.0,100.0);
    vec3 captured=textureLod(sceneColor,samplePixel/waterExtent,0.0).rgb;vec3 transmitted=hdrOutput?captured:pow(captured,vec3(2.2));
    vec3 attenuation=exp(-absorptionFoam.rgb*thickness);
    float f=0.02+0.98*pow(1.0-clamp(nv,0.0,1.0),5.0);
    vec3 result=mix(display,transmitted*attenuation+surfaceFog(clamp(body,vec3(0),vec3(1)),medium)*(vec3(1)-attenuation),transmission.x*(1.0-f*reflectionHorizon.w));
    float gap=max(dot(worldPosition-background,normalize(planeNormal)),0.0);
    float pattern=0.65+0.35*sin(uv.x*absorptionFoam.w*6.2831853+time*0.8)*sin(uv.y*absorptionFoam.w*5.1-time*0.6);
    float foam=transmission.z*(1.0-smoothstep(0.0,transmission.w,gap))*pattern;
    return mix(result,surfaceFog(foamColor.rgb,medium),foam);
}
vec4 surfaceAdvanced[9];
void loadAdvanced(vec2 uv) {
    for(int i=0;i<9;++i)surfaceAdvanced[i]=advanced[i];
    if(advanced[8].x>.5) {
        vec4 packed=textureLod(layerDetail0,uv,0.0);
        surfaceAdvanced[0].x*=packed.r;surfaceAdvanced[0].y*=packed.g;surfaceAdvanced[0].z*=packed.b;surfaceAdvanced[1].x*=packed.a;
    }
    if(advanced[8].z>.5) {
        vec2 inset=.5/vec2(textureSize(layerDetail2,0));int wraps=int(advanced[7].z);
        vec2 q=mix(fract(uv),clamp(uv,vec2(0),vec2(1)),bvec2((wraps&1)!=0,(wraps&2)!=0));
        float x=clamp(q.x,inset.x,1.0-inset.x);
        vec4 sheen=textureLod(layerDetail2,vec2(x,clamp(q.y*.5,inset.y,.5-inset.y)),0.0);
        vec4 subsurface=textureLod(layerDetail2,vec2(x,clamp(.5+q.y*.5,.5+inset.y,1.0-inset.y)),0.0);
        surfaceAdvanced[4].xyz*=pow(sheen.rgb,vec3(2.2));surfaceAdvanced[2].w*=sheen.a;
        surfaceAdvanced[3].xyz*=pow(subsurface.rgb,vec3(2.2));surfaceAdvanced[1].z*=subsurface.a;
    }
    if(advanced[8].w>.5) {
        vec4 packed=textureLod(layerDetail3,uv,0.0);vec2 direction=packed.rg*2.0-1.0;
        surfaceAdvanced[3].w*=packed.b;surfaceAdvanced[5].x*=packed.a;
        if(dot(direction,direction)>1e-6)surfaceAdvanced[4].w+=atan(direction.y,direction.x);
    }
}
vec3 coatNormal(vec2 uv,vec3 n,vec3 t,vec3 b) {
    if(advanced[8].y<.5)return n;
    vec3 sampled=textureLod(layerDetail1,uv,0.0).xyz*2.0-1.0;sampled.xy*=advanced[7].y;
    return normalize(t*sampled.x+b*sampled.y+n*sampled.z);
}
vec3 glassComposite(vec3 normal,vec3 display,float nv) {
    float ior=surfaceAdvanced[0].w,distance=surfaceAdvanced[1].x;vec3 transmitted=ambient;
    if(sceneWater) {
        vec2 lo=waterViewport.xy+.5,hi=waterViewport.xy+waterViewport.zw-.5,base=clamp(gl_FragCoord.xy,lo,hi);
        vec2 bend=(vp*vec4(normal,0)).xy*(1.0-1.0/ior)*min(max(distance,.05),2.0)*.03;
        vec2 pixel=clamp(base+bend*waterViewport.zw,lo,hi);
        float depth=texelFetch(sceneDepth,ivec2(pixel),0).r;
        if(depth<=gl_FragCoord.z+.000001)pixel=base;
        vec3 sampleColor=textureLod(sceneColor,pixel/waterExtent,0.0).rgb;
        transmitted=hdrOutput?sampleColor:pow(sampleColor,vec3(2.2));
    }
    vec3 attenuation=vec3(1);
    if(surfaceAdvanced[1].y>0.0)attenuation=pow(max(surfaceAdvanced[2].xyz,vec3(.0001)),vec3(distance/surfaceAdvanced[1].y));
    float f0=pow((ior-1.0)/(ior+1.0),2.0)*(surfaceAdvanced[5].x*2.0),f=f0+(1.0-f0)*pow(1.0-clamp(nv,0.0,1.0),5.0);
    return mix(display,transmitted*attenuation,surfaceAdvanced[0].z*(1.0-f));
}
float interactionHeight(vec3 p,vec3 normal) {
    float height=0.0;
    for(int i=0;i<16;i++) {
        if(i>=interactionCount) break;
        vec4 origin=interactionData[i*4],heading=interactionData[i*4+1],shape=interactionData[i*4+2];
        float life=interactionData[i*4+3].x;
        vec3 delta=p-origin.xyz;float gap=dot(delta,normal);delta-=normal*gap;
        if(abs(gap)>shape.y) continue;
        float phase=length(delta)-origin.w*shape.w,envelope=exp(-pow(phase/shape.y,2.0));
        if(heading.w>0.0) {
            vec3 projected=heading.xyz-normal*dot(heading.xyz,normal);
            if(dot(projected,projected)<1e-6) continue;
            vec3 forward=normalize(projected);float along=-dot(delta,forward),side=abs(dot(delta,cross(normal,forward)));
            if(along<0.0 || along>heading.w) continue;
            phase=side-along*.32;
            envelope=exp(-pow(phase/shape.y,2.0))*smoothstep(0.0,.3,along)*(1.0-smoothstep(heading.w*.65,heading.w,along));
            phase-=origin.w*shape.w;
        }
        height+=shape.x*sin(6.2831853*phase/shape.z)*envelope*pow(1.0-origin.w/life,2.0);
    }
    return height;
}
vec4 interactionSurface(vec3 p,vec3 normal) {
    vec3 axis=abs(normal.y)>.9 ? vec3(1,0,0) : vec3(0,1,0);
    vec3 u=normalize(cross(normal,axis)),v=cross(normal,u);float h=interactionHeight(p,normal);
    vec3 gradient=u*(interactionHeight(p+u*.025,normal)-h)/.025+v*(interactionHeight(p+v*.025,normal)-h)/.025;
    return vec4(gradient,h);
}
vec3 layerSafeNormal(vec3 value,vec3 fallback) {
    float squared=dot(value,value);return squared>1e-12 ? value*inversesqrt(max(squared,1e-12)) : fallback;
}
struct LayerSample { vec4 color; vec3 normal; float height; };
LayerSample sampleLayer(sampler2D colorMap,sampler2D detailMap,int index,vec2 uv) {
    vec4 transform=layerUv[index],options=layerDetail[index];
    vec3 n=normalize(worldNormal),t=normalize(worldTangent-n*dot(n,worldTangent)),b=cross(n,t)*(dot(cross(n,t),worldBitangent)<0.0 ? -1.0 : 1.0);
    vec4 color=vec4(0);vec3 normal=vec3(0);float height=0.0;
    if(layerOptions.x>1.5) {
        vec3 axis=pow(abs(n),vec3(layerOptions.w)),w=axis/max(dot(axis,vec3(1)),1e-6);
        vec3 signN=mix(vec3(-1),vec3(1),greaterThanEqual(n,vec3(0))),p=worldPosition*layerOptions.z;
        vec2 coord[3]=vec2[3](vec2(-p.z*signN.x,p.y),vec2(p.x,-p.z*signN.y),vec2(p.x*signN.z,p.y));
        vec3 tangents[3]=vec3[3](vec3(0,0,-signN.x),vec3(1,0,0),vec3(signN.z,0,0));
        vec3 bitangents[3]=vec3[3](vec3(0,1,0),vec3(0,0,-signN.y),vec3(0,1,0));
        for(int k=0;k<3;k++) {
            vec2 q=(coord[k]*uvTransform.xy+uvTransform.zw)*transform.xy+transform.zw;
            vec4 reliefSample=texture(detailMap,q),c=texture(colorMap,q);
            vec3 v=(reliefSample.xyz*2.0-1.0)*vec3(options.x*options.w,options.x*options.w,1);
            vec2 orient=sign(transform.xy*uvTransform.xy);
            color+=vec4(pow(c.rgb,vec3(2.2)),c.a)*w[k];height+=reliefSample.a*w[k];
            normal+=(n*v.z+tangents[k]*v.x*orient.x+bitangents[k]*v.y*orient.y)*w[k];
        }
    } else {
        vec2 q=uv*transform.xy+transform.zw;vec4 reliefSample=texture(detailMap,q),c=texture(colorMap,q);
        color=vec4(pow(c.rgb,vec3(2.2)),c.a);height=reliefSample.a;
        vec3 v=(reliefSample.xyz*2.0-1.0)*vec3(options.x*options.w,options.x*options.w,1);vec2 orient=sign(transform.xy*uvTransform.xy);
        normal=t*v.x*orient.x+b*v.y*orient.y+n*v.z;
    }
    return LayerSample(color,layerSafeNormal(normal,n),(height*options.y+options.z)*options.w);
}
vec3 compactFresnel(vec3 f0,float cosine) {
    return f0+(vec3(1)-f0)*pow(1.0-clamp(cosine,0.0,1.0),5.0);
}
vec3 compactDirect(vec3 n,vec3 v,vec3 l,vec3 coatN,vec3 tangent,vec3 base,float rough,float metal,vec3 f0) {
    const float pi=3.14159265;
    float nl=max(dot(n,l),0.0),nv=max(dot(n,v),0.001);

    vec3 h=layerSafeNormal(v+l,n);
    float nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);
    float a=rough*rough,a2=a*a,denom=nh*nh*(a2-1.0)+1.0;
    float distribution=a2/max(pi*denom*denom,1e-7);
    float an=surfaceAdvanced[3].w;
    if(abs(an)>.0001) {
        vec3 axis=layerSafeNormal(tangent-n*dot(tangent,n),vec3(1,0,0)),bitangent=cross(n,axis);
        vec3 t=axis*cos(surfaceAdvanced[4].w)+bitangent*sin(surfaceAdvanced[4].w),b=cross(n,t);
        float aspect=sqrt(max(.1,1.0-.9*abs(an)));if(an<0.0)aspect=1.0/aspect;
        float ax=max(.002,a/aspect),ay=max(.002,a*aspect);
        float ellipse=pow(dot(h,t)/ax,2.0)+pow(dot(h,b)/ay,2.0)+nh*nh;
        distribution=1.0/max(pi*ax*ay*ellipse*ellipse,1e-7);
    }
    float geometry=.5/max(nl*sqrt(nv*nv*(1.0-a2)+a2)+nv*sqrt(nl*nl*(1.0-a2)+a2),1e-5);
    vec3 f=compactFresnel(f0,vh);
    float coatNh=max(dot(coatN,h),0.0);
    float coat=surfaceAdvanced[0].x,coatRough=max(.045,surfaceAdvanced[0].y),ca=pow(coatRough,4.0),cd=coatNh*coatNh*(ca-1.0)+1.0,cf=.04+.96*pow(1.0-vh,5.0);
    float coatLobe=coat*ca/max(pi*cd*cd,1e-7)*geometry*cf;
    float sss=surfaceAdvanced[1].z,wrapped=clamp((dot(n,l)+.5)/1.5,0.0,1.0)+max(-dot(n,l),0.0)*.5;
    vec3 diffuse=(vec3(1)-f)*(1.0-metal)*base/pi;
    vec3 sheen=surfaceAdvanced[4].xyz*surfaceAdvanced[1].w*pow(1.0-vh,mix(1.0,5.0,surfaceAdvanced[2].w))*nl;
    return ((diffuse*(1.0-surfaceAdvanced[0].z)*(1.0-sss)+f*(distribution*geometry))*nl+diffuse*surfaceAdvanced[3].xyz*sss*wrapped+sheen)*(1.0-coat*cf)+vec3(coatLobe*max(dot(coatN,l),0.0));
}
vec3 compactEnvironmentFace(vec3 d,float rotation) {
    float c=cos(rotation),s=sin(rotation);
    d=vec3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
    vec3 axis=abs(d);
    vec2 uv;
    float face;
    if(axis.x>=axis.y&&axis.x>=axis.z) {
        uv=vec2(d.x>=0.0?-d.z:d.z,-d.y)/axis.x;
        face=d.x>=0.0?0.0:1.0;
    } else if(axis.y>=axis.z) {
        uv=vec2(d.x,d.y>=0.0?d.z:-d.z)/axis.y;
        face=d.y>=0.0?2.0:3.0;
    } else {
        uv=vec2(d.z>=0.0?d.x:-d.x,-d.y)/axis.z;
        face=d.z>=0.0?4.0:5.0;
    }
    return vec3(uv*.5+.5,face);
}
vec3 compactEnvironmentSample(vec3 face,int cube,int tile) {
    vec4 rect=environmentRects[tile];
    // Filter within one tile. Face edges stay clamped independently.
    vec2 uv=(rect.xy+clamp(face.xy*rect.z,vec2(.5),vec2(rect.z-.5)))/vec2(textureSize(environmentAtlas,0).xy);
    return textureLod(environmentAtlas,vec3(uv,face.z+float(cube)*6.0),0.0).rgb;
}
#ifdef WEBENGINE_SCREEN_REFLECTIONS
vec4 traceScreenReflection(vec3 normal,vec3 ray,float rough) {
    float smoothness=1.0-smoothstep(.18,.5,rough);
    if(smoothness<=0.0||dot(ray,normal)<=.01) return vec4(0);
    vec3 origin=worldPosition+normal*.04;
    float previousGap=-1.0,previousDistance=0.0;
    for(int i=1;i<=24;++i) {
        float fraction=float(i)/24.0;
        float distance=.08+10.0*fraction*fraction;
        vec3 point=origin+ray*distance;
        vec4 clip=vp*vec4(point,1);
        if(clip.w<=.0001||clip.z<0.0||clip.z>=clip.w) break;
        vec2 pixel=(clip.xy/clip.w*.5+.5)*waterViewport.zw;
        float edge=min(min(pixel.x,pixel.y),min(waterViewport.z-pixel.x,waterViewport.w-pixel.y));
        if(edge<1.0) break;
        ivec2 samplePixel=ivec2(waterViewport.xy+pixel);
        float depth=texelFetch(sceneDepth,samplePixel,0).r;
        if(depth>=.999999) {previousGap=-1.0;previousDistance=distance;continue;}
        vec2 center=vec2(samplePixel)+.5;
        float gap=dot(point-scenePoint(center,depth),viewForward);
        float thickness=.10+.75*(distance-previousDistance)*abs(dot(ray,viewForward));
        if(previousGap<0.0&&gap>=0.0&&gap<thickness&&length(center-gl_FragCoord.xy)>2.0) {
            float confidence=.9*smoothness*smoothstep(1.0,12.0,edge)*(1.0-smoothstep(7.0,10.0,distance));
            return vec4(texelFetch(sceneColor,samplePixel,0).rgb,confidence);
        }
        previousGap=gap;previousDistance=distance;
    }
    return vec4(0);
}
#endif
vec3 compactSpecularProbe(vec3 r,int cube,float rotation,float lod) {
    vec3 reflectionFace=compactEnvironmentFace(r,rotation);
    int mip=int(lod);
    vec3 a=compactEnvironmentSample(reflectionFace,cube,mip+1);
    vec3 b=compactEnvironmentSample(reflectionFace,cube,min(mip+1,int(environmentOptions.z))+1);
    return mix(a,b,fract(lod));
}
vec3 compactProbe(vec3 n,vec3 r,int cube,float rotation,float lod,out vec3 irradiance) {
    irradiance=compactEnvironmentSample(compactEnvironmentFace(n,rotation),cube,0);
    return compactSpecularProbe(r,cube,rotation,lod);
}
vec4 projectedReflectionProbe(vec3 world,vec3 ray) {
    vec4 lower=reflectionProbe[0],upper=reflectionProbe[1];
    if(lower.w<=0.0) return vec4(0);
    vec3 distanceToFace=min(world-lower.xyz,upper.xyz-world);
    float edge=min(distanceToFace.x,min(distanceToFace.y,distanceToFace.z));
    float weight=smoothstep(0.0,lower.w,edge);
    if(weight<=0.0) return vec4(0);
    vec3 target=mix(lower.xyz,upper.xyz,greaterThanEqual(ray,vec3(0)));
    vec3 divisor=mix(vec3(1),ray,greaterThan(abs(ray),vec3(1e-6)));
    vec3 exit=mix(vec3(1e20),(target-world)/divisor,greaterThan(abs(ray),vec3(1e-6)));
    float distance=min(exit.x,min(exit.y,exit.z));
    return vec4(world+ray*distance-reflectionProbe[2].xyz,weight);
}
vec3 compactIndirect(vec3 n,vec3 v,vec3 base,float rough,float metal,vec3 f0) {
    float nv=clamp(dot(n,v),0.001,1.0);
    vec3 f=f0+(max(vec3(1.0-rough),f0)-f0)*pow(1.0-nv,5.0);
    vec3 diffuse=(vec3(1)-f)*(1.0-metal)*base*(1.0-surfaceAdvanced[0].z);
    // Karis's analytic environment BRDF fit from Physically Based Shading on Mobile.
    vec4 fit=rough*vec4(-1,-.0275,-.572,.022)+vec4(1,.0425,1.04,-.04);
    float edge=min(fit.x*fit.x,exp2(-9.28*nv))*fit.x+fit.y;
    vec2 integral=vec2(-1.04,1.04)*edge+fit.zw;
    vec3 specular=max(vec3(0),f0*integral.x+integral.y);
    vec3 reflection=reflect(-v,n);
    float lod=rough*environmentOptions.z;
    vec2 weights=vec2(0);
    for(int i=0;i<2;++i) {
        vec4 low=lightingZones[i*2],high=lightingZones[i*2+1];
        if(low.w<=0.0) continue;
        vec3 edgeDistance=min(worldPosition-low.xyz,high.xyz-worldPosition);
        weights[i]=smoothstep(0.0,low.w,min(edgeDistance.x,min(edgeDistance.y,edgeDistance.z)));
    }
    float total=weights.x+weights.y;
    vec3 diffuseEnvironment=vec3(0),specularEnvironment=vec3(0),irradiance;
    if(total<1.0&&environmentOptions.y>0.0) {
        float weight=environmentOptions.y*(1.0-total);
        specularEnvironment+=compactProbe(n,reflection,0,environmentOptions.x,lod,irradiance)*weight;
        diffuseEnvironment+=irradiance*weight;
    }
    for(int i=0;i<2;++i) {
        if(weights[i]<=0.0) continue;
        float weight=weights[i]*lightingZones[2*i+1].w/max(1.0,total);
        specularEnvironment+=compactProbe(n,reflection,i+1,lightingZoneRotations[i],lod,irradiance)*weight;
        diffuseEnvironment+=irradiance*weight;
    }
    if(fogWorld) {
        vec4 projected=projectedReflectionProbe(worldPosition,reflection);
        if(projected.w>0.0) {
            vec3 cache=compactSpecularProbe(projected.xyz,int(reflectionProbe[2].w),0.0,lod)*reflectionProbe[1].w;
            specularEnvironment=mix(specularEnvironment,cache,projected.w);
        }
    }
#ifdef WEBENGINE_SCREEN_REFLECTIONS
    if(screenReflectionPass) {
        vec4 hit=traceScreenReflection(n,reflection,rough);
        specularEnvironment=mix(specularEnvironment,hit.rgb,hit.a);
    }
#endif
    if(fogWorld && metal<1.0) {
        vec4 gi=sampleDiffuseGi(worldPosition,n);
        if(gi.a>0.0) diffuseEnvironment=mix(diffuseEnvironment,gi.rgb,gi.a);
    }
    return diffuse*(diffuseEnvironment+ambient)+specular*specularEnvironment;
}
void main() {
    vec4 effectiveTint=ribbon||instanceMode?vertexColor:tint;
    vec2 sampleUv=texcoord*uvTransform.xy+uvTransform.zw;if(ribbon)sampleUv.x=fract(sampleUv.x);
    loadAdvanced(sampleUv);
    vec4 texel=texture(baseTexture,sampleUv);
    vec3 baseLinear=pow(texel.rgb,vec3(2.2));
    vec3 layerNormal=normalize(worldNormal);
    float layerRoughness=1.0;
    if(layered) {
        vec2 uv=texcoord*uvTransform.xy+uvTransform.zw;
        vec4 weights=texture(layerWeights,clamp(uv*weightUv.xy+weightUv.zw,vec2(0),vec2(1)));
        float total=dot(weights,vec4(1));weights=total>0.00001 ? weights/total : vec4(1,0,0,0);
        LayerSample a=sampleLayer(baseTexture,layerDetail0,0,uv),b=sampleLayer(layerOne,layerDetail1,1,uv);
        LayerSample c=sampleLayer(layerTwo,layerDetail2,2,uv),d=sampleLayer(layerThree,layerDetail3,3,uv);
        if(layerOptions.y>0.0) {
            vec4 score=vec4(a.height,b.height,c.height,d.height)+weights;
            vec4 activeScores=mix(vec4(-1e6),score,greaterThan(weights,vec4(0)));
            float peak=max(max(activeScores.x,activeScores.y),max(activeScores.z,activeScores.w));
            vec4 adjusted=max(score-vec4(peak-layerOptions.y),vec4(0))*weights;float sum=dot(adjusted,vec4(1));if(sum>1e-6) weights=adjusted/sum;
        }
        baseLinear=weights.x*a.color.rgb*layerColor[0].rgb+weights.y*b.color.rgb*layerColor[1].rgb+
            weights.z*c.color.rgb*layerColor[2].rgb+weights.w*d.color.rgb*layerColor[3].rgb;
        texel.a=dot(weights,vec4(a.color.a,b.color.a,c.color.a,d.color.a));
        layerNormal=layerSafeNormal(weights.x*a.normal+weights.y*b.normal+weights.z*c.normal+weights.w*d.normal,normalize(worldNormal));
        if(compactPbr)layerRoughness=dot(weights,vec4(layerColor[0].w,layerColor[1].w,layerColor[2].w,layerColor[3].w));
    }
    float alpha=texel.a*effectiveTint.a;
    vec3 lighting=ambient;
    vec3 n=layered?layerNormal:normalize(worldNormal);
    vec4 interaction=vec4(0);
    if(water && worldWater && interactionCount>0) interaction=interactionSurface(worldPosition,normalize(planeNormal));
    vec2 wuv=(texcoord*uvTransform.xy+uvTransform.zw)*detail.z;
    float wt=waterTime*wave.z;
    vec3 ra=vec3(0,0,1),rb=vec3(0,0,1);
    if(water) {
        ra=texture(waterNormalTexture,wuv+vec2(0.021,0.013)*wt).xyz*2.0-1.0;
        rb=texture(waterNormalTexture,wuv*1.37+vec2(-0.017,0.019)*wt).xyz*2.0-1.0;
    }
    if(reflectionPass && dot(reflectionPlane,vec4(worldPosition,1))<0.002) discard;
    if(masked && alpha<cutoff) discard;
    if(shadowPass) {outputColor=vec4(0);return;}
    if(water) {
        vec3 t=normalize(worldTangent-n*dot(n,worldTangent));
        float signB=dot(cross(n,t),worldBitangent)<0.0 ? -1.0 : 1.0;
        vec3 b=cross(n,t)*signB;
        vec2 r=(ra.xy+rb.xy)*normalScale+0.12*vec2(sin(wuv.x*6.2831853+wt),cos(wuv.y*8.1-wt*1.3));
        n=normalize(n+(t*r.x+b*r.y)*detail.w);
    }
    n=normalize(n-interaction.xyz*.12);
    if(!gl_FrontFacing) n=-n;
    vec3 geometric=normalize(worldNormal)*(gl_FrontFacing?1.0:-1.0);
    vec3 coatT=normalize(worldTangent-geometric*dot(geometric,worldTangent));vec3 coatB=normalize(worldBitangent);
    vec3 coatN=coatNormal(sampleUv,geometric,coatT,coatB);
    vec3 viewDirection=normalize(cameraPosition-worldPosition);
    float fWater=0.02+0.98*pow(1.0-clamp(dot(n,viewDirection),0.0,1.0),5.0);
    vec3 body=baseLinear*effectiveTint.rgb;
    bool pbr=compactPbr&&!water&&!unlit;
    float surfaceRoughness=1.0,surfaceMetallic=0.0;
    vec3 f0=vec3(.04);
    if(pbr) {
        vec2 factors=vec2(layerRoughness,1.0);
        if(!layered) {
            vec2 surfaceUv=vec2(sampleUv.x,compactBaseSurface?1.0-sampleUv.y:sampleUv.y);
            factors=textureLod(layerOne,surfaceUv,0.0).gb;
        }
        surfaceRoughness=clamp(roughness*factors.x,.045,1.0);
        surfaceMetallic=clamp(metallic*factors.y,0.0,1.0);
        float dielectric=pow((surfaceAdvanced[0].w-1.0)/(surfaceAdvanced[0].w+1.0),2.0)*(surfaceAdvanced[5].x*2.0);
        f0=mix(vec3(dielectric),body,surfaceMetallic);
        lighting=compactIndirect(n,viewDirection,body,surfaceRoughness,surfaceMetallic,f0);
    }
    else if(fogWorld&&!unlit&&!water) {
        vec4 gi=sampleDiffuseGi(worldPosition,n);
        lighting+=gi.rgb*gi.a;
    }
    vec3 waterLighting=body*(ambient+vec3(0.12));
    for(int i=0;i<lightCount;++i) {
        vec3 delta=lightPosition[i].xyz-worldPosition;
        float distance=length(delta);
        bool directional=lightPosition[i].w<0.5;
        vec3 direction=directional ? -lightDirection[i].xyz : delta/max(distance,0.0001);
        float attenuation=directional ? 1.0 : pow(max(1.0-distance/max(lightDirection[i].w,0.0001),0.0),2.0);
        if(lightPosition[i].w>1.5)attenuation*=smoothstep(lightCone[i].y,lightCone[i].x,dot(lightDirection[i].xyz,-direction));
        attenuation*=visibility(i,direction);
#ifdef VOLUMETRIC_CLOUDS
        if(!unlit && fogWorld && receivesShadow && instanceShadow>.5 && directional) attenuation*=cloudSurfaceShadow(worldPosition,normalize(direction));
#endif
        if(!unlit && fogWorld && attenuation>0.0 && lightColor[i].a>0.0 &&
           fog.volumeMinSteps.w>0.0 && (uint(fog.volumeOptions.w)&1u)!=0u && (directional || distance>1e-6)) {
            attenuation*=fogSpotTransmission(i,worldPosition,normalize(direction),directional ? 1e20 : distance);
        }
        vec3 direct=pbr?compactDirect(n,viewDirection,direction,coatN,worldTangent,body,surfaceRoughness,surfaceMetallic,f0):vec3(max(dot(n,direction),0.0));
        if(!pbr&&!water&&!unlit) {
            vec3 h=layerSafeNormal(viewDirection+direction,n);float nv=max(dot(n,viewDirection),.001),nl=max(dot(n,direction),0.0),vh=max(dot(viewDirection,h),0.0);
            float cf=.04+.96*pow(1.0-vh,5.0),coat=surfaceAdvanced[0].x;
            direct=direct*(1.0-surfaceAdvanced[0].z)*(1.0-surfaceAdvanced[1].z)*(1.0-coat*cf);
            direct+=surfaceAdvanced[3].xyz*surfaceAdvanced[1].z*(clamp((dot(n,direction)+.5)/1.5,0.0,1.0)+max(-dot(n,direction),0.0)*.5);
            direct+=surfaceAdvanced[4].xyz*surfaceAdvanced[1].w*pow(1.0-vh,mix(1.0,5.0,surfaceAdvanced[2].w))*nl;
            direct+=vec3(coat*cf*pow(max(dot(coatN,h),0.0),mix(512.0,2.0,surfaceAdvanced[0].y))*max(dot(coatN,direction),0.0));
            // The basic tier has no environment atlas, but imported anisotropy
            // must still affect direct highlights without opting into compact PBR.
            float an=surfaceAdvanced[3].w;
            bool customSpecular=abs(surfaceAdvanced[0].w-1.5)>.0001||abs(surfaceAdvanced[5].x-.5)>.0001;
            if(abs(an)>.0001||surfaceAdvanced[8].w>.5||customSpecular) {
                vec3 axis=layerSafeNormal(worldTangent-n*dot(n,worldTangent),vec3(1,0,0));
                vec3 bitangent=cross(n,axis);float rotation=surfaceAdvanced[4].w;
                vec3 at=axis*cos(rotation)+bitangent*sin(rotation),ab=cross(n,at);
                float a=pow(clamp(roughness,.045,1.0),2.0),aspect=sqrt(max(.1,1.0-.9*abs(an)));
                if(an<0.0)aspect=1.0/aspect;
                float ax=max(.002,a/aspect),ay=max(.002,a*aspect),nh=max(dot(n,h),0.0);
                float ellipse=pow(dot(h,at)/ax,2.0)+pow(dot(h,ab)/ay,2.0)+nh*nh;
                float distribution=1.0/max(3.14159265*ax*ay*ellipse*ellipse,1e-7);
                float k=pow(clamp(roughness,.045,1.0)+1.0,2.0)/8.0;
                float geometry=(nv/(nv*(1.0-k)+k))*(nl/max(nl*(1.0-k)+k,.001));
                float dielectric=pow((surfaceAdvanced[0].w-1.0)/(surfaceAdvanced[0].w+1.0),2.0)*(surfaceAdvanced[5].x*2.0);
                float f0=mix(dielectric,1.0,metallic);
                float f=f0+(1.0-f0)*pow(1.0-vh,5.0);
                direct+=vec3(min(distribution*geometry*f/max(4.0*nv,.001),4.0)*max(abs(an),max(surfaceAdvanced[8].w,customSpecular?1.0:0.0)));
            }
        }
        lighting+=lightColor[i].rgb*lightColor[i].a*direct*attenuation;
        if(water) {
            vec3 h=(viewDirection+direction)*inversesqrt(max(dot(viewDirection+direction,viewDirection+direction),0.0001));
            float highlight=pow(max(dot(n,h),0.0),mix(256.0,8.0,clamp(roughness,0.045,1.0)))*fWater*reflectionHorizon.w;
            waterLighting+=(body*max(dot(n,direction),0.0)*0.25+vec3(highlight))*lightColor[i].rgb*lightColor[i].a*attenuation;
        }
    }
    if(pbr) {
        float nv=max(dot(n,viewDirection),.001),coatF=.04+.96*pow(1.0-nv,5.0);
        lighting=lighting*(1.0-surfaceAdvanced[0].x*coatF)+compactSpecularProbe(reflect(-viewDirection,coatN),0,environmentOptions.x,surfaceAdvanced[0].y*environmentOptions.z)*environmentOptions.y*(surfaceAdvanced[0].x*coatF);
        lighting+=ambient*body*surfaceAdvanced[3].xyz*surfaceAdvanced[1].z*.25;
        lighting+=ambient*surfaceAdvanced[4].xyz*surfaceAdvanced[1].w*pow(1.0-nv,mix(1.0,5.0,surfaceAdvanced[2].w));
    }
    vec3 linear=(pbr ? lighting : body*(unlit ? vec3(1.0) : lighting))+emissive;
    if(water) {
        vec3 reflection=mix(reflectionHorizon.rgb,reflectionZenith.rgb,clamp(reflect(-viewDirection,n).y,0.0,1.0));
        linear=waterLighting*(1.0-fWater*reflectionHorizon.w)+reflection*fWater*reflectionHorizon.w+emissive;
    }
#ifdef VOLUMETRIC_CLOUDS
    if(fogWorld) linear=cloudApply(linear,worldPosition,fogAdditive);
#endif
    vec3 displayLinear=toneMapped && !hdrOutput ? linear/(vec3(1.0)+linear) : linear;
    vec4 medium=fogWorld || water ? surfaceFogMedium() : vec4(0,0,0,1);
    vec4 surfaceMedium=fogWorld ? medium : vec4(0,0,0,1);
    if(fogAdditive && (!reflectionPass || fog.volumeMinSteps.w>0.0)) surfaceMedium.rgb=vec3(0);
    displayLinear=surfaceFog(displayLinear,surfaceMedium);
    if(water && worldWater && planarActive && planarSurface>0.5 && foamColor.w>0.0) {
        vec4 clip=reflectionVp*vec4(worldPosition,1);
        vec2 uv=clip.xy/max(clip.w,0.0001)*0.5+0.5+n.xz*0.008*(1.0-roughness);
        bool inside=clip.w>0.0 && all(greaterThan(uv,vec2(0))) && all(lessThan(uv,vec2(1))) && clip.z>=0.0 && clip.z<=clip.w;
        vec4 reflected=textureLod(planarReflection,clamp(waterViewport.xy+uv*waterViewport.zw,waterViewport.xy+0.5,waterViewport.xy+waterViewport.zw-0.5)/waterExtent,0.0);
        displayLinear=mix(displayLinear,hdrOutput?reflected.rgb:pow(reflected.rgb,vec3(2.2)),inside ? foamColor.w*reflectionHorizon.w*fWater*reflected.a : 0.0);
    }
    if(water) displayLinear=waterComposite(n,body,displayLinear,texcoord*uvTransform.xy+uvTransform.zw,wt,max(dot(n,viewDirection),0.0),surfaceMedium)+vec3(min(abs(interaction.w)*.18,.25))*medium.a;
    if(!water&&!unlit&&surfaceAdvanced[0].z>0.0) displayLinear=glassComposite(n,displayLinear,max(dot(n,viewDirection),0.0));
    vec3 display=hdrOutput?displayLinear:pow(displayLinear,vec3(1.0/2.2));
    float fade=1.0;
    if(particleSoftDistance>0.0) {
        vec2 pixel=clamp(gl_FragCoord.xy,waterViewport.xy+0.5,waterViewport.xy+waterViewport.zw-0.5);
        float depth=texelFetch(sceneDepth,ivec2(pixel),0).r;
        if(depth<0.999999) fade=clamp(dot(scenePoint(pixel,depth)-worldPosition,viewForward)/particleSoftDistance,0.0,1.0);
    }
    outputColor=vec4(display,reflectionPass ? 1.0 : alpha*fade);
})";
constexpr const char* guiVertex=R"(#version 300 es
precision highp float;
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
uniform vec2 logicalSize;
out vec2 texcoord;
out vec4 tint;
void main() { gl_Position=vec4(position.x/logicalSize.x*2.0-1.0,1.0-position.y/logicalSize.y*2.0,0.0,1.0);texcoord=uv;tint=color; }
)";
constexpr const char* guiFragment=R"(#version 300 es
precision highp float;
uniform sampler2D baseTexture;
in vec2 texcoord;
in vec4 tint;
out vec4 outputColor;
void main() { outputColor=texture(baseTexture,texcoord)*tint; }
)";
GLuint shader(GLenum type,const char* text) {
    std::string source=text;
    for(const auto& [marker,body]:std::array<std::pair<std::string_view,const char*>,6>{{
        {"// FOG_FUNCTIONS",fog_glsl},{"// FOG_VOLUME",fog_volume_glsl},{"// FOG_GRID_SAMPLING",fog_grid_sampling_glsl},
        {"// DIFFUSE_GI",diffuse_gi_glsl},{"// CLOUD_VOLUME",cloud_volume_glsl},{"// CLOUD_GRID_SAMPLING",cloud_grid_sampling_glsl}}}) {
        if(const auto at=source.find(marker);at!=std::string::npos) source.replace(at,marker.size(),body);
    }
    text=source.c_str();
    const auto id=glCreateShader(type);glShaderSource(id,1,&text,nullptr);glCompileShader(id);
    GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
    if(!ok) { char log[2048]{};glGetShaderInfoLog(id,sizeof(log),nullptr,log);glDeleteShader(id);throw std::runtime_error(log); }
    return id;
}
GLuint program(const char* vertex,const char* fragment) {
    const auto v=shader(GL_VERTEX_SHADER,vertex);GLuint f=0,p=0;
    try {
        f=shader(GL_FRAGMENT_SHADER,fragment);p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
        GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
        if(!ok) { char log[2048]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);throw std::runtime_error(log); }
    } catch(...) { glDeleteShader(v);if(f) glDeleteShader(f);if(p) glDeleteProgram(p);throw; }
    glDeleteShader(v);glDeleteShader(f);return p;
}
GLuint makeSkyProgram(bool volumetricClouds) {
    std::string fragment=sky_fragment_glsl;
    if(volumetricClouds) fragment.insert(fragment.find('\n')+1,"#define VOLUMETRIC_CLOUDS\n");
    return program(sky_vertex_glsl,fragment.c_str());
}
GLuint makeMeshProgram(bool volumetricClouds) {
    std::string fragment=meshFragment;
    if(volumetricClouds) fragment.insert(fragment.find('\n')+1,"#define VOLUMETRIC_CLOUDS\n");
    const auto result=program(meshVertex,fragment.c_str());
    glUseProgram(result);glUniform1i(glGetUniformLocation(result,"environmentAtlas"),14);
    glUniform1i(glGetUniformLocation(result,"shadowMaps"),13);glUniform1i(glGetUniformLocation(result,"fogGrid"),15);
    return result;
}
void fogUniform(GLuint program,const FogUniform& f) {
    const char* names[]={"fog.colorDensity","fog.height","fog.eye","fog.forward"};
    const std::array values={f.colorDensity,f.height,f.eye,f.forward};
    for(int i=0;i<4;++i) glUniform4fv(glGetUniformLocation(program,names[i]),1,values[i].data());
    glUniform4fv(glGetUniformLocation(program,"fog.volumeMinSteps"),1,f.volumeMinSteps.data());
    glUniform4fv(glGetUniformLocation(program,"fog.volumeMaxDistance"),1,f.volumeMaxDistance.data());
    glUniform4fv(glGetUniformLocation(program,"fog.volumeColorDensity"),1,f.volumeColorDensity.data());
    glUniform4fv(glGetUniformLocation(program,"fog.volumeOptions"),1,f.volumeOptions.data());
    glUniform4fv(glGetUniformLocation(program,"fog.volumeLighting"),1,f.volumeLighting.data());
    for(unsigned i=0;i<static_cast<unsigned>(f.volumeLighting[2]);++i) {
        const auto prefix="fog.lightCache["+std::to_string(i)+"].";
        glUniform4fv(glGetUniformLocation(program,(prefix+"minimumIndex").c_str()),1,f.lightCache[i].minimumIndex.data());
        glUniform4fv(glGetUniformLocation(program,(prefix+"maximum").c_str()),1,f.lightCache[i].maximum.data());
    }
    for(unsigned i=1;i<static_cast<unsigned>(f.volumeLighting[0]);++i) {
        const auto prefix="fog.additionalVolumes["+std::to_string(i-1)+"].";
        const auto& box=f.additionalVolumes[i-1];
        glUniform4fv(glGetUniformLocation(program,(prefix+"minimumFade").c_str()),1,box.minimumFade.data());
        glUniform4fv(glGetUniformLocation(program,(prefix+"maximumDistance").c_str()),1,box.maximumDistance.data());
        glUniform4fv(glGetUniformLocation(program,(prefix+"colorDensity").c_str()),1,box.colorDensity.data());
    }
}
void attributes() {
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,position)));
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,normal)));
    glEnableVertexAttribArray(2);glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,uv)));
    glEnableVertexAttribArray(4);glVertexAttribPointer(4,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,tangent)));
}
void matrix(GLuint p,const char* name,const Mat4& value) { glUniformMatrix4fv(glGetUniformLocation(p,name),1,GL_FALSE,value.m.data()); }
void blend(BlendMode mode) {
    glDepthMask(mode==BlendMode::Opaque);
    if(mode==BlendMode::Opaque) glDisable(GL_BLEND);
    else { glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,mode==BlendMode::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA); }
}
void eraseMesh(auto& mesh) { glDeleteVertexArrays(1,&mesh.vao);glDeleteBuffers(1,&mesh.vertices);glDeleteBuffers(1,&mesh.indices); }
}

WebGlRenderer::~WebGlRenderer() {
    if(context_) {
        emscripten_set_webglcontextlost_callback("#viewport",nullptr,false,nullptr);
        emscripten_set_webglcontextrestored_callback("#viewport",nullptr,false,nullptr);
        emscripten_webgl_make_context_current(context_);releaseResources(emscripten_is_webgl_context_lost(context_));
        emscripten_webgl_destroy_context(context_);
    }
}
void WebGlRenderer::setScene(Scene& scene,std::span<const RenderView> views) {
    for(const auto& view:views) if(!view.camera||view.camera->scene()!=&scene) throw std::invalid_argument("Render camera does not belong to the target scene");
    setViews(views);
    if(scene_!=&scene) {
        lightSelection_.clear();activeLighting_={};reportedLighting_={};
        for(auto& [key,surface]:surfaces_) surface.source.reset();
    }
    scene_=&scene;
}
void WebGlRenderer::setViews(std::span<const RenderView> views) {
    views_.set(views);
    if(lightSelection_.size()>views.size())lightSelection_.resize(views.size());
    for(auto& [key,surface]:surfaces_) {
        if(!views_.hasSurface(key)) surface.source.reset();
    }
}
void WebGlRenderer::setQuality(const RenderQuality& quality) {
    quality.validate();const bool oldHdr=hdr_,nextHdr=quality.post.enabled&&capabilities_.hdrPostProcessing;
    if(ready_) {
        emscripten_webgl_make_context_current(context_);
        const auto extent=resolveResolution(browser::canvasCssWidth(),browser::canvasCssHeight(),browser::canvasPixelRatio(),quality.resolution,capabilities_);
        const bool cloudVariant=(quality_.volumetricCloudSteps>0)!=(quality.volumetricCloudSteps>0);
        const auto nextSky=cloudVariant?makeSkyProgram(quality.volumetricCloudSteps>0):0;
        GLuint nextMesh=0;
        try {if(cloudVariant) nextMesh=makeMeshProgram(quality.volumetricCloudSteps>0);}
        catch(...) {if(nextSky)glDeleteProgram(nextSky);throw;}
        hdr_=nextHdr;
        try {resizeTargets(extent.width,extent.height,samplesFor(quality));} catch(...) {hdr_=oldHdr;if(nextSky)glDeleteProgram(nextSky);if(nextMesh)glDeleteProgram(nextMesh);throw;}
        if(oldHdr!=nextHdr){releaseWaterTargets();releaseReflectionTargets();
            if(!nextHdr){glDeleteFramebuffers(1,&bloomFramebuffer_);glDeleteTextures(1,&bloomTexture_);bloomFramebuffer_=bloomTexture_=0;bloomWidth_=bloomHeight_=0;}}
        if(!quality.webGlPbr&&quality_.webGlPbr) releaseCompactLighting();
        if(nextSky) {glDeleteProgram(skyProgram_);skyProgram_=nextSky;}
        if(nextMesh) {
            glDeleteProgram(basicMeshProgram_);glDeleteProgram(compactMeshProgram_);glDeleteProgram(screenReflectionMeshProgram_);
            basicMeshProgram_=meshProgram_=nextMesh;compactMeshProgram_=screenReflectionMeshProgram_=0;
            environmentUnavailable_=screenReflectionUnavailable_=false;
        }
    }
    quality_=quality;
    if(ready_){resizeCanvas();resetMeasurements();refreshQualityReport();}
}
void WebGlRenderer::setShadowQuality(std::uint32_t resolution,std::uint32_t taps) {
    auto candidate=quality_;candidate.shadowResolution=resolution;candidate.shadowFilterTaps=taps;candidate.validate();quality_=candidate;
}
void WebGlRenderer::fail(const std::string& message) { ready_=false;failed_=true;refreshQualityReport();gl_status(message.c_str(),1); }
bool WebGlRenderer::lost(int,const void*,void* data) {
    auto& self=*static_cast<WebGlRenderer*>(data);self.ready_=false;self.releaseResources(true);self.refreshQualityReport();
    gl_status("WebGL context lost. Waiting for restoration…",0);return true;
}
bool WebGlRenderer::restored(int,const void*,void* data) { static_cast<WebGlRenderer*>(data)->restorePending_=true;return true; }
void WebGlRenderer::serviceRecovery() {
    if(!restorePending_) return;
    restorePending_=false;failed_=false;
    try { emscripten_webgl_make_context_current(context_);createResources(); } catch(const std::exception& e) { fail(e.what()); }
}
void WebGlRenderer::retryRecovery() {
    if(!failed_) return;
    failed_=false;if(!context_) initialize();else { releaseResources();restorePending_=true;serviceRecovery(); }
}
void WebGlRenderer::simulateDeviceLoss() { if(ready_) gl_lose(); }
void WebGlRenderer::initialize() {
    if(context_||failed_) return;
    refreshQualityReport();
    EmscriptenWebGLContextAttributes attributes{};emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion=2;attributes.minorVersion=0;attributes.alpha=false;attributes.depth=true;attributes.antialias=false;
    context_=emscripten_webgl_create_context("#viewport",&attributes);
    if(context_<=0) { context_=0;fail("No usable rendering backend: WebGL 2 context creation failed.");return; }
    emscripten_webgl_make_context_current(context_);
    emscripten_set_webglcontextlost_callback("#viewport",this,false,lost);
    emscripten_set_webglcontextrestored_callback("#viewport",this,false,restored);
    try { createResources(); } catch(const std::exception& e) { fail(std::string("WebGL 2 initialization failed: ")+e.what()); }
}
void WebGlRenderer::createResources() {
    capabilities_.hdrPostProcessing=gl_hdr();hdrMsaa_=capabilities_.hdrPostProcessing&&gl_hdr_msaa();hdr_=quality_.post.enabled&&capabilities_.hdrPostProcessing;
    postProgram_=program(post_vertex_glsl,post_fragment_glsl);glGenVertexArrays(1,&postVao_);
    skyProgram_=makeSkyProgram(quality_.volumetricCloudSteps>0);glGenVertexArrays(1,&skyVao_);glGenSamplers(1,&skySampler_);
    glSamplerParameteri(skySampler_,GL_TEXTURE_WRAP_S,GL_REPEAT);glSamplerParameteri(skySampler_,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glSamplerParameteri(skySampler_,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glSamplerParameteri(skySampler_,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    underwaterProgram_=program(underwater_vertex_glsl,underwater_fragment_glsl);glGenVertexArrays(1,&underwaterVao_);
    basicMeshProgram_=meshProgram_=makeMeshProgram(quality_.volumetricCloudSteps>0);guiProgram_=program(guiVertex,guiFragment);
    glUseProgram(basicMeshProgram_);
    glUniform1i(glGetUniformLocation(basicMeshProgram_,"environmentAtlas"),14);
    glGenBuffers(1,&objects_);objectCapacity_=0;windValuesValid_=false;windProgram_=0;frame_.invalidateObjects();
    glGenVertexArrays(1,&guiVao_);glGenBuffers(1,&guiVertices_);glGenBuffers(1,&guiIndices_);
    glBindVertexArray(guiVao_);glBindBuffer(GL_ARRAY_BUFFER,guiVertices_);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,guiIndices_);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(GuiVertex),reinterpret_cast<void*>(offsetof(GuiVertex,position)));
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(GuiVertex),reinterpret_cast<void*>(offsetof(GuiVertex,uv)));
    glEnableVertexAttribArray(2);glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(GuiVertex),reinterpret_cast<void*>(offsetof(GuiVertex,color)));
    glGenVertexArrays(1,&particleVao_);glGenBuffers(1,&particleBuffer_);
    glBindVertexArray(particleVao_);glBindBuffer(GL_ARRAY_BUFFER,particleBuffer_);attributes();
    glGenVertexArrays(1,&ribbonVao_);glGenBuffers(1,&ribbonBuffer_);
    glBindVertexArray(ribbonVao_);glBindBuffer(GL_ARRAY_BUFFER,ribbonBuffer_);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(RibbonVertex),nullptr);
    glDisableVertexAttribArray(1);glVertexAttrib3f(1,0,0,1);
    glEnableVertexAttribArray(2);glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(RibbonVertex),reinterpret_cast<void*>(32));
    glEnableVertexAttribArray(3);glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,sizeof(RibbonVertex),reinterpret_cast<void*>(16));
    GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);capabilities_.maxTextureDimension2D=limit;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS,&limit);capabilities_.maxTextureArrayLayers=limit;
    capabilities_.backend=RenderBackend::WebGl2;capabilities_.shadowMaps=true;capabilities_.pbr=false;capabilities_.instancing=true;
    capabilities_.compactPbr=capabilities_.maxTextureDimension2D>=256&&capabilities_.maxTextureArrayLayers>=6*(2+maxLightingZones);
    capabilities_.msaa4x=gl_msaa4x_supported()!=0;
    ++generation_;ready_=true;failed_=false;resizeCanvas();resetMeasurements();refreshQualityReport();gl_status("WebGL 2 renderer ready",0);
}
void WebGlRenderer::releaseResources(bool lost) {
    if(!lost) glDeleteTextures(1,&diffuseGiTexture_);
    diffuseGiTexture_=0;diffuseGiMetadata_={};diffuseGiSource_.reset();
    releaseCompactLighting(lost);capabilities_.compactPbr=false;
    releaseFogGrid(lost);
    if(!lost) {glDeleteProgram(cloudGridProgram_);glDeleteProgram(cloudShadowProgram_);}
    cloudGridProgram_=cloudShadowProgram_=0;cloudGridUnavailable_=false;activeCloud_={};activeSky_={};
    if(!lost) {glDeleteProgram(fogGridProgram_);glDeleteProgram(fogGridCachedProgram_);glDeleteProgram(fogLightCacheProgram_);}
    fogGridProgram_=fogGridCachedProgram_=fogLightCacheProgram_=0;
    fogGridUnavailable_=fogLightCacheUnavailable_=false;
    releaseShadows(lost);releasePost(lost);
    releaseWaterTargets(lost);releaseReflectionTargets(lost);
    if(!lost) {
        for(const auto& [key,surface]:surfaces_) deleteSurface(surface);
        glDeleteFramebuffers(1,&msaaFramebuffer_);glDeleteRenderbuffers(1,&msaaColor_);glDeleteRenderbuffers(1,&msaaDepth_);
        glDeleteProgram(underwaterProgram_);glDeleteVertexArrays(1,&underwaterVao_);
        glDeleteProgram(skyProgram_);glDeleteVertexArrays(1,&skyVao_);glDeleteSamplers(1,&skySampler_);
        for(auto& [key,value]:meshes_) eraseMesh(value);
        for(auto& [key,value]:textures_) glDeleteTextures(1,&value.image);
        for(auto& [key,value]:bends_)glDeleteBuffers(1,&value.buffer);
        glDeleteBuffers(1,&objects_);
        glDeleteProgram(basicMeshProgram_);glDeleteProgram(compactMeshProgram_);glDeleteProgram(screenReflectionMeshProgram_);
        glDeleteProgram(guiProgram_);glDeleteVertexArrays(1,&guiVao_);glDeleteVertexArrays(1,&particleVao_);
        glDeleteBuffers(1,&guiVertices_);glDeleteBuffers(1,&guiIndices_);glDeleteBuffers(1,&particleBuffer_);glDeleteVertexArrays(1,&ribbonVao_);glDeleteBuffers(1,&ribbonBuffer_);
    }
    surfaces_.clear();basicMeshProgram_=compactMeshProgram_=screenReflectionMeshProgram_=0;
    meshes_.clear();textures_.clear();meshProgram_=guiProgram_=guiVao_=guiVertices_=guiIndices_=particleVao_=particleBuffer_=ribbonVao_=ribbonBuffer_=0;
    bends_.clear();objects_=0;objectCapacity_=0;frame_.invalidateObjects();
    skyProgram_=skyVao_=skySampler_=0;underwaterProgram_=underwaterVao_=0;underwaterViews_.clear();
    msaaFramebuffer_=msaaColor_=msaaDepth_=0;targetWidth_=targetHeight_=0;sampleCount_=1;capabilities_.msaa4x=false;
}
void WebGlRenderer::resizeTargets(std::uint32_t width,std::uint32_t height,std::uint32_t samples) {
    if(width==targetWidth_&&height==targetHeight_&&samples==sampleCount_&&targetHdr_==hdr_) return;
    GLuint framebuffer=0,color=0,depth=0;
    if(samples>1) {
        glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glGenRenderbuffers(1,&color);glBindRenderbuffer(GL_RENDERBUFFER,color);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,hdr_?GL_RGBA16F:GL_RGB8,width,height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color);
        glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_DEPTH_COMPONENT24,width,height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
        const auto error=glGetError();const auto status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER,0);glBindRenderbuffer(GL_RENDERBUFFER,0);
        if(error!=GL_NO_ERROR||status!=GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1,&framebuffer);glDeleteRenderbuffers(1,&color);glDeleteRenderbuffers(1,&depth);
            throw std::runtime_error("Cannot create complete MSAA color/depth framebuffer");
        }
    }
    glDeleteFramebuffers(1,&msaaFramebuffer_);glDeleteRenderbuffers(1,&msaaColor_);glDeleteRenderbuffers(1,&msaaDepth_);
    msaaFramebuffer_=framebuffer;msaaColor_=color;msaaDepth_=depth;
    targetWidth_=width;targetHeight_=height;sampleCount_=samples;targetHdr_=hdr_;
}
void WebGlRenderer::resizeCanvas() {
    const auto extent=resolveResolution(browser::canvasCssWidth(),browser::canvasCssHeight(),browser::canvasPixelRatio(),quality_.resolution,capabilities_);
    if(ready_) resizeTargets(extent.width,extent.height,samplesFor(quality_));
    int cw=0,ch=0;emscripten_get_canvas_element_size("#viewport",&cw,&ch);
    if(cw!=int(extent.width)||ch!=int(extent.height)) emscripten_set_canvas_element_size("#viewport",extent.width,extent.height);
    width_=extent.width;height_=extent.height;
}
void WebGlRenderer::refreshQualityReport() {
    if(!context_&&!failed_) return; // Do not overwrite the selected backend's report before acquisition.
    const auto& q=quality_;const auto& c=capabilities_;
    std::ostringstream s;
    s<<"{\"spotLights\":true,\"spotLightShadows\":true,\"maxLights\":8,\"backend\":\"webgl2\",\"shadows\":true,\"pbr\":false,\"instancing\":true,\"state\":\""<<(ready_?"ready":failed_?"failed":generation_?"recovering":"initializing")
     <<"\",\"generation\":"<<generation_<<",\"recoveryAttempts\":0,\"maxTextureDimension2D\":"<<c.maxTextureDimension2D
     <<",\"maxTextureArrayLayers\":"<<c.maxTextureArrayLayers<<",\"maxBufferSize\":0,\"timestampQuery\":false,\"compressionBC\":false,\"compressionETC2\":false,\"compressionASTC\":false,\"compressedTextureCount\":0"
     <<",\"width\":"<<width_<<",\"height\":"<<height_<<",\"resolutionScale\":"<<q.resolution.scale<<",\"maxPixelRatio\":"<<q.resolution.maxPixelRatio
     <<",\"maxDimension\":"<<q.resolution.maxDimension<<",\"maxPixels\":"<<q.resolution.maxPixels<<",\"shadowResolution\":"<<q.shadowResolution<<",\"shadowFilterTaps\":"<<q.shadowFilterTaps<<",\"maxShadowMaps\":"<<q.maxShadowMaps
     <<",\"msaaSamples\":"<<q.msaaSamples<<",\"effectiveMsaaSamples\":"<<sampleCount_<<",\"msaa4x\":"<<(c.msaa4x?"true":"false")
     <<",\"detailScale\":"<<q.detailScale<<",\"lod\":"<<(q.lod?"true":"false")<<",\"shadowLod\":"<<(q.shadowLod?"true":"false")
     <<",\"postToneMapper\":"<<int(q.post.toneMapper)<<",\"compressedTextures\":false"
     <<",\"compactPbr\":"<<(c.compactPbr?"true":"false")<<",\"webGlPbr\":"<<(q.webGlPbr?"true":"false")
     <<",\"diffuseGiProbeCount\":"<<(diffuseGiSource_?diffuseGiSource_->probes.size():0)
     <<",\"effectiveCompactPbr\":"<<(effectiveCompactPbr_?"true":"false")<<"}";
    auto report=s.str();report.pop_back();report+=",\"postEnabled\":"+std::string(q.post.enabled?"true":"false")+",\"hdrPostProcessing\":"+(c.hdrPostProcessing?"true":"false")+",\"effectivePost\":"+(hdr_?"true":"false")+",\"directionalCascades\":"+std::to_string(q.directionalCascades)+",\"shadowDistance\":"+std::to_string(q.shadowDistance)+",\"postExposure\":"+std::to_string(q.post.exposure)+",\"postBloom\":"+std::to_string(q.post.bloomStrength)+",\"postThreshold\":"+std::to_string(q.post.bloomThreshold)+",\"postSaturation\":"+std::to_string(q.post.saturation)+",\"postContrast\":"+std::to_string(q.post.contrast)+"}";
    gl_report(report.c_str());
}

namespace { struct UploadDeferred {}; }
void WebGlRenderer::admitUpload(std::uint64_t bytes) {
    if(budgeted_&&!uploadAdmission_.admit(bytes,emscripten_get_now()-uploadStarted_)) throw UploadDeferred{};
}
void WebGlRenderer::releaseCompactLighting(bool lost) {
    if(!lost) glDeleteTextures(1,&environmentAtlas_);
    environmentAtlas_=0;environmentSource_.reset();environmentZones_={};reflectionProbeSource_.reset();
    environmentBytes_=0;effectiveCompactPbr_=environmentUnavailable_=screenReflectionUnavailable_=false;
}
void WebGlRenderer::uploadDiffuseGi() {
    const auto& source=scene_->diffuseGi();
    if(source==diffuseGiSource_) return;
    if(!source) {
        diffuseGiMetadata_={};
        diffuseGiSource_.reset();
        return;
    }
    const auto data=packDiffuseGi(source.get());
    constexpr GLsizei texelsPerProbe=sizeof(DiffuseGiProbe)/sizeof(DiffuseGiGpuTexel);
    glActiveTexture(GL_TEXTURE1);
    if(!diffuseGiTexture_) {
        glGenTextures(1,&diffuseGiTexture_);
        glBindTexture(GL_TEXTURE_2D,diffuseGiTexture_);
        glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA32F,texelsPerProbe,maxDiffuseGiProbes);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        ++counters_.newTextures;
    } else {
        glBindTexture(GL_TEXTURE_2D,diffuseGiTexture_);
    }
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,texelsPerProbe,GLsizei(source->probes.size()),
        GL_RGBA,GL_FLOAT,data.probes.data());
    glActiveTexture(GL_TEXTURE0);
    ++counters_.uploadCalls;counters_.uploadBytes+=source->probes.size()*sizeof(DiffuseGiProbe);
    diffuseGiMetadata_=data.metadata;
    diffuseGiSource_=source;
}
void WebGlRenderer::bindCompactLighting() {
    meshProgram_=basicMeshProgram_;effectiveCompactPbr_=false;
    if(!quality_.webGlPbr||!capabilities_.compactPbr||environmentUnavailable_) return;
    const auto prepareProgram=[&](GLuint& id,bool reflections) {
        if(id) return true;
        std::string fragment=meshFragment;
        if(quality_.volumetricCloudSteps>0) fragment.insert(fragment.find('\n')+1,"#define VOLUMETRIC_CLOUDS\n");
        fragment.insert(fragment.find('\n')+1,reflections
            ? "#define WEBENGINE_COMPACT_PBR\n#define WEBENGINE_SCREEN_REFLECTIONS\n"
            : "#define WEBENGINE_COMPACT_PBR\n");
        try { id=program(meshVertex,fragment.c_str()); }
        catch(const std::runtime_error&) { return false; }
        glUseProgram(id);glUniform1i(glGetUniformLocation(id,"environmentAtlas"),14);
        glUniform1i(glGetUniformLocation(id,"shadowMaps"),13);glUniform1i(glGetUniformLocation(id,"fogGrid"),15);
        return true;
    };
    GLuint selected=0;
    if(hdr_&&quality_.screenSpaceReflections&&!screenReflectionUnavailable_) {
        if(prepareProgram(screenReflectionMeshProgram_,true)) selected=screenReflectionMeshProgram_;
        else screenReflectionUnavailable_=true;
    }
    if(!selected) {
        if(!prepareProgram(compactMeshProgram_,false)) {environmentUnavailable_=true;return;}
        selected=compactMeshProgram_;
    }

    const auto& zones=scene_->lightingZones();
    const auto source=scene_->environment ? scene_->environment : Environment::studio();
    const auto sources=lightingZoneSources(zones);
    const auto& probe=scene_->reflectionProbe();
    const auto reflectionSource=probe?probe->environment:std::shared_ptr<const Environment>{};
    if(source!=environmentSource_||sources!=environmentZones_||reflectionSource!=reflectionProbeSource_) {
        source->validate();
        const auto base=source->specular.front().size;
        const auto diffuse=source->diffuse.size;
        const auto width=std::max(base+source->specular[1].size,diffuse),height=base+diffuse;
        const auto layers=6*(1+zones.size()+(reflectionSource?1:0));
        const auto pixelCount=std::size_t(width)*height*layers;
        const auto bytes=pixelCount*8;
        try { admitUpload(bytes); } catch(const UploadDeferred&) { ++counters_.deferredAssetUploads;return; }
        const auto packed=packLightingEnvironments(*source,zones,reflectionSource.get());
        std::vector<std::uint16_t> pixels(pixelCount*4);
        std::array<std::array<float,4>,9> rects{};
        const auto write=[&](const EnvironmentLevel& level,std::uint32_t x,std::uint32_t y,std::size_t tile) {
            rects[tile]={float(x),float(y),float(level.size),0};
            for(std::size_t face=0;face<layers;++face) {
                for(std::uint32_t row=0;row<level.size;++row) {
                    for(std::uint32_t column=0;column<level.size;++column) {
                        const auto& value=level.pixels[(face*level.size+row)*level.size+column];
                        const auto output=((face*height+y+row)*width+x+column)*4;
                        for(std::size_t channel=0;channel<3;++channel) pixels[output+channel]=floatToHalf(value[channel]);
                        pixels[output+3]=floatToHalf(1);
                    }
                }
            }
        };
        // Put diffuse below the sharpest level and stack smaller specular levels beside it.
        write(packed.diffuse,0,base,0);write(packed.specular.front(),0,0,1);
        std::uint32_t y=0;
        for(std::size_t i=1;i<packed.specular.size();++i) {
            write(packed.specular[i],base,y,i+1);
            y+=packed.specular[i].size;
        }
        GLuint candidate=0;
        glGenTextures(1,&candidate);glActiveTexture(GL_TEXTURE14);glBindTexture(GL_TEXTURE_2D_ARRAY,candidate);
        glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA16F,width,height,layers,0,GL_RGBA,GL_HALF_FLOAT,pixels.data());
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glActiveTexture(GL_TEXTURE0);
        if(!candidate||glGetError()!=GL_NO_ERROR) {
            glDeleteTextures(1,&candidate);environmentUnavailable_=true;return;
        }
        glDeleteTextures(1,&environmentAtlas_);environmentAtlas_=candidate;
        environmentSource_=source;environmentZones_=sources;reflectionProbeSource_=reflectionSource;
        environmentRects_=rects;environmentBytes_=bytes;
        ++counters_.uploadCalls;++counters_.newTextures;counters_.uploadBytes+=bytes;
    }
    glActiveTexture(GL_TEXTURE14);glBindTexture(GL_TEXTURE_2D_ARRAY,environmentAtlas_);glActiveTexture(GL_TEXTURE0);
    meshProgram_=selected;glUseProgram(meshProgram_);
    const auto constants=lightingZoneConstants(zones);
    const auto rotations=lightingZoneRotations(zones);
    glUniform4fv(glGetUniformLocation(meshProgram_,"environmentRects"),int(environmentRects_.size()),environmentRects_[0].data());
    glUniform4f(glGetUniformLocation(meshProgram_,"environmentOptions"),scene_->environmentRotation,scene_->environmentIntensity,float(source->specular.size()-1),0);
    glUniform4fv(glGetUniformLocation(meshProgram_,"lightingZones"),int(constants.size()),constants[0].data());
    glUniform4fv(glGetUniformLocation(meshProgram_,"lightingZoneRotations"),1,rotations.data());
    const auto reflection=reflectionProbeConstants(probe.get(),1+zones.size());
    glUniform4fv(glGetUniformLocation(meshProgram_,"reflectionProbe"),int(reflection.size()),reflection[0].data());
    effectiveCompactPbr_=true;
}
WebGlRenderer::GpuMesh& WebGlRenderer::mesh(const MeshDraw& draw) {
    if(auto stale=meshes_.find(draw.mesh.get());stale!=meshes_.end()&&stale->second.source.expired()) {
        eraseMesh(stale->second);meshes_.erase(stale);
    }
    if(!meshes_.contains(draw.mesh.get())) admitUpload(draw.mesh->vertices.size()*sizeof(Vertex)+draw.mesh->indices.size()*sizeof(std::uint32_t));
    auto [it,inserted]=meshes_.try_emplace(draw.mesh.get());auto& gpu=it->second;
    if(inserted) {
        ProfileScope uploadProfile(ProfileCategory::Loading,"Mesh upload");
        gpu.source=draw.mesh;glGenVertexArrays(1,&gpu.vao);glGenBuffers(1,&gpu.vertices);glGenBuffers(1,&gpu.indices);
        glBindVertexArray(gpu.vao);glBindBuffer(GL_ARRAY_BUFFER,gpu.vertices);attributes();glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gpu.indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,draw.mesh->indices.size()*sizeof(std::uint32_t),draw.mesh->indices.data(),GL_STATIC_DRAW);
        gpu.bytes=draw.mesh->vertices.size()*sizeof(Vertex)+draw.mesh->indices.size()*sizeof(std::uint32_t);
        ++counters_.newMeshes;++counters_.uploadCalls;counters_.uploadBytes+=draw.mesh->indices.size()*sizeof(std::uint32_t);
    }
    glBindVertexArray(gpu.vao);glBindBuffer(GL_ARRAY_BUFFER,gpu.vertices);
    if(inserted||draw.morphPose||draw.skinPose||gpu.deformed) {
        auto vertices=std::span<const Vertex>(draw.morphPose ? draw.morphPose->vertices : draw.mesh->vertices);
        if(draw.skinPose) {
            deformed_.assign(vertices.begin(),vertices.end());
            for(std::size_t i=0;i<deformed_.size();++i) {
                const auto& skin=draw.mesh->skin.at(i);Vec3 p{},n{},t{},b{};
                const Vec3 tangent{vertices[i].tangent[0],vertices[i].tangent[1],vertices[i].tangent[2]};
                const auto bitangent=cross(vertices[i].normal,tangent)*vertices[i].tangent[3];
                for(std::size_t j=0;j<4;++j) if(skin.weights[j]!=0) {
                    const auto& bone=draw.skinPose->bones.at(skin.joints[j]);
                    p=p+transformPoint(bone.transform,vertices[i].position)*skin.weights[j];
                    n=n+transformDirection(bone.normal,vertices[i].normal)*skin.weights[j];
                    t=t+transformDirection(bone.transform,tangent)*skin.weights[j];
                    b=b+transformDirection(bone.transform,bitangent)*skin.weights[j];
                }
                deformed_[i].position=p;deformed_[i].normal=n;
                deformed_[i].tangent={t.x,t.y,t.z,dot(cross(n,t),b)<0 ? -1.f : 1.f};
            }
            vertices=deformed_;
        }
        glBufferData(GL_ARRAY_BUFFER,vertices.size_bytes(),vertices.data(),draw.skinPose||draw.morphPose ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        ++counters_.uploadCalls;counters_.uploadBytes+=vertices.size_bytes();
        gpu.deformed=bool(draw.skinPose||draw.morphPose);
    }
    return gpu;
}
GLuint WebGlRenderer::texture(const std::shared_ptr<const Texture>& source) {
    const auto asset=source ? source : Texture::white();
    if(auto stale=textures_.find(asset.get());stale!=textures_.end()&&stale->second.source.expired()) {
        glDeleteTextures(1,&stale->second.image);textures_.erase(stale);
    }
    if(!textures_.contains(asset.get())) admitUpload(asset->pixels.size());
    auto [it,inserted]=textures_.try_emplace(asset.get());auto& gpu=it->second;
    if(inserted) {
        if(!asset->width||!asset->height||std::uint64_t(asset->width)*asset->height*4!=asset->pixels.size()) {
            textures_.erase(it);throw std::invalid_argument("Invalid RGBA texture payload");
        }
        if(asset->width>capabilities_.maxTextureDimension2D||asset->height>capabilities_.maxTextureDimension2D) { textures_.erase(it);throw std::invalid_argument("Texture exceeds WebGL 2 device limit"); }
        gpu.source=asset;glGenTextures(1,&gpu.image);glBindTexture(GL_TEXTURE_2D,gpu.image);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,asset->width,asset->height,0,GL_RGBA,GL_UNSIGNED_BYTE,asset->pixels.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,asset->wrapU==TextureWrap::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,asset->wrapV==TextureWrap::Clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        gpu.bytes=asset->pixels.size();++counters_.newTextures;++counters_.uploadCalls;counters_.uploadBytes+=gpu.bytes;
    }
    glBindTexture(GL_TEXTURE_2D,gpu.image);return gpu.image;
}
void WebGlRenderer::bindFogLights(GLuint program) {
    glUniform3f(glGetUniformLocation(program,"ambient"),scene_->ambient.x,scene_->ambient.y,scene_->ambient.z);
    const auto count=activeLighting_.lightCount;std::array<float,32> positions{},directions{},colors{},cones{};
    for(std::size_t i=0;i<count;++i) {
        const auto& l=activeLighting_.lights[i];const auto k=i*4;
        positions[k]=l.position.x;positions[k+1]=l.position.y;positions[k+2]=l.position.z;positions[k+3]=float(l.type);
        directions[k]=l.direction.x;directions[k+1]=l.direction.y;directions[k+2]=l.direction.z;directions[k+3]=l.range;
        cones[k]=std::cos(l.innerAngle);cones[k+1]=std::cos(l.outerAngle);
        colors[k]=l.color.x;colors[k+1]=l.color.y;colors[k+2]=l.color.z;colors[k+3]=l.intensity;
    }
    glUniform1i(glGetUniformLocation(program,"lightCount"),count);
    if(count) { glUniform4fv(glGetUniformLocation(program,"lightPosition"),count,positions.data());glUniform4fv(glGetUniformLocation(program,"lightDirection"),count,directions.data());glUniform4fv(glGetUniformLocation(program,"lightColor"),count,colors.data());glUniform4fv(glGetUniformLocation(program,"lightCone"),count,cones.data()); }
    glUniform1i(glGetUniformLocation(program,"shadowMaps"),13);
    const auto& plan=activeLighting_.shadows;
    glUniform1i(glGetUniformLocation(program,"shadowCount"),plan.count);
    glUniform1i(glGetUniformLocation(program,"shadowTaps"),quality_.shadowFilterTaps);
    glActiveTexture(GL_TEXTURE13);glBindTexture(GL_TEXTURE_2D_ARRAY,shadowTexture_);glActiveTexture(GL_TEXTURE0);
    for(unsigned j=0;j<plan.count;++j) {
        const auto& map=plan.maps[j];const auto& light=activeLighting_.lights[map.light];const auto suffix="["+std::to_string(j)+"]";
        matrix(program,("shadowMatrix"+suffix).c_str(),map.matrix);
        glUniform4f(glGetUniformLocation(program,("shadowOptions"+suffix).c_str()),float(j),light.shadow.depthBias,light.shadow.normalBias,1.f/shadowSize_);
        glUniform4f(glGetUniformLocation(program,("shadowSelection"+suffix).c_str()),float(map.light),map.nearDistance,map.farDistance,map.cascade?1.f:0.f);
    }
}
void WebGlRenderer::releaseFogGrid(bool lost) {
    if(!lost) {
        glDeleteTextures(1,&fogGridTexture_);
        glDeleteFramebuffers(1,&fogGridFramebuffer_);
        glDeleteTextures(1,&fogGridBuildTexture_);
        glDeleteFramebuffers(1,&fogGridBuildFramebuffer_);
    }
    fogGridTexture_=fogGridFramebuffer_=fogGridBuildTexture_=fogGridBuildFramebuffer_=0;
    fogAtlasWidth_=fogAtlasHeight_=0;
    cloudAtlas_=fogCacheAtlas_=false;
    fogLightCache_.reset();activeFog_={};
}
bool WebGlRenderer::prepareFogGrid(bool required,bool cacheRequired,bool cloudRequired) {
    const bool clouds=cloudRequired&&!cloudGridUnavailable_&&capabilities_.maxTextureDimension2D>=1152;
    const auto cacheBase=clouds?1152u:fog_grid::height;
    const bool cache=cacheRequired&&!fogLightCacheUnavailable_&&capabilities_.maxTextureDimension2D>=cacheBase+fog_light_cache::height;
    if(!required&&!cache&&!clouds) {releaseFogGrid();return false;}
    if(fogGridUnavailable_||!capabilities_.hdrPostProcessing) return false;
    const auto width=cache?fog_light_cache::width:clouds?128u:fog_grid::width;
    const auto height=cache?cacheBase+fog_light_cache::height:clouds?1152u:fog_grid::height;
    if(fogAtlasWidth_!=width||fogAtlasHeight_!=height) releaseFogGrid();
    // The grid producer reads the cache. A separate grid target avoids texture feedback,
    // then its prefixes are copied into the consumer atlas without adding a sampler.
    auto allocate=[](GLuint& texture,GLuint& framebuffer,std::uint32_t w,std::uint32_t h) {
        glGenTextures(1,&texture);
        glActiveTexture(GL_TEXTURE15);glBindTexture(GL_TEXTURE_2D,texture);
        glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA16F,w,h);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    };
    try {
        if(clouds&&!cloudGridProgram_) cloudGridProgram_=program(underwater_vertex_glsl,cloud_grid_fragment_glsl);
        if(required&&!cache&&!fogGridProgram_) {
            std::string source=fog_grid_fragment_glsl;
            source.insert(source.find('\n')+1,"#define FOG_LIGHT_CACHE_BUILD\n");
            fogGridProgram_=program(underwater_vertex_glsl,source.c_str());
        }
        if(cache) {
            if(!fogLightCacheProgram_) fogLightCacheProgram_=program(underwater_vertex_glsl,fog_light_cache_fragment_glsl);
            if(required&&!fogGridCachedProgram_) fogGridCachedProgram_=program(underwater_vertex_glsl,fog_grid_fragment_glsl);
        }
    } catch(const std::exception&) {
        if(cache) {fogLightCacheUnavailable_=true;releaseFogGrid();return prepareFogGrid(required,false,cloudRequired);}
        if(clouds) {cloudGridUnavailable_=true;releaseFogGrid();return prepareFogGrid(required,false,false);}
        fogGridUnavailable_=true;releaseFogGrid();return false;
    }
    if(!fogGridTexture_) {
        if(!allocate(fogGridTexture_,fogGridFramebuffer_,width,height)) {
            glActiveTexture(GL_TEXTURE0);glGetError();releaseFogGrid();
            if(cache) {fogLightCacheUnavailable_=true;return prepareFogGrid(required,false,cloudRequired);}
            if(clouds) {cloudGridUnavailable_=true;return prepareFogGrid(required,false,false);}
            fogGridUnavailable_=true;return false;
        }
        fogAtlasWidth_=width;fogAtlasHeight_=height;
    }
    if(cache&&required&&!fogGridBuildTexture_&&!allocate(fogGridBuildTexture_,fogGridBuildFramebuffer_,fog_grid::width,fog_grid::height)) {
        glActiveTexture(GL_TEXTURE0);glGetError();releaseFogGrid();fogLightCacheUnavailable_=true;
        return prepareFogGrid(required,false,cloudRequired);
    }
    if(!required&&fogGridBuildTexture_) {
        glDeleteTextures(1,&fogGridBuildTexture_);glDeleteFramebuffers(1,&fogGridBuildFramebuffer_);
        fogGridBuildTexture_=fogGridBuildFramebuffer_=0;
    }
    glActiveTexture(GL_TEXTURE0);
    cloudAtlas_=clouds;fogCacheAtlas_=cache;
    counters_.gpuTextureBytes+=std::uint64_t(width)*height*8;
    if(fogGridBuildTexture_) counters_.gpuTextureBytes+=std::uint64_t(fog_grid::width)*fog_grid::height*8;
    // Include the extra atlas padding and feedback-safe staging in the optional cache footprint.
    if(cache) counters_.fogLightCacheBytes=std::uint64_t(width)*height*8-
        (clouds?std::uint64_t(128)*1152*8:required?std::uint64_t(fog_grid::width)*fog_grid::height*8:0)+
        (fogGridBuildTexture_?std::uint64_t(fog_grid::width)*fog_grid::height*8:0);
    return required;
}
bool WebGlRenderer::drawFogLightCache(FogUniform& fog) {
    if(fogLightCacheUnavailable_||!fogCacheAtlas_) {
        fogLightCache_.reset();fog.volumeLighting[2]=0;return false;
    }
    if(!fogLightCache_.prepare(fog,activeLighting_,quality_)) return fogLightCache_.active();
    glUseProgram(fogLightCacheProgram_);fogUniform(fogLightCacheProgram_,fog);bindFogLights(fogLightCacheProgram_);
    glBindFramebuffer(GL_FRAMEBUFFER,fogGridFramebuffer_);
    glUniform1f(glGetUniformLocation(fogLightCacheProgram_,"fogCacheBaseY"),cloudAtlas_?1152.f:float(fog_grid::height));
    glViewport(0,cloudAtlas_?1152:fog_grid::height,fog_light_cache::width,fog_light_cache::height);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
    glBindVertexArray(skyVao_);glDrawArrays(GL_TRIANGLES,0,3);glEnable(GL_DEPTH_TEST);
    if(glGetError()!=GL_NO_ERROR) {
        fogLightCache_.reset();fogLightCacheUnavailable_=true;fog.volumeLighting[2]=0;return false;
    }
    ++counters_.fogLightCacheBuilds;++counters_.passes;++counters_.drawCalls;++counters_.triangles;
    return true;
}
void WebGlRenderer::drawFogGrid(const CameraComponent& camera,float aspect,const FogUniform& fog) {
    const auto producer=fogGridBuildTexture_?fogGridCachedProgram_:fogGridProgram_;
    glUseProgram(producer);fogUniform(producer,fog);bindFogLights(producer);bindFogGrid(producer,camera,aspect);
    matrix(producer,"fogInverseViewProjection",camera.inverseViewProjection(aspect));
    glBindFramebuffer(GL_FRAMEBUFFER,fogGridBuildTexture_?fogGridBuildFramebuffer_:fogGridFramebuffer_);
    glViewport(0,0,fog_grid::width,fog_grid::height);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
    glBindVertexArray(skyVao_);glDrawArrays(GL_TRIANGLES,0,3);
    if(fogGridBuildTexture_) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER,fogGridBuildFramebuffer_);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fogGridFramebuffer_);
        glBlitFramebuffer(0,0,fog_grid::width,fog_grid::height,0,0,fog_grid::width,fog_grid::height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        ++counters_.passes;
    }
    glEnable(GL_DEPTH_TEST);
    ++counters_.passes;++counters_.fogGridPasses;++counters_.drawCalls;++counters_.triangles;
}
void WebGlRenderer::bindFogGrid(GLuint program,const CameraComponent& camera,float aspect) {
    glActiveTexture(GL_TEXTURE15);glBindTexture(GL_TEXTURE_2D,fogGridTexture_);glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(program,"fogGrid"),15);
    glUniform1f(glGetUniformLocation(program,"fogCacheBaseY"),cloudAtlas_?1152.f:float(fog_grid::height));
    matrix(program,"fogViewProjection",camera.viewProjection(aspect));
}
void WebGlRenderer::bindClouds(GLuint program) {
    const std::array<const char*,4> names={"cloud.layer","cloud.eye","cloud.sun","cloud.shadow"};
    const std::array values={activeCloud_.layer,activeCloud_.eye,activeCloud_.sun,activeCloud_.shadow};
    for(unsigned i=0;i<names.size();++i) glUniform4fv(glGetUniformLocation(program,names[i]),1,values[i].data());
    matrix(program,"cloudViewProjection",cloudViewProjection_);matrix(program,"cloudInverseVp",cloudInverseVp_);
    glUniform4fv(glGetUniformLocation(program,"cloudViewport"),1,cloudViewport_.data());
}
void WebGlRenderer::cloudProducerUniforms(GLuint program) {
    glUniform4fv(glGetUniformLocation(program,"skyCloudLayer"),1,activeSky_.cloudLayer.data());
    glUniform4fv(glGetUniformLocation(program,"cloudShape"),1,activeSky_.cloudShape.data());
    glUniform4fv(glGetUniformLocation(program,"cloudOrigin"),1,activeSky_.cloudOrigin.data());
    glUniform4fv(glGetUniformLocation(program,"skySun"),1,activeSky_.sun.data());
    glUniform4fv(glGetUniformLocation(program,"skyClouds"),1,activeSky_.clouds.data());
    glUniform4fv(glGetUniformLocation(program,"skyHorizon"),1,activeSky_.horizon.data());
    glUniform4fv(glGetUniformLocation(program,"cloudEye"),1,activeCloud_.eye.data());
    glUniform4fv(glGetUniformLocation(program,"cloudShadow"),1,activeCloud_.shadow.data());
    matrix(program,"cloudInverseVp",cloudInverseVp_);
    glActiveTexture(GL_TEXTURE1);
    try {texture(skyCloudNoiseTexture(scene_->sky()->clouds.billowing));} catch(...) {glActiveTexture(GL_TEXTURE0);throw;}
    glActiveTexture(GL_TEXTURE0);glUniform1i(glGetUniformLocation(program,"cloudNoiseAtlas"),1);
}
void WebGlRenderer::drawCloudGrid() {
    if(activeCloud_.layer[3]<.5f||!cloudAtlas_) return;
    glUseProgram(cloudGridProgram_);
    try {cloudProducerUniforms(cloudGridProgram_);}
    catch(const UploadDeferred&) {activeCloud_.layer[3]=0;++counters_.deferredAssetUploads;return;}
    glBindFramebuffer(GL_FRAMEBUFFER,fogGridFramebuffer_);glViewport(64,0,64,1152);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
    glBindVertexArray(skyVao_);glDrawArrays(GL_TRIANGLES,0,3);glEnable(GL_DEPTH_TEST);
    if(glGetError()!=GL_NO_ERROR) {activeCloud_.layer[3]=0;cloudGridUnavailable_=true;return;}
    ++counters_.cloudGridPasses;++counters_.passes;++counters_.drawCalls;++counters_.triangles;
}
void WebGlRenderer::drawCloudShadow() {
    if(activeCloud_.sun[3]<=0) return;
    if(!cloudShadowProgram_) cloudShadowProgram_=program(underwater_vertex_glsl,cloud_shadow_fragment_glsl);
    glUseProgram(cloudShadowProgram_);
    try {cloudProducerUniforms(cloudShadowProgram_);}
    catch(const UploadDeferred&) {activeCloud_.sun[3]=0;++counters_.deferredAssetUploads;return;}
    glBindFramebuffer(GL_FRAMEBUFFER,shadowFramebuffer_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,shadowTexture_,0,cloud_grid::shadowLayer);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) {activeCloud_.sun[3]=0;return;}
    glViewport(0,0,shadowSize_,shadowSize_);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_ALWAYS);glClearDepthf(1);glClear(GL_DEPTH_BUFFER_BIT);
    glBindVertexArray(skyVao_);glDrawArrays(GL_TRIANGLES,0,3);glDepthFunc(GL_LEQUAL);
    ++counters_.cloudShadowPasses;++counters_.passes;++counters_.drawCalls;++counters_.triangles;
}
void WebGlRenderer::prepareObjects() {
    for(auto it=bends_.begin();it!=bends_.end();)if(it->second.source.expired()){glDeleteBuffers(1,&it->second.buffer);it=bends_.erase(it);}else ++it;
    frame_.prepareObjects();const auto data=frame_.objectData();glBindBuffer(GL_ARRAY_BUFFER,objects_);
    // WebGL limits attribute stride to 255 bytes. Pack only shader attributes;
    // retain the portable 256-byte staging slots and their dirty-range contract.
    static_assert(sizeof(ObjectUniform)<=255);
    auto upload=[&](std::size_t first,std::size_t count) {
        packedObjects_.resize(count*sizeof(ObjectUniform));
        for(std::size_t i=0;i<count;++i)std::memcpy(packedObjects_.data()+i*sizeof(ObjectUniform),data.data()+(first+i)*RenderFrame::objectStride,sizeof(ObjectUniform));
        if(count){glBufferSubData(GL_ARRAY_BUFFER,first*sizeof(ObjectUniform),packedObjects_.size(),packedObjects_.data());++counters_.uploadCalls;counters_.uploadBytes+=packedObjects_.size();++counters_.objectUploadCalls;counters_.objectUploadBytes+=packedObjects_.size();}
    };
    const auto count=data.size()/RenderFrame::objectStride,bytes=count*sizeof(ObjectUniform);
    if(bytes>objectCapacity_) {
        objectCapacity_=std::max(bytes,objectCapacity_*2+4096);glBufferData(GL_ARRAY_BUFFER,objectCapacity_,nullptr,GL_DYNAMIC_DRAW);++counters_.objectBufferGrowths;upload(0,count);
    } else for(const auto& range:frame_.objectUploads())upload(range.offset/RenderFrame::objectStride,1+(range.bytes-sizeof(ObjectUniform))/RenderFrame::objectStride);
    counters_.changedObjects=frame_.changedObjects();
}
void WebGlRenderer::drawMesh(const MeshDraw& draw,const Mat4& vp,bool capturingSurface) {
    drawMesh(draw,vp,frame_.sceneFrame(),1,0,false,capturingSurface);
}
void WebGlRenderer::drawMesh(const MeshDraw& draw,const Mat4& vp,const SceneFrame& frame,std::uint32_t instances,std::uint32_t firstInstance,bool instanced,bool capturingSurface) {
    const auto& m=draw.material;
    const GLuint surface=(!capturingSurface&&!reflectionPass_&&!shadowPass_&&m.baseColorSurface)
        ? surfaceTexture(m.baseColorSurface) : 0;
    try {
        mesh(draw);
        glUseProgram(meshProgram_);
        glUniform1i(glGetUniformLocation(meshProgram_,"hdrOutput"),hdr_&&!capturingSurface);
        glUniform1i(glGetUniformLocation(meshProgram_,"ribbon"),0);
        glUniform1f(glGetUniformLocation(meshProgram_,"particleSoftDistance"),0);
        glActiveTexture(GL_TEXTURE0);
        if(surface) glBindTexture(GL_TEXTURE_2D,surface);
        else texture(draw.texture);
    }
    catch(const UploadDeferred&) { ++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;return; }
    matrix(meshProgram_,"model",draw.model);matrix(meshProgram_,"normalMatrix",draw.normal);matrix(meshProgram_,"vp",vp);
    glUniform1i(glGetUniformLocation(meshProgram_,"instanceMode"),instanced);
    glUniform1i(glGetUniformLocation(meshProgram_,"gpuWind"),bool(draw.bend&&draw.wind));
    if(instanced) {
        glBindBuffer(GL_ARRAY_BUFFER,objects_);const auto base=std::size_t(firstInstance)*sizeof(ObjectUniform);
        for(GLuint i=0;i<10;++i) {
            const GLuint location=5+i;const std::size_t offset=i<7?std::size_t(i)*16:i==7?128:i==8?144:160;
            glEnableVertexAttribArray(location);glVertexAttribPointer(location,4,GL_FLOAT,GL_FALSE,sizeof(ObjectUniform),reinterpret_cast<void*>(base+offset));glVertexAttribDivisor(location,1);
        }
    } else {for(GLuint i=5;i<15;++i){glDisableVertexAttribArray(i);glVertexAttribDivisor(i,0);}glVertexAttrib4f(14,draw.windPhase,0,0,0);}
    if(draw.bend&&draw.wind) {
        auto found=bends_.find(draw.bend.get());
        if(found==bends_.end()||found->second.source.lock()!=draw.bend) {
            const auto bytes=draw.bend->vertices().size()*16;
            try{admitUpload(bytes);}catch(const UploadDeferred&){++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;return;}
            auto& gpu=bends_[draw.bend.get()];gpu.source=draw.bend;glGenBuffers(1,&gpu.buffer);glBindBuffer(GL_ARRAY_BUFFER,gpu.buffer);glBufferData(GL_ARRAY_BUFFER,bytes,draw.bend->vertices().data(),GL_STATIC_DRAW);++counters_.uploadCalls;counters_.uploadBytes+=bytes;
        }
        glBindBuffer(GL_ARRAY_BUFFER,bends_.at(draw.bend.get()).buffer);glEnableVertexAttribArray(15);glVertexAttribDivisor(15,0);glVertexAttribPointer(15,4,GL_FLOAT,GL_FALSE,16,nullptr);
        const auto& wind=draw.wind->parameters();if(!windValuesValid_||windProgram_!=meshProgram_||windValues_!=wind){windValues_=wind;windValuesValid_=true;windProgram_=meshProgram_;glUniform4fv(glGetUniformLocation(meshProgram_,"windDirection"),1,wind.data());glUniform4fv(glGetUniformLocation(meshProgram_,"windClock"),1,wind.data()+4);++counters_.uploadCalls;counters_.uploadBytes+=32;}
    } else glDisableVertexAttribArray(15);
    glUniform4f(glGetUniformLocation(meshProgram_,"tint"),draw.color.x,draw.color.y,draw.color.z,draw.opacity);
    glUniform1i(glGetUniformLocation(meshProgram_,"layered"),m.layers.enabled);
    if(m.layers.enabled) {
        const auto sources=materialTextures(m);const auto parameters=materialLayerParameters(m.layers);
        glUniform4fv(glGetUniformLocation(meshProgram_,"layerColor"),4,parameters.data());
        glUniform4fv(glGetUniformLocation(meshProgram_,"layerUv"),4,parameters.data()+16);
        glUniform4fv(glGetUniformLocation(meshProgram_,"weightUv"),1,parameters.data()+32);
        glUniform4fv(glGetUniformLocation(meshProgram_,"layerOptions"),1,parameters.data()+36);
        glUniform4fv(glGetUniformLocation(meshProgram_,"layerDetail"),4,parameters.data()+40);
        constexpr std::array units={0,5,6,7,8,9,10,11,12};
        try {for(std::size_t i=0;i<9;++i) {glActiveTexture(GL_TEXTURE0+units[i]);texture(sources[i]);}}
        catch(const UploadDeferred&) {++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;glActiveTexture(GL_TEXTURE0);return;}
        for(int i=0;i<4;++i) {const auto name=std::string("layerDetail")+std::to_string(i);glUniform1i(glGetUniformLocation(meshProgram_,name.c_str()),9+i);}
        glUniform1i(glGetUniformLocation(meshProgram_,"layerOne"),5);glUniform1i(glGetUniformLocation(meshProgram_,"layerTwo"),6);
        glUniform1i(glGetUniformLocation(meshProgram_,"layerThree"),7);glUniform1i(glGetUniformLocation(meshProgram_,"layerWeights"),8);glActiveTexture(GL_TEXTURE0);
    } else if(effectiveCompactPbr_&&!shadowPass_&&m.shader==MaterialShader::Pbr) {
        glActiveTexture(GL_TEXTURE5);
        try {texture(m.metallicRoughnessTexture);}
        catch(const UploadDeferred&) {++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;glActiveTexture(GL_TEXTURE0);return;}
        glUniform1i(glGetUniformLocation(meshProgram_,"layerOne"),5);glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(meshProgram_,"compactBaseSurface"),surface!=0);
    }
    if(!m.layers.enabled) {
        const auto sources=materialTextures(m);
        try {for(int i=0;i<4;++i) {glActiveTexture(GL_TEXTURE9+i);texture(sources[5+i]);const auto name=std::string("layerDetail")+std::to_string(i);glUniform1i(glGetUniformLocation(meshProgram_,name.c_str()),9+i);}}
        catch(const UploadDeferred&) {++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;glActiveTexture(GL_TEXTURE0);return;}
        glActiveTexture(GL_TEXTURE0);
    }
    glUniform1i(glGetUniformLocation(meshProgram_,"shadowPass"),shadowPass_);
    glUniform1i(glGetUniformLocation(meshProgram_,"receivesShadow"),!shadowPass_&&shadowReceiving_&&(instanced||draw.receivesShadow));
    glActiveTexture(GL_TEXTURE13);glBindTexture(GL_TEXTURE_2D_ARRAY,shadowPass_?0:shadowTexture_);
    glUniform1i(glGetUniformLocation(meshProgram_,"shadowMaps"),13);glActiveTexture(GL_TEXTURE0);
    const auto w=waterParameters(m.water);
    glUniform1i(glGetUniformLocation(meshProgram_,"worldWater"),draw.layer==RenderLayer::World);
    glUniform1i(glGetUniformLocation(meshProgram_,"sceneWater"),waterCapture_&&!capturingSurface&&!reflectionPass_&&draw.layer==RenderLayer::World&&((m.shader==MaterialShader::Water&&m.water.usesSceneInputs())||m.advanced.usesSceneInputs()));
    glUniform4fv(glGetUniformLocation(meshProgram_,"transmission"),1,w.data()+16);
    glUniform4fv(glGetUniformLocation(meshProgram_,"absorptionFoam"),1,w.data()+20);
    glUniform4fv(glGetUniformLocation(meshProgram_,"foamColor"),1,w.data()+24);
    // Bind scratch textures only after opaque capture. Never bind an active attachment.
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,waterColor_);glUniform1i(glGetUniformLocation(meshProgram_,"sceneColor"),2);
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,waterDepth_);glUniform1i(glGetUniformLocation(meshProgram_,"sceneDepth"),3);glActiveTexture(GL_TEXTURE0);
    glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D,reflectionPass_ ? waterColor_ : reflectionColor_);glUniform1i(glGetUniformLocation(meshProgram_,"planarReflection"),4);glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(meshProgram_,"water"),m.shader==MaterialShader::Water);
    glUniform1f(glGetUniformLocation(meshProgram_,"waterTime"),float(waterTime_));
    const auto advanced=materialAdvancedParameters(m.advanced);glUniform4fv(glGetUniformLocation(meshProgram_,"advanced"),9,advanced.data());
    if(m.shader==MaterialShader::Water) {
        const auto& interactions=interactionParameters_;
        glUniform1i(glGetUniformLocation(meshProgram_,"interactionCount"),int(interactions.options[0]));
        if(interactions.options[0]>0) glUniform4fv(glGetUniformLocation(meshProgram_,"interactionData"),int(interactions.options[0])*4,interactions.data[0].data());
    }
    glUniform4fv(glGetUniformLocation(meshProgram_,"wave"),1,w.data());
    glUniform4fv(glGetUniformLocation(meshProgram_,"detail"),1,w.data()+4);
    glUniform4fv(glGetUniformLocation(meshProgram_,"reflectionHorizon"),1,w.data()+8);
    glUniform4fv(glGetUniformLocation(meshProgram_,"reflectionZenith"),1,w.data()+12);
    glUniform1f(glGetUniformLocation(meshProgram_,"roughness"),m.roughness);
    glUniform1f(glGetUniformLocation(meshProgram_,"metallic"),m.metallic);
    glUniform1f(glGetUniformLocation(meshProgram_,"normalScale"),m.normalScale);
    glActiveTexture(GL_TEXTURE1);
    if(diffuseGiSource_&&!shadowPass_&&draw.layer==RenderLayer::World&&m.shader==MaterialShader::Pbr) {
        glBindTexture(GL_TEXTURE_2D,diffuseGiTexture_);
    } else {
        try {texture(m.shader==MaterialShader::Water&&m.normalTexture ? m.normalTexture : Texture::flatNormal());}
        catch(const UploadDeferred&) {++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;glActiveTexture(GL_TEXTURE0);return;}
    }
    glUniform1i(glGetUniformLocation(meshProgram_,"waterNormalTexture"),1);glActiveTexture(GL_TEXTURE0);
    // Rendered rows use GL's lower-left origin. Flip after the material UV transform.
    glUniform4f(glGetUniformLocation(meshProgram_,"uvTransform"),m.uvScale.x,
        surface ? -m.uvScale.y : m.uvScale.y,m.uvOffset.x,
        surface ? 1-m.uvOffset.y : m.uvOffset.y);
    glUniform3f(glGetUniformLocation(meshProgram_,"emissive"),m.emissive.x,m.emissive.y,m.emissive.z);
    glUniform1i(glGetUniformLocation(meshProgram_,"unlit"),m.shader==MaterialShader::Unlit);
    glUniform1i(glGetUniformLocation(meshProgram_,"toneMapped"),m.shader!=MaterialShader::Unlit);
    glUniform1i(glGetUniformLocation(meshProgram_,"masked"),m.alphaMode==AlphaMode::Mask);
    glUniform1f(glGetUniformLocation(meshProgram_,"cutoff"),m.alphaCutoff);
    glUniform1i(glGetUniformLocation(meshProgram_,"fogWorld"),draw.layer==RenderLayer::World);glUniform1i(glGetUniformLocation(meshProgram_,"fogAdditive"),draw.blend==BlendMode::Additive);
    blend(draw.blend);glFrontFace((draw.mirrored!=reflectionPass_) ? GL_CW : GL_CCW);
    if(m.doubleSided) glDisable(GL_CULL_FACE);else { glEnable(GL_CULL_FACE);glCullFace(GL_BACK); }
    if(instanced)glDrawElementsInstanced(GL_TRIANGLES,draw.indexCount,GL_UNSIGNED_INT,reinterpret_cast<void*>(std::size_t(draw.firstIndex)*4),instances);
    else glDrawElements(GL_TRIANGLES,draw.indexCount,GL_UNSIGNED_INT,reinterpret_cast<void*>(std::size_t(draw.firstIndex)*4));
    ++counters_.drawCalls;counters_.submittedInstances+=instances;counters_.instancedDrawCalls+=instances>1;counters_.maxInstancesPerDraw=std::max<std::uint64_t>(counters_.maxInstancesPerDraw,instances);counters_.triangles+=std::uint64_t(draw.indexCount/3)*instances;
    if(draw.skinPose) ++counters_.skinnedInstances;
    if(draw.blend!=BlendMode::Opaque) ++counters_.transparentMeshDraws;
}
void WebGlRenderer::drawParticles(const FxBatch& batch,const CameraComponent& camera,const Mat4& vp) {
    glUseProgram(meshProgram_);glUniform1i(glGetUniformLocation(meshProgram_,"instanceMode"),0);glUniform1i(glGetUniformLocation(meshProgram_,"gpuWind"),0);glUniform1i(glGetUniformLocation(meshProgram_,"hdrOutput"),hdr_);glBindVertexArray(particleVao_);glBindBuffer(GL_ARRAY_BUFFER,particleBuffer_);glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(meshProgram_,"ribbon"),0);
    glUniform1i(glGetUniformLocation(meshProgram_,"layered"),0);
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,waterDepth_);glUniform1i(glGetUniformLocation(meshProgram_,"sceneDepth"),3);glActiveTexture(GL_TEXTURE0);
    try { texture(batch.texture); } catch(const UploadDeferred&) { ++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;return; }
    matrix(meshProgram_,"model",Mat4::identity());matrix(meshProgram_,"normalMatrix",Mat4::identity());matrix(meshProgram_,"vp",vp);
    const auto neutralAdvanced=materialAdvancedParameters(MaterialAdvanced{});glUniform4fv(glGetUniformLocation(meshProgram_,"advanced"),9,neutralAdvanced.data());
    glUniform1i(glGetUniformLocation(meshProgram_,"water"),0);glUniform1i(glGetUniformLocation(meshProgram_,"sceneWater"),0);
    glUniform1i(glGetUniformLocation(meshProgram_,"unlit"),1);glUniform1i(glGetUniformLocation(meshProgram_,"masked"),0);
    glUniform1i(glGetUniformLocation(meshProgram_,"fogWorld"),1);glUniform1i(glGetUniformLocation(meshProgram_,"fogAdditive"),batch.blend==BlendMode::Additive);
    glUniform1i(glGetUniformLocation(meshProgram_,"toneMapped"),1);
    glUniform3f(glGetUniformLocation(meshProgram_,"emissive"),0,0,0);blend(batch.blend);glDisable(GL_CULL_FACE);
    const std::array<Vec2,6> corners={Vec2{-.5f,-.5f},{.5f,-.5f},{-.5f,.5f},{-.5f,.5f},{.5f,-.5f},{.5f,.5f}};
    for(std::size_t i=batch.first;i<batch.first+batch.count;++i) {
        const auto& p=fx_.instances()[i];std::array<Vertex,6> vertices;
        for(std::size_t j=0;j<6;++j) {
            const auto corner=corners[j];const float c=std::cos(p.ageRotation[1]),s=std::sin(p.ageRotation[1]);
            const Vec3 origin{p.positionSize[0],p.positionSize[1],p.positionSize[2]};
            const Vec3 right=p.parameters[1]>.5f?Vec3{p.planeRight[0],p.planeRight[1],p.planeRight[2]}:camera.right();
            const Vec3 up=p.parameters[1]>.5f?Vec3{p.planeUp[0],p.planeUp[1],p.planeUp[2]}:camera.up();
            vertices[j].position=origin+right*(c*corner.x*p.positionSize[3]-s*corner.y*(p.positionSize[3]+p.ageRotation[3]))+up*(s*corner.x*p.positionSize[3]+c*corner.y*(p.positionSize[3]+p.ageRotation[3]));
            vertices[j].normal={0,0,1};vertices[j].uv={corner.x+.5f,.5f-corner.y};
        }
        const float phase=p.ageRotation[0]>=1 ? 1 : std::fmod(p.ageRotation[0]*p.atlas[3],1.0f);
        const auto cell=std::min(std::uint32_t(phase*p.atlas[2]),std::uint32_t(p.atlas[2])-1);
        glUniform4f(glGetUniformLocation(meshProgram_,"uvTransform"),1/p.atlas[0],1/p.atlas[1],float(cell%std::uint32_t(p.atlas[0]))/p.atlas[0],float(cell/std::uint32_t(p.atlas[0]))/p.atlas[1]);
        glUniform1f(glGetUniformLocation(meshProgram_,"particleSoftDistance"),p.parameters[0]);
        glUniform4fv(glGetUniformLocation(meshProgram_,"tint"),1,p.color.data());glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,6);
        ++counters_.drawCalls;++counters_.particleDrawCalls;counters_.triangles+=2;++counters_.uploadCalls;counters_.uploadBytes+=sizeof(vertices);
    }
}
void WebGlRenderer::drawRibbon(const FxBatch& batch,const Mat4& vp) {
    glUseProgram(meshProgram_);glUniform1i(glGetUniformLocation(meshProgram_,"instanceMode"),0);glUniform1i(glGetUniformLocation(meshProgram_,"gpuWind"),0);glUniform1i(glGetUniformLocation(meshProgram_,"hdrOutput"),hdr_);glBindVertexArray(ribbonVao_);glBindBuffer(GL_ARRAY_BUFFER,ribbonBuffer_);
    glActiveTexture(GL_TEXTURE0);try{texture(batch.texture);}catch(const UploadDeferred&){++counters_.deferredAssetDraws;return;}
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,waterDepth_);glUniform1i(glGetUniformLocation(meshProgram_,"sceneDepth"),3);glActiveTexture(GL_TEXTURE0);
    matrix(meshProgram_,"model",Mat4::identity());matrix(meshProgram_,"normalMatrix",Mat4::identity());matrix(meshProgram_,"vp",vp);
    const auto neutralAdvanced=materialAdvancedParameters(MaterialAdvanced{});glUniform4fv(glGetUniformLocation(meshProgram_,"advanced"),9,neutralAdvanced.data());
    for(const auto* name:{"layered","water","sceneWater","masked"})glUniform1i(glGetUniformLocation(meshProgram_,name),0);
    for(const auto* name:{"ribbon","unlit","fogWorld","toneMapped"})glUniform1i(glGetUniformLocation(meshProgram_,name),1);
    glUniform1i(glGetUniformLocation(meshProgram_,"fogAdditive"),batch.blend==BlendMode::Additive);
    glUniform1f(glGetUniformLocation(meshProgram_,"particleSoftDistance"),batch.softDistance);
    glUniform4f(glGetUniformLocation(meshProgram_,"uvTransform"),1,1,0,0);glUniform3f(glGetUniformLocation(meshProgram_,"emissive"),0,0,0);
    blend(batch.blend);glDisable(GL_CULL_FACE);const auto vertices=fx_.ribbonVertices().subspan(batch.first,batch.count);
    glBufferData(GL_ARRAY_BUFFER,vertices.size_bytes(),vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,batch.count);
    ++counters_.drawCalls;++counters_.trailDrawCalls;counters_.trailSegments+=batch.count/6;counters_.triangles+=batch.count/3;
    ++counters_.uploadCalls;counters_.uploadBytes+=vertices.size_bytes();counters_.trailUploadBytes+=vertices.size_bytes();
}
void WebGlRenderer::drawGui(double now) {
    if(!gui_) return;
    const double w=browser::canvasCssWidth(),h=browser::canvasCssHeight();
    const Vec2 logical{float(w),float(h)};
    gui_->layout(logical);const auto& list=gui_->draw(now/1000);
    glUseProgram(guiProgram_);glBindVertexArray(guiVao_);glBindBuffer(GL_ARRAY_BUFFER,guiVertices_);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,guiIndices_);
    glBufferData(GL_ARRAY_BUFFER,list.vertices.size()*sizeof(GuiVertex),list.vertices.data(),GL_STREAM_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,list.indices.size()*4,list.indices.data(),GL_STREAM_DRAW);
    glUniform2f(glGetUniformLocation(guiProgram_,"logicalSize"),logical.x,logical.y);
    glViewport(0,0,width_,height_);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);blend(BlendMode::Alpha);glActiveTexture(GL_TEXTURE0);
    for(const auto& cmd:list.commands) {
        const int x=std::max(0,int(std::floor(cmd.clip.x*width_/w))),y=std::max(0,int(std::floor(cmd.clip.y*height_/h)));
        const int right=std::min(int(width_),int(std::ceil((cmd.clip.x+cmd.clip.width)*width_/w))),bottom=std::min(int(height_),int(std::ceil((cmd.clip.y+cmd.clip.height)*height_/h)));
        if(right<=x||bottom<=y) continue;
        glScissor(x,height_-bottom,right-x,bottom-y);texture(cmd.texture);
        glDrawElements(GL_TRIANGLES,cmd.indexCount,GL_UNSIGNED_INT,reinterpret_cast<void*>(std::size_t(cmd.firstIndex)*4));
        ++counters_.guiDrawCalls;counters_.guiTriangles+=cmd.indexCount/3;
    }
    ++counters_.passes;counters_.drawCalls+=counters_.guiDrawCalls;counters_.triangles+=counters_.guiTriangles;
    counters_.guiUploadCalls=2;counters_.guiUploadBytes=list.vertices.size()*sizeof(GuiVertex)+list.indices.size()*4;
    counters_.uploadCalls+=2;counters_.uploadBytes+=counters_.guiUploadBytes;
}
void WebGlRenderer::drawSky(const RenderView& view,float aspect,bool capturingSurface) {
    if(!view.skyEnabled||!scene_->sky()) return;
    const auto& sky=*scene_->sky();const auto payload=prepareSky(sky,*view.camera,aspect,quality_.volumetricCloudSteps);
    GpuMesh* sphere=nullptr;
    glActiveTexture(GL_TEXTURE0);
    try {
        if(sky.geometry==SkyGeometry::Sphere) {MeshDraw draw;draw.mesh=skySphereMesh();sphere=&mesh(draw);}
        texture(sky.mode==SkyMode::Panorama ? sky.panorama : Texture::white());
        if(payload.cloudShape[2]>0) {glActiveTexture(GL_TEXTURE1);texture(skyCloudNoiseTexture(sky.clouds.billowing));glActiveTexture(GL_TEXTURE0);}
    }
    catch(const UploadDeferred&) {
        glActiveTexture(GL_TEXTURE0);activeCloud_={};glUseProgram(meshProgram_);bindClouds(meshProgram_);
        ++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;return;
    }
    glUseProgram(skyProgram_);glUniform1i(glGetUniformLocation(skyProgram_,"hdrOutput"),hdr_&&!capturingSurface);
    auto fog=capturingSurface?prepareRenderFog(scene_->fog(),*view.camera,view.fogEnabled,quality_,true):activeFog_;
    fogUniform(skyProgram_,fog);bindFogLights(skyProgram_);bindFogGrid(skyProgram_,*view.camera,aspect);
    bindClouds(skyProgram_);
    glBindVertexArray(sphere ? sphere->vao : skyVao_);glBindSampler(0,skySampler_);
    const std::array<const char*,10> names={"skyRight","skyUp","skyForward","skyHorizon","skyZenith","skyNadir","skySun","skyAir","skyClouds","skyCloudWind"};
    const std::array values={payload.right,payload.up,payload.forward,payload.horizon,payload.zenith,payload.nadir,payload.sun,payload.air,payload.clouds,payload.cloudWind};
    for(std::size_t i=0;i<names.size();++i) glUniform4fv(glGetUniformLocation(skyProgram_,names[i]),1,values[i].data());
    glUniform4fv(glGetUniformLocation(skyProgram_,"fogRight"),1,payload.fogRight.data());glUniform4fv(glGetUniformLocation(skyProgram_,"fogUp"),1,payload.fogUp.data());
    if(quality_.volumetricCloudSteps>0) {
        glUniform4fv(glGetUniformLocation(skyProgram_,"skyCloudLayer"),1,payload.cloudLayer.data());
        glUniform4fv(glGetUniformLocation(skyProgram_,"cloudShape"),1,payload.cloudShape.data());
        glUniform4fv(glGetUniformLocation(skyProgram_,"cloudOrigin"),1,payload.cloudOrigin.data());
        glUniform1i(glGetUniformLocation(skyProgram_,"cloudNoiseAtlas"),1);
    }
    glUniform1i(glGetUniformLocation(skyProgram_,"panorama"),0);
    glUniform1i(glGetUniformLocation(skyProgram_,"sphereGeometry"),sphere!=nullptr);matrix(skyProgram_,"clipFromDirection",payload.clipFromDirection);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    if(sphere) {glDrawElements(GL_TRIANGLES,skySphereMesh()->indices.size(),GL_UNSIGNED_INT,nullptr);counters_.triangles+=skySphereMesh()->indices.size()/3;}
    else {glDrawArrays(GL_TRIANGLES,0,3);++counters_.triangles;}
    ++counters_.drawCalls;
    glBindSampler(0,0);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
}
void WebGlRenderer::deleteSurface(const GpuSurface& surface) {
    glDeleteFramebuffers(1,&surface.framebuffer);
    glDeleteTextures(1,&surface.color);
    glDeleteRenderbuffers(1,&surface.depth);
}
WebGlRenderer::GpuSurface* WebGlRenderer::prepareSurface(const RenderSurfaceHandle& target) {
    if(target->width()>capabilities_.maxTextureDimension2D || target->height()>capabilities_.maxTextureDimension2D)
        return nullptr;
    auto [entry,inserted]=surfaces_.try_emplace(target.get());
    if(!inserted) return &entry->second;

    GpuSurface candidate;
    candidate.source=target;
    glGenTextures(1,&candidate.color);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D,candidate.color);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,target->width(),target->height());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D,0);
    glGenRenderbuffers(1,&candidate.depth);
    glBindRenderbuffer(GL_RENDERBUFFER,candidate.depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,target->width(),target->height());
    glBindRenderbuffer(GL_RENDERBUFFER,0);
    glGenFramebuffers(1,&candidate.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER,candidate.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,candidate.color,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,candidate.depth);
    const auto status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    const auto error=glGetError();
    if(!candidate.framebuffer || !candidate.color || !candidate.depth ||
       status!=GL_FRAMEBUFFER_COMPLETE || error!=GL_NO_ERROR) {
        deleteSurface(candidate);
        surfaces_.erase(entry);
        return nullptr;
    }
    entry->second=candidate;
    return &entry->second;
}
GLuint WebGlRenderer::surfaceTexture(const RenderSurfaceHandle& target) const {
    const auto entry=surfaces_.find(target.get());
    if(entry==surfaces_.end() || !entry->second.captured || entry->second.source.expired()) return 0;
    return entry->second.color;
}
void WebGlRenderer::drawSurfaces(const ClusterPageUpload& uploadCluster,FrameTiming& timing) {
    for(auto entry=surfaces_.begin();entry!=surfaces_.end();) {
        if(entry->second.source.expired() || !views_.hasSurface(entry->first)) {
            deleteSurface(entry->second);
            entry=surfaces_.erase(entry);
        } else ++entry;
    }
    views_.resolveSurfaces(resolvedSurfaces_);
    for(const auto& resolved:resolvedSurfaces_) {
        const auto& view=views_.views()[resolved.index];
        const auto& camera=*view.camera;
        const auto height=view.target->height();
        const float aspect=resolved.viewport.width/resolved.viewport.height;
        auto* surface=prepareSurface(view.target);
        if(!surface) continue;

        const double collectStart=emscripten_get_now();
        const auto& vp=camera.viewProjection(aspect);
        const DetailView detail{vp,resolved.viewport.width*quality_.detailScale,
            resolved.viewport.height*quality_.detailScale};
        frame_.collect(*scene_,false,camera.visibilityMask(),culling_ ? &camera.frustum(aspect) : nullptr,
            quality_.lod ? &detail : nullptr,uploadCluster);
        const auto& scene=frame_.sceneFrame();
        timing.collectMs+=emscripten_get_now()-collectStart;
        counters_.meshes+=scene.meshCount;
        counters_.meshParticles+=scene.meshParticleCount;
        activeLighting_=lightSelection_[resolved.index].prepare(scene.lights,camera,aspect,quality_,0,false);
        activeCloud_=prepareRenderClouds(scene_->sky(),camera,quality_,view.skyEnabled,0,false);
        if(!cloudAtlas_) activeCloud_.layer[3]=0;
        if(scene_->sky()) activeSky_=prepareSky(*scene_->sky(),camera,aspect,quality_.volumetricCloudSteps);
        cloudViewProjection_=vp;cloudInverseVp_=camera.inverseViewProjection(aspect);
        cloudViewport_={-resolved.viewport.x/resolved.viewport.width,-float(height-resolved.viewport.y-resolved.viewport.height)/resolved.viewport.height,
                        1.f/resolved.viewport.width,1.f/resolved.viewport.height};
        drawCloudGrid();
        counters_.lights+=activeLighting_.lightCount;
        counters_.culledMeshes+=scene.culledMeshes;
        counters_.visibleMeshes+=scene.meshCount-scene.culledMeshes;
        counters_.cameraLodParts+=frame_.cameraLodParts();

        const double encodeStart=emscripten_get_now();
        glBindFramebuffer(GL_FRAMEBUFFER,surface->framebuffer);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glDepthRangef(0,1);
        glClearDepthf(1);
        glClearColor(.018f,.026f,.042f,1);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(resolved.viewport.x,height-resolved.viewport.y-resolved.viewport.height,
            resolved.viewport.width,resolved.viewport.height);
        glEnable(GL_SCISSOR_TEST);
        glScissor(resolved.x,height-resolved.y-resolved.height,resolved.width,resolved.height);
        if(view.clearColor) {
            const auto& color=*view.clearColor;
            glClearColor(color[0],color[1],color[2],color[3]);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        glUseProgram(meshProgram_);
        glUniform1i(glGetUniformLocation(meshProgram_,"reflectionPass"),0);
        glUniform1i(glGetUniformLocation(meshProgram_,"planarActive"),0);
        const auto eye=camera.worldPosition();
        glUniform3f(glGetUniformLocation(meshProgram_,"cameraPosition"),eye.x,eye.y,eye.z);
        fogUniform(meshProgram_,prepareFog(scene_->fog(),camera,view.fogEnabled,0));
        bindFogGrid(meshProgram_,camera,aspect);bindClouds(meshProgram_);
        bindFogLights(meshProgram_);
        for(std::size_t layer=0;layer<std::size_t(RenderLayer::Count);++layer) {
            glClear(GL_DEPTH_BUFFER_BIT);
            ++counters_.passes;
            if(layer==0) drawSky(view,aspect,true);
            for(const auto& batch:frame_.cameraBatches(RenderLayer(layer))) {
                for(std::uint32_t i=0;i<batch.instanceCount;++i) {
                    const auto& draw=frame_.instanceDraw(batch.firstInstance+i);
                    if(draw.material.shader==MaterialShader::Water) continue;
                    if(batch.indexCount) drawMesh(frame_.batchDraw(batch,i),vp,true);
                    else drawMesh(draw,vp,true);
                }
            }
        }
        surface->captured=true;
        ++counters_.renderSurfacePasses;
        timing.encodeMs+=emscripten_get_now()-encodeStart;
    }
}
void WebGlRenderer::render(double updateMs,double intervalMs) {
    ProfileScope profile(ProfileCategory::Render,"WebGL2 render");
    if(!ready_||emscripten_is_webgl_context_lost(context_)) return;
    const double started=emscripten_get_now();FrameTiming timing;timing.updateMs=updateMs;timing.intervalMs=intervalMs;counters_={};counters_.shadowMapResolution=0;counters_.shadowFilterTaps=0;
    uploadAdmission_.begin(uploadBudget_);uploadStarted_=started;budgeted_=true;
    try {
        emscripten_webgl_make_context_current(context_);resizeCanvas();views_.resolve(width_,height_,resolved_);
        uploadDiffuseGi();
        if(diffuseGiTexture_) counters_.gpuTextureBytes+=maxDiffuseGiProbes*sizeof(DiffuseGiProbe);
        bindCompactLighting();
        glUseProgram(meshProgram_);
        glUniform4fv(glGetUniformLocation(meshProgram_,"diffuseGiMetadata[0]"),3,diffuseGiMetadata_[0].data());
        lightSelection_.resize(views_.views().size());reportedLighting_={};
        std::uint32_t waterMask=0;for(const auto& resolved:resolved_) waterMask|=views_.views()[resolved.index].camera->visibilityMask();
        underwaterViews_.resize(resolved_.size());bool submerged=false;
        for(std::size_t i=0;i<resolved_.size();++i) {const auto& camera=*views_.views()[resolved_[i].index].camera;
            underwaterViews_[i]=underwaterView(*scene_,camera.worldPosition(),camera.visibilityMask());submerged|=bool(underwaterViews_[i]);}
        prepareWaterTargets(hdr_||submerged||waterSceneInputsRequired(*scene_,waterMask)||softParticlesRequired(*scene_,waterMask));
        prepareReflectionTargets(waterPlanarReflectionRequired(*scene_,waterMask));
        const auto& fogSettings=scene_->fog();
        const bool fogView=std::any_of(resolved_.begin(),resolved_.end(),[&](const auto& resolved){return views_.views()[resolved.index].fogEnabled;});
        const bool cache=quality_.volumetricFogLightCache&&quality_.volumetricFogSteps&&fogView&&fogSettings&&
            ((fogSettings->volume&&fogSettings->volume->density>0)||std::any_of(fogSettings->additionalVolumes.begin(),fogSettings->additionalVolumes.end(),[](const auto& volume){return volume.density>0;}));
        const auto& cloudSettings=scene_->sky();
        const bool clouds=quality_.volumetricCloudSteps&&cloudSettings&&cloudSettings->clouds.enabled&&cloudSettings->clouds.volumetric&&
            cloudSettings->clouds.extinction>0&&cloudSettings->clouds.coverage>0&&cloudSettings->clouds.density>0&&
            std::any_of(views_.views().begin(),views_.views().end(),[](const auto& view){return view.skyEnabled;});
        cloudShadowsFrame_=clouds&&quality_.volumetricCloudShadows&&shadowsEnabled_&&shadowReceiving_;
        const bool grid=prepareFogGrid(quality_.volumetricFogGrid&&quality_.volumetricFogSteps&&fogHasSingleActiveVolume(fogSettings)&&
            fogView,cache,clouds&&quality_.volumetricCloudDepth);
        ClusterUploadBudget clusterUploads;
        const ClusterPageUpload uploadCluster=[&](const auto& resource,bool pinned) {
            if(const auto it=meshes_.find(resource.get());it!=meshes_.end()&&!it->second.source.expired())return true;
            if(!pinned&&!clusterUploads.admit(*resource))return false;
            MeshDraw draw;draw.mesh=resource;
            try { mesh(draw);return true; }
            catch(const UploadDeferred&) { ++counters_.deferredAssetUploads;return false; }
        };
        drawSurfaces(uploadCluster,timing);
        glBindFramebuffer(GL_FRAMEBUFFER,sampleCount_>1 ? msaaFramebuffer_ : waterCapture_ ? sceneFramebuffer_ : 0);
        glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);glClearColor(.018f,.026f,.042f,1);glClearDepthf(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);
        std::size_t ordinal=0;
        for(const auto& resolved:resolved_) {
            const auto& view=views_.views()[resolved.index];auto& camera=*view.camera;const float aspect=resolved.viewport.width/resolved.viewport.height;
            interactionParameters_=waterInteractionParameters(waterInteractions_,waterTime_,camera.visibilityMask());
            auto fog=prepareRenderFog(scene_->fog(),camera,view.fogEnabled,quality_);
            activeCloud_=prepareRenderClouds(scene_->sky(),camera,quality_,view.skyEnabled,
                std::min(quality_.shadowResolution,capabilities_.maxTextureDimension2D),shadowsEnabled_&&shadowReceiving_);
            if(!cloudAtlas_) activeCloud_.layer[3]=0;
            if(scene_->sky()) activeSky_=prepareSky(*scene_->sky(),camera,aspect,quality_.volumetricCloudSteps);
            cloudViewProjection_=camera.viewProjection(aspect);cloudInverseVp_=camera.inverseViewProjection(aspect);
            cloudViewport_={-resolved.viewport.x/resolved.viewport.width,-float(height_-resolved.viewport.y-resolved.viewport.height)/resolved.viewport.height,
                            1.f/resolved.viewport.width,1.f/resolved.viewport.height};
            fog.volumeOptions[1]=grid&&fog.volumeMinSteps[3]>0?1.f:0.f;
            glUseProgram(meshProgram_);glUniform1i(glGetUniformLocation(meshProgram_,"hdrOutput"),hdr_);fogUniform(meshProgram_,fog);bindFogGrid(meshProgram_,camera,aspect);
            const auto eye=camera.worldPosition();
            glUniform3f(glGetUniformLocation(meshProgram_,"cameraPosition"),eye.x,eye.y,eye.z);
            matrix(meshProgram_,"inverseVp",camera.inverseViewProjection(aspect));
            glUniform4f(glGetUniformLocation(meshProgram_,"waterViewport"),resolved.viewport.x,height_-resolved.viewport.y-resolved.viewport.height,resolved.viewport.width,resolved.viewport.height);
            glUniform2f(glGetUniformLocation(meshProgram_,"waterExtent"),width_,height_);
            const auto cameraWorld=camera.worldMatrix();const auto forward=normalized(Vec3{-cameraWorld.m[8],-cameraWorld.m[9],-cameraWorld.m[10]});
            glUniform3f(glGetUniformLocation(meshProgram_,"viewForward"),forward.x,forward.y,forward.z);
            glViewport(resolved.viewport.x,height_-resolved.viewport.y-resolved.viewport.height,resolved.viewport.width,resolved.viewport.height);
            glScissor(resolved.x,height_-resolved.y-resolved.height,resolved.width,resolved.height);glDepthMask(GL_TRUE);glDepthRangef(0,1);
            if(view.clearColor) { const auto& color=*view.clearColor;glClearColor(color[0],color[1],color[2],color[3]);glClear(GL_COLOR_BUFFER_BIT); }
            const double collectStart=emscripten_get_now();const auto& vp=camera.viewProjection(aspect);const DetailView detail{vp,resolved.viewport.width*quality_.detailScale,resolved.viewport.height*quality_.detailScale};
            frame_.collect(*scene_,instancing_,camera.visibilityMask(),culling_ ? &camera.frustum(aspect) : nullptr,quality_.lod ? &detail : nullptr,uploadCluster);
            prepareObjects();
            const auto& scene=frame_.sceneFrame();fx_.prepare(scene,camera.view());timing.collectMs+=emscripten_get_now()-collectStart;
            const auto shadowResolution=std::min(quality_.shadowResolution,capabilities_.maxTextureDimension2D);
            activeLighting_=lightSelection_[resolved.index].prepare(scene.lights,camera,aspect,quality_,shadowResolution,shadowsEnabled_);
            if(resolved.index==resolved_.front().index) reportedLighting_=activeLighting_;
            counters_.meshParticles+=scene.meshParticleCount;counters_.meshes+=scene.meshCount;counters_.lights+=activeLighting_.lightCount;counters_.culledMeshes+=scene.culledMeshes;counters_.visibleMeshes+=scene.meshCount-scene.culledMeshes;
            counters_.cameraLodParts+=frame_.cameraLodParts();counters_.particles+=fx_.instances().size();
            const double encodeStart=emscripten_get_now();
            drawShadows(uploadCluster);
            drawCloudShadow();
            drawFogLightCache(fog);activeFog_=fog;
            counters_.fogLightCacheSlots+=static_cast<std::uint64_t>(fog.volumeLighting[2]);
            if(fog.volumeOptions[1]>0) drawFogGrid(camera,aspect,fog);
            glUseProgram(meshProgram_);
            fogUniform(meshProgram_,fog);bindFogGrid(meshProgram_,camera,aspect);bindFogLights(meshProgram_);
            glBindFramebuffer(GL_FRAMEBUFFER,sampleCount_>1?msaaFramebuffer_:waterCapture_?sceneFramebuffer_:0);
            glViewport(resolved.viewport.x,height_-resolved.viewport.y-resolved.viewport.height,resolved.viewport.width,resolved.viewport.height);
            glScissor(resolved.x,height_-resolved.y-resolved.height,resolved.width,resolved.height);glEnable(GL_SCISSOR_TEST);
            const auto reflection=waterReflection(scene.meshes,eye);
            glUniform1i(glGetUniformLocation(meshProgram_,"planarActive"),bool(reflection));
            if(reflection) {
                const auto reflectedVp=vp*reflection->mirror;matrix(meshProgram_,"reflectionVp",reflectedVp);
                glUniform4fv(glGetUniformLocation(meshProgram_,"reflectionPlane"),1,reflection->plane.data());
                reflectionPass_=true;glUniform1i(glGetUniformLocation(meshProgram_,"reflectionPass"),1);
                const auto reflectedEye=transformPoint(reflection->mirror,eye);glUniform3f(glGetUniformLocation(meshProgram_,"cameraPosition"),reflectedEye.x,reflectedEye.y,reflectedEye.z);
                const auto mainSky=activeSky_;const auto mainCloud=activeCloud_;
                if(activeCloud_.layer[3]>.5f) {
                    activeCloud_.eye={reflectedEye.x,reflectedEye.y,reflectedEye.z,mainCloud.eye[3]};
                    activeSky_.cloudOrigin[0]+=(reflectedEye.x-eye.x)*activeSky_.cloudOrigin[3];
                    activeSky_.cloudOrigin[1]=reflectedEye.y;
                    activeSky_.cloudOrigin[2]+=(reflectedEye.z-eye.z)*activeSky_.cloudOrigin[3];
                    cloudViewProjection_=reflectedVp;cloudInverseVp_=inverse(reflectedVp);drawCloudGrid();
                }
                glUseProgram(meshProgram_);bindClouds(meshProgram_);
                glBindFramebuffer(GL_FRAMEBUFFER,reflectionFramebuffer_);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glEnable(GL_SCISSOR_TEST);
                glViewport(resolved.viewport.x,height_-resolved.viewport.y-resolved.viewport.height,resolved.viewport.width,resolved.viewport.height);
                for(const auto& batch:frame_.batches(RenderLayer::World)) {
                    const auto& draw=frame_.instanceDraw(batch.firstInstance);
                    if(draw.material.shader!=MaterialShader::Water) drawMesh(draw,reflectedVp,scene,batch.instanceCount,batch.firstInstance,true);
                }
                ++counters_.passes;reflectionPass_=false;glUniform1i(glGetUniformLocation(meshProgram_,"reflectionPass"),0);
                glUniform3f(glGetUniformLocation(meshProgram_,"cameraPosition"),eye.x,eye.y,eye.z);
                activeSky_=mainSky;activeCloud_=mainCloud;
                cloudViewProjection_=vp;cloudInverseVp_=camera.inverseViewProjection(aspect);
                glBindFramebuffer(GL_FRAMEBUFFER,sampleCount_>1 ? msaaFramebuffer_ : waterCapture_ ? sceneFramebuffer_ : 0);
            } else glUniform4f(glGetUniformLocation(meshProgram_,"reflectionPlane"),0,0,0,0);
            drawCloudGrid();
            glUseProgram(meshProgram_);bindClouds(meshProgram_);bindFogGrid(meshProgram_,camera,aspect);
            glBindFramebuffer(GL_FRAMEBUFFER,sampleCount_>1?msaaFramebuffer_:waterCapture_?sceneFramebuffer_:0);
            glViewport(resolved.viewport.x,height_-resolved.viewport.y-resolved.viewport.height,resolved.viewport.width,resolved.viewport.height);
            glScissor(resolved.x,height_-resolved.y-resolved.height,resolved.width,resolved.height);glEnable(GL_SCISSOR_TEST);
            for(std::size_t layer=0;layer<std::size_t(RenderLayer::Count);++layer) {
                glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);++counters_.passes;
                if(layer==0) drawSky(view,aspect);
                const bool splitWater=waterCapture_&&layer==std::size_t(RenderLayer::World)
                    &&std::any_of(scene.meshes.begin(),scene.meshes.end(),[](const MeshDraw& draw){return draw.cameraVisible&&draw.layer==RenderLayer::World&&(draw.material.shader==MaterialShader::Water&&draw.material.water.usesSceneInputs()||draw.material.advanced.usesSceneInputs());});
                const bool splitScreenReflection=meshProgram_==screenReflectionMeshProgram_&&
                    layer==std::size_t(RenderLayer::World)&&camera.projectionMode()==ProjectionMode::Perspective&&hasScreenReflectionReceivers(scene);
                const auto batches=frame_.cameraBatches(static_cast<RenderLayer>(layer));
                auto drawOpaque=[&](bool receivers){
                    for(const auto& batch:batches) {
                        const auto& draw=frame_.instanceDraw(batch.firstInstance);
                        if(splitScreenReflection&&screenReflectionReceiver(draw)!=receivers) continue;
                        if(batch.indexCount)drawMesh(frame_.batchDraw(batch),vp,scene,batch.instanceCount,batch.firstInstance,true);
                        else if(!splitWater||(draw.material.shader!=MaterialShader::Water&&!draw.material.advanced.usesSceneInputs())) drawMesh(draw,vp,scene,batch.instanceCount,batch.firstInstance,true);
                    }
                };
                drawOpaque(false);
                if(splitScreenReflection) {
                    captureWater();++counters_.screenReflectionSnapshots;
                    glUseProgram(meshProgram_);
                    const auto enabled=glGetUniformLocation(meshProgram_,"screenReflectionPass");
                    glUniform1i(enabled,1);drawOpaque(true);glUniform1i(enabled,0);
                }
                const bool softDepth=(layer==std::size_t(RenderLayer::World)&&std::any_of(scene.particles.begin(),scene.particles.end(),[](const ParticleDraw& p){return p.softDistance>0;}))
                    ||(layer==std::size_t(RenderLayer::World)&&std::any_of(scene.ribbons.begin(),scene.ribbons.end(),[](const RibbonDraw& r){return r.softDistance>0;}));
                if(splitWater||softDepth) {
                    captureWater();++counters_.passes;
                    for(const auto& batch:batches) {
                        const auto& draw=frame_.instanceDraw(batch.firstInstance);
                        if(splitWater&&(draw.material.shader==MaterialShader::Water||draw.material.advanced.usesSceneInputs())) drawMesh(draw,vp,scene,batch.instanceCount,batch.firstInstance,true);
                    }
                }
                for(const auto& batch:fx_.batches(static_cast<RenderLayer>(layer))) {
                    if(batch.ribbon)drawRibbon(batch,vp);else if(batch.particle) drawParticles(batch,camera,vp);else drawMesh(scene.meshes[batch.first],vp);
                }
                if(layer==std::size_t(RenderLayer::World)&&underwaterViews_[ordinal]) {
                    captureWater();drawUnderwater(*underwaterViews_[ordinal],camera,resolved);
                }
            }
            ++ordinal;timing.encodeMs+=emscripten_get_now()-encodeStart;
        }
        if(sampleCount_>1||(waterCapture_&&!hdr_)) {
            // View scissor must not clip the full-canvas resolve.
            glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_READ_FRAMEBUFFER,sampleCount_>1 ? msaaFramebuffer_ : sceneFramebuffer_);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,hdr_?sceneFramebuffer_:0);
            glBlitFramebuffer(0,0,width_,height_,0,0,width_,height_,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            ++counters_.passes;
        }
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        if(hdr_)drawPost();
        budgeted_=false;drawGui(started);glDepthMask(GL_TRUE);glDisable(GL_SCISSOR_TEST);
        // Drop stale asset references before pruning backend caches.
        for(auto it=meshes_.begin();it!=meshes_.end();) { if(it->second.source.expired()) { eraseMesh(it->second);it=meshes_.erase(it); }else ++it; }
        for(auto it=textures_.begin();it!=textures_.end();) { if(it->second.source.expired()) { glDeleteTextures(1,&it->second.image);it=textures_.erase(it); }else ++it; }
        counters_.assetUploadBytes=uploadAdmission_.bytes();counters_.assetUploadResources=uploadAdmission_.resources();
        counters_.cachedMeshes=meshes_.size();counters_.cachedTextures=textures_.size();counters_.width=width_;counters_.height=height_;
        for(const auto& [key,gpu]:meshes_) { counters_.gpuMeshBytes+=gpu.bytes;counters_.cpuAssetBytes+=gpu.bytes; }
        for(const auto& [key,gpu]:textures_) { counters_.gpuTextureBytes+=gpu.bytes;counters_.cpuAssetBytes+=gpu.bytes; }
        counters_.gpuTextureBytes+=environmentBytes_;
        for(const auto& [key,surface]:surfaces_) {
            if(const auto target=surface.source.lock()) {
                const auto bytes=std::uint64_t(target->width())*target->height()*4;
                counters_.renderSurfaceBytes+=2*bytes;
                counters_.gpuTextureBytes+=bytes;
                counters_.gpuDepthBytes+=bytes;
            }
        }
        if(reflectionWidth_>1) {counters_.gpuTextureBytes+=std::uint64_t(width_)*height_*(hdr_?8:4);counters_.gpuDepthBytes+=std::uint64_t(width_)*height_*4;}
        if(waterCapture_) {counters_.gpuTextureBytes+=std::uint64_t(width_)*height_*(hdr_?16:8);}
        counters_.frameStorageBytes=frame_.storageBytes()+fx_.storageBytes()+packedObjects_.capacity();counters_.gpuBufferBytes+=objectCapacity_;
        for(const auto& [key,gpu]:bends_)counters_.gpuBufferBytes+=gpu.source.expired()?0:gpu.source.lock()->vertices().size()*16;
        // The browser default framebuffer retains its depth allocation alongside our MSAA depth.
        counters_.gpuDepthBytes+=std::uint64_t(width_)*height_*4*(sampleCount_>1 ? sampleCount_+1 : 1);
        if(waterCapture_) counters_.gpuDepthBytes+=std::uint64_t(width_)*height_*8;
        if(msaaFramebuffer_) counters_.gpuTextureBytes+=std::uint64_t(width_)*height_*(hdr_?8:4)*sampleCount_;
        timing.cpuMs=updateMs+emscripten_get_now()-started;measurements_.recordFrame(timing,counters_);
        if(started-lastReport_>=250) {
            lastReport_=started;refreshQualityReport();
#ifdef NDEBUG
            EM_ASM({ Module.__webengineBuild='Release'; });
#else
            EM_ASM({ Module.__webengineBuild='Debug'; });
#endif
            auto json=measurements_.json(started,"unsupported");json.pop_back();
            json+=",\"lightingSlots\":"+reportedLighting_.diagnosticsJson()+"}";
            gl_performance(json.c_str());
        }
        gl_stats(counters_.meshes,counters_.lights,counters_.passes);
    } catch(const std::exception& e) { if(!emscripten_is_webgl_context_lost(context_))fail(std::string("WebGL 2 rendering failed: ")+e.what()); }
}
void WebGlRenderer::releaseShadows(bool lost) {
    if(!lost) {glDeleteTextures(1,&shadowTexture_);glDeleteFramebuffers(1,&shadowFramebuffer_);}
    shadowTexture_=shadowFramebuffer_=0;shadowSize_=shadowLayers_=0;
}
void WebGlRenderer::drawShadows(const ClusterPageUpload& uploadCluster) {
    const auto size=std::min(quality_.shadowResolution,capabilities_.maxTextureDimension2D),layers=cloudShadowsFrame_?5u:std::max(1u,quality_.maxShadowMaps);
    if(shadowSize_!=size||shadowLayers_!=layers) {
        releaseShadows();glGenTextures(1,&shadowTexture_);glActiveTexture(GL_TEXTURE13);glBindTexture(GL_TEXTURE_2D_ARRAY,shadowTexture_);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY,1,GL_DEPTH_COMPONENT24,size,size,layers);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_COMPARE_MODE,GL_COMPARE_REF_TO_TEXTURE);glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_COMPARE_FUNC,GL_LEQUAL);
        glGenFramebuffers(1,&shadowFramebuffer_);shadowSize_=size;shadowLayers_=layers;glActiveTexture(GL_TEXTURE0);
    }
    const auto& plan=activeLighting_.shadows;
    glActiveTexture(GL_TEXTURE13);glBindTexture(GL_TEXTURE_2D_ARRAY,0);glActiveTexture(GL_TEXTURE0);
    shadowPass_=true;glUseProgram(meshProgram_);glUniform1i(glGetUniformLocation(meshProgram_,"hdrOutput"),hdr_);glUniform1i(glGetUniformLocation(meshProgram_,"reflectionPass"),0);
    glUniform1i(glGetUniformLocation(meshProgram_,"shadowCount"),0);
    glDisable(GL_SCISSOR_TEST);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(1,1);
    glBindFramebuffer(GL_FRAMEBUFFER,shadowFramebuffer_);const GLenum none=GL_NONE;glDrawBuffers(1,&none);glReadBuffer(GL_NONE);glViewport(0,0,size,size);
    for(unsigned j=0;j<plan.count;++j) {
        const auto& map=plan.maps[j];glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,shadowTexture_,0,j);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Cannot create shadow framebuffer");
        glClearDepthf(1);glClear(GL_DEPTH_BUFFER_BIT);++counters_.passes;++counters_.shadowPasses;
        frame_.prepareShadow(j,{map.matrix,float(size)*quality_.detailScale,float(size)*quality_.detailScale},quality_.shadowLod,shadowCulling_,shadowCasting_,uploadCluster);
        for(const auto& batch:frame_.shadowBatches(j)) {
            auto draw=frame_.instanceDraw(batch.instances.firstInstance);draw.mesh=batch.mesh;draw.firstIndex=batch.firstIndex;draw.indexCount=batch.indexCount;
            drawMesh(draw,map.matrix,frame_.sceneFrame(),batch.instances.instanceCount,batch.instances.firstInstance,true);++counters_.shadowDrawCalls;counters_.shadowInstances+=batch.instances.instanceCount;counters_.shadowTriangles+=std::uint64_t(draw.indexCount/3)*batch.instances.instanceCount;
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);shadowPass_=false;
    glUniform1i(glGetUniformLocation(meshProgram_,"shadowPass"),0);
    counters_.shadowMapResolution=size;counters_.shadowFilterTaps=quality_.shadowFilterTaps;
    counters_.gpuDepthBytes=std::uint64_t(size)*size*layers*4;
}

void WebGlRenderer::releasePost(bool lost) {
    if(!lost){glDeleteFramebuffers(1,&bloomFramebuffer_);glDeleteTextures(1,&bloomTexture_);glDeleteProgram(postProgram_);glDeleteVertexArrays(1,&postVao_);}
    bloomFramebuffer_=bloomTexture_=postProgram_=postVao_=0;bloomWidth_=bloomHeight_=0;
}
void WebGlRenderer::drawPost() {
    const auto w=std::max(1u,(width_+3)/4),h=std::max(1u,(height_+3)/4);
    if(bloomWidth_!=w||bloomHeight_!=h) {
        glDeleteTextures(1,&bloomTexture_);glDeleteFramebuffers(1,&bloomFramebuffer_);
        glGenTextures(1,&bloomTexture_);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,bloomTexture_);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,w,h,0,GL_RGBA,GL_HALF_FLOAT,nullptr);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1,&bloomFramebuffer_);glBindFramebuffer(GL_FRAMEBUFFER,bloomFramebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,bloomTexture_,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Cannot create HDR bloom target");
        bloomWidth_=w;bloomHeight_=h;
    }
    glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    glUseProgram(postProgram_);glBindVertexArray(postVao_);const auto& p=quality_.post;
    glUniform4f(glGetUniformLocation(postProgram_,"params"),std::exp2(p.exposure),p.bloomStrength,p.bloomThreshold,0);
    glUniform4f(glGetUniformLocation(postProgram_,"grade"),p.saturation,p.contrast,float(p.toneMapper),0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,sceneColor_);glUniform1i(glGetUniformLocation(postProgram_,"sourceColor"),0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,sceneColor_);glUniform1i(glGetUniformLocation(postProgram_,"bloomColor"),1);
    if(p.bloomStrength>0) {
        glBindFramebuffer(GL_FRAMEBUFFER,bloomFramebuffer_);glViewport(0,0,w,h);glUniform1i(glGetUniformLocation(postProgram_,"extractBloom"),1);glDrawArrays(GL_TRIANGLES,0,3);
        ++counters_.passes;++counters_.drawCalls;++counters_.triangles;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width_,height_);glBindTexture(GL_TEXTURE_2D,bloomTexture_);
    glUniform1i(glGetUniformLocation(postProgram_,"extractBloom"),0);glDrawArrays(GL_TRIANGLES,0,3);
    glActiveTexture(GL_TEXTURE0);glDepthMask(GL_TRUE);++counters_.passes;++counters_.drawCalls;++counters_.triangles;
    counters_.gpuTextureBytes+=std::uint64_t(w)*h*8;
}

}

