// SkyUniform and post settings follow the browser sky/post shader contracts.
cbuffer Sky : register(b4) {
    float4 skyRight,skyUp,skyForward,skyHorizon,skyZenith,skyNadir;
    column_major float4x4 skyClip;
    float4 skySun,skyAir,skyClouds,skyWind;
    Fog skyFog;float4 fogRight,fogUp,skyCloudLayer,skyCloudShape,skyCloudOrigin;
    CloudData skyCloud;float4 skyOutput;
};
Texture2D skyCloudNoise : register(t27);
cbuffer Post : register(b5) {float4 postParams,postGrade;};
Texture2D postScene : register(t9);
Texture2D postBloom : register(t10);
struct SkyOutput {float4 clip:SV_Position;float3 direction:TEXCOORD0;float3 offset:TEXCOORD1;};
float2 fullPosition(uint i) {return i==0?float2(-1,-1):i==1?float2(3,-1):float2(-1,3);}
float4 fullVS(uint i:SV_VertexID):SV_Position {return float4(fullPosition(i),0,1);}
SkyOutput skyVS(uint i:SV_VertexID) {
    const float2 p=fullPosition(i);SkyOutput o;o.clip=float4(p,0,1);
    o.offset=fogRight.xyz*p.x+fogUp.xyz*p.y;
    o.direction=skyForward.xyz+skyRight.xyz*(p.x*skyRight.w)+skyUp.xyz*(p.y*skyUp.w);return o;
}
SkyOutput skySphereVS(float3 position:POSITION) {
    SkyOutput o;o.clip=mul(skyClip,float4(position,1));o.clip.z=o.clip.w*.9999;
    o.offset=(fogRight.xyz*o.clip.x+fogUp.xyz*o.clip.y)/o.clip.w;
    o.direction=skyForward.w>.5?position:skyForward.xyz;return o;
}
float boundary(float3 p,float3 d,float r) {const float b=dot(p,d);return -b+sqrt(max(0,b*b-dot(p,p)+r*r));}
float2 density(float3 p) {const float h=max(length(p)-6360,0);return exp(-float2(h/8,h/1.2));}
float3 atmosphere(float3 d) {
    const float daylight=smoothstep(-.08,.2,skySun.y);const float3 night=float3(.001,.002,.006),ground=skyNadir.xyz*(.025+daylight*.5);
    if(d.y<-.08)return ground;
    const float3 ray=normalize(float3(d.x,max(d.y,0),d.z)),origin=float3(0,6360+skyAir.w,0);
    const float3 betaR=float3(.005802,.013558,.033100)*skyAir.x,betaM=float3(.003996,.003996,.003996)*skyAir.y;
    const float total=boundary(origin,ray,6460);float2 optical=0;float3 sumR=0,sumM=0;
    for(int i=0;i<8;i++) {
        const float a=float(i)/8,b=float(i+1)/8,dt=(b*b-a*a)*total;
        const float3 p=origin+ray*((a*a+b*b)*.5*total);const float2 rho=density(p);optical+=rho*dt*.5;
        const float q=dot(p,skySun.xyz),discriminant=q*q-dot(p,p)+6360*6360;
        const bool blocked=discriminant>0&&-q-sqrt(max(discriminant,0))>0;
        if(!blocked) {
            const float path=boundary(p,skySun.xyz,6460);float2 lightDepth=0;
            for(int j=0;j<4;j++) {const float x=float(j)/4,y=float(j+1)/4;lightDepth+=density(p+skySun.xyz*((x*x+y*y)*.5*path))*((y*y-x*x)*path);}
            const float3 trans=exp(-betaR*(optical.x+lightDepth.x)-betaM*1.1*(optical.y+lightDepth.y));
            sumR+=trans*rho.x*dt;sumM+=trans*rho.y*dt;
        }
        optical+=rho*dt*.5;
    }
    const float mu=clamp(dot(d,skySun.xyz),-1,1),g=skyAir.z;
    const float phaseR=3*(1+mu*mu)/(16*3.14159265);
    const float phaseM=3*(1-g*g)*(1+mu*mu)/(8*3.14159265*(2+g*g)*pow(max(1+g*g-2*g*mu,.001),1.5));
    const float3 trans=exp(-betaR*optical.x-betaM*1.1*optical.y);
    const float disc=smoothstep(cos(.009),cos(.0045),mu)*smoothstep(-.015,.015,skySun.y);
    const float moon=smoothstep(cos(.012),cos(.006),-mu)*(1-daylight);
    const float3 scattering=night+(sumR*betaR*phaseR+sumM*betaM*phaseM)*skySun.w+trans*disc*skySun.w+float3(.15,.2,.3)*moon;
    return lerp(ground,scattering,smoothstep(-.08,0,d.y));
}
float skyHash(float2 p) {float3 q=frac(float3(p.x,p.y,p.x)*.1031);q+=dot(q,q.yzx+33.33);return frac((q.x+q.y)*q.z);}
float skyNoise(float2 p) {
    const float2 a=floor(p),f=frac(p),u=f*f*(3-2*f);
    return lerp(lerp(skyHash(a),skyHash(a+float2(1,0)),u.x),lerp(skyHash(a+float2(0,1)),skyHash(a+float2(1,1)),u.x),u.y);
}
float fbm(float2 p) {float2 q=p;float weight=.5,value=0;for(int i=0;i<5;i++){value+=weight*skyNoise(q);q=q*2.03+float2(17.1,9.2);weight*=.5;}return value;}
float3 cloudLayer(float3 d,float3 background) {
    if(skyClouds.x<=0||skyClouds.y<=0||d.y<=.02)return background;
    const float2 p=d.xz/max(d.y,.03)*skyClouds.z+skyWind.xy;
    const float shape=smoothstep(1-skyClouds.x,1-skyClouds.x+.2,fbm(p));
    const float alpha=(1-exp(-shape*skyClouds.y*3))*smoothstep(.02,.15,d.y),daylight=smoothstep(-.08,.2,skySun.y),shade=.6+.4*skyNoise(p+skySun.xz*.4);
    const float3 warm=float3(1,.45,.22)*(1-smoothstep(0,.3,skySun.y));
    return lerp(background,float3(.015,.025,.05)+daylight*(float3(1.8,1.9,2.1)+warm)*shade,alpha);
}
float2 cloudNoise(float3 samplePoint) {
    const float3 node=frac(samplePoint)*64.0-.5;
    const uint slice=uint(floor(node.z)+64.0)%64;
    const uint next=(slice+1)%64;
    const float2 tile=float2(slice%8,slice/8)*66.0;
    const float2 nextTile=float2(next%8,next/8)*66.0;
    const float2 a=skyCloudNoise.SampleLevel(clampClamp,(tile+node.xy+1.5)/528.0,0).rg;
    const float2 b=skyCloudNoise.SampleLevel(clampClamp,(nextTile+node.xy+1.5)/528.0,0).rg;
    return lerp(a,b,frac(node.z));
}
float cloudDensity(float3 samplePoint,float height) {
    const float h=(height-skyCloudLayer.x)/skyCloudLayer.y;
    if(h<=0.0||h>=1.0)return 0.0;
    const float2 value=cloudNoise(samplePoint);
    const float shape=smoothstep(1.0-skyClouds.x,1.0-skyClouds.x+.18,
        value.x-skyCloudShape.x*(1.0-value.y)*.12);
    const float profile=smoothstep(0.0,.16,h)*(1.0-smoothstep(.65,1.0,h));
    return shape*profile*skyClouds.y;
}
float cloudSunlight(float3 samplePoint,float height) {
    if(skySun.y<=.01)return 0.0;
    const float distance=min(skyCloudLayer.w,max(0.0,(skyCloudLayer.x+skyCloudLayer.y-height)/skySun.y));
    float optical=0.0;
    [loop]for(uint j=0;j<4;++j) {
        const float a=float(j)/4.0,b=float(j+1)/4.0;
        const float s=(a*a+b*b)*.5*distance;
        optical+=cloudDensity(samplePoint+skySun.xyz*(s*skyCloudOrigin.w),height+skySun.y*s)*((b*b-a*a)*distance);
    }
    return exp(-min(80.0,optical*skyCloudLayer.z));
}
float3 volumetricClouds(float3 d,float3 offset,float3 background) {
    if(skyCloudShape.z<1.0)return cloudLayer(d,background);
    if(skyClouds.x<=0.0||skyClouds.y<=0.0||skyCloudLayer.z<=0.0)return background;
    const float originY=skyCloudOrigin.y+offset.y;
    float enter=0.0,leave=skyCloudLayer.w;
    if(abs(d.y)<.00001) {
        if(originY<=skyCloudLayer.x||originY>=skyCloudLayer.x+skyCloudLayer.y)return background;
    } else {
        const float a=(skyCloudLayer.x-originY)/d.y;
        const float b=(skyCloudLayer.x+skyCloudLayer.y-originY)/d.y;
        enter=max(0.0,min(a,b));leave=min(leave,max(a,b));
    }
    if(leave<=enter)return background;
    const uint steps=uint(clamp(skyCloudShape.z,1.0,64.0));
    const float ds=(leave-enter)/float(steps);
    const float day=smoothstep(-.08,.2,skySun.y);
    const float3 ambient=lerp(float3(.012,.018,.035),float3(.18,.22,.29),day);
    const float3 sunColor=lerp(float3(1.0,.45,.22),float3(1.0,.98,.92),smoothstep(0.0,.35,skySun.y));
    const float g=skyCloudShape.y,mu=clamp(dot(d,skySun.xyz),-1.0,1.0);
    const float phase=(1.0-g*g)/pow(max(1.0+g*g-2.0*g*mu,.001),1.5);
    const float3 origin=float3(skyCloudOrigin.x+offset.x*skyCloudOrigin.w,originY*skyCloudOrigin.w,
        skyCloudOrigin.z+offset.z*skyCloudOrigin.w);
    float transmission=1.0;
    float3 radiance=0;
    [loop]for(uint i=0;i<64;++i) {
        if(i>=steps||transmission<.01)break;
        const float distance=enter+(float(i)+.5)*ds;
        const float height=originY+d.y*distance;
        const float3 samplePoint=origin+d*(distance*skyCloudOrigin.w);
        const float rho=cloudDensity(samplePoint,height);
        if(rho>.0001) {
            const float t=exp(-min(80.0,rho*skyCloudLayer.z*ds));
            const float3 source=ambient+sunColor*(day*skySun.w*.08*phase*cloudSunlight(samplePoint,height));
            radiance+=transmission*(1.0-t)*source;transmission*=t;
        }
    }
    return radiance+transmission*background;
}
float4 skyFragment(SkyOutput o,bool volume) {
    const float3 d=normalize(o.direction);float3 radiance=lerp(skyHorizon.xyz,d.y>=0?skyZenith.xyz:skyNadir.xyz,abs(d.y));
    if(skyNadir.w>1.5)radiance=atmosphere(d);
    else if(skyNadir.w>.5) {
        const float c=cos(skyZenith.w),s=sin(skyZenith.w);const float3 rotated=float3(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
        const float2 uv=float2(atan2(rotated.z,rotated.x)/6.28318530718+.5,acos(clamp(rotated.y,-1,1))/3.14159265359);
        radiance=baseImage.SampleLevel(repeatClamp,uv,0).rgb;
    }
    float3 value;
    if(volume&&skyCloud.layer.w>.5) {
        const float2 uv=(o.clip.xy-waterViewport.xy)/waterViewport.zw;
        const float4 medium=cloudGridSlice(uv,32.0);
        value=medium.rgb+medium.a*radiance*skyHorizon.w;
    } else value=(volume?volumetricClouds(d,o.offset,radiance):cloudLayer(d,radiance))*skyHorizon.w;
    const float3 display=fogVolumeApply(skyOutput.x>.5?value:value/(1+value),skyFog.eye.xyz+o.offset+d*(skyFog.height.w/max(dot(d,skyFog.forward.xyz),.001)),false);
    return float4(skyOutput.x>.5?display:pow(max(display,0),1.0/2.2),1);
}
float4 skyPS(SkyOutput o):SV_Target {return skyFragment(o,false);}
float4 skyVolumePS(SkyOutput o):SV_Target {return skyFragment(o,true);}
float4 cloudGridPS(float4 pixel:SV_Position):SV_Target {
    const float row=pixel.y-576.0,slice=floor(row/36.0)+1.0;
    const float2 uv=float2(pixel.x,row-(slice-1.0)*36.0)/float2(64,36);
    const float2 ndc=uv*float2(2,-2)+float2(-1,1);
    const float3 offset=fogRight.xyz*ndc.x+fogUp.xyz*ndc.y;
    const float3 d=normalize(skyForward.xyz+skyRight.xyz*(ndc.x*skyRight.w)+skyUp.xyz*(ndc.y*skyUp.w));
    const float originY=skyCloudOrigin.y+offset.y;
    float enter=0.0,leave=skyCloudLayer.w;
    if(abs(d.y)<.00001) {
        if(originY<=skyCloudLayer.x||originY>=skyCloudLayer.x+skyCloudLayer.y)leave=0;
    } else {
        const float a=(skyCloudLayer.x-originY)/d.y,b=(skyCloudLayer.x+skyCloudLayer.y-originY)/d.y;
        enter=max(0.0,min(a,b));leave=min(leave,max(a,b));
    }
    const uint count=uint(clamp(skyCloudShape.z,1.0,64.0));
    const float ds=max(0.0,leave-enter)/float(count),prefix=slice*float(count)/32.0;
    const float3 origin=float3(skyCloudOrigin.x+offset.x*skyCloudOrigin.w,originY*skyCloudOrigin.w,skyCloudOrigin.z+offset.z*skyCloudOrigin.w);
    const float day=smoothstep(-.08,.2,skySun.y);
    const float3 ambient=lerp(float3(.012,.018,.035),float3(.18,.22,.29),day);
    const float3 sunColor=lerp(float3(1,.45,.22),float3(1,.98,.92),smoothstep(0.0,.35,skySun.y));
    const float g=skyCloudShape.y;
    const float phase=(1-g*g)/pow(max(1+g*g-2*g*clamp(dot(d,skySun.xyz),-1.0,1.0),.001),1.5);
    float3 radiance=0;
    float transmission=1.0;
    [loop]for(uint step=0;step<64;++step) {
        if(step>=count||float(step)>=prefix||transmission<.01)break;
        // Prefixes retain the full-ray midpoint locations. Only the last
        // interval length is truncated, matching the streamed compute grid.
        const float distance=enter+(float(step)+.5)*ds,height=originY+d.y*distance;
        const float3 samplePoint=origin+d*(distance*skyCloudOrigin.w);
        const float rho=cloudDensity(samplePoint,height);
        if(rho>.0001) {
            const float t=exp(-min(80.0,rho*skyCloudLayer.z*ds*saturate(prefix-float(step))));
            const float3 source=(ambient+sunColor*(day*skySun.w*.08*phase*cloudSunlight(samplePoint,height)))*skyHorizon.w;
            radiance+=transmission*(1-t)*source;transmission*=t;
        }
    }
    return float4(min(radiance,65504.0),transmission);
}
float cloudShadowPS(float4 pixel:SV_Position):SV_Depth {
    const float2 uv=pixel.xy*skyCloud.shadow.w;
    const float2 entry=skyCloud.shadow.xy+(uv-.5)*(2*skyCloud.shadow.z);
    const float3 origin=float3(skyCloudOrigin.x+(entry.x-skyCloud.eye.x)*skyCloudOrigin.w,
        skyCloud.layer.x*skyCloudOrigin.w,skyCloudOrigin.z+(entry.y-skyCloud.eye.z)*skyCloudOrigin.w);
    const float distance=min(skyCloudLayer.w,skyCloud.layer.y/skyCloud.sun.y);
    float optical=0.0;
    [loop]for(uint i=0;i<16;++i) {
        const float s=(float(i)+.5)*distance/16.0;
        optical+=cloudDensity(origin+skyCloud.sun.xyz*(s*skyCloudOrigin.w),skyCloud.layer.x+skyCloud.sun.y*s)*(distance/16.0);
    }
    return exp(-min(80.0,optical*skyCloudLayer.z));
}
float4 bloomPS(float4 p:SV_Position):SV_Target {
    uint w,h;postScene.GetDimensions(w,h);float3 sum=0;
    [unroll]for(int y=0;y<4;y++)[unroll]for(int x=0;x<4;x++) {
        const float3 c=postScene.Load(int3(clamp(int2(p.xy)*4+int2(x,y),int2(0,0),int2(w,h)-1),0)).rgb;
        const float peak=max(max(c.x,c.y),c.z);sum+=c*(max(peak-postParams.z,0)/max(peak,1e-5));
    }return float4(sum/16,1);
}
// Khronos PBR Neutral. Nonnegative linear Rec.709 in, bounded linear Rec.709 out.
float3 pbrNeutral(float3 color) {
    const float low=min(color.r,min(color.g,color.b));
    const float toe=low<.08 ? low*(1-6.25*low) : .04;
    const float3 adjusted=color-toe;
    const float peak=max(adjusted.r,max(adjusted.g,adjusted.b));
    const float shoulder=.76;
    if(peak<shoulder) return adjusted;
    const float headroom=1-shoulder;
    const float compressed=1-headroom*headroom/(peak+headroom-shoulder);
    const float desaturate=1-1/(1+.15*(peak-compressed));
    return lerp(adjusted*(compressed/peak),compressed,desaturate);
}
float4 compositePS(float4 p:SV_Position):SV_Target {
    uint w,h,bw,bh;postScene.GetDimensions(w,h);postBloom.GetDimensions(bw,bh);const float2 uv=p.xy/float2(w,h);float3 glow=0;float weight=0;
    if(postParams.y>0) {
        [unroll]for(int y=-2;y<=2;y++)[unroll]for(int x=-2;x<=2;x++) {
            const float v=float((3-abs(x))*(3-abs(y)));weight+=v;
            glow+=postBloom.SampleLevel(environmentSampler,uv+float2(x,y)*2/float2(bw,bh),0).rgb*v;
        }glow/=weight;
    }
    float3 c=max(postScene.Load(int3(int2(p.xy),0)).rgb+glow*postParams.y,0)*postParams.x;
    c=postGrade.z==1 ? pbrNeutral(c) : c/(1+c);
    c=lerp(dot(c,float3(.2126,.7152,.0722)),c,postGrade.x);c=saturate((c-.18)*postGrade.y+.18);
    return float4(srgb(c),1);
}
