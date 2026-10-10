struct Screen { size: vec2f, padding: vec2f };
@group(0) @binding(0) var<uniform> screen: Screen;
@group(1) @binding(0) var imageSampler: sampler;
@group(1) @binding(1) var image: texture_2d<f32>;
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};
@vertex fn vertexMain(@location(0) position: vec2f, @location(1) uv: vec2f,
    @location(2) color: vec4f) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4f(position.x / screen.size.x * 2.0 - 1.0,
        1.0 - position.y / screen.size.y * 2.0, 0.0, 1.0);
    output.uv = uv;
    output.color = color;
    return output;
}
@fragment fn fragmentMain(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(image, imageSampler, input.uv) * input.color;
    return vec4f(color.rgb * color.a, color.a);
}
