// MaterialLayers payload and sampling follow materialLayerParameters / mesh.wgsl.
struct LayerSample {float4 color;float3 normal;float height;};
LayerSample sampleLayer(Texture2D colorMap,Texture2D detailMap,uint index,MeshOutput input) {
    const float4 transform=layerUv[index],detail=layerDetail[index];
    const float3 n=safeNormal(input.normal,float3(0,1,0));
    const float3 t=safeNormal(input.tangent.xyz-n*dot(n,input.tangent.xyz),float3(1,0,0)),b=cross(n,t)*input.tangent.w;
    float4 color=0;float3 normal=0;float height=0;
    const float2 orient=sign(transform.xy*uvTransform.xy);
    if(layerOptions.x>1.5) {
        const float3 axis=pow(abs(n),layerOptions.w),weights=axis/max(dot(axis,float3(1,1,1)),1e-6);
        const float3 signN=float3(n.x>=0?1:-1,n.y>=0?1:-1,n.z>=0?1:-1),p=input.world*layerOptions.z;
        const float2 coord[3]={float2(-p.z*signN.x,p.y),float2(p.x,-p.z*signN.y),float2(p.x*signN.z,p.y)};
        const float3 tangents[3]={float3(0,0,-signN.x),float3(1,0,0),float3(signN.z,0,0)};
        const float3 bitangents[3]={float3(0,1,0),float3(0,0,-signN.y),float3(0,1,0)};
        [unroll]for(uint k=0;k<3;k++) {
            const float2 q=(coord[k]*uvTransform.xy+uvTransform.zw)*transform.xy+transform.zw;
            const float4 relief=sampleMap(detailMap,5+index,q);
            const float3 v=(relief.xyz*2-1)*float3(detail.x*detail.w,detail.x*detail.w,1);
            color+=sampleMap(colorMap,index,q)*weights[k];height+=relief.a*weights[k];
            normal+=(n*v.z+tangents[k]*v.x*orient.x+bitangents[k]*v.y*orient.y)*weights[k];
        }
    }else{
        const float2 q=input.uv*transform.xy+transform.zw;const float4 relief=sampleMap(detailMap,5+index,q);
        color=sampleMap(colorMap,index,q);height=relief.a;
        const float3 v=(relief.xyz*2-1)*float3(detail.x*detail.w,detail.x*detail.w,1);
        normal=t*v.x*orient.x+b*v.y*orient.y+n*v.z;
    }
    LayerSample result;result.color=color;result.normal=safeNormal(normal,n);result.height=(height*detail.y+detail.z)*detail.w;return result;
}
struct LayerSurface {float4 color;float roughness;float3 normal;};
LayerSurface layerSurface(MeshOutput input) {
    float4 weights=sampleMap(emissiveImage,4,saturate(input.uv*weightUv.xy+weightUv.zw));
    const float total=dot(weights,float4(1,1,1,1));weights=total>1e-5?weights/max(total,1e-5):float4(1,0,0,0);
    const LayerSample a=sampleLayer(baseImage,layerDetail0,0,input),b=sampleLayer(surfaceImage,layerDetail1,1,input);
    const LayerSample c=sampleLayer(normalImage,layerDetail2,2,input),d=sampleLayer(occlusionImage,layerDetail3,3,input);
    if(layerOptions.y>0) {
        const float4 score=float4(a.height,b.height,c.height,d.height)+weights;
        const float4 active=float4(weights.x>0?score.x:-1e6,weights.y>0?score.y:-1e6,weights.z>0?score.z:-1e6,weights.w>0?score.w:-1e6);
        const float peak=max(max(active.x,active.y),max(active.z,active.w));
        const float4 adjusted=max(score-(peak-layerOptions.y),0)*weights;const float sum=dot(adjusted,float4(1,1,1,1));
        if(sum>1e-6)weights=adjusted/max(sum,1e-6);
    }
    LayerSurface result;
    result.color=weights.x*a.color*float4(layerColor[0].xyz,1)+weights.y*b.color*float4(layerColor[1].xyz,1)+weights.z*c.color*float4(layerColor[2].xyz,1)+weights.w*d.color*float4(layerColor[3].xyz,1);
    result.roughness=dot(weights,float4(layerColor[0].w,layerColor[1].w,layerColor[2].w,layerColor[3].w));
    result.normal=safeNormal(weights.x*a.normal+weights.y*b.normal+weights.z*c.normal+weights.w*d.normal,safeNormal(input.normal,float3(0,1,0)));
    return result;
}
