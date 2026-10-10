struct Light { positionType: vec4f, directionRange: vec4f, colorIntensity: vec4f };
struct Shadow { viewProjection: mat4x4f, options: vec4f }; // layer, depth bias, normal bias, texel size
struct Frame {
    viewProjection: mat4x4f, ambientCount: vec4f, lights: array<Light,8>,
    cameraEnvironment: vec4f, environmentOptions: vec4f,
    shadows: array<Shadow,8>,
};
struct Object { model: mat4x4f, normalMatrix: mat4x4f, color: vec4f, shadow: vec4f };
struct Material { surface: vec4f, emissiveCutoff: vec4f, uv: vec4f, flags: vec4f };
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var specularEnvironment: texture_cube<f32>;
@group(0) @binding(2) var diffuseEnvironment: texture_cube<f32>;
@group(0) @binding(3) var brdfLut: texture_2d<f32>;
@group(0) @binding(4) var environmentSampler: sampler;
@group(0) @binding(5) var shadowMaps: texture_depth_2d_array;
@group(0) @binding(6) var shadowSampler: sampler_comparison;
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
};
fn safeNormal(v: vec3f, fallback: vec3f) -> vec3f { return select(fallback,v*inverseSqrt(max(dot(v,v),1e-12)),dot(v,v)>1e-12); }
fn outputVertex(p: vec3f,n: vec3f,t: vec3f,b: vec3f,uv: vec2f,o: Object) -> VertexOutput {
    var out: VertexOutput;
    let world=o.model*vec4f(p,1);out.clip=frame.viewProjection*world;out.world=world.xyz;
    let wn=safeNormal((o.normalMatrix*vec4f(n,0)).xyz,vec3f(0,1,0));
    let wt=(o.model*vec4f(t,0)).xyz;let wb=(o.model*vec4f(b,0)).xyz;
    let tangent=safeNormal(wt-wn*dot(wn,wt),vec3f(1,0,0));
    out.normal=wn;out.tangent=vec4f(tangent,select(1.0,-1.0,dot(cross(wn,tangent),wb)<0));
    out.uv=uv;out.color=o.color;out.receivesShadow=o.shadow.x;return out;
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
@fragment fn shadowFragment(input: VertexOutput) {
    let alpha=textureSample(baseTexture,baseSampler,input.uv*material.uv.xy+material.uv.zw).a*input.color.a;
    if(material.flags.x==1 && alpha<material.emissiveCutoff.w) { discard; }
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
fn shade(input: VertexOutput,front: bool) -> vec4f {
    let uv=input.uv*material.uv.xy+material.uv.zw;
    // Sample before discard or divergent branches to preserve derivative validity.
    let texel=textureSample(baseTexture,baseSampler,uv);
    let surface=textureSample(surfaceTexture,surfaceSampler,uv);
    let normalSample=textureSample(normalTexture,normalSampler,uv).xyz*2-1;
    let aoSample=textureSample(occlusionTexture,occlusionSampler,uv).r;
    let emission=textureSample(emissiveTexture,emissiveSampler,uv).rgb*material.emissiveCutoff.xyz;
    let alpha=texel.a*input.color.a;
    if(material.flags.x==1 && alpha<material.emissiveCutoff.w) { discard; }
    let base=max(input.color.rgb*texel.rgb,vec3f(0));
    if(material.flags.y>0.5) { return vec4f(srgb(base+emission),select(alpha,1.0,material.flags.x<2)); }
    var normal=safeNormal(input.normal,vec3f(0,1,0));
    let t=safeNormal(input.tangent.xyz-normal*dot(normal,input.tangent.xyz),vec3f(1,0,0));
    let b=cross(normal,t)*input.tangent.w;
    if(material.flags.w>0.5) {
        let mapped=safeNormal(vec3f(normalSample.xy*material.surface.z,normalSample.z),vec3f(0,0,1));
        normal=safeNormal(t*mapped.x*select(1.0,-1.0,material.uv.x<0)+b*mapped.y*select(1.0,-1.0,material.uv.y<0)+normal*mapped.z,normal);
    }
    if(material.flags.z>0.5 && !front) { normal=-normal; }
    let rough=clamp(material.surface.x*surface.g,0.045,1);let metal=clamp(material.surface.y*surface.b,0,1);
    let ao=mix(1.0,aoSample,material.surface.w);
    let v=safeNormal(frame.cameraEnvironment.xyz-input.world,normal);let nv=max(dot(normal,v),0.001);
    let f0=mix(vec3f(0.04),base,metal);
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
    let linear=lighting+emission;
    return vec4f(srgb(linear/(vec3f(1)+linear)),select(alpha,1.0,material.flags.x<2));
}
@fragment fn fragmentMain(input: VertexOutput,@builtin(front_facing) front: bool) -> @location(0) vec4f { return vec4f(shade(input,front).rgb,1); }
@fragment fn transparentMain(input: VertexOutput,@builtin(front_facing) front: bool) -> @location(0) vec4f {
    let color=shade(input,front);return vec4f(color.rgb*color.a,color.a);
}
