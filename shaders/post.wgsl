struct Settings { params:vec4f, grade:vec4f }; // exposure multiplier, bloom strength, threshold; saturation, contrast, tone mapper
@group(0) @binding(0) var scene: texture_2d<f32>;
@group(0) @binding(1) var bloom: texture_2d<f32>;
@group(0) @binding(2) var linearSampler: sampler;
@group(0) @binding(3) var<uniform> settings: Settings;
@vertex fn vertexMain(@builtin(vertex_index) i:u32) -> @builtin(position) vec4f {
    let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));return vec4f(p[i],0,1);
}
@fragment fn extractMain(@builtin(position) p:vec4f) -> @location(0) vec4f {
    let size=vec2i(textureDimensions(scene));var sum=vec3f(0);
    for(var y=0;y<4;y++) {for(var x=0;x<4;x++) {
        let c=textureLoad(scene,clamp(vec2i(p.xy)*4+vec2i(x,y),vec2i(0),size-vec2i(1)),0).rgb;
        let peak=max(max(c.x,c.y),c.z);sum+=c*(max(peak-settings.params.z,0)/max(peak,1e-5));
    }}
    return vec4f(sum/16,1);
}
// Khronos PBR Neutral. Nonnegative linear Rec.709 in, bounded linear Rec.709 out.
fn pbrNeutral(color:vec3f) -> vec3f {
    let low=min(color.r,min(color.g,color.b));
    let toe=select(.04,low*(1-6.25*low),low<.08);
    let adjusted=color-vec3f(toe);
    let peak=max(adjusted.r,max(adjusted.g,adjusted.b));
    let shoulder=.76;
    if(peak<shoulder) { return adjusted; }
    let headroom=1-shoulder;
    let compressed=1-headroom*headroom/(peak+headroom-shoulder);
    let desaturate=1-1/(1+.15*(peak-compressed));
    return mix(adjusted*(compressed/peak),vec3f(compressed),desaturate);
}
@fragment fn compositeMain(@builtin(position) p:vec4f) -> @location(0) vec4f {
    let size=vec2f(textureDimensions(scene));let uv=p.xy/size;var glow=vec3f(0);var weight=0.0;
    if(settings.params.y>0) {
        for(var y=-2;y<=2;y++){for(var x=-2;x<=2;x++) {
            let w=f32((3-abs(x))*(3-abs(y)));weight+=w;
            glow+=textureSampleLevel(bloom,linearSampler,uv+vec2f(f32(x),f32(y))*2/vec2f(textureDimensions(bloom)),0).rgb*w;
        }}glow/=weight;
    }
    var c=max(textureLoad(scene,vec2i(p.xy),0).rgb+glow*settings.params.y,vec3f(0))*settings.params.x;
    if(settings.grade.z==1) { c=pbrNeutral(c); } else { c=c/(vec3f(1)+c); }
    c=mix(vec3f(dot(c,vec3f(.2126,.7152,.0722))),c,settings.grade.x);
    c=clamp((c-vec3f(.18))*settings.grade.y+vec3f(.18),vec3f(0),vec3f(1));
    return vec4f(select(12.92*c,1.055*pow(c,vec3f(1.0/2.4))-.055,c>vec3f(.0031308)),1);
}
