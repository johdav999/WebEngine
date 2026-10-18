@group(0) @binding(0) var sourceColor: texture_2d<f32>;
@group(0) @binding(1) var sourceDepth: texture_depth_2d;
const samples=1;
@vertex fn vertexMain(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));return vec4f(p[index],0,1);
}
struct Capture { @location(0) color: vec4f, @location(1) depth: f32 };
@fragment fn captureMain(@builtin(position) position: vec4f) -> Capture {
    var color=vec4f(0);var depth=1.0;
    for(var i=0;i<samples;i++) {
        color+=textureLoad(sourceColor,vec2i(position.xy),i);
        depth=min(depth,textureLoad(sourceDepth,vec2i(position.xy),i));
    }
    var out: Capture;out.color=color/f32(samples);out.depth=depth;return out;
}
@fragment fn presentMain(@builtin(position) position: vec4f) -> @location(0) vec4f {
    return textureLoad(sourceColor,vec2i(position.xy),0);
}
