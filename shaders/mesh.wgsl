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
// FOG_FUNCTIONS
struct Light { positionType: vec4f, directionRange: vec4f, colorIntensity: vec4f };
struct Shadow { viewProjection: mat4x4f, options: vec4f }; // layer, depth bias, normal bias, texel size
struct Frame {
    viewProjection: mat4x4f, ambientCount: vec4f, lights: array<Light,8>,
    cameraEnvironment: vec4f, environmentOptions: vec4f,
    shadows: array<Shadow,8>, waterClock: vec4f, inverseViewProjection: mat4x4f, viewport: vec4f, extent: vec4f, viewForward: vec4f, reflectionViewProjection: mat4x4f, reflectionPlane: vec4f, reflectionOptions: vec4f, interactionOptions: vec4f, interactionData: array<vec4f,64>, fog:Fog,
};
struct Object { model: mat4x4f, normalMatrix: mat4x4f, color: vec4f, shadow: vec4f };
struct Material { surface: vec4f, emissiveCutoff: vec4f, uv: vec4f, flags: vec4f, wave: vec4f, detail: vec4f, horizon: vec4f, zenith: vec4f, transmission: vec4f, absorptionFoam: vec4f, foamColor: vec4f, layerColor: array<vec4f,4>, layerUv: array<vec4f,4>, weightUv: vec4f, layerOptions: vec4f, layerDetail: array<vec4f,4> };
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var specularEnvironment: texture_cube<f32>;
@group(0) @binding(2) var diffuseEnvironment: texture_cube<f32>;
@group(0) @binding(3) var brdfLut: texture_2d<f32>;
@group(0) @binding(4) var environmentSampler: sampler;
@group(0) @binding(5) var shadowMaps: texture_depth_2d_array;
@group(0) @binding(6) var shadowSampler: sampler_comparison;
@group(0) @binding(7) var sceneColor: texture_2d<f32>;
@group(0) @binding(8) var sceneDepth: texture_2d<f32>;
@group(0) @binding(9) var sceneSampler: sampler;
@group(0) @binding(10) var planarReflection: texture_2d<f32>;
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
    out.interactionSurface=o.shadow.z;
    out.planarSurface=select(0.0,1.0,o.shadow.z>0.5 && abs(dot(frame.reflectionPlane,o.model[3]))<0.01 && abs(dot(normalize((o.normalMatrix*vec4f(0,1,0,0)).xyz),frame.reflectionPlane.xyz))>0.999);
    let world=o.model*vec4f(position,1);out.clip=frame.viewProjection*world;out.world=world.xyz;
    let wn=safeNormal((o.normalMatrix*vec4f(surfaceNormal,0)).xyz,vec3f(0,1,0));
    let wt=(o.model*vec4f(surfaceTangent,0)).xyz;let wb=(o.model*vec4f(surfaceBitangent,0)).xyz;
    let tangent=safeNormal(wt-wn*dot(wn,wt),vec3f(1,0,0));
    out.normal=wn;out.tangent=vec4f(tangent,select(1.0,-1.0,dot(cross(wn,tangent),wb)<0));
    out.fogMode=o.shadow.w;out.uv=uv;out.color=o.color;out.receivesShadow=o.shadow.x;out.sceneWater=o.shadow.y;out.planeNormal=safeNormal((o.normalMatrix*vec4f(0,1,0,0)).xyz,vec3f(0,1,0));return out;
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
fn shadowVisibility(index: u32,input: VertexOutput,normal: vec3f,l: vec3f) -> f32 {
    let shadow=frame.shadows[index];
    if(shadow.options.x<0 || input.receivesShadow<0.5 || frame.environmentOptions.z>0.5) { return 1; }
    let geometric=safeNormal(input.normal,normal);
    let offset=geometric*shadow.options.z*(1-clamp(dot(geometric,l),0,1));
    let clip=shadow.viewProjection*vec4f(input.world+offset,1);
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
fn environmentDirection(d: vec3f) -> vec3f {
    let c=cos(frame.environmentOptions.x);let s=sin(frame.environmentOptions.x);
    return vec3f(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
}
fn fresnel(f0: vec3f,vh: f32) -> vec3f { return f0+(vec3f(1)-f0)*pow(clamp(1-vh,0,1),5); }
fn directBrdf(n: vec3f,v: vec3f,l: vec3f,base: vec3f,rough: f32,metal: f32,f0: vec3f) -> vec3f {
    let h=safeNormal(v+l,n);let nv=max(dot(n,v),0.001);let nl=max(dot(n,l),0);let nh=max(dot(n,h),0);let vh=max(dot(v,h),0);
    let a=rough*rough;let a2=a*a;let denom=nh*nh*(a2-1)+1;let d=a2/(3.14159265*denom*denom);
    let k=(rough+1)*(rough+1)/8;let g=(nv/(nv*(1-k)+k))*(nl/max(nl*(1-k)+k,0.001));
    let f=fresnel(f0,vh);let specular=d*g*f/max(4*nv*nl,0.001);
    return ((vec3f(1)-f)*(1-metal)*base/3.14159265+specular)*nl;
}
fn srgb(linear: vec3f) -> vec3f {
    return select(12.92*linear,1.055*pow(max(linear,vec3f(0)),vec3f(1.0/2.4))-0.055,linear>vec3f(0.0031308));
}
fn scenePoint(pixel: vec2f,depth: f32) -> vec3f {
    let ndc=(pixel-frame.viewport.xy)/frame.viewport.zw*vec2f(2,-2)+vec2f(-1,1);
    let world=frame.inverseViewProjection*vec4f(ndc,depth,1);return world.xyz/world.w;
}
fn linearDisplay(c: vec3f) -> vec3f {
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
fn shade(input: VertexOutput,front: bool) -> vec4f {
    let uv=input.uv*material.uv.xy+material.uv.zw;
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
    if(f32(materialShader)>0.5 && f32(materialShader)<1.5) { return vec4f(srgb(surfaceFog(base+emission,input)),select(alpha,1.0,f32(materialAlpha)<2)); }
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
    let rough=clamp(material.surface.x*surface.g,0.045,1);let metal=clamp(material.surface.y*surface.b,0,1);
    let ao=mix(1.0,aoSample,material.surface.w);
    let v=safeNormal(frame.cameraEnvironment.xyz-input.world,normal);let nv=max(dot(normal,v),0.001);
    let f0=select(mix(vec3f(0.04),base,metal),vec3f(0.02),f32(materialShader)>1.5);
    var lighting=vec3f(0);
    for(var i=0u;i<u32(frame.ambientCount.w);i++) {
        let light=frame.lights[i];var l=safeNormal(-light.directionRange.xyz,normal);var attenuation=1.0;
        if(light.positionType.w>0.5) {
            let delta=light.positionType.xyz-input.world;let distance=max(length(delta),0.001);l=delta/distance;
            attenuation=pow(clamp(1-distance/max(light.directionRange.w,0.001),0,1),2);
        }
        lighting+=directBrdf(normal,v,l,base,rough,metal,f0)*light.colorIntensity.xyz*light.colorIntensity.w*attenuation*shadowVisibility(i,input,normal,l);
    }
    let irradiance=textureSampleLevel(diffuseEnvironment,environmentSampler,environmentDirection(normal),0).rgb;
    let reflection=textureSampleLevel(specularEnvironment,environmentSampler,environmentDirection(reflect(-v,normal)),rough*frame.environmentOptions.y).rgb;
    let integrated=textureSampleLevel(brdfLut,environmentSampler,vec2f(nv,rough),0).rg;
    let f=f0+(max(vec3f(1-rough),f0)-f0)*pow(1-nv,5);
    let diffuse=(vec3f(1)-f)*(1-metal)*base;
    lighting+=ao*(diffuse*(irradiance*frame.cameraEnvironment.w+frame.ambientCount.xyz)+reflection*(f0*integrated.x+integrated.y)*frame.cameraEnvironment.w);
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
            }
            let h=safeNormal(v+l,normal);
            let power=mix(256.0,8.0,rough);
            let highlight=pow(max(dot(normal,h),0),power)*fWater*material.horizon.w;
            waterLight+=(base*max(dot(normal,l),0)*0.25+vec3f(highlight))*light.colorIntensity.xyz*light.colorIntensity.w*attenuation*shadowVisibility(i,input,normal,l);
        }
        lighting=waterLight*(1-fWater*material.horizon.w)+reflectedColor*fWater*material.horizon.w;
    }
    let linear=lighting+emission;
    var display=surfaceFog(linear/(vec3f(1)+linear),input);
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
    if(f32(materialShader)>1.5) {display=waterComposite(input,normal,base,display,uv,waterTime,nv)+fogApply(frame.fog,vec3f(min(abs(interaction.w)*.18,.25)),input.world,true);}
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
