#include "webgl_renderer.hpp"
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
EM_JS(double, gl_dpr, (), { return devicePixelRatio || 1; });
EM_JS(int, gl_msaa4x_supported, (), {
    const gl=document.querySelector('#viewport').getContext('webgl2');
    // WebGL exposes the sample array, but not GLES's GL_NUM_SAMPLE_COUNTS query.
    // The opaque (alpha:false) default framebuffer is RGB8; resolve formats must match.
    return [gl.RGB8,gl.DEPTH_COMPONENT24].every(format =>
        Array.from(gl.getInternalformatParameter(gl.RENDERBUFFER,format,gl.SAMPLES) || []).includes(4));
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
out vec4 vertexColor;
uniform mat4 model,normalMatrix,vp;
#ifdef ENGINE_GL_SIMPLE_MATERIAL
const bool water=false;
#else
uniform bool water;
#endif
uniform vec4 reflectionPlane;
flat out float planarSurface;
uniform vec4 wave,detail;
uniform float waterTime;
out vec3 worldTangent,worldBitangent;
out vec3 planeNormal;
out vec3 worldPosition,worldNormal;
out vec2 texcoord;
void main() {
    vertexColor=ribbonColor;
    planarSurface=abs(dot(reflectionPlane,model[3]))<0.01 && abs(dot(normalize((normalMatrix*vec4(0,1,0,0)).xyz),reflectionPlane.xyz))>0.999 ? 1.0 : 0.0;
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
    vec4 world=model*vec4(p,1.0);worldPosition=world.xyz;
    worldNormal=(normalMatrix*vec4(n,0.0)).xyz;texcoord=uv;
    worldTangent=(model*vec4(t,0.0)).xyz;worldBitangent=(model*vec4(b,0.0)).xyz;planeNormal=normalize((normalMatrix*vec4(0,1,0,0)).xyz);
    gl_Position=vp*world;
    gl_Position.z=2.0*gl_Position.z-gl_Position.w;
})";
constexpr const char* meshFragment=R"(#version 300 es
precision highp float;
// FOG_FUNCTIONS
uniform bool fogWorld,fogAdditive;
in vec3 worldPosition,worldNormal;
in vec2 texcoord;
uniform sampler2D baseTexture;
uniform sampler2D waterNormalTexture;
uniform sampler2D layerOne,layerTwo,layerThree,layerWeights;
#ifdef ENGINE_GL_SIMPLE_MATERIAL
const bool layered=false;
#else
uniform bool layered;
#endif
uniform vec4 layerColor[4],layerUv[4],weightUv,layerOptions,layerDetail[4];
uniform sampler2D layerDetail0,layerDetail1,layerDetail2,layerDetail3;
uniform sampler2D planarReflection;
uniform mat4 reflectionVp;
uniform vec4 reflectionPlane;
uniform bool planarActive,worldWater;
#ifdef ENGINE_GL_SIMPLE_MATERIAL
const bool reflectionPass=false;
#else
uniform bool reflectionPass;
#endif
flat in float planarSurface;
uniform int interactionCount;
uniform vec4 interactionData[64];
#ifdef ENGINE_GL_SIMPLE_MATERIAL
const bool water=false;
#else
uniform bool water;
#endif
uniform vec4 wave,detail,reflectionHorizon,reflectionZenith;
uniform float waterTime,roughness,normalScale;
uniform vec3 cameraPosition;
in vec3 worldTangent,worldBitangent,planeNormal;
uniform sampler2D sceneColor,sceneDepth;
uniform bool sceneWater;
uniform float particleSoftDistance;
#ifdef ENGINE_GL_SIMPLE_MATERIAL
const bool ribbon=false;
#else
uniform bool ribbon;
#endif
in vec4 vertexColor;
uniform mat4 inverseVp,vp;
uniform vec4 waterViewport,transmission,absorptionFoam,foamColor;
uniform vec2 waterExtent;
uniform vec3 viewForward;
uniform vec4 tint,uvTransform;
uniform vec3 ambient,emissive;
uniform vec4 lightPosition[8],lightDirection[8],lightColor[8];
uniform int lightCount;
uniform bool unlit,toneMapped;
#ifdef ENGINE_GL_SIMPLE_MATERIAL
const bool masked=false;
#else
uniform bool masked;
#endif
uniform float cutoff;
out vec4 outputColor;
vec3 surfaceFog(vec3 color) {
    if(!fogWorld) return color;
    if(reflectionPass && fog.colorDensity.w>0.0) {
        vec3 n=reflectionPlane.xyz,reflectedForward=reflect(fog.forward.xyz,n);
        vec3 virtualEye=fog.eye.w>0.5 ? worldPosition-reflectedForward*(dot(worldPosition-cameraPosition,reflectedForward)-fog.forward.w) : cameraPosition;
        vec3 delta=worldPosition-virtualEye;
        float denom=dot(n,delta);
        float t=clamp(-dot(reflectionPlane,vec4(virtualEye,1))/(abs(denom)>1e-6 ? denom : 1e-6),0.0,1.0);
        vec3 hit=virtualEye+delta*t,eye=virtualEye-2.0*dot(reflectionPlane,vec4(virtualEye,1))*n;
        float depth=fogDepth(fog,eye,hit,fog.height.x)+fogDepth(fog,hit,worldPosition,max(0.0,fog.height.x-length(hit-eye)));
        return mix(fog.colorDensity.rgb,color,exp(-depth));
    }
    return fogApply(fog,color,worldPosition,fogAdditive);
}
vec3 scenePoint(vec2 pixel,float depth) {
    vec2 ndc=(pixel-waterViewport.xy)/waterViewport.zw*2.0-1.0;
    vec4 world=inverseVp*vec4(ndc,depth,1);return world.xyz/world.w;
}
vec3 waterComposite(vec3 normal,vec3 body,vec3 display,vec2 uv,float time,float nv) {
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
    vec3 transmitted=pow(textureLod(sceneColor,samplePixel/waterExtent,0.0).rgb,vec3(2.2));
    vec3 attenuation=exp(-absorptionFoam.rgb*thickness);
    float f=0.02+0.98*pow(1.0-clamp(nv,0.0,1.0),5.0);
    vec3 result=mix(display,transmitted*attenuation+surfaceFog(clamp(body,vec3(0),vec3(1)))*(vec3(1)-attenuation),transmission.x*(1.0-f*reflectionHorizon.w));
    float gap=max(dot(worldPosition-background,normalize(planeNormal)),0.0);
    float pattern=0.65+0.35*sin(uv.x*absorptionFoam.w*6.2831853+time*0.8)*sin(uv.y*absorptionFoam.w*5.1-time*0.6);
    float foam=transmission.z*(1.0-smoothstep(0.0,transmission.w,gap))*pattern;
    return mix(result,surfaceFog(foamColor.rgb),foam);
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
void main() {
    vec4 effectiveTint=ribbon?vertexColor:tint;
    vec2 sampleUv=texcoord*uvTransform.xy+uvTransform.zw;if(ribbon)sampleUv.x=fract(sampleUv.x);
    vec4 texel=texture(baseTexture,sampleUv);
    vec3 baseLinear=pow(texel.rgb,vec3(2.2));
    vec3 layerNormal=normalize(worldNormal);
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
    if(water) {
        vec3 t=normalize(worldTangent-n*dot(n,worldTangent));
        float signB=dot(cross(n,t),worldBitangent)<0.0 ? -1.0 : 1.0;
        vec3 b=cross(n,t)*signB;
        vec2 r=(ra.xy+rb.xy)*normalScale+0.12*vec2(sin(wuv.x*6.2831853+wt),cos(wuv.y*8.1-wt*1.3));
        n=normalize(n+(t*r.x+b*r.y)*detail.w);
    }
    n=normalize(n-interaction.xyz*.12);
    if(!gl_FrontFacing) n=-n;
    vec3 viewDirection=normalize(cameraPosition-worldPosition);
    float fWater=0.02+0.98*pow(1.0-clamp(dot(n,viewDirection),0.0,1.0),5.0);
    vec3 body=baseLinear*effectiveTint.rgb;
    vec3 waterLighting=body*(ambient+vec3(0.12));
    for(int i=0;i<8;++i) {
        if(i>=lightCount) break;
        vec3 delta=lightPosition[i].xyz-worldPosition;
        float distance=length(delta);
        bool directional=lightPosition[i].w<0.5;
        vec3 direction=directional ? -lightDirection[i].xyz : delta/max(distance,0.0001);
        float attenuation=directional ? 1.0 : pow(max(1.0-distance/max(lightDirection[i].w,0.0001),0.0),2.0);
        lighting+=lightColor[i].rgb*lightColor[i].a*max(dot(n,direction),0.0)*attenuation;
        if(water) {
            vec3 h=(viewDirection+direction)*inversesqrt(max(dot(viewDirection+direction,viewDirection+direction),0.0001));
            float highlight=pow(max(dot(n,h),0.0),mix(256.0,8.0,clamp(roughness,0.045,1.0)))*fWater*reflectionHorizon.w;
            waterLighting+=(body*max(dot(n,direction),0.0)*0.25+vec3(highlight))*lightColor[i].rgb*lightColor[i].a*attenuation;
        }
    }
    vec3 linear=baseLinear*effectiveTint.rgb*(unlit ? vec3(1.0) : lighting)+emissive;
    if(water) {
        vec3 reflection=mix(reflectionHorizon.rgb,reflectionZenith.rgb,clamp(reflect(-viewDirection,n).y,0.0,1.0));
        linear=waterLighting*(1.0-fWater*reflectionHorizon.w)+reflection*fWater*reflectionHorizon.w+emissive;
    }
    vec3 displayLinear=surfaceFog(toneMapped ? linear/(vec3(1.0)+linear) : linear);
    if(water && worldWater && planarActive && planarSurface>0.5 && foamColor.w>0.0) {
        vec4 clip=reflectionVp*vec4(worldPosition,1);
        vec2 uv=clip.xy/max(clip.w,0.0001)*0.5+0.5+n.xz*0.008*(1.0-roughness);
        bool inside=clip.w>0.0 && all(greaterThan(uv,vec2(0))) && all(lessThan(uv,vec2(1))) && clip.z>=0.0 && clip.z<=clip.w;
        vec4 reflected=textureLod(planarReflection,clamp(waterViewport.xy+uv*waterViewport.zw,waterViewport.xy+0.5,waterViewport.xy+waterViewport.zw-0.5)/waterExtent,0.0);
        displayLinear=mix(displayLinear,pow(reflected.rgb,vec3(2.2)),inside ? foamColor.w*reflectionHorizon.w*fWater*reflected.a : 0.0);
    }
    if(water) displayLinear=waterComposite(n,body,displayLinear,texcoord*uvTransform.xy+uvTransform.zw,wt,max(dot(n,viewDirection),0.0))+fogApply(fog,vec3(min(abs(interaction.w)*.18,.25)),worldPosition,true);
    vec3 display=pow(displayLinear,vec3(1.0/2.2));
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
GLuint shader(GLenum type,const char* text,bool simpleMaterial=false) {
    std::string source=text;
    if(simpleMaterial) source.insert(source.find('\n')+1,"#define ENGINE_GL_SIMPLE_MATERIAL 1\n");
    const auto marker=source.find("// FOG_FUNCTIONS");if(marker!=std::string::npos) source.replace(marker,16,fog_glsl);text=source.c_str();
    const auto id=glCreateShader(type);glShaderSource(id,1,&text,nullptr);glCompileShader(id);
    GLint ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
    if(!ok) { char log[2048]{};glGetShaderInfoLog(id,sizeof(log),nullptr,log);glDeleteShader(id);throw std::runtime_error(log); }
    return id;
}
GLuint program(const char* vertex,const char* fragment,bool simpleMaterial=false) {
    const auto v=shader(GL_VERTEX_SHADER,vertex,simpleMaterial);GLuint f=0,p=0;
    try {
        f=shader(GL_FRAGMENT_SHADER,fragment,simpleMaterial);p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
        GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
        if(!ok) { char log[2048]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);throw std::runtime_error(log); }
    } catch(...) { glDeleteShader(v);if(f) glDeleteShader(f);if(p) glDeleteProgram(p);throw; }
    glDeleteShader(v);glDeleteShader(f);return p;
}
void fogUniform(GLuint program,const FogUniform& f) {
    const char* names[]={"fog.colorDensity","fog.height","fog.eye","fog.forward"};
    const std::array values={f.colorDensity,f.height,f.eye,f.forward};
    for(int i=0;i<4;++i) glUniform4fv(glGetUniformLocation(program,names[i]),1,values[i].data());
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
    views_.set(views);scene_=&scene;
}
void WebGlRenderer::setQuality(const RenderQuality& quality) {
    quality.validate();
    if(ready_) {
        emscripten_webgl_make_context_current(context_);
        double w=0,h=0;emscripten_get_element_css_size("#viewport",&w,&h);
        const auto extent=resolveResolution(w,h,gl_dpr(),quality.resolution,capabilities_);
        // Stage and validate targets before replacing valid quality/resources.
        resizeTargets(extent.width,extent.height,resolveMsaaSamples(quality,capabilities_));
    }
    quality_=quality;
    if(ready_) { resizeCanvas();resetMeasurements();refreshQualityReport(); }
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
    skyProgram_=program(sky_vertex_glsl,sky_fragment_glsl);glGenVertexArrays(1,&skyVao_);glGenSamplers(1,&skySampler_);
    glSamplerParameteri(skySampler_,GL_TEXTURE_WRAP_S,GL_REPEAT);glSamplerParameteri(skySampler_,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glSamplerParameteri(skySampler_,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glSamplerParameteri(skySampler_,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    underwaterProgram_=program(underwater_vertex_glsl,underwater_fragment_glsl);glGenVertexArrays(1,&underwaterVao_);
    meshProgram_=program(meshVertex,meshFragment);simpleMeshProgram_=program(meshVertex,meshFragment,true);guiProgram_=program(guiVertex,guiFragment);
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
    capabilities_.backend=RenderBackend::WebGl2;capabilities_.shadowMaps=false;capabilities_.pbr=false;capabilities_.instancing=false;
    capabilities_.msaa4x=gl_msaa4x_supported()!=0;
    ++generation_;ready_=true;failed_=false;resizeCanvas();resetMeasurements();refreshQualityReport();gl_status("WebGL 2 renderer ready",0);
}
void WebGlRenderer::releaseResources(bool lost) {
    releaseWaterTargets(lost);releaseReflectionTargets(lost);
    if(!lost) {
        glDeleteFramebuffers(1,&msaaFramebuffer_);glDeleteRenderbuffers(1,&msaaColor_);glDeleteRenderbuffers(1,&msaaDepth_);
        glDeleteProgram(underwaterProgram_);glDeleteVertexArrays(1,&underwaterVao_);
        glDeleteProgram(skyProgram_);glDeleteVertexArrays(1,&skyVao_);glDeleteSamplers(1,&skySampler_);
        for(auto& [key,value]:meshes_) eraseMesh(value);
        for(auto& [key,value]:textures_) glDeleteTextures(1,&value.image);
        glDeleteProgram(meshProgram_);glDeleteProgram(simpleMeshProgram_);glDeleteProgram(guiProgram_);glDeleteVertexArrays(1,&guiVao_);glDeleteVertexArrays(1,&particleVao_);
        glDeleteBuffers(1,&guiVertices_);glDeleteBuffers(1,&guiIndices_);glDeleteBuffers(1,&particleBuffer_);glDeleteVertexArrays(1,&ribbonVao_);glDeleteBuffers(1,&ribbonBuffer_);
    }
    meshes_.clear();textures_.clear();meshProgram_=simpleMeshProgram_=guiProgram_=guiVao_=guiVertices_=guiIndices_=particleVao_=particleBuffer_=ribbonVao_=ribbonBuffer_=0;
    skyProgram_=skyVao_=skySampler_=0;underwaterProgram_=underwaterVao_=0;underwaterViews_.clear();
    msaaFramebuffer_=msaaColor_=msaaDepth_=0;targetWidth_=targetHeight_=0;sampleCount_=1;capabilities_.msaa4x=false;
}
void WebGlRenderer::resizeTargets(std::uint32_t width,std::uint32_t height,std::uint32_t samples) {
    if(width==targetWidth_&&height==targetHeight_&&samples==sampleCount_) return;
    GLuint framebuffer=0,color=0,depth=0;
    if(samples>1) {
        glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glGenRenderbuffers(1,&color);glBindRenderbuffer(GL_RENDERBUFFER,color);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RGB8,width,height);
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
    targetWidth_=width;targetHeight_=height;sampleCount_=samples;
}
void WebGlRenderer::resizeCanvas() {
    double w=0,h=0;emscripten_get_element_css_size("#viewport",&w,&h);
    const auto extent=resolveResolution(w,h,gl_dpr(),quality_.resolution,capabilities_);
    if(ready_) resizeTargets(extent.width,extent.height,resolveMsaaSamples(quality_,capabilities_));
    int cw=0,ch=0;emscripten_get_canvas_element_size("#viewport",&cw,&ch);
    if(cw!=int(extent.width)||ch!=int(extent.height)) emscripten_set_canvas_element_size("#viewport",extent.width,extent.height);
    width_=extent.width;height_=extent.height;
}
void WebGlRenderer::refreshQualityReport() {
    if(!context_&&!failed_) return; // Do not overwrite the selected backend's report before acquisition.
    const auto& q=quality_;const auto& c=capabilities_;
    std::ostringstream s;
    s<<"{\"backend\":\"webgl2\",\"shadows\":false,\"pbr\":false,\"instancing\":false,\"state\":\""<<(ready_?"ready":failed_?"failed":generation_?"recovering":"initializing")
     <<"\",\"generation\":"<<generation_<<",\"recoveryAttempts\":0,\"maxTextureDimension2D\":"<<c.maxTextureDimension2D
     <<",\"maxTextureArrayLayers\":"<<c.maxTextureArrayLayers<<",\"maxBufferSize\":0,\"timestampQuery\":false,\"compressionBC\":false,\"compressionETC2\":false,\"compressionASTC\":false,\"compressedTextureCount\":0"
     <<",\"width\":"<<width_<<",\"height\":"<<height_<<",\"resolutionScale\":"<<q.resolution.scale<<",\"maxPixelRatio\":"<<q.resolution.maxPixelRatio
     <<",\"maxDimension\":"<<q.resolution.maxDimension<<",\"maxPixels\":"<<q.resolution.maxPixels<<",\"shadowResolution\":0,\"shadowFilterTaps\":0,\"maxShadowMaps\":0"
     <<",\"msaaSamples\":"<<q.msaaSamples<<",\"effectiveMsaaSamples\":"<<sampleCount_<<",\"msaa4x\":"<<(c.msaa4x?"true":"false")
     <<",\"lod\":"<<(q.lod?"true":"false")<<",\"shadowLod\":false,\"compressedTextures\":false}";
    gl_report(s.str().c_str());
}

namespace { struct UploadDeferred {}; }
void WebGlRenderer::admitUpload(std::uint64_t bytes) {
    if(budgeted_&&!uploadAdmission_.admit(bytes,emscripten_get_now()-uploadStarted_)) throw UploadDeferred{};
}
WebGlRenderer::GpuMesh& WebGlRenderer::mesh(const MeshDraw& draw) {
    if(auto stale=meshes_.find(draw.mesh.get());stale!=meshes_.end()&&stale->second.source.expired()) {
        eraseMesh(stale->second);meshes_.erase(stale);
    }
    if(!meshes_.contains(draw.mesh.get())) admitUpload(draw.mesh->vertices.size()*sizeof(Vertex)+draw.mesh->indices.size()*sizeof(std::uint32_t));
    auto [it,inserted]=meshes_.try_emplace(draw.mesh.get());auto& gpu=it->second;
    if(inserted) {
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
void WebGlRenderer::drawMesh(const MeshDraw& draw,const Mat4& vp,const SceneFrame& frame) {
    const auto& m=draw.material;
    const bool simple=m.shader!=MaterialShader::Water&&!m.layers.enabled
        &&m.alphaMode!=AlphaMode::Mask&&!reflectionPass_;
    const auto activeProgram=simple ? simpleMeshProgram_ : meshProgram_;
    try { mesh(draw);glUseProgram(activeProgram);glUniform1i(glGetUniformLocation(activeProgram,"ribbon"),0);glUniform1f(glGetUniformLocation(activeProgram,"particleSoftDistance"),0);glActiveTexture(GL_TEXTURE0);texture(draw.texture); }
    catch(const UploadDeferred&) { ++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;return; }
    matrix(activeProgram,"model",draw.model);matrix(activeProgram,"normalMatrix",draw.normal);matrix(activeProgram,"vp",vp);
    glUniform4f(glGetUniformLocation(activeProgram,"tint"),draw.color.x,draw.color.y,draw.color.z,draw.opacity);
    glUniform1i(glGetUniformLocation(activeProgram,"layered"),m.layers.enabled);
    if(m.layers.enabled) {
        const auto sources=materialTextures(m);const auto parameters=materialLayerParameters(m.layers);
        glUniform4fv(glGetUniformLocation(activeProgram,"layerColor"),4,parameters.data());
        glUniform4fv(glGetUniformLocation(activeProgram,"layerUv"),4,parameters.data()+16);
        glUniform4fv(glGetUniformLocation(activeProgram,"weightUv"),1,parameters.data()+32);
        glUniform4fv(glGetUniformLocation(activeProgram,"layerOptions"),1,parameters.data()+36);
        glUniform4fv(glGetUniformLocation(activeProgram,"layerDetail"),4,parameters.data()+40);
        constexpr std::array units={0,5,6,7,8,9,10,11,12};
        try {for(std::size_t i=0;i<9;++i) {glActiveTexture(GL_TEXTURE0+units[i]);texture(sources[i]);}}
        catch(const UploadDeferred&) {++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;glActiveTexture(GL_TEXTURE0);return;}
        for(int i=0;i<4;++i) {const auto name=std::string("layerDetail")+std::to_string(i);glUniform1i(glGetUniformLocation(activeProgram,name.c_str()),9+i);}
        glUniform1i(glGetUniformLocation(activeProgram,"layerOne"),5);glUniform1i(glGetUniformLocation(activeProgram,"layerTwo"),6);
        glUniform1i(glGetUniformLocation(activeProgram,"layerThree"),7);glUniform1i(glGetUniformLocation(activeProgram,"layerWeights"),8);glActiveTexture(GL_TEXTURE0);
    }
    const auto w=waterParameters(m.water);
    glUniform1i(glGetUniformLocation(activeProgram,"worldWater"),draw.layer==RenderLayer::World);
    glUniform1i(glGetUniformLocation(activeProgram,"sceneWater"),waterCapture_&&draw.layer==RenderLayer::World&&m.shader==MaterialShader::Water&&m.water.usesSceneInputs());
    glUniform4fv(glGetUniformLocation(activeProgram,"transmission"),1,w.data()+16);
    glUniform4fv(glGetUniformLocation(activeProgram,"absorptionFoam"),1,w.data()+20);
    glUniform4fv(glGetUniformLocation(activeProgram,"foamColor"),1,w.data()+24);
    // Bind scratch textures only after opaque capture. Never bind an active attachment.
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,waterColor_);glUniform1i(glGetUniformLocation(activeProgram,"sceneColor"),2);
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,waterDepth_);glUniform1i(glGetUniformLocation(activeProgram,"sceneDepth"),3);glActiveTexture(GL_TEXTURE0);
    glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D,reflectionPass_ ? waterColor_ : reflectionColor_);glUniform1i(glGetUniformLocation(activeProgram,"planarReflection"),4);glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(activeProgram,"water"),m.shader==MaterialShader::Water);
    glUniform1f(glGetUniformLocation(activeProgram,"waterTime"),float(waterTime_));
    if(m.shader==MaterialShader::Water) {
        const auto& interactions=interactionParameters_;
        glUniform1i(glGetUniformLocation(activeProgram,"interactionCount"),int(interactions.options[0]));
        if(interactions.options[0]>0) glUniform4fv(glGetUniformLocation(activeProgram,"interactionData"),int(interactions.options[0])*4,interactions.data[0].data());
    }
    glUniform4fv(glGetUniformLocation(activeProgram,"wave"),1,w.data());
    glUniform4fv(glGetUniformLocation(activeProgram,"detail"),1,w.data()+4);
    glUniform4fv(glGetUniformLocation(activeProgram,"reflectionHorizon"),1,w.data()+8);
    glUniform4fv(glGetUniformLocation(activeProgram,"reflectionZenith"),1,w.data()+12);
    glUniform1f(glGetUniformLocation(activeProgram,"roughness"),m.roughness);
    glUniform1f(glGetUniformLocation(activeProgram,"normalScale"),m.normalScale);
    glActiveTexture(GL_TEXTURE1);
    try {texture(m.shader==MaterialShader::Water&&m.normalTexture ? m.normalTexture : Texture::flatNormal());}
    catch(const UploadDeferred&) {++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;return;}
    glUniform1i(glGetUniformLocation(activeProgram,"waterNormalTexture"),1);glActiveTexture(GL_TEXTURE0);
    glUniform4f(glGetUniformLocation(activeProgram,"uvTransform"),m.uvScale.x,m.uvScale.y,m.uvOffset.x,m.uvOffset.y);
    glUniform3f(glGetUniformLocation(activeProgram,"ambient"),scene_->ambient.x,scene_->ambient.y,scene_->ambient.z);
    glUniform3f(glGetUniformLocation(activeProgram,"emissive"),m.emissive.x,m.emissive.y,m.emissive.z);
    glUniform1i(glGetUniformLocation(activeProgram,"unlit"),m.shader==MaterialShader::Unlit);
    glUniform1i(glGetUniformLocation(activeProgram,"toneMapped"),m.shader!=MaterialShader::Unlit);
    glUniform1i(glGetUniformLocation(activeProgram,"masked"),m.alphaMode==AlphaMode::Mask);
    glUniform1f(glGetUniformLocation(activeProgram,"cutoff"),m.alphaCutoff);
    const auto count=std::min(std::size_t(8),frame.lights.size());std::array<float,32> positions{},directions{},colors{};
    for(std::size_t i=0;i<count;++i) {
        const auto& l=frame.lights[i];const auto k=i*4;
        positions[k]=l.position.x;positions[k+1]=l.position.y;positions[k+2]=l.position.z;positions[k+3]=l.type==LightType::Directional ? 0:1;
        directions[k]=l.direction.x;directions[k+1]=l.direction.y;directions[k+2]=l.direction.z;directions[k+3]=l.range;
        colors[k]=l.color.x;colors[k+1]=l.color.y;colors[k+2]=l.color.z;colors[k+3]=l.intensity;
    }
    glUniform1i(glGetUniformLocation(activeProgram,"lightCount"),count);
    if(count) { glUniform4fv(glGetUniformLocation(activeProgram,"lightPosition"),count,positions.data());glUniform4fv(glGetUniformLocation(activeProgram,"lightDirection"),count,directions.data());glUniform4fv(glGetUniformLocation(activeProgram,"lightColor"),count,colors.data()); }
    glUniform1i(glGetUniformLocation(activeProgram,"fogWorld"),draw.layer==RenderLayer::World);glUniform1i(glGetUniformLocation(activeProgram,"fogAdditive"),draw.blend==BlendMode::Additive);
    blend(draw.blend);glFrontFace((draw.mirrored!=reflectionPass_) ? GL_CW : GL_CCW);
    if(m.doubleSided) glDisable(GL_CULL_FACE);else { glEnable(GL_CULL_FACE);glCullFace(GL_BACK); }
    glDrawElements(GL_TRIANGLES,draw.indexCount,GL_UNSIGNED_INT,reinterpret_cast<void*>(std::size_t(draw.firstIndex)*4));
    ++counters_.drawCalls;++counters_.submittedInstances;counters_.maxInstancesPerDraw=1;counters_.triangles+=draw.indexCount/3;
    if(draw.skinPose) ++counters_.skinnedInstances;
    if(draw.blend!=BlendMode::Opaque) ++counters_.transparentMeshDraws;
}
void WebGlRenderer::drawParticles(const FxBatch& batch,const CameraComponent& camera,const Mat4& vp) {
    glUseProgram(meshProgram_);glBindVertexArray(particleVao_);glBindBuffer(GL_ARRAY_BUFFER,particleBuffer_);glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(meshProgram_,"ribbon"),0);
    glUniform1i(glGetUniformLocation(meshProgram_,"layered"),0);
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,waterDepth_);glUniform1i(glGetUniformLocation(meshProgram_,"sceneDepth"),3);glActiveTexture(GL_TEXTURE0);
    try { texture(batch.texture); } catch(const UploadDeferred&) { ++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;return; }
    matrix(meshProgram_,"model",Mat4::identity());matrix(meshProgram_,"normalMatrix",Mat4::identity());matrix(meshProgram_,"vp",vp);
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
    glUseProgram(meshProgram_);glBindVertexArray(ribbonVao_);glBindBuffer(GL_ARRAY_BUFFER,ribbonBuffer_);
    glActiveTexture(GL_TEXTURE0);try{texture(batch.texture);}catch(const UploadDeferred&){++counters_.deferredAssetDraws;return;}
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,waterDepth_);glUniform1i(glGetUniformLocation(meshProgram_,"sceneDepth"),3);glActiveTexture(GL_TEXTURE0);
    matrix(meshProgram_,"model",Mat4::identity());matrix(meshProgram_,"normalMatrix",Mat4::identity());matrix(meshProgram_,"vp",vp);
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
    double w=0,h=0;emscripten_get_element_css_size("#viewport",&w,&h);const Vec2 logical{float(w),float(h)};
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
void WebGlRenderer::drawSky(const RenderView& view,float aspect) {
    if(!view.skyEnabled||!scene_->sky()) return;
    const auto& sky=*scene_->sky();const auto payload=prepareSky(sky,*view.camera,aspect);
    GpuMesh* sphere=nullptr;
    glActiveTexture(GL_TEXTURE0);
    try {
        if(sky.geometry==SkyGeometry::Sphere) {MeshDraw draw;draw.mesh=skySphereMesh();sphere=&mesh(draw);}
        texture(sky.mode==SkyMode::Panorama ? sky.panorama : Texture::white());
    }
    catch(const UploadDeferred&) { ++counters_.deferredAssetUploads;++counters_.deferredAssetDraws;return; }
    glUseProgram(skyProgram_);fogUniform(skyProgram_,prepareFog(scene_->fog(),*view.camera,view.fogEnabled));glBindVertexArray(sphere ? sphere->vao : skyVao_);glBindSampler(0,skySampler_);
    const std::array<const char*,10> names={"skyRight","skyUp","skyForward","skyHorizon","skyZenith","skyNadir","skySun","skyAir","skyClouds","skyCloudWind"};
    const std::array values={payload.right,payload.up,payload.forward,payload.horizon,payload.zenith,payload.nadir,payload.sun,payload.air,payload.clouds,payload.cloudWind};
    for(std::size_t i=0;i<names.size();++i) glUniform4fv(glGetUniformLocation(skyProgram_,names[i]),1,values[i].data());
    glUniform4fv(glGetUniformLocation(skyProgram_,"fogRight"),1,payload.fogRight.data());glUniform4fv(glGetUniformLocation(skyProgram_,"fogUp"),1,payload.fogUp.data());
    glUniform1i(glGetUniformLocation(skyProgram_,"panorama"),0);
    glUniform1i(glGetUniformLocation(skyProgram_,"sphereGeometry"),sphere!=nullptr);matrix(skyProgram_,"clipFromDirection",payload.clipFromDirection);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    if(sphere) {glDrawElements(GL_TRIANGLES,skySphereMesh()->indices.size(),GL_UNSIGNED_INT,nullptr);counters_.triangles+=skySphereMesh()->indices.size()/3;}
    else {glDrawArrays(GL_TRIANGLES,0,3);++counters_.triangles;}
    ++counters_.drawCalls;
    glBindSampler(0,0);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
}
void WebGlRenderer::render(double updateMs,double intervalMs) {
    if(!ready_) return;
    const double started=emscripten_get_now();FrameTiming timing;timing.updateMs=updateMs;timing.intervalMs=intervalMs;counters_={};counters_.shadowMapResolution=0;counters_.shadowFilterTaps=0;
    uploadAdmission_.begin(uploadBudget_);uploadStarted_=started;budgeted_=true;
    try {
        emscripten_webgl_make_context_current(context_);resizeCanvas();views_.resolve(width_,height_,resolved_);
        std::uint32_t waterMask=0;for(const auto& resolved:resolved_) waterMask|=views_.views()[resolved.index].camera->visibilityMask();
        underwaterViews_.resize(resolved_.size());bool submerged=false;
        for(std::size_t i=0;i<resolved_.size();++i) {const auto& camera=*views_.views()[resolved_[i].index].camera;
            underwaterViews_[i]=underwaterView(*scene_,camera.worldPosition(),camera.visibilityMask());submerged|=bool(underwaterViews_[i]);}
        prepareWaterTargets(submerged||waterSceneInputsRequired(*scene_,waterMask)||softParticlesRequired(*scene_,waterMask));
        prepareReflectionTargets(waterPlanarReflectionRequired(*scene_,waterMask));
        glBindFramebuffer(GL_FRAMEBUFFER,sampleCount_>1 ? msaaFramebuffer_ : waterCapture_ ? sceneFramebuffer_ : 0);
        glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);glClearColor(.018f,.026f,.042f,1);glClearDepthf(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);
        std::size_t ordinal=0;
        for(const auto& resolved:resolved_) {
            const auto& view=views_.views()[resolved.index];auto& camera=*view.camera;const float aspect=resolved.viewport.width/resolved.viewport.height;
            interactionParameters_=waterInteractionParameters(waterInteractions_,waterTime_,camera.visibilityMask());
            const auto eye=camera.worldPosition();
            const auto cameraWorld=camera.worldMatrix();const auto forward=normalized(Vec3{-cameraWorld.m[8],-cameraWorld.m[9],-cameraWorld.m[10]});
            const auto fog=prepareFog(scene_->fog(),camera,view.fogEnabled);
            for(const auto activeProgram:{simpleMeshProgram_,meshProgram_}) {
                glUseProgram(activeProgram);fogUniform(activeProgram,fog);
                glUniform3f(glGetUniformLocation(activeProgram,"cameraPosition"),eye.x,eye.y,eye.z);
                matrix(activeProgram,"inverseVp",camera.inverseViewProjection(aspect));
                glUniform4f(glGetUniformLocation(activeProgram,"waterViewport"),resolved.viewport.x,height_-resolved.viewport.y-resolved.viewport.height,resolved.viewport.width,resolved.viewport.height);
                glUniform2f(glGetUniformLocation(activeProgram,"waterExtent"),width_,height_);
                glUniform3f(glGetUniformLocation(activeProgram,"viewForward"),forward.x,forward.y,forward.z);
            }
            glViewport(resolved.viewport.x,height_-resolved.viewport.y-resolved.viewport.height,resolved.viewport.width,resolved.viewport.height);
            glScissor(resolved.x,height_-resolved.y-resolved.height,resolved.width,resolved.height);glDepthMask(GL_TRUE);glDepthRangef(0,1);
            if(view.clearColor) { const auto& color=*view.clearColor;glClearColor(color[0],color[1],color[2],color[3]);glClear(GL_COLOR_BUFFER_BIT); }
            const double collectStart=emscripten_get_now();const auto& vp=camera.viewProjection(aspect);const DetailView detail{vp,resolved.viewport.width,resolved.viewport.height};
            frame_.collect(*scene_,false,camera.visibilityMask(),culling_ ? &camera.frustum(aspect) : nullptr,quality_.lod ? &detail : nullptr);
            const auto& scene=frame_.sceneFrame();fx_.prepare(scene,camera.view());timing.collectMs+=emscripten_get_now()-collectStart;
            counters_.meshParticles+=scene.meshParticleCount;counters_.meshes+=scene.meshCount;counters_.lights+=std::min(std::size_t(8),scene.lights.size());counters_.culledMeshes+=scene.culledMeshes;counters_.visibleMeshes+=scene.meshCount-scene.culledMeshes;
            counters_.cameraLodParts+=frame_.cameraLodParts();counters_.particles+=fx_.instances().size();
            const double encodeStart=emscripten_get_now();
            const auto reflection=waterReflection(scene.meshes,eye);
            glUniform1i(glGetUniformLocation(meshProgram_,"planarActive"),bool(reflection));
            if(reflection) {
                const auto reflectedVp=vp*reflection->mirror;matrix(meshProgram_,"reflectionVp",reflectedVp);
                glUniform4fv(glGetUniformLocation(meshProgram_,"reflectionPlane"),1,reflection->plane.data());
                reflectionPass_=true;glUniform1i(glGetUniformLocation(meshProgram_,"reflectionPass"),1);
                const auto reflectedEye=transformPoint(reflection->mirror,eye);glUniform3f(glGetUniformLocation(meshProgram_,"cameraPosition"),reflectedEye.x,reflectedEye.y,reflectedEye.z);
                glBindFramebuffer(GL_FRAMEBUFFER,reflectionFramebuffer_);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glEnable(GL_SCISSOR_TEST);
                for(const auto& batch:frame_.batches(RenderLayer::World)) for(std::uint32_t i=0;i<batch.instanceCount;++i) {
                    const auto& draw=frame_.instanceDraw(batch.firstInstance+i);
                    if(draw.material.shader!=MaterialShader::Water) drawMesh(draw,reflectedVp,scene);
                }
                ++counters_.passes;reflectionPass_=false;glUniform1i(glGetUniformLocation(meshProgram_,"reflectionPass"),0);
                glUniform3f(glGetUniformLocation(meshProgram_,"cameraPosition"),eye.x,eye.y,eye.z);
                glBindFramebuffer(GL_FRAMEBUFFER,sampleCount_>1 ? msaaFramebuffer_ : waterCapture_ ? sceneFramebuffer_ : 0);
            } else glUniform4f(glGetUniformLocation(meshProgram_,"reflectionPlane"),0,0,0,0);
            for(std::size_t layer=0;layer<std::size_t(RenderLayer::Count);++layer) {
                glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);++counters_.passes;
                if(layer==0) drawSky(view,aspect);
                const bool splitWater=waterCapture_&&layer==std::size_t(RenderLayer::World)
                    &&std::any_of(scene.meshes.begin(),scene.meshes.end(),[](const MeshDraw& draw){return draw.cameraVisible&&draw.layer==RenderLayer::World&&draw.material.shader==MaterialShader::Water&&draw.material.water.usesSceneInputs();});
                const auto batches=frame_.cameraBatches(static_cast<RenderLayer>(layer));
                for(const auto& batch:batches) for(std::uint32_t i=0;i<batch.instanceCount;++i) {
                    const auto& draw=frame_.instanceDraw(batch.firstInstance+i);
                    if(!splitWater||draw.material.shader!=MaterialShader::Water) drawMesh(draw,vp,scene);
                }
                const bool softDepth=(layer==std::size_t(RenderLayer::World)&&std::any_of(scene.particles.begin(),scene.particles.end(),[](const ParticleDraw& p){return p.softDistance>0;}))
                    ||(layer==std::size_t(RenderLayer::World)&&std::any_of(scene.ribbons.begin(),scene.ribbons.end(),[](const RibbonDraw& r){return r.softDistance>0;}));
                if(splitWater||softDepth) {
                    captureWater();++counters_.passes;
                    for(const auto& batch:batches) for(std::uint32_t i=0;i<batch.instanceCount;++i) {
                        const auto& draw=frame_.instanceDraw(batch.firstInstance+i);
                        if(splitWater&&draw.material.shader==MaterialShader::Water) drawMesh(draw,vp,scene);
                    }
                }
                for(const auto& batch:fx_.batches(static_cast<RenderLayer>(layer))) {
                    if(batch.ribbon)drawRibbon(batch,vp);else if(batch.particle) drawParticles(batch,camera,vp);else drawMesh(scene.meshes[batch.first],vp,scene);
                }
                if(layer==std::size_t(RenderLayer::World)&&underwaterViews_[ordinal]) {
                    captureWater();drawUnderwater(*underwaterViews_[ordinal],camera,resolved);
                }
            }
            ++ordinal;timing.encodeMs+=emscripten_get_now()-encodeStart;
        }
        if(sampleCount_>1||waterCapture_) {
            // View scissor must not clip the full-canvas resolve.
            glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_READ_FRAMEBUFFER,sampleCount_>1 ? msaaFramebuffer_ : sceneFramebuffer_);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
            glBlitFramebuffer(0,0,width_,height_,0,0,width_,height_,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            ++counters_.passes;
        }
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        budgeted_=false;drawGui(started);glDepthMask(GL_TRUE);glDisable(GL_SCISSOR_TEST);
        // Drop stale asset references before pruning backend caches.
        for(auto it=meshes_.begin();it!=meshes_.end();) { if(it->second.source.expired()) { eraseMesh(it->second);it=meshes_.erase(it); }else ++it; }
        for(auto it=textures_.begin();it!=textures_.end();) { if(it->second.source.expired()) { glDeleteTextures(1,&it->second.image);it=textures_.erase(it); }else ++it; }
        counters_.assetUploadBytes=uploadAdmission_.bytes();counters_.assetUploadResources=uploadAdmission_.resources();
        counters_.cachedMeshes=meshes_.size();counters_.cachedTextures=textures_.size();counters_.width=width_;counters_.height=height_;
        for(const auto& [key,gpu]:meshes_) { counters_.gpuMeshBytes+=gpu.bytes;counters_.cpuAssetBytes+=gpu.bytes; }
        for(const auto& [key,gpu]:textures_) { counters_.gpuTextureBytes+=gpu.bytes;counters_.cpuAssetBytes+=gpu.bytes; }
        if(reflectionWidth_>1) {counters_.gpuTextureBytes+=std::uint64_t(width_)*height_*4;counters_.gpuDepthBytes+=std::uint64_t(width_)*height_*4;}
        if(waterCapture_) {counters_.gpuTextureBytes+=std::uint64_t(width_)*height_*8;}
        counters_.frameStorageBytes=frame_.storageBytes()+fx_.storageBytes();
        // The browser default framebuffer retains its depth allocation alongside our MSAA depth.
        counters_.gpuDepthBytes+=std::uint64_t(width_)*height_*4*(sampleCount_>1 ? sampleCount_+1 : 1);
        if(waterCapture_) counters_.gpuDepthBytes+=std::uint64_t(width_)*height_*8;
        if(msaaFramebuffer_) counters_.gpuTextureBytes+=std::uint64_t(width_)*height_*4*sampleCount_;
        timing.cpuMs=updateMs+emscripten_get_now()-started;measurements_.recordFrame(timing,counters_);
        if(started-lastReport_>=250) {
            lastReport_=started;refreshQualityReport();
#ifdef NDEBUG
            EM_ASM({ Module.__webengineBuild='Release'; });
#else
            EM_ASM({ Module.__webengineBuild='Debug'; });
#endif
            gl_performance(measurements_.json(started,"unsupported").c_str());
        }
        gl_stats(counters_.meshes,counters_.lights,counters_.passes);
    } catch(const std::exception& e) { fail(std::string("WebGL 2 rendering failed: ")+e.what()); }
}
}

