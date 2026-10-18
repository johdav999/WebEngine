struct WaterFog {
    inverseViewProjection: mat4x4f, worldToLocal: mat4x4f,
    viewport: vec4f, eye: vec4f, minimum: vec4f, maximum: vec4f,
    absorptionStrength: vec4f, colorDensity: vec4f, options: vec4f, caustics: vec4f,
};
@group(0) @binding(0) var sourceColor: texture_2d<f32>;
@group(0) @binding(1) var sourceDepth: texture_2d<f32>;
@group(0) @binding(2) var<uniform> fog: WaterFog;
@vertex fn vertexMain(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));return vec4f(p[index],0,1);
}
fn point(pixel: vec2f,depth: f32) -> vec3f {
    let ndc=(pixel-fog.viewport.xy)/fog.viewport.zw*vec2f(2,-2)+vec2f(-1,1);
    let p=fog.inverseViewProjection*vec4f(ndc,depth,1);return p.xyz/p.w;
}
fn slab(p: f32,d: f32,lo: f32,hi: f32,interval: vec2f) -> vec2f {
    if(abs(d)<1e-7) {return select(vec2f(1,0),interval,p>=lo && p<=hi);}
    let a=(lo-p)/d;let b=(hi-p)/d;return vec2f(max(interval.x,min(a,b)),min(interval.y,max(a,b)));
}
fn distanceInWater(start: vec3f,end: vec3f) -> f32 {
    let origin=(fog.worldToLocal*vec4f(start,1)).xyz;
    let delta=(fog.worldToLocal*vec4f(end-start,0)).xyz;
    var interval=slab(origin.x,delta.x,fog.minimum.x,fog.maximum.x,vec2f(0,1));
    interval=slab(origin.z,delta.z,fog.minimum.z,fog.maximum.z,interval);
    if(abs(delta.y)<1e-7) {if(origin.y>0) {return 0;}}
    else if(delta.y>0) {interval.y=min(interval.y,-origin.y/delta.y);}
    else {interval.x=max(interval.x,-origin.y/delta.y);}
    return min(fog.options.x,max(0.0,interval.y-interval.x)*length(end-start));
}
fn decode(c: vec3f) -> vec3f {return select(c/12.92,pow((c+0.055)/1.055,vec3f(2.4)),c>vec3f(0.04045));}
fn encode(c: vec3f) -> vec3f {return select(c*12.92,1.055*pow(max(c,vec3f(0)),vec3f(1.0/2.4))-0.055,c>vec3f(0.0031308));}
// Smooth animated cellular ridges, projected in the water plane's coordinates.
fn causticPattern(p: vec2f,t: f32) -> f32 {
    let cell=floor(p);let f=fract(p);var nearest=vec2f(8,8);
    for(var y=-1;y<=1;y++) {for(var x=-1;x<=1;x++) {
        let g=vec2f(f32(x),f32(y));let seed=cell+g;
        let phase=vec2f(dot(seed,vec2f(127.1,311.7)),dot(seed,vec2f(269.5,183.3)));
        let offset=.5+.32*sin(phase+t*vec2f(.8,1.1));
        let d=length(g+offset-f);
        if(d<nearest.x) {nearest=vec2f(d,nearest.x);} else {nearest.y=min(nearest.y,d);}
    }}
    return 1-smoothstep(.025,.14,nearest.y-nearest.x);
}
@fragment fn fragmentMain(@builtin(position) pixel: vec4f) -> @location(0) vec4f {
    let source=textureLoad(sourceColor,vec2i(pixel.xy),0);
    let depth=textureLoad(sourceDepth,vec2i(pixel.xy),0).r;
    let end=point(pixel.xy,depth);
    let dx=dpdx(end);let dy=dpdy(end);let surface=cross(dx,dy);
    let local=(fog.worldToLocal*vec4f(end,1)).xyz;
    let planeNormal=vec3f(fog.worldToLocal[0].y,fog.worldToLocal[1].y,fog.worldToLocal[2].y);
    let planeLength=length(planeNormal);let below=-local.y/planeLength;
    let facing=abs(dot(surface,planeNormal))/(max(length(surface),1e-8)*planeLength);
    var light=0.0;
    if(fog.caustics.x>0 && depth<.999999 && local.y < -fog.options.w &&
        local.x>=fog.minimum.x && local.x<=fog.maximum.x && local.z>=fog.minimum.z && local.z<=fog.maximum.z) {
        let units=vec2f(length(vec3f(fog.worldToLocal[0].x,fog.worldToLocal[1].x,fog.worldToLocal[2].x)),
                        length(vec3f(fog.worldToLocal[0].z,fog.worldToLocal[1].z,fog.worldToLocal[2].z)));
        light=causticPattern(local.xz/units*fog.caustics.y,fog.caustics.z)*fog.caustics.x*
            (1-smoothstep(0.0,fog.caustics.w,below))*smoothstep(.15,.75,facing)*fog.absorptionStrength.w;
    }
    let start=select(fog.eye.xyz,point(pixel.xy,0),fog.options.y>0.5);
    let path=distanceInWater(start,end)*fog.absorptionStrength.w;
    let attenuation=exp(-fog.absorptionStrength.xyz*path);
    let transmission=exp(-fog.colorDensity.w*path);
    return vec4f(encode(decode(source.rgb)*(1+light)*attenuation*transmission+fog.colorDensity.xyz*(1-transmission)),source.a);
}
