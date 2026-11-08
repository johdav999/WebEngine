// Pipeline constants mirror the effective material/pipeline key. Compiling out
// unused paths retains the same material math without general shader overhead.
override materialShader: u32 = 0;
override materialAlpha: u32 = 0;
override materialLayered: bool = false;
override materialNormalMap: bool = false;
override materialSurfaceMap: bool = false;
override materialOcclusionMap: bool = false;
override materialEmissiveMap: bool = false;
override materialReflectionPass: bool = false;
override materialScreenReflection: bool = false;
// FOG_FUNCTIONS
// FOG_FRAME
struct Object { model: mat4x4f, normalMatrix: mat4x4f, color: vec4f, shadow: vec4f };
struct Wind { direction: vec4f, clock: vec4f };
@group(2) @binding(1) var<uniform> wind: Wind;
struct Material { surface: vec4f, emissiveCutoff: vec4f, uv: vec4f, flags: vec4f, wave: vec4f, detail: vec4f, horizon: vec4f, zenith: vec4f, transmission: vec4f, absorptionFoam: vec4f, foamColor: vec4f, layerColor: array<vec4f,4>, layerUv: array<vec4f,4>, weightUv: vec4f, layerOptions: vec4f, layerDetail: array<vec4f,4>, advanced: array<vec4f,9> };
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var specularEnvironment: texture_cube_array<f32>;
@group(0) @binding(2) var diffuseEnvironment: texture_cube_array<f32>;
@group(0) @binding(3) var brdfLut: texture_2d<f32>;
@group(0) @binding(4) var environmentSampler: sampler;
@group(0) @binding(5) var shadowMaps: texture_depth_2d_array;
@group(0) @binding(6) var shadowSampler: sampler_comparison;
@group(0) @binding(7) var sceneColor: texture_2d<f32>;
@group(0) @binding(8) var sceneDepth: texture_2d<f32>;
@group(0) @binding(9) var sceneSampler: sampler;
@group(0) @binding(10) var planarReflection: texture_2d<f32>;
@group(0) @binding(13) var fogLightCache:texture_3d<f32>;
@group(0) @binding(14) var fogLightCacheSampler:sampler;
@group(1) @binding(0) var<uniform> material: Material;
@group(1) @binding(1) var baseSampler: sampler;
@group(1) @binding(2) var baseTexture: texture_2d<f32>;
@group(1) @binding(3) var surfaceSampler: sampler;
@group(1) @binding(4) var surfaceTexture: texture_2d<f32>;
@group(1) @binding(5) var normalSampler: sampler;
@group(1) @binding(6) var normalTexture: texture_2d<f32>;
@group(1) @binding(7) var occlusionSampler: sampler;
@group(1) @binding(8) var occlusionTexture: texture_2d<f32>;
@group(1) @binding(9) var emissiveSampler: sampler;
@group(1) @binding(10) var emissiveTexture: texture_2d<f32>;
struct Bone { transform: mat4x4f, normalMatrix: mat4x4f };
@group(2) @binding(0) var<storage,read> bones: array<Bone>;
struct VertexOutput {
    @builtin(position) clip: vec4f,
    @location(0) world: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) @interpolate(flat) color: vec4f,
    @location(4) tangent: vec4f,
    @location(5) @interpolate(flat) receivesShadow: f32,
    @location(6) @interpolate(flat) sceneWater: f32,
    @location(7) @interpolate(flat) planeNormal: vec3f,
    @location(8) @interpolate(flat) planarSurface: f32,
    @location(10) @interpolate(flat) fogMode:f32,
    @location(9) @interpolate(flat) interactionSurface: f32,
};
fn surfaceFog(color:vec3f,input:VertexOutput) -> vec3f {
    if(input.fogMode<0.5) {return color;}
    if(frame.fog.volumeMinSteps.w>0) {
        var medium:vec4f;
        if(materialReflectionPass) {
            let n=frame.reflectionPlane.xyz;
            let p=input.world;
            let forward=reflect(frame.fog.forward.xyz,n);
            let camera=frame.cameraEnvironment.xyz;
            let virtualEye=select(camera,p-forward*(dot(p-camera,forward)-frame.fog.forward.w),frame.fog.eye.w>0.5);
            let delta=p-virtualEye;
            let denom=dot(n,delta);
            let hit=virtualEye+delta*clamp(-dot(frame.reflectionPlane,vec4f(virtualEye,1))/select(1e-6,denom,abs(denom)>1e-6),0,1);
            let eye=virtualEye-2*dot(frame.reflectionPlane,vec4f(virtualEye,1))*n;
            let first=fogVolumeSegment(eye,hit,0);
            let second=fogVolumeSegment(hit,p,length(hit-eye));
            medium=vec4f(first.rgb+first.a*second.rgb,first.a*second.a);
        } else {
            return fogVolumeApply(color,input.world,input.fogMode>1.5);
        }
        return color*medium.a+select(medium.rgb,vec3f(0),input.fogMode>1.5);
    }
    if(materialReflectionPass && frame.fog.colorDensity.w>0) {
        // The virtual reflected eye defines the hit, but density is integrated on the real two-leg path.
        let n=frame.reflectionPlane.xyz;let p=input.world;
        let reflectedForward=reflect(frame.fog.forward.xyz,n);
        let virtualEye=select(frame.cameraEnvironment.xyz,p-reflectedForward*(dot(p-frame.cameraEnvironment.xyz,reflectedForward)-frame.fog.forward.w),frame.fog.eye.w>0.5);
        let delta=p-virtualEye;let denom=dot(n,delta);
        let t=clamp(-dot(frame.reflectionPlane,vec4f(virtualEye,1))/select(1e-6,denom,abs(denom)>1e-6),0,1);
        let hit=virtualEye+delta*t;
        let eye=virtualEye-2*dot(frame.reflectionPlane,vec4f(virtualEye,1))*n;
        let first=length(hit-eye);
        let depth=fogDepth(frame.fog,eye,hit,frame.fog.height.x)+fogDepth(frame.fog,hit,p,max(0.0,frame.fog.height.x-first));
        return mix(frame.fog.colorDensity.rgb,color,exp(-depth));
    }
    return fogApply(frame.fog,color,input.world,input.fogMode>1.5);
}
// FOG_VOLUME
@group(0) @binding(11) var<storage,read> fogGrid:array<vec2u>;
// FOG_GRID_SAMPLING
// DIFFUSE_GI
fn safeNormal(v: vec3f, fallback: vec3f) -> vec3f { return select(fallback,v*inverseSqrt(max(dot(v,v),1e-12)),dot(v,v)>1e-12); }
fn outputVertex(p: vec3f,n: vec3f,t: vec3f,b: vec3f,uv: vec2f,o: Object) -> VertexOutput {
    var out: VertexOutput;
    var position=p;var surfaceNormal=n;var surfaceTangent=t;var surfaceBitangent=b;
    if(f32(materialShader)>1.5) {
        var dx=vec3f(1,0,0);var dz=vec3f(0,0,1);
        for(var i=0;i<3;i++) {
            let angle=f32(i)*1.17;let c=cos(angle);let s=sin(angle);
            let d=vec2f(c*material.detail.x-s*material.detail.y,s*material.detail.x+c*material.detail.y);
            let weight=select(select(0.15,0.3,i==1),0.55,i==0);
            let scale=select(select(0.31,0.57,i==1),1.0,i==0);
            let k=6.2831853/(material.wave.y*scale);let a=material.wave.x*weight;
            let q=min(material.wave.w,0.8/max(3*k*a,0.0001));
            let phase=k*dot(d,p.xz)-sqrt(9.81*k)*frame.waterClock.x*material.wave.z;
            let sn=sin(phase);let cs=cos(phase);
            position+=vec3f(q*a*d.x*cs,a*sn,q*a*d.y*cs);
            dx+=vec3f(-q*a*k*d.x*d.x*sn,a*k*d.x*cs,-q*a*k*d.x*d.y*sn);
            dz+=vec3f(-q*a*k*d.x*d.y*sn,a*k*d.y*cs,-q*a*k*d.y*d.y*sn);
        }
        surfaceNormal=safeNormal(cross(dz,dx),vec3f(0,1,0));surfaceTangent=dx;surfaceBitangent=-dz;
    }
    if(material.advanced[5].w>0) {
        let direction=normalize(material.advanced[6].xyz);let q=vec3f(.73,.19,.51)*material.advanced[6].w;
        let phase=dot(position,q)+frame.waterClock.x*material.advanced[7].x;
        let gradient=q*(material.advanced[5].w*cos(phase));
        position+=direction*(material.advanced[5].w*sin(phase));
        surfaceTangent+=direction*dot(gradient,surfaceTangent);surfaceBitangent+=direction*dot(gradient,surfaceBitangent);
        let warped=cross(surfaceTangent,surfaceBitangent);surfaceNormal=safeNormal(warped*select(1.0,-1.0,dot(warped,surfaceNormal)<0),surfaceNormal);
    }
    out.interactionSurface=o.shadow.z;
    out.planarSurface=select(0.0,1.0,o.shadow.z>0.5 && abs(dot(frame.reflectionPlane,o.model[3]))<0.01 && abs(dot(normalize((o.normalMatrix*vec4f(0,1,0,0)).xyz),frame.reflectionPlane.xyz))>0.999);
    let world=o.model*vec4f(position,1);out.clip=frame.viewProjection*world;out.world=world.xyz;
    let wn=safeNormal((o.normalMatrix*vec4f(surfaceNormal,0)).xyz,vec3f(0,1,0));
    let wt=(o.model*vec4f(surfaceTangent,0)).xyz;let wb=(o.model*vec4f(surfaceBitangent,0)).xyz;
    let tangent=safeNormal(wt-wn*dot(wn,wt),vec3f(1,0,0));
    out.normal=wn;out.tangent=vec4f(tangent,select(1.0,-1.0,dot(cross(wn,tangent),wb)<0));
    out.fogMode=o.shadow.w;out.uv=uv+material.advanced[5].yz*frame.waterClock.x;out.color=o.color;out.receivesShadow=o.shadow.x;out.sceneWater=o.shadow.y;out.planeNormal=safeNormal((o.normalMatrix*vec4f(0,1,0,0)).xyz,vec3f(0,1,0));return out;
}
@vertex fn vertexMain(@location(0) p: vec3f,@location(1) n: vec3f,@location(2) uv: vec2f,
    @location(3) m0: vec4f,@location(4) m1: vec4f,@location(5) m2: vec4f,@location(6) m3: vec4f,
    @location(7) n0: vec4f,@location(8) n1: vec4f,@location(9) n2: vec4f,@location(10) n3: vec4f,
    @location(11) color: vec4f,@location(14) tangent: vec4f,@location(15) shadow: vec4f) -> VertexOutput {
    return outputVertex(p,n,tangent.xyz,cross(n,tangent.xyz)*tangent.w,uv,Object(mat4x4f(m0,m1,m2,m3),mat4x4f(n0,n1,n2,n3),color,shadow));
}
@vertex fn skinnedVertexMain(@location(0) p: vec3f,@location(1) n: vec3f,@location(2) uv: vec2f,
    @location(3) m0: vec4f,@location(4) m1: vec4f,@location(5) m2: vec4f,@location(6) m3: vec4f,
    @location(7) n0: vec4f,@location(8) n1: vec4f,@location(9) n2: vec4f,@location(10) n3: vec4f,
    @location(11) color: vec4f,@location(12) joints: vec4u,@location(13) weights: vec4f,@location(14) tangent: vec4f,@location(15) shadow: vec4f) -> VertexOutput {
    var position=vec3f(0);var normal=vec3f(0);var t=vec3f(0);var b=vec3f(0);
    for(var i=0u;i<4u;i++) {
        let bone=bones[joints[i]];
        position+=weights[i]*(bone.transform*vec4f(p,1)).xyz;
        normal+=weights[i]*(bone.normalMatrix*vec4f(n,0)).xyz;
        t+=weights[i]*(bone.transform*vec4f(tangent.xyz,0)).xyz;
        b+=weights[i]*(bone.transform*vec4f(cross(n,tangent.xyz)*tangent.w,0)).xyz;
    }
    return outputVertex(position,normal,t,b,uv,Object(mat4x4f(m0,m1,m2,m3),mat4x4f(n0,n1,n2,n3),color,shadow));
}
@vertex fn windVertexMain(@location(0) p: vec3f,@location(1) n: vec3f,@location(2) uv: vec2f,
    @location(3) m0: vec4f,@location(4) m1: vec4f,@location(5) m2: vec4f,@location(6) m3: vec4f,
    @location(7) n0: vec4f,@location(8) n1: vec4f,@location(9) n2: vec4f,@location(10) n3: vec4f,
    @location(11) color: vec4f,@location(12) bend: vec4f,@location(13) plant: vec4f,@location(14) tangent: vec4f,@location(15) shadow: vec4f) -> VertexOutput {
    let object=Object(mat4x4f(m0,m1,m2,m3),mat4x4f(n0,n1,n2,n3),color,shadow);
    var out=outputVertex(p,n,tangent.xyz,cross(n,tangent.xyz)*tangent.w,uv,object);
    let amount=wind.direction.w*sin(wind.clock.x+plant.x)*(1-wind.clock.z+wind.clock.z*0.5*(1+sin(wind.clock.y+plant.x*1.37)));
    let offset=wind.direction.xyz*amount;
    let gradient=(object.normalMatrix*vec4f(bend.yzw*(2*bend.x),0)).xyz;
    let denominator=1+dot(gradient,offset);
    let safeDenominator=select(select(-0.05,0.05,denominator>=0),denominator,abs(denominator)>=0.05);
    let normal=safeNormal(out.normal-gradient*(dot(offset,out.normal)/safeDenominator),out.normal);
    let oldTangent=out.tangent.xyz;let oldBitangent=cross(out.normal,oldTangent)*out.tangent.w;
    let movedTangent=oldTangent+offset*dot(gradient,oldTangent);let movedBitangent=oldBitangent+offset*dot(gradient,oldBitangent);
    let t=safeNormal(movedTangent-normal*dot(normal,movedTangent),oldTangent);
    out.normal=normal;out.tangent=vec4f(t,select(1.0,-1.0,dot(cross(normal,t),movedBitangent)<0));
    out.world+=offset*(bend.x*bend.x);out.clip=frame.viewProjection*vec4f(out.world,1);return out;
}
// Share vertex skinning and material alpha/UVs with the color pass.
struct LayerSample { color: vec4f, normal: vec3f, height: f32 };
fn sampleLayer(colorMap:texture_2d<f32>,colorSampler:sampler,detailMap:texture_2d<f32>,detailSampler:sampler,index:u32,uv:vec2f,input:VertexOutput) -> LayerSample {
    let transform=material.layerUv[index];let options=material.layerDetail[index];
    let n=safeNormal(input.normal,vec3f(0,1,0));
    let t=safeNormal(input.tangent.xyz-n*dot(n,input.tangent.xyz),vec3f(1,0,0));let b=cross(n,t)*input.tangent.w;
    var color=vec4f(0);var normal=vec3f(0);var height=0.0;
    if(material.layerOptions.x>1.5) {
        let axis=pow(abs(n),vec3f(material.layerOptions.w));let w=axis/max(dot(axis,vec3f(1)),1e-6);
        let signN=select(vec3f(-1),vec3f(1),n>=vec3f(0));
        let p=input.world*material.layerOptions.z;
        let coord=array<vec2f,3>(vec2f(-p.z*signN.x,p.y),vec2f(p.x,-p.z*signN.y),vec2f(p.x*signN.z,p.y));
        let tangents=array<vec3f,3>(vec3f(0,0,-signN.x),vec3f(1,0,0),vec3f(signN.z,0,0));
        let bitangents=array<vec3f,3>(vec3f(0,1,0),vec3f(0,0,-signN.y),vec3f(0,1,0));
        for(var k=0u;k<3u;k++) {
            let q=(coord[k]*material.uv.xy+material.uv.zw)*transform.xy+transform.zw;
            let packed=textureSample(detailMap,detailSampler,q);
            let v=(packed.xyz*2-1)*vec3f(options.x*options.w,options.x*options.w,1);
            let orient=sign(transform.xy*material.uv.xy);
            color+=textureSample(colorMap,colorSampler,q)*w[k];height+=packed.a*w[k];
            normal+=(n*v.z+tangents[k]*v.x*orient.x+bitangents[k]*v.y*orient.y)*w[k];
        }
    } else {
        let q=uv*transform.xy+transform.zw;let packed=textureSample(detailMap,detailSampler,q);
        color=textureSample(colorMap,colorSampler,q);height=packed.a;
        let v=(packed.xyz*2-1)*vec3f(options.x*options.w,options.x*options.w,1);let orient=sign(transform.xy*material.uv.xy);
        normal=t*v.x*orient.x+b*v.y*orient.y+n*v.z;
    }
    return LayerSample(color,safeNormal(normal,n),(height*options.y+options.z)*options.w);
}
struct LayerSurface { color: vec4f, roughness: f32, normal: vec3f };
fn layerSurface(uv: vec2f,input:VertexOutput) -> LayerSurface {
    var weights=textureSample(emissiveTexture,emissiveSampler,clamp(uv*material.weightUv.xy+material.weightUv.zw,vec2f(0),vec2f(1)));
    let total=dot(weights,vec4f(1));weights=select(vec4f(1,0,0,0),weights/max(total,0.00001),total>0.00001);
    let a=sampleLayer(baseTexture,baseSampler,layerDetailTexture0,layerDetailSampler0,0u,uv,input);
    let b=sampleLayer(surfaceTexture,surfaceSampler,layerDetailTexture1,layerDetailSampler1,1u,uv,input);
    let c=sampleLayer(normalTexture,normalSampler,layerDetailTexture2,layerDetailSampler2,2u,uv,input);
    let d=sampleLayer(occlusionTexture,occlusionSampler,layerDetailTexture3,layerDetailSampler3,3u,uv,input);
    if(material.layerOptions.y>0) {
        let score=vec4f(a.height,b.height,c.height,d.height)+weights;
        let activeScores=select(vec4f(-1e6),score,weights>vec4f(0));let peak=max(max(activeScores.x,activeScores.y),max(activeScores.z,activeScores.w));
        let adjusted=max(score-vec4f(peak-material.layerOptions.y),vec4f(0))*weights;
        let sum=dot(adjusted,vec4f(1));weights=select(weights,adjusted/max(sum,1e-6),sum>1e-6);
    }
    let color=weights.x*a.color*vec4f(material.layerColor[0].xyz,1)+weights.y*b.color*vec4f(material.layerColor[1].xyz,1)+weights.z*c.color*vec4f(material.layerColor[2].xyz,1)+weights.w*d.color*vec4f(material.layerColor[3].xyz,1);
    let roughness=dot(weights,vec4f(material.layerColor[0].w,material.layerColor[1].w,material.layerColor[2].w,material.layerColor[3].w));
    return LayerSurface(color,roughness,safeNormal(weights.x*a.normal+weights.y*b.normal+weights.z*c.normal+weights.w*d.normal,safeNormal(input.normal,vec3f(0,1,0))));
}
@fragment fn shadowFragment(input: VertexOutput) {
    let uv=input.uv*material.uv.xy+material.uv.zw;
    var alpha=textureSample(baseTexture,baseSampler,uv).a*input.color.a;
    if(materialLayered) {alpha=layerSurface(uv,input).color.a*input.color.a;}
    if(f32(materialAlpha)==1 && alpha<material.emissiveCutoff.w) { discard; }
}
fn sampleShadow(index: u32,input: VertexOutput,normal: vec3f,l: vec3f) -> f32 {
    let shadow=frame.shadows[index];
    if(shadow.options.x<0 || input.receivesShadow<0.5 || frame.environmentOptions.z>0.5) { return 1; }
    let geometric=safeNormal(input.normal,normal);
    let offset=geometric*shadow.options.z*(1-clamp(dot(geometric,l),0,1));
    let clip=shadow.viewProjection*vec4f(input.world+offset,1);
    if(clip.w<=0) {return 1;}
    let ndc=clip.xyz/clip.w;
    let uv=ndc.xy*vec2f(0.5,-0.5)+vec2f(0.5);
    if(any(uv<vec2f(0)) || any(uv>vec2f(1)) || ndc.z<0 || ndc.z>1) { return 1; }
    if(frame.environmentOptions.w<2) {
        return textureSampleCompareLevel(shadowMaps,shadowSampler,uv,i32(shadow.options.x),ndc.z-shadow.options.y);
    }
    var visibility=0.0;
    for(var y=-1;y<=1;y++) { for(var x=-1;x<=1;x++) {
        let tap=uv+vec2f(f32(x),f32(y))*shadow.options.w;
        visibility+=textureSampleCompareLevel(shadowMaps,shadowSampler,tap,i32(shadow.options.x),ndc.z-shadow.options.y);
    } }
    return visibility/9;
}
fn shadowVisibility(index:u32,input:VertexOutput,normal:vec3f,l:vec3f) -> f32 {
    let depth=dot(input.world-frame.cameraEnvironment.xyz,frame.viewForward.xyz);
    for(var j=0u;j<4u;j++) {
        let s=frame.shadows[j];
        if(s.options.x<0 || u32(s.selection.x)!=index) {continue;}
        if(s.selection.w>0.5 && depth>s.selection.z) {continue;}
        var value=sampleShadow(j,input,normal,l);
        if(s.selection.w>0.5 && j<3u) {
            let next=frame.shadows[j+1u];
            if(next.options.x>=0 && next.selection.x==s.selection.x) {
                let blend=smoothstep(mix(s.selection.y,s.selection.z,.9),s.selection.z,depth);
                value=mix(value,sampleShadow(j+1u,input,normal,l),blend);
            }
        }
        return value;
    }
    return 1;
}
fn surfaceLightVisibility(index:u32,input:VertexOutput,normal:vec3f,l:vec3f,light:Light,attenuation:f32) -> f32 {
    let cloudVisibility=select(1.0,cloudSurfaceShadow(input.world,l),light.positionType.w<0.5 && input.receivesShadow>0.5 && input.fogMode>0.5);
    let visibility=shadowVisibility(index,input,normal,l)*cloudVisibility;
    if(visibility<=0 || attenuation<=0 || light.colorIntensity.w<=0 || input.fogMode<0.5 ||
       frame.fog.volumeMinSteps.w<=0 || (u32(frame.fog.volumeOptions.w)&1u)==0u) {return visibility;}

    var distance=1e20;
    if(light.positionType.w>0.5) {distance=length(light.positionType.xyz-input.world);}
    if(distance<=1e-6) {return visibility;}
    return visibility*fogSpotTransmission(i32(index),frame.fog,input.world,safeNormal(l,normal),distance);
}
fn environmentDirection(d: vec3f,rotation:f32) -> vec3f {
    let c=cos(rotation);let s=sin(rotation);
    return vec3f(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
}
fn lightingZoneWeight(world:vec3f,index:u32) -> f32 {
    let lower=frame.lightingZones[2u*index];
    if(lower.w==0.0) { return 0.0; }
    let upper=frame.lightingZones[2u*index+1u];
    let edge=min(world-lower.xyz,upper.xyz-world);
    return smoothstep(0.0,lower.w,min(edge.x,min(edge.y,edge.z)));
}
var<private> surfaceAdvanced: array<vec4f,9>;
fn loadAdvanced(uv:vec2f) {
    surfaceAdvanced=material.advanced;
    if(material.advanced[8].x>0.5) {
        let packed=textureSampleLevel(layerDetailTexture0,layerDetailSampler0,uv,0);
        surfaceAdvanced[0].x*=packed.r;surfaceAdvanced[0].y*=packed.g;surfaceAdvanced[0].z*=packed.b;surfaceAdvanced[1].x*=packed.a;
    }
    if(material.advanced[8].z>0.5) {
        let dims=vec2f(textureDimensions(layerDetailTexture2,0));let inset=.5/dims;
        let wraps=u32(material.advanced[7].z);let repeatUv=fract(uv);
        let q=select(repeatUv,clamp(uv,vec2f(0),vec2f(1)),vec2<bool>((wraps&1u)!=0u,(wraps&2u)!=0u));
        let x=clamp(q.x,inset.x,1-inset.x);
        let sheen=textureSampleLevel(layerDetailTexture2,layerDetailSampler2,vec2f(x,clamp(q.y*.5,inset.y,.5-inset.y)),0);
        let subsurface=textureSampleLevel(layerDetailTexture2,layerDetailSampler2,vec2f(x,clamp(.5+q.y*.5,.5+inset.y,1-inset.y)),0);
        surfaceAdvanced[4]=vec4f(surfaceAdvanced[4].xyz*sheen.rgb,surfaceAdvanced[4].w);surfaceAdvanced[2].w*=sheen.a;
        surfaceAdvanced[3]=vec4f(surfaceAdvanced[3].xyz*subsurface.rgb,surfaceAdvanced[3].w);surfaceAdvanced[1].z*=subsurface.a;
    }
    if(material.advanced[8].w>0.5) {
        let packed=textureSampleLevel(layerDetailTexture3,layerDetailSampler3,uv,0);let direction=packed.rg*2-1;
        surfaceAdvanced[3].w*=packed.b;surfaceAdvanced[5].x*=packed.a;
        if(dot(direction,direction)>1e-6) {surfaceAdvanced[4].w+=atan2(direction.y,direction.x);}
    }
}
fn coatNormal(uv:vec2f,n:vec3f,t:vec3f,b:vec3f) -> vec3f {
    if(material.advanced[8].y<0.5) {return n;}
    let sample=textureSampleLevel(layerDetailTexture1,layerDetailSampler1,uv,0).xyz*2-1;
    let mapped=vec3f(sample.xy*material.advanced[7].y,sample.z);
    return safeNormal(t*mapped.x*select(1.0,-1.0,material.uv.x<0)+b*mapped.y*select(1.0,-1.0,material.uv.y<0)+n*mapped.z,n);
}
fn fresnel(f0: vec3f,vh: f32) -> vec3f { return f0+(vec3f(1)-f0)*pow(clamp(1-vh,0,1),5); }
fn directBrdf(n: vec3f,v: vec3f,l: vec3f,coatN:vec3f,tangent: vec3f,base: vec3f,rough: f32,metal: f32,f0: vec3f) -> vec3f {
    let h=safeNormal(v+l,n);let nv=max(dot(n,v),0.001);let nl=max(dot(n,l),0);let nh=max(dot(n,h),0);let vh=max(dot(v,h),0);
    let a=rough*rough;let a2=a*a;let denom=nh*nh*(a2-1)+1;var d=a2/(3.14159265*denom*denom);
    let an=surfaceAdvanced[3].w;
    if(abs(an)>0.0001) {
        let axis=safeNormal(tangent-n*dot(tangent,n),vec3f(1,0,0));let bitangent=cross(n,axis);
        let angle=surfaceAdvanced[4].w;let t=axis*cos(angle)+bitangent*sin(angle);let b=cross(n,t);
        let aspect=sqrt(max(0.1,1-0.9*abs(an)));let ax=max(0.002,a/select(aspect,1/aspect,an<0));let ay=max(0.002,a*select(aspect,1/aspect,an<0));
        let ellipse=pow(dot(h,t)/ax,2)+pow(dot(h,b)/ay,2)+nh*nh;d=1/max(3.14159265*ax*ay*ellipse*ellipse,1e-7);
    }
    let k=(rough+1)*(rough+1)/8;let g=(nv/(nv*(1-k)+k))*(nl/max(nl*(1-k)+k,0.001));
    let f=fresnel(f0,vh);let specular=d*g*f/max(4*nv*nl,0.001);
    let coat=surfaceAdvanced[0].x;let coatRough=max(.045,surfaceAdvanced[0].y);
    let coatNh=max(dot(coatN,h),0);let coatNl=max(dot(coatN,l),0);let coatNv=max(dot(coatN,v),.001);
    let ca=pow(coatRough,4);let cd=coatNh*coatNh*(ca-1)+1;let cf=.04+.96*pow(1-vh,5);
    let ck=(coatRough+1)*(coatRough+1)/8;let cg=(coatNv/(coatNv*(1-ck)+ck))*(coatNl/max(coatNl*(1-ck)+ck,.001));
    let coatLobe=coat*ca/max(3.14159265*cd*cd,1e-7)*cg*cf/max(4*coatNv,.001);
    let sss=surfaceAdvanced[1].z;let transmitted=surfaceAdvanced[0].z;
    let wrapped=clamp((dot(n,l)+.5)/1.5,0,1)+max(-dot(n,l),0)*.5;
    let diffuse=(vec3f(1)-f)*(1-metal)*base/3.14159265;
    let sheen=surfaceAdvanced[4].xyz*surfaceAdvanced[1].w*pow(1-vh,mix(1.0,5.0,surfaceAdvanced[2].w))*nl;
    return ((diffuse*(1-transmitted)*(1-sss)+specular)*nl+diffuse*surfaceAdvanced[3].xyz*sss*wrapped+sheen)*(1-coat*cf)+vec3f(coatLobe*coatNl);
}
fn srgb(linear: vec3f) -> vec3f {
    if(hdrOutput){return linear;}
    return select(12.92*linear,1.055*pow(max(linear,vec3f(0)),vec3f(1.0/2.4))-0.055,linear>vec3f(0.0031308));
}
fn scenePoint(pixel: vec2f,depth: f32) -> vec3f {
    let ndc=(pixel-frame.viewport.xy)/frame.viewport.zw*vec2f(2,-2)+vec2f(-1,1);
    let world=frame.inverseViewProjection*vec4f(ndc,depth,1);return world.xyz/world.w;
}
fn linearDisplay(c: vec3f) -> vec3f {
    if(hdrOutput){return c;}
    return select(c/12.92,pow((c+0.055)/1.055,vec3f(2.4)),c>vec3f(0.04045));
}
fn waterComposite(input: VertexOutput,normal: vec3f,body: vec3f,display: vec3f,uv: vec2f,time: f32,nv: f32) -> vec3f {
    var result=display;
    if(input.sceneWater<0.5) {return result;}
    let lo=frame.viewport.xy+vec2f(0.5);let hi=frame.viewport.xy+frame.viewport.zw-vec2f(0.5);
    let basePixel=clamp(input.clip.xy,lo,hi);
    let baseDepth=textureLoad(sceneDepth,vec2i(basePixel),0).r;
    // A clear depth is not a shoreline; foreground surfaces are never transmitted.
    if(baseDepth>=0.999999||baseDepth<=input.clip.z+0.000001) {return result;}
    let background=scenePoint(basePixel,baseDepth);
    let viewThickness=max(dot(background-input.world,frame.viewForward.xyz),0);
    let projectedNormal=(frame.viewProjection*vec4f(normal,0)).xy*vec2f(1,-1);
    var samplePixel=clamp(basePixel+projectedNormal*frame.viewport.zw*material.transmission.y*smoothstep(0,0.5,viewThickness),lo,hi);
    let sampleDepth=textureLoad(sceneDepth,vec2i(samplePixel),0).r;
    var samplePoint=scenePoint(samplePixel,min(sampleDepth,0.999999));
    if(sampleDepth>=0.999999||dot(samplePoint-input.world,frame.viewForward.xyz)<=0.001||dot(samplePoint-input.world,input.planeNormal)>0.01) {
        samplePixel=basePixel;samplePoint=background;
    }
    let nearPoint=scenePoint(basePixel,0);let farPoint=scenePoint(basePixel,0.99);
    let ray=safeNormal(farPoint-nearPoint,frame.viewForward.xyz);
    let thickness=clamp(dot(samplePoint-input.world,frame.viewForward.xyz)/max(dot(ray,frame.viewForward.xyz),0.1),0,100);
    let transmitted=linearDisplay(textureSampleLevel(sceneColor,sceneSampler,samplePixel/frame.extent.xy,0).rgb);
    let attenuation=exp(-material.absorptionFoam.rgb*thickness);
    let f=0.02+0.98*pow(1-clamp(nv,0,1),5);
    result=mix(result,transmitted*attenuation+surfaceFog(clamp(body,vec3f(0),vec3f(1)),input)*(vec3f(1)-attenuation),material.transmission.x*(1-f*material.horizon.w));
    let gap=max(dot(input.world-background,input.planeNormal),0);
    let pattern=0.65+0.35*sin(uv.x*material.absorptionFoam.w*6.2831853+time*0.8)*sin(uv.y*material.absorptionFoam.w*5.1-time*0.6);
    let foam=material.transmission.z*(1-smoothstep(0,material.transmission.w,gap))*pattern;
    return mix(result,surfaceFog(material.foamColor.rgb,input),foam);
}
// Thin-surface screen-space glass: only the previously completed opaque scene.
fn glassComposite(input:VertexOutput,normal:vec3f,display:vec3f,v:vec3f,nv:f32) -> vec3f {
    let a=surfaceAdvanced;let ior=a[0].w;let distance=a[1].x;
    let direction=refract(-v,normal,1/ior);
    var transmitted=textureSampleLevel(specularEnvironment,environmentSampler,environmentDirection(direction,frame.environmentOptions.x),0,material.surface.x*frame.environmentOptions.y).rgb*frame.cameraEnvironment.w;
    if(!hdrOutput) {transmitted=transmitted/(vec3f(1)+transmitted);}
    if(input.sceneWater>0.5 && frame.waterClock.y>0.5 && !materialReflectionPass) {
        let lo=frame.viewport.xy+vec2f(.5);let hi=frame.viewport.xy+frame.viewport.zw-vec2f(.5);let base=clamp(input.clip.xy,lo,hi);
        let bend=(frame.viewProjection*vec4f(normal,0)).xy*vec2f(1,-1)*(1-1/ior)*min(max(distance,.05),2)*.03;
        var pixel=clamp(base+bend*frame.viewport.zw,lo,hi);
        let depth=textureLoad(sceneDepth,vec2i(pixel),0).r;
        if(depth<=input.clip.z+.000001) {pixel=base;}
        transmitted=linearDisplay(textureSampleLevel(sceneColor,sceneSampler,pixel/frame.extent.xy,0).rgb);
    }
    var absorption=vec3f(1);
    if(a[1].y>0) {absorption=pow(max(a[2].xyz,vec3f(.0001)),vec3f(distance/a[1].y));}
    let f0=pow((ior-1)/(ior+1),2)*(a[5].x*2);let fresnel=f0+(1-f0)*pow(1-clamp(nv,0,1),5);
    return mix(display,transmitted*absorption,a[0].z*(1-fresnel));
}
fn interactionHeight(p: vec3f,n: vec3f) -> f32 {
    var height=0.0;
    for(var i=0u;i<u32(frame.interactionOptions.x);i++) {
        let origin=frame.interactionData[i*4];let heading=frame.interactionData[i*4+1];
        let shape=frame.interactionData[i*4+2];let life=frame.interactionData[i*4+3].x;
        var delta=p-origin.xyz;let gap=dot(delta,n);delta-=n*gap;
        if(abs(gap)>shape.y) {continue;}
        var phase=length(delta)-origin.w*shape.w;var envelope=exp(-pow(phase/shape.y,2));
        if(heading.w>0) {
            let projected=heading.xyz-n*dot(heading.xyz,n);
            if(dot(projected,projected)<1e-6) {continue;}
            let forward=normalize(projected);let along=-dot(delta,forward);
            let side=abs(dot(delta,cross(n,forward)));
            if(along<0 || along>heading.w) {continue;}
            phase=side-along*.32;
            envelope=exp(-pow(phase/shape.y,2))*smoothstep(0.0,.3,along)*(1-smoothstep(heading.w*.65,heading.w,along));
            phase-=origin.w*shape.w;
        }
        height+=shape.x*sin(6.2831853*phase/shape.z)*envelope*pow(1-origin.w/life,2);
    }
    return height;
}
fn interactionSurface(p: vec3f,n: vec3f) -> vec4f {
    let axis=select(vec3f(0,1,0),vec3f(1,0,0),abs(n.y)>.9);
    let u=normalize(cross(n,axis));let v=cross(n,u);let h=interactionHeight(p,n);
    let gradient=u*(interactionHeight(p+u*.025,n)-h)/.025+v*(interactionHeight(p+v*.025,n)-h)/.025;
    return vec4f(gradient,h);
}
fn traceScreenReflection(input:VertexOutput,normal:vec3f,ray:vec3f,rough: f32) -> vec4f {
    let smoothness=1.0-smoothstep(0.18,0.5,rough);
    if(smoothness<=0.0||dot(ray,normal)<=0.01) { return vec4f(0); }
    let origin=input.world+normal*0.04;
    var previousGap=-1.0;
    var previousDistance=0.0;
    for(var i=1u;i<=24u;i++) {
        let fraction=f32(i)/24.0;
        let distance=0.08+10.0*fraction*fraction;
        let point=origin+ray*distance;
        let clip=frame.viewProjection*vec4f(point,1);
        if(clip.w<=0.0001||clip.z<0.0||clip.z>=clip.w) { break; }
        let ndc=clip.xy/clip.w;
        let uv=ndc*vec2f(0.5,-0.5)+vec2f(0.5);
        let pixel=uv*frame.viewport.zw;
        let edge=min(min(pixel.x,pixel.y),min(frame.viewport.z-pixel.x,frame.viewport.w-pixel.y));
        if(edge<1.0) { break; }
        let samplePixel=vec2i(frame.viewport.xy+pixel);
        let depth=textureLoad(sceneDepth,samplePixel,0).r;
        if(depth>=0.999999) { previousGap=-1.0;previousDistance=distance;continue; }
        let center=(vec2f(samplePixel)+vec2f(0.5)-frame.viewport.xy)/frame.viewport.zw;
        let world=frame.inverseViewProjection*vec4f(center*vec2f(2,-2)+vec2f(-1,1),depth,1);
        let gap=dot(point-world.xyz/world.w,frame.viewForward.xyz);
        let thickness=0.10+0.75*(distance-previousDistance)*abs(dot(ray,frame.viewForward.xyz));
        if(previousGap<0.0&&gap>=0.0&&gap<thickness&&length(vec2f(samplePixel)+vec2f(0.5)-input.clip.xy)>2.0) {
            let confidence=0.9*smoothness*smoothstep(1.0,12.0,edge)*(1.0-smoothstep(7.0,10.0,distance));
            return vec4f(textureLoad(sceneColor,samplePixel,0).rgb,confidence);
        }
        previousGap=gap;previousDistance=distance;
    }
    return vec4f(0);
}
fn projectedReflectionProbe(world:vec3f,ray:vec3f) -> vec4f {
    let lower=frame.reflectionProbe[0];
    let upper=frame.reflectionProbe[1];
    if(lower.w<=0.0) {return vec4f(0);}
    let distanceToFace=min(world-lower.xyz,upper.xyz-world);
    let edge=min(distanceToFace.x,min(distanceToFace.y,distanceToFace.z));
    let weight=smoothstep(0.0,lower.w,edge);
    if(weight<=0.0) {return vec4f(0);}
    let boxFace=select(lower.xyz,upper.xyz,ray>=vec3f(0));
    let safeRay=select(vec3f(-1e-6),vec3f(1e-6),ray>=vec3f(0));
    let divisor=select(safeRay,ray,abs(ray)>vec3f(1e-6));
    let axisDistance=select(vec3f(1e20),(boxFace-world)/divisor,abs(ray)>vec3f(1e-6));
    let distance=min(axisDistance.x,min(axisDistance.y,axisDistance.z));
    return vec4f(world+ray*distance-frame.reflectionProbe[2].xyz,weight);
}
fn shade(input: VertexOutput,front: bool) -> vec4f {
    let uv=input.uv*material.uv.xy+material.uv.zw;
    loadAdvanced(uv);
    // Sample before discard or divergent branches to preserve derivative validity.
    var texel=textureSample(baseTexture,baseSampler,uv);
    var surface=vec4f(1);
    if(materialSurfaceMap) {surface=textureSample(surfaceTexture,surfaceSampler,uv);}
    var layerNormal=input.normal;
    if(materialLayered) {let layered=layerSurface(uv,input);layerNormal=layered.normal;texel=layered.color;surface=vec4f(1,layered.roughness,1,1);}
    var normalSample=vec3f(0,0,1);
    if(materialNormalMap) {normalSample=textureSample(normalTexture,normalSampler,uv).xyz*2-1;}
    let waterUv=uv*material.detail.z;
    let waterTime=frame.waterClock.x*material.wave.z;
    var rippleA=vec3f(0,0,1);var rippleB=vec3f(0,0,1);
    if(f32(materialShader)>1.5) {
        rippleA=textureSample(normalTexture,normalSampler,waterUv+vec2f(0.021,0.013)*waterTime).xyz*2-1;
        rippleB=textureSample(normalTexture,normalSampler,waterUv*1.37+vec2f(-0.017,0.019)*waterTime).xyz*2-1;
    }
    var aoSample=1.0;
    if(materialOcclusionMap) {aoSample=textureSample(occlusionTexture,occlusionSampler,uv).r;}
    var emission=material.emissiveCutoff.xyz;
    if(materialEmissiveMap) {emission*=textureSample(emissiveTexture,emissiveSampler,uv).rgb;}
    let alpha=texel.a*input.color.a;
    if(materialReflectionPass && dot(frame.reflectionPlane,vec4f(input.world,1))<0.002) {discard;}
    if(f32(materialAlpha)==1 && alpha<material.emissiveCutoff.w) { discard; }
    let base=max(input.color.rgb*texel.rgb,vec3f(0));
    if(f32(materialShader)>0.5 && f32(materialShader)<1.5) { return vec4f(srgb(surfaceFog(select(base+emission,cloudApply(base+emission,input.world,input.fogMode>1.5),input.fogMode>0.5),input)),select(alpha,1.0,f32(materialAlpha)<2)); }
    var interaction=vec4f(0);
    if(f32(materialShader)>1.5 && input.interactionSurface>0.5 && frame.interactionOptions.x>0) {interaction=interactionSurface(input.world,input.planeNormal);}
    var normal=safeNormal(input.normal,vec3f(0,1,0));
    let t=safeNormal(input.tangent.xyz-normal*dot(normal,input.tangent.xyz),vec3f(1,0,0));
    let b=cross(normal,t)*input.tangent.w;
    if(materialLayered) {normal=layerNormal;}
    if(materialNormalMap && f32(materialShader)<1.5) {
        let mapped=safeNormal(vec3f(normalSample.xy*material.surface.z,normalSample.z),vec3f(0,0,1));
        normal=safeNormal(t*mapped.x*select(1.0,-1.0,material.uv.x<0)+b*mapped.y*select(1.0,-1.0,material.uv.y<0)+normal*mapped.z,normal);
    }
    if(f32(materialShader)>1.5) {
        let r=(rippleA.xy+rippleB.xy)*material.surface.z+0.12*vec2f(sin(waterUv.x*6.2831853+waterTime),cos(waterUv.y*8.1-waterTime*1.3));
        normal=safeNormal(normal+(t*r.x+b*r.y)*material.detail.w,normal);
    }
    normal=safeNormal(normal-interaction.xyz*.12,normal);
    if(material.flags.z>0.5 && !front) { normal=-normal; }
    let geometric=safeNormal(input.normal,vec3f(0,1,0))*select(1.0,-1.0,material.flags.z>0.5 && !front);
    let coatN=coatNormal(uv,geometric,t,b);
    let rough=clamp(material.surface.x*surface.g,0.045,1);let metal=clamp(material.surface.y*surface.b,0,1);
    let ao=mix(1.0,aoSample,material.surface.w);
    let v=safeNormal(frame.cameraEnvironment.xyz-input.world,normal);let nv=max(dot(normal,v),0.001);
    let dielectric=pow((surfaceAdvanced[0].w-1)/(surfaceAdvanced[0].w+1),2)*(surfaceAdvanced[5].x*2);
    let f0=select(mix(vec3f(dielectric),base,metal),vec3f(0.02),f32(materialShader)>1.5);
    var lighting=vec3f(0);
    if(f32(materialShader)<=1.5) {
        for(var i=0u;i<u32(frame.ambientCount.w);i++) {
            let light=frame.lights[i];var l=safeNormal(-light.directionRange.xyz,normal);var attenuation=1.0;
            if(light.positionType.w>0.5) {
                let delta=light.positionType.xyz-input.world;let distance=max(length(delta),0.001);l=delta/distance;
                attenuation=pow(clamp(1-distance/max(light.directionRange.w,0.001),0,1),2);
                if(light.positionType.w>1.5){attenuation*=smoothstep(light.cone.y,light.cone.x,dot(light.directionRange.xyz,-l));}
            }
            lighting+=directBrdf(normal,v,l,coatN,t,base,rough,metal,f0)*light.colorIntensity.xyz*light.colorIntensity.w*attenuation*surfaceLightVisibility(i,input,normal,l,light,attenuation);
        }
    }
    let reflectionDirection=reflect(-v,normal);
    let environmentLod=rough*frame.environmentOptions.y;
    let irradiance=textureSampleLevel(diffuseEnvironment,environmentSampler,environmentDirection(normal,frame.environmentOptions.x),0,0).rgb;
    let reflection=textureSampleLevel(specularEnvironment,environmentSampler,environmentDirection(reflectionDirection,frame.environmentOptions.x),0,environmentLod).rgb;
    let integrated=textureSampleLevel(brdfLut,environmentSampler,vec2f(nv,rough),0).rg;
    let f=f0+(max(vec3f(1-rough),f0)-f0)*pow(1-nv,5);
    let diffuse=(vec3f(1)-f)*(1-metal)*base*(1-surfaceAdvanced[0].z);
    var ssrFallbackSpecular=reflection*frame.cameraEnvironment.w;
    var giFallbackDiffuse=irradiance*frame.cameraEnvironment.w;
    var indirect=ao*(diffuse*(giFallbackDiffuse+frame.ambientCount.xyz)+reflection*(f0*integrated.x+integrated.y)*frame.cameraEnvironment.w);
    if(materialShader!=2u && (frame.lightingZones[0].w>0.0||frame.lightingZones[2].w>0.0)) {
        let weights=vec2f(lightingZoneWeight(input.world,0u),lightingZoneWeight(input.world,1u));
        let total=weights.x+weights.y;
        var localDiffuse=irradiance*(frame.cameraEnvironment.w*max(0.0,1.0-total));
        var localSpecular=reflection*(frame.cameraEnvironment.w*max(0.0,1.0-total));
        for(var i=0u;i<2u;i++) {
            if(weights[i]>0.0) {
                let weight=weights[i]*frame.lightingZones[2u*i+1u].w/max(1.0,total);
                let rotation=frame.lightingZoneRotations[i];
                localDiffuse+=textureSampleLevel(diffuseEnvironment,environmentSampler,environmentDirection(normal,rotation),i32(i+1u),0).rgb*weight;
                localSpecular+=textureSampleLevel(specularEnvironment,environmentSampler,environmentDirection(reflectionDirection,rotation),i32(i+1u),environmentLod).rgb*weight;
            }
        }
        ssrFallbackSpecular=localSpecular;
        giFallbackDiffuse=localDiffuse;
        indirect=ao*(diffuse*(localDiffuse+frame.ambientCount.xyz)+localSpecular*(f0*integrated.x+integrated.y));
    }
    if(materialShader==0u && input.fogMode>0.5) {
        let projected=projectedReflectionProbe(input.world,reflectionDirection);
        if(projected.w>0.0) {
            let cache=textureSampleLevel(specularEnvironment,environmentSampler,projected.xyz,
                i32(frame.reflectionProbe[2].w),environmentLod).rgb*frame.reflectionProbe[1].w;
            let replacement=mix(ssrFallbackSpecular,cache,projected.w);
            indirect+=ao*(replacement-ssrFallbackSpecular)*(f0*integrated.x+integrated.y);
            ssrFallbackSpecular=replacement;
        }
    }
    if(materialShader==0u && input.fogMode>0.5 && metal<1.0) {
        let gi=sampleDiffuseGi(input.world,normal);
        if gi.a>0.0 { indirect+=ao*diffuse*(gi.rgb-giFallbackDiffuse)*gi.a; }
    }
    if(materialShader==0u) {
        let coatF=.04+.96*pow(1-nv,5);let coat=surfaceAdvanced[0].x;
        let coatReflection=textureSampleLevel(specularEnvironment,environmentSampler,environmentDirection(reflect(-v,coatN),frame.environmentOptions.x),0,surfaceAdvanced[0].y*frame.environmentOptions.y).rgb*frame.cameraEnvironment.w;
        indirect=indirect*(1-coat*coatF)+coatReflection*(coat*coatF);
        indirect+=ao*(giFallbackDiffuse+frame.ambientCount.xyz)*base*surfaceAdvanced[3].xyz*surfaceAdvanced[1].z*.25;
        indirect+=ssrFallbackSpecular*surfaceAdvanced[4].xyz*surfaceAdvanced[1].w*pow(1-nv,mix(1.0,5.0,surfaceAdvanced[2].w));
    }
    lighting+=indirect;
    if(materialScreenReflection) {
        let hit=traceScreenReflection(input,normal,reflectionDirection,rough);
        lighting+=ao*(hit.rgb-ssrFallbackSpecular)*hit.a*(f0*integrated.x+integrated.y);
    }
    if(f32(materialShader)>1.5) {
        let reflected=reflect(-v,normal);
        let sky=mix(material.horizon.rgb,material.zenith.rgb,clamp(reflected.y,0,1));
        let reflectedColor=select(sky,reflection*frame.cameraEnvironment.w,frame.cameraEnvironment.w>0);
        let fWater=0.02+0.98*pow(1-nv,5);
        // Surface reflection is composed with depth-aware transmission after tone mapping.
        var waterLight=base*(frame.ambientCount.xyz+vec3f(0.12));
        for(var i=0u;i<u32(frame.ambientCount.w);i++) {
            let light=frame.lights[i];var l=safeNormal(-light.directionRange.xyz,normal);var attenuation=1.0;
            if(light.positionType.w>0.5) {
                let delta=light.positionType.xyz-input.world;let distance=max(length(delta),0.001);l=delta/distance;
                attenuation=pow(clamp(1-distance/max(light.directionRange.w,0.001),0,1),2);
                if(light.positionType.w>1.5){attenuation*=smoothstep(light.cone.y,light.cone.x,dot(light.directionRange.xyz,-l));}
            }
            let h=safeNormal(v+l,normal);
            let power=mix(256.0,8.0,rough);
            let highlight=pow(max(dot(normal,h),0),power)*fWater*material.horizon.w;
            waterLight+=(base*max(dot(normal,l),0)*0.25+vec3f(highlight))*light.colorIntensity.xyz*light.colorIntensity.w*attenuation*surfaceLightVisibility(i,input,normal,l,light,attenuation);
        }
        lighting=waterLight*(1-fWater*material.horizon.w)+reflectedColor*fWater*material.horizon.w;
    }
    let linear=select(lighting+emission,cloudApply(lighting+emission,input.world,input.fogMode>1.5),input.fogMode>0.5);
    var display=surfaceFog(select(linear/(vec3f(1)+linear),linear,hdrOutput),input);
    if(f32(materialShader)>1.5 && frame.reflectionOptions.x>0.5 && input.planarSurface>0.5 && material.foamColor.w>0) {
        let clip=frame.reflectionViewProjection*vec4f(input.world,1);
        let uv=clip.xy/max(clip.w,0.0001)*vec2f(0.5,-0.5)+vec2f(0.5);
        let distorted=uv+normal.xz*0.008*(1.0-rough);
        let inside=clip.w>0 && all(distorted>vec2f(0)) && all(distorted<vec2f(1)) && clip.z>=0 && clip.z<=clip.w;
        let pixel=clamp(frame.viewport.xy+distorted*frame.viewport.zw,frame.viewport.xy+vec2f(0.5),frame.viewport.xy+frame.viewport.zw-vec2f(0.5))/frame.extent.xy;
        let reflected=textureSampleLevel(planarReflection,sceneSampler,pixel,0);
        let weight=select(0.0,material.foamColor.w*material.horizon.w*(0.02+0.98*pow(1.0-nv,5.0))*reflected.a,inside);
        display=mix(display,linearDisplay(reflected.rgb),weight);
    }
    if(f32(materialShader)>1.5) {display=waterComposite(input,normal,base,display,uv,waterTime,nv)+fogVolumeApply(vec3f(min(abs(interaction.w)*.18,.25)),input.world,true);}
    if(materialShader==0u && surfaceAdvanced[0].z>0) {display=glassComposite(input,normal,display,v,nv);}
    return vec4f(srgb(display),select(alpha,1.0,f32(materialAlpha)<2));
}
@fragment fn fragmentMain(input: VertexOutput,@builtin(front_facing) front: bool) -> @location(0) vec4f { return vec4f(shade(input,front).rgb,1); }
@fragment fn transparentMain(input: VertexOutput,@builtin(front_facing) front: bool) -> @location(0) vec4f {
    let color=shade(input,front);return vec4f(color.rgb*color.a,color.a);
}

@group(1) @binding(11) var layerDetailSampler0: sampler;
@group(1) @binding(12) var layerDetailTexture0: texture_2d<f32>;

@group(1) @binding(13) var layerDetailSampler1: sampler;
@group(1) @binding(14) var layerDetailTexture1: texture_2d<f32>;

@group(1) @binding(15) var layerDetailSampler2: sampler;
@group(1) @binding(16) var layerDetailTexture2: texture_2d<f32>;

@group(1) @binding(17) var layerDetailSampler3: sampler;
@group(1) @binding(18) var layerDetailTexture3: texture_2d<f32>;
