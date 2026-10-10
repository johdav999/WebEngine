struct Camera { viewProjection: mat4x4f, right: vec4f, up: vec4f };
@group(0) @binding(0) var<uniform> camera: Camera;
@group(1) @binding(0) var colorSampler: sampler;
@group(1) @binding(1) var colorTexture: texture_2d<f32>;
struct Output {
    @builtin(position) clip: vec4f,
    @location(0) uv: vec2f,
    @location(1) @interpolate(flat) color: vec4f,
};
@vertex fn particleVertex(@builtin(vertex_index) vertex: u32,
    @location(0) positionSize: vec4f, @location(1) color: vec4f,
    @location(2) ageRotation: vec4f, @location(3) atlas: vec4f) -> Output {
    let corners=array<vec2f,6>(vec2f(-0.5,-0.5),vec2f(0.5,-0.5),vec2f(-0.5,0.5),
        vec2f(-0.5,0.5),vec2f(0.5,-0.5),vec2f(0.5,0.5));
    let corner=corners[vertex];
    let c=cos(ageRotation.y);let s=sin(ageRotation.y);
    let rotated=vec2f(c*corner.x-s*corner.y,s*corner.x+c*corner.y)*positionSize.w;
    var out: Output;
    out.clip=camera.viewProjection*vec4f(positionSize.xyz+camera.right.xyz*rotated.x+camera.up.xyz*rotated.y,1);
    let age=clamp(ageRotation.x,0,1);
    let phase=select(fract(age*atlas.w),1.0,age>=1.0);
    let frame=min(u32(phase*atlas.z),u32(atlas.z)-1u);
    let grid=vec2f(atlas.x,atlas.y);
    let cell=vec2f(f32(frame%u32(atlas.x)),f32(frame/u32(atlas.x)));
    // Top-left frame order; half-texel inset and mip level zero prevent neighboring
    // atlas cells from bleeding into this frame, including transparent edges.
    let localUV=vec2f(corner.x+0.5,0.5-corner.y);
    let inset=0.5/vec2f(textureDimensions(colorTexture));
    out.uv=(cell+localUV)/grid+inset*(vec2f(1)-2*localUV);
    out.color=color;return out;
}
@fragment fn particleFragment(in: Output) -> @location(0) vec4f {
    let texel=textureSampleLevel(colorTexture,colorSampler,in.uv,0);
    let alpha=texel.a*in.color.a;
    let linear=texel.rgb*in.color.rgb;
    let mapped=linear/(vec3f(1)+linear);
    // Match the current mesh renderer's display conversion, premultiply for blending.
    return vec4f(pow(mapped,vec3f(1.0/2.2))*alpha,alpha);
}
