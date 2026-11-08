cbuffer DiffuseGi : register(b11) {
    float4 diffuseGiMetadata[3];
    float4 diffuseGiProbes[1536];
};

float4 diffuseGiLobes(uint probe, uint offset, float3 direction) {
    const uint base = probe*12+offset;
    const float4 x = diffuseGiProbes[base + (direction.x >= 0 ? 0 : 1)];
    const float4 y = diffuseGiProbes[base + (direction.y >= 0 ? 2 : 3)];
    const float4 z = diffuseGiProbes[base + (direction.z >= 0 ? 4 : 5)];
    const float3 weights = direction*direction;
    return float4(x.xyz*x.w,x.w)*weights.x + float4(y.xyz*y.w,y.w)*weights.y
         + float4(z.xyz*z.w,z.w)*weights.z;
}

float diffuseGiVisibility(uint probe, float3 offset, float bias) {
    const float distance = length(offset);
    if (distance <= bias) return 1;
    const float4 moments = diffuseGiLobes(probe,6,offset/distance);
    if (moments.w <= 0) return 1;
    const float mean = moments.x/moments.w;
    const float beyond = distance-mean-bias;
    if (beyond <= 0) return 1;
    const float variance = max(0,moments.y/moments.w-mean*mean);
    const float probability = variance/max(variance+beyond*beyond,1e-8);
    return probability*probability*probability;
}

float4 sampleDiffuseGi(float3 world, float3 normal) {
    const float4 high = diffuseGiMetadata[1];
    if (high.w == 0) return 0;
    const float3 low = diffuseGiMetadata[0].xyz;
    if (any(world < low) || any(world > high.xyz)) return 0;
    const uint3 counts = (uint3)diffuseGiMetadata[2].xyz;
    const float3 last = (float3)(counts-1);
    const float3 spacing = (high.xyz-low)/last;
    const float3 coordinate = clamp((world-low)/spacing,0,last);
    const float3 edge = min(coordinate,last-coordinate);
    const float fade = smoothstep(0,0.5,min(edge.x,min(edge.y,edge.z)));
    if (fade <= 0) return 0;
    const uint3 cell = min((uint3)coordinate,counts-2);
    const float3 fraction = coordinate-(float3)cell;
    const float bias = max(1e-4,0.025*min(spacing.x,min(spacing.y,spacing.z)));
    float3 radiance = 0;
    float valid = 0, visible = 0;
    [loop] for (uint corner=0; corner<8; ++corner) {
        const uint3 bits = uint3(corner&1,(corner>>1)&1,(corner>>2)&1);
        const float3 weights = lerp(1-fraction,fraction,(float3)bits);
        const float trilinear = weights.x*weights.y*weights.z;
        if (trilinear <= 0) continue;
        const uint3 node = cell+bits;
        const uint probe = node.x+counts.x*(node.y+counts.y*node.z);
        const float4 light = diffuseGiLobes(probe,0,normal);
        if (light.w <= 0) continue;
        const float visibility = diffuseGiVisibility(probe,world-(low+(float3)node*spacing),bias);
        radiance += light.rgb*trilinear*visibility;
        valid += light.w*trilinear;
        visible += light.w*trilinear*visibility;
    }
    return float4(radiance/max(visible,max(valid*0.2,1e-5)),saturate(valid)*fade);
}
