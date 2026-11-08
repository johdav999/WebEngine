struct DiffuseGiData {
    metadata: array<vec4f,3>,
    probes: array<vec4f,1536>,
};
@group(0) @binding(12) var<uniform> diffuseGi: DiffuseGiData;

// Positive and negative lobes store a cosine integral about each signed axis.
fn diffuseGiLobes(probe: u32, offset: u32, direction: vec3f) -> vec4f {
    let base = probe * 12u + offset;
    let x = diffuseGi.probes[base + select(1u, 0u, direction.x >= 0.0)];
    let y = diffuseGi.probes[base + select(3u, 2u, direction.y >= 0.0)];
    let z = diffuseGi.probes[base + select(5u, 4u, direction.z >= 0.0)];
    let weights = direction * direction;
    return vec4f(x.xyz*x.w, x.w)*weights.x + vec4f(y.xyz*y.w, y.w)*weights.y
         + vec4f(z.xyz*z.w, z.w)*weights.z;
}

fn diffuseGiVisibility(probe: u32, offset: vec3f, bias: f32) -> f32 {
    let distance = length(offset);
    if distance <= bias { return 1.0; }
    let moments = diffuseGiLobes(probe, 6u, offset / distance);
    if moments.w <= 0.0 { return 1.0; }
    let mean = moments.x / moments.w;
    let beyond = distance - mean - bias;
    if beyond <= 0.0 { return 1.0; }
    let variance = max(0.0, moments.y / moments.w - mean*mean);
    let probability = variance / max(variance + beyond*beyond, 1e-8);
    return probability*probability*probability;
}

fn sampleDiffuseGi(world: vec3f, normal: vec3f) -> vec4f {
    let high = diffuseGi.metadata[1];
    if high.w == 0.0 { return vec4f(0.0); }
    let low = diffuseGi.metadata[0].xyz;
    if any(world < low) || any(world > high.xyz) { return vec4f(0.0); }
    let counts = vec3u(diffuseGi.metadata[2].xyz);
    let last = vec3f(counts - vec3u(1u));
    let spacing = (high.xyz - low) / last;
    let coordinate = clamp((world - low) / spacing, vec3f(0.0), last);
    let edge = min(coordinate, last-coordinate);
    let fade = smoothstep(0.0, 0.5, min(edge.x, min(edge.y, edge.z)));
    if fade <= 0.0 { return vec4f(0.0); }
    let cell = min(vec3u(coordinate), counts - vec3u(2u));
    let fraction = coordinate - vec3f(cell);
    let bias = max(1e-4, 0.025 * min(spacing.x, min(spacing.y, spacing.z)));
    var radiance = vec3f(0.0);
    var valid = 0.0;
    var visible = 0.0;
    for (var corner=0u; corner<8u; corner++) {
        let bits = vec3u(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        let weights = select(vec3f(1.0)-fraction, fraction, bits != vec3u(0u));
        let trilinear = weights.x*weights.y*weights.z;
        if trilinear <= 0.0 { continue; }
        let node = cell+bits;
        let probe = node.x+counts.x*(node.y+counts.y*node.z);
        let light = diffuseGiLobes(probe, 0u, normal);
        if light.w <= 0.0 { continue; }
        let visibility = diffuseGiVisibility(probe, world-(low+vec3f(node)*spacing), bias);
        radiance += light.rgb*trilinear*visibility;
        valid += light.w*trilinear;
        visible += light.w*trilinear*visibility;
    }
    // Valid but occluded probes darken indirect light. Untraced probes fade to IBL.
    return vec4f(radiance / max(visible, max(valid*0.2, 1e-5)), clamp(valid, 0.0, 1.0)*fade);
}
