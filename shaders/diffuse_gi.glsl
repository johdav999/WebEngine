uniform highp vec4 diffuseGiMetadata[3];

vec4 diffuseGiLobes(int probe, int offset, vec3 direction) {
    // Non-water World draws bind a row of 12 probe records on this sampler.
    vec4 x = texelFetch(waterNormalTexture,ivec2(offset + (direction.x >= 0.0 ? 0 : 1),probe),0);
    vec4 y = texelFetch(waterNormalTexture,ivec2(offset + (direction.y >= 0.0 ? 2 : 3),probe),0);
    vec4 z = texelFetch(waterNormalTexture,ivec2(offset + (direction.z >= 0.0 ? 4 : 5),probe),0);
    vec3 weights = direction*direction;
    return vec4(x.xyz*x.w,x.w)*weights.x + vec4(y.xyz*y.w,y.w)*weights.y
         + vec4(z.xyz*z.w,z.w)*weights.z;
}

float diffuseGiVisibility(int probe, vec3 offset, float bias) {
    float distance = length(offset);
    if (distance <= bias) return 1.0;
    vec4 moments = diffuseGiLobes(probe,6,offset/distance);
    if (moments.w <= 0.0) return 1.0;
    float mean = moments.x/moments.w;
    float beyond = distance-mean-bias;
    if (beyond <= 0.0) return 1.0;
    float variance = max(0.0,moments.y/moments.w-mean*mean);
    float probability = variance/max(variance+beyond*beyond,1e-8);
    return probability*probability*probability;
}

vec4 sampleDiffuseGi(vec3 world, vec3 normal) {
    vec4 high = diffuseGiMetadata[1];
    if (high.w == 0.0) return vec4(0.0);
    vec3 low = diffuseGiMetadata[0].xyz;
    if (any(lessThan(world,low)) || any(greaterThan(world,high.xyz))) return vec4(0.0);
    ivec3 counts = ivec3(diffuseGiMetadata[2].xyz);
    vec3 last = vec3(counts-1);
    vec3 spacing = (high.xyz-low)/last;
    vec3 coordinate = clamp((world-low)/spacing,vec3(0.0),last);
    vec3 edge = min(coordinate,last-coordinate);
    float fade = smoothstep(0.0,0.5,min(edge.x,min(edge.y,edge.z)));
    if (fade <= 0.0) return vec4(0.0);
    ivec3 cell = min(ivec3(coordinate),counts-2);
    vec3 fraction = coordinate-vec3(cell);
    float bias = max(1e-4,0.025*min(spacing.x,min(spacing.y,spacing.z)));
    vec3 radiance = vec3(0.0);
    float valid = 0.0, visible = 0.0;
    for (int corner=0; corner<8; ++corner) {
        ivec3 bits = ivec3(corner&1,(corner>>1)&1,(corner>>2)&1);
        vec3 weights = mix(1.0-fraction,fraction,vec3(bits));
        float trilinear = weights.x*weights.y*weights.z;
        if (trilinear <= 0.0) continue;
        ivec3 node = cell+bits;
        int probe = node.x+counts.x*(node.y+counts.y*node.z);
        vec4 light = diffuseGiLobes(probe,0,normal);
        if (light.w <= 0.0) continue;
        float visibility = diffuseGiVisibility(probe,world-(low+vec3(node)*spacing),bias);
        radiance += light.rgb*trilinear*visibility;
        valid += light.w*trilinear;
        visible += light.w*trilinear*visibility;
    }
    return vec4(radiance/max(visible,max(valid*0.2,1e-5)),clamp(valid,0.0,1.0)*fade);
}
