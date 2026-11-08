float4 traceScreenReflection(MeshOutput input,float3 normal,float3 ray,float rough){
    const float smoothness=1-smoothstep(.18,.5,rough);
    if(smoothness<=0||dot(ray,normal)<=.01)return 0;
    const float3 origin=input.world+normal*.04;
    float previousGap=-1,previousDistance=0;
    [loop]for(uint i=1;i<=24;++i){
        const float fraction=float(i)/24;
        const float distance=.08+10*fraction*fraction;
        const float3 rayPoint=origin+ray*distance;
        const float4 clip=mul(viewProjection,float4(rayPoint,1));
        if(clip.w<=.0001||clip.z<0||clip.z>=clip.w)break;
        const float2 uv=clip.xy/clip.w*float2(.5,-.5)+.5;
        const float2 pixel=uv*waterViewport.zw;
        const float edge=min(min(pixel.x,pixel.y),min(waterViewport.z-pixel.x,waterViewport.w-pixel.y));
        if(edge<1)break;
        const int2 samplePixel=int2(waterViewport.xy+pixel);
        const float depth=sceneWaterDepth.Load(int3(samplePixel,0));
        if(depth>=.999999){previousGap=-1;previousDistance=distance;continue;}
        const float2 center=float2(samplePixel)+.5;
        const float gap=dot(rayPoint-scenePoint(center,depth),viewForward.xyz);
        const float thickness=.10+.75*(distance-previousDistance)*abs(dot(ray,viewForward.xyz));
        if(previousGap<0&&gap>=0&&gap<thickness&&length(center-input.position.xy)>2){
            const float confidence=.9*smoothness*smoothstep(1,12,edge)*(1-smoothstep(7,10,distance));
            return float4(sceneWaterColor.Load(int3(samplePixel,0)).rgb,confidence);
        }
        previousGap=gap;previousDistance=distance;
    }
    return 0;
}
