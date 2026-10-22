// FOG_FUNCTIONS
struct Sky {
    right:vec4f, up:vec4f, forward:vec4f, horizon:vec4f, zenith:vec4f, nadir:vec4f,
    clipFromDirection:mat4x4f, sun:vec4f, air:vec4f, clouds:vec4f, cloudWind:vec4f, fog:Fog, fogRight:vec4f, fogUp:vec4f
};
@group(0) @binding(0) var<uniform> sky:Sky;
@group(1) @binding(0) var panoramaSampler:sampler;
@group(1) @binding(1) var panorama:texture_2d<f32>;
struct Output { @builtin(position) clip:vec4f, @location(0) direction:vec3f, @location(1) offset:vec3f };
@vertex fn vertexMain(@builtin(vertex_index) i:u32) -> Output {
    let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));
    var o:Output;o.clip=vec4f(p[i],0,1);
    o.offset=sky.fogRight.xyz*p[i].x+sky.fogUp.xyz*p[i].y;
    o.direction=sky.forward.xyz+sky.right.xyz*(p[i].x*sky.right.w)+sky.up.xyz*(p[i].y*sky.up.w);return o;
}
@vertex fn sphereVertex(@location(0) position:vec3f) -> Output {
    var o:Output;o.clip=sky.clipFromDirection*vec4f(position,1);o.clip.z=o.clip.w*0.9999;
    o.offset=(sky.fogRight.xyz*o.clip.x+sky.fogUp.xyz*o.clip.y)/o.clip.w;
    o.direction=select(sky.forward.xyz,position,sky.forward.w>0.5);return o;
}
fn boundary(p:vec3f,d:vec3f,r:f32) -> f32 {
    let b=dot(p,d);return -b+sqrt(max(0,b*b-dot(p,p)+r*r));
}
fn density(p:vec3f) -> vec2f {
    let h=max(length(p)-6360,0);return exp(-vec2f(h/8,h/1.2));
}
fn atmosphere(d:vec3f) -> vec3f {
    let daylight=smoothstep(-0.08,0.2,sky.sun.y);
    let night=vec3f(0.001,0.002,0.006);
    let ground=sky.nadir.xyz*(0.025+daylight*0.5);
    if(d.y< -0.08) {return ground;}
    let ray=normalize(vec3f(d.x,max(d.y,0),d.z));
    let origin=vec3f(0,6360+sky.air.w,0);
    let betaR=vec3f(0.005802,0.013558,0.033100)*sky.air.x;
    let betaM=vec3f(0.003996)*sky.air.y;
    let total=boundary(origin,ray,6460);
    var optical=vec2f(0);var sumR=vec3f(0);var sumM=vec3f(0);
    for(var i=0;i<8;i++) {
        let a=f32(i)/8;let b=f32(i+1)/8;
        let dt=(b*b-a*a)*total;let p=origin+ray*((a*a+b*b)*0.5*total);
        let rho=density(p);optical+=rho*dt*0.5;
        let q=dot(p,sky.sun.xyz);let discriminant=q*q-dot(p,p)+6360*6360;
        let blocked=discriminant>0 && -q-sqrt(max(discriminant,0))>0;
        if(!blocked) {
            let path=boundary(p,sky.sun.xyz,6460);var lightDepth=vec2f(0);
            for(var j=0;j<4;j++) {
                let x=f32(j)/4;let y=f32(j+1)/4;
                lightDepth+=density(p+sky.sun.xyz*((x*x+y*y)*0.5*path))*((y*y-x*x)*path);
            }
            let trans=exp(-betaR*(optical.x+lightDepth.x)-betaM*1.1*(optical.y+lightDepth.y));
            sumR+=trans*rho.x*dt;sumM+=trans*rho.y*dt;
        }
        optical+=rho*dt*0.5;
    }
    let mu=clamp(dot(d,sky.sun.xyz),-1,1);let g=sky.air.z;
    let phaseR=3*(1+mu*mu)/(16*3.14159265);
    let phaseM=3*(1-g*g)*(1+mu*mu)/(8*3.14159265*(2+g*g)*pow(max(1+g*g-2*g*mu,0.001),1.5));
    let trans=exp(-betaR*optical.x-betaM*1.1*optical.y);
    let disc=smoothstep(cos(0.009),cos(0.0045),mu)*smoothstep(-0.015,0.015,sky.sun.y);
    let moon=smoothstep(cos(0.012),cos(0.006),-mu)*(1-daylight);
    let scattering=night+(sumR*betaR*phaseR+sumM*betaM*phaseM)*sky.sun.w+trans*disc*sky.sun.w+vec3f(0.15,0.20,0.3)*moon;
    return mix(ground,scattering,smoothstep(-0.08,0,d.y));
}
fn hash(p:vec2f) -> f32 {
    var q=fract(vec3f(p.x,p.y,p.x)*0.1031);q+=vec3f(dot(q,q.yzx+vec3f(33.33)));return fract((q.x+q.y)*q.z);
}
fn noise(p:vec2f) -> f32 {
    let a=floor(p);let f=fract(p);let u=f*f*(vec2f(3)-2*f);
    return mix(mix(hash(a),hash(a+vec2f(1,0)),u.x),mix(hash(a+vec2f(0,1)),hash(a+vec2f(1,1)),u.x),u.y);
}
fn fbm(p:vec2f) -> f32 {
    var q=p;var weight=0.5;var value=0.0;
    for(var i=0;i<5;i++) {value+=weight*noise(q);q=q*2.03+vec2f(17.1,9.2);weight*=0.5;}
    return value;
}
fn cloudLayer(d:vec3f,background:vec3f) -> vec3f {
    if(sky.clouds.x<=0 || sky.clouds.y<=0 || d.y<=0.02) {return background;}
    let p=d.xz/max(d.y,0.03)*sky.clouds.z+sky.cloudWind.xy;
    let shape=smoothstep(1-sky.clouds.x,1-sky.clouds.x+0.2,fbm(p));
    let alpha=(1-exp(-shape*sky.clouds.y*3))*smoothstep(0.02,0.15,d.y);
    let daylight=smoothstep(-0.08,0.2,sky.sun.y);
    let shade=0.6+0.4*noise(p+sky.sun.xz*0.4);
    let warm=vec3f(1,0.45,0.22)*(1-smoothstep(0,0.3,sky.sun.y));
    let color=vec3f(0.015,0.025,0.05)+daylight*(vec3f(1.8,1.9,2.1)+warm)*shade;
    return mix(background,color,alpha);
}
@fragment fn fragmentMain(o:Output) -> @location(0) vec4f {
    let d=normalize(o.direction);
    var radiance=mix(sky.horizon.xyz,select(sky.nadir.xyz,sky.zenith.xyz,d.y>=0),abs(d.y));
    if(sky.nadir.w>1.5) {radiance=atmosphere(d);}
    else if(sky.nadir.w>0.5) {
        let c=cos(sky.zenith.w);let s=sin(sky.zenith.w);
        let rotated=vec3f(c*d.x+s*d.z,d.y,-s*d.x+c*d.z);
        let uv=vec2f(atan2(rotated.z,rotated.x)/6.28318530718+0.5,acos(clamp(rotated.y,-1,1))/3.14159265359);
        radiance=textureSampleLevel(panorama,panoramaSampler,uv,0).rgb;
    }
    let linear=cloudLayer(d,radiance)*sky.horizon.w;
    return vec4f(hdrEncode(fogApply(sky.fog,select(linear/(vec3f(1)+linear),linear,hdrOutput),sky.fog.eye.xyz+o.offset+d*(sky.fog.height.w/max(dot(d,sky.fog.forward.xyz),0.001)),false)),1);
}
