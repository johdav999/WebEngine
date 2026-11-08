// FOG_FUNCTIONS
struct Cloud {layer:vec4f,eye:vec4f,sun:vec4f,shadow:vec4f};
struct Sky {right:vec4f,up:vec4f,forward:vec4f,horizon:vec4f,zenith:vec4f,nadir:vec4f,clipFromDirection:mat4x4f,sun:vec4f,air:vec4f,clouds:vec4f,cloudWind:vec4f,fog:Fog,fogRight:vec4f,fogUp:vec4f,cloudLayer:vec4f,cloudShape:vec4f,cloudOrigin:vec4f,cloud:Cloud};
@group(0) @binding(0) var<uniform> sky:Sky;
@group(1) @binding(0) var panoramaSampler:sampler;
@group(1) @binding(2) var cloudNoiseAtlas:texture_2d<f32>;
fn cloudLayer(d:vec3f,bg:vec3f)->vec3f {return bg;}
// CLOUD_VOLUME

struct Output {@builtin(position) position:vec4f,@location(0) uv:vec2f};
@vertex fn cloudShadowVertex(@builtin(vertex_index) i:u32)->Output {let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));var o:Output;o.position=vec4f(p[i],0,1);o.uv=p[i]*vec2f(.5,-.5)+.5;return o;}
@fragment fn cloudShadowFragment(o:Output)->@builtin(frag_depth) f32 {
    let c=sky.cloud;let entry=c.shadow.xy+(o.uv-.5)*(2*c.shadow.z);
    let origin=vec3f(sky.cloudOrigin.x+(entry.x-c.eye.x)*sky.cloudOrigin.w,c.layer.x*sky.cloudOrigin.w,sky.cloudOrigin.z+(entry.y-c.eye.z)*sky.cloudOrigin.w);
    let distance=min(sky.cloudLayer.w,c.layer.y/c.sun.y);var optical=0.0;
    for(var i=0u;i<16u;i++) {let s=(f32(i)+.5)*distance/16.0;optical+=cloudDensity(origin+c.sun.xyz*(s*sky.cloudOrigin.w),c.layer.x+c.sun.y*s)*(distance/16.0);}
    return exp(-min(80.0,optical*sky.cloudLayer.z));
}
