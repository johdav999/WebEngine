#include "engine/environment.hpp"
#include <bit>

namespace engine {
namespace {
constexpr std::uint32_t samples=128;
float radical(std::uint32_t bits) {
    bits=(bits<<16)|(bits>>16);bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
    bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
    bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);return float(bits)*2.3283064365386963e-10f;
}
Vec3 oriented(Vec3 n,Vec3 local) {
    const auto t=normalized(cross(std::abs(n.z)<0.999f ? Vec3{0,0,1} : Vec3{1,0,0},n));
    return t*local.x+cross(n,t)*local.y+n*local.z;
}
Vec3 ggx(Vec3 n,float roughness,float x,float y) {
    const float a=roughness*roughness,phi=2*pi*x,c=std::sqrt((1-y)/(1+(a*a-1)*y)),s=std::sqrt(std::max(0.0f,1-c*c));
    return oriented(n,{s*std::cos(phi),s*std::sin(phi),c});
}
std::array<float,4> pixel(Vec3 v) {
    if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||v.x<0||v.y<0||v.z<0||std::max({v.x,v.y,v.z})>65000)
        throw std::invalid_argument("Environment radiance must be finite, positive and representable in float16");
    return {v.x,v.y,v.z,1};
}
}
Vec3 cubeDirection(std::uint32_t face,float u,float v) {
    switch(face) {
        case 0:return normalized({1,-v,-u});case 1:return normalized({-1,-v,u});
        case 2:return normalized({u,1,v});case 3:return normalized({u,-1,-v});
        case 4:return normalized({u,-v,1});case 5:return normalized({-u,-v,-1});
        default:throw std::invalid_argument("Invalid cube face");
    }
}
std::uint16_t floatToHalf(float value) {
    const auto bits=std::bit_cast<std::uint32_t>(value);const auto sign=(bits>>16)&0x8000u;
    const int exp=int((bits>>23)&255)-127+15;const auto mantissa=bits&0x7fffffu;
    if(exp>=31) return std::uint16_t(sign|0x7c00u|(mantissa?0x200u:0));
    if(exp<=0) {
        if(exp<-10) return std::uint16_t(sign);
        const auto m=mantissa|0x800000u;const int shift=14-exp;
        return std::uint16_t(sign|((m+((1u<<(shift-1))-1)+((m>>shift)&1))>>shift));
    }
    return std::uint16_t(sign|((std::uint32_t(exp)<<10)+(mantissa+0xfffu+((mantissa>>13)&1))/0x2000u));
}
void Environment::validate() const {
    if(specular.empty()||specular.front().size<4||specular.front().size>128||!lutSize||lutSize>128) throw std::invalid_argument("Invalid environment dimensions");
    std::size_t levelCount=1;for(auto size=specular.front().size;size>1;size/=2) ++levelCount;
    if(specular.size()!=levelCount) throw std::invalid_argument("Invalid environment mip count");
    auto level=[](const EnvironmentLevel& l) {
        if(!l.size||l.pixels.size()!=std::size_t(l.size)*l.size*6) throw std::invalid_argument("Invalid environment cube payload");
        for(const auto& p:l.pixels) pixel({p[0],p[1],p[2]});
    };
    auto size=specular.front().size;
    for(const auto& l:specular) { if(l.size!=size) throw std::invalid_argument("Invalid environment mip chain");level(l);size=std::max(size/2,1u); }
    if(specular.back().size!=1) throw std::invalid_argument("Incomplete environment mip chain");
    if(diffuse.size>128) throw std::invalid_argument("Invalid diffuse cube dimensions");
    level(diffuse);
    if(brdf.size()!=std::size_t(lutSize)*lutSize) throw std::invalid_argument("Invalid BRDF LUT");
    for(const auto& p:brdf) pixel({p[0],p[1],p[2]});
}
std::shared_ptr<const Environment> Environment::bake(std::string name,const std::function<Vec3(Vec3)>& radiance,std::uint32_t size) {
    if(!radiance||size<4||size>128||(size&(size-1))) throw std::invalid_argument("Environment cube size must be a power of two in [4,128]");
    auto out=std::make_shared<Environment>();out->name=std::move(name);
    std::uint32_t levels=1;for(auto s=size;s>1;s/=2) ++levels;
    for(std::uint32_t mip=0;mip<levels;++mip) {
        EnvironmentLevel l{std::max(size>>mip,1u),{}};l.pixels.reserve(std::size_t(l.size)*l.size*6);
        const float roughness=float(mip)/(levels-1);
        for(std::uint32_t face=0;face<6;++face) for(std::uint32_t y=0;y<l.size;++y) for(std::uint32_t x=0;x<l.size;++x) {
            const auto n=cubeDirection(face,2*(x+0.5f)/l.size-1,2*(y+0.5f)/l.size-1);Vec3 sum{};float weight=0;
            if(mip==0) { sum=radiance(n);weight=1; }
            else for(std::uint32_t i=0;i<samples;++i) {
                const auto h=ggx(n,roughness,float(i)/samples,radical(i));const auto direction=h*(2*dot(n,h))-n;
                const auto w=std::max(dot(n,direction),0.0f);if(w>0) { sum=sum+radiance(direction)*w;weight+=w; }
            }
            l.pixels.push_back(pixel(sum*(1/std::max(weight,0.001f))));
        }
        out->specular.push_back(std::move(l));
    }
    out->diffuse.size=8;auto& d=out->diffuse;d.pixels.reserve(6*d.size*d.size);
    for(std::uint32_t face=0;face<6;++face) for(std::uint32_t y=0;y<d.size;++y) for(std::uint32_t x=0;x<d.size;++x) {
        const auto n=cubeDirection(face,2*(x+0.5f)/d.size-1,2*(y+0.5f)/d.size-1);Vec3 sum{};
        for(std::uint32_t i=0;i<samples;++i) {
            const float r=std::sqrt((i+0.5f)/samples),phi=2*pi*radical(i);
            sum=sum+radiance(oriented(n,{r*std::cos(phi),r*std::sin(phi),std::sqrt(1-r*r)}));
        }
        d.pixels.push_back(pixel(sum*(1.0f/samples))); // Cosine-weighted integral / pi.
    }
    // Split-sum GGX BRDF integration: x=NdotV, y=perceptual roughness.
    out->lutSize=64;out->brdf.reserve(64*64);
    for(std::uint32_t y=0;y<64;++y) for(std::uint32_t x=0;x<64;++x) {
        const float nv=(x+0.5f)/64,rough=(y+0.5f)/64;const Vec3 v{std::sqrt(1-nv*nv),0,nv};float a=0,b=0;
        for(std::uint32_t i=0;i<samples;++i) {
            const auto h=ggx({0,0,1},rough,float(i)/samples,radical(i));const auto l=h*(2*dot(v,h))-v;
            const auto nl=std::max(l.z,0.0f),nh=std::max(h.z,0.0f),vh=std::max(dot(v,h),0.0f);
            if(nl>0&&nh>0) {
                const float k=rough*rough/2,g=(nv/(nv*(1-k)+k))*(nl/(nl*(1-k)+k));
                const float visibility=g*vh/(nh*nv),fc=std::pow(1-vh,5);a+=(1-fc)*visibility;b+=fc*visibility;
            }
        }
        out->brdf.push_back({a/samples,b/samples,0,1});
    }
    out->validate();return out;
}
std::shared_ptr<const Environment> Environment::studio() {
    static const auto value=bake("Studio sky",[](Vec3 d) {
        const float sky=std::clamp(d.y,0.0f,1.0f),ground=std::clamp(-d.y,0.0f,1.0f);
        Vec3 color=Vec3{0.65f,0.72f,0.85f}*(1-sky-ground)+Vec3{0.15f,0.32f,0.65f}*sky+Vec3{0.12f,0.10f,0.08f}*ground;
        const auto key=std::pow(std::max(dot(d,normalized({-0.4f,0.7f,0.6f})),0.0f),64);
        return color+Vec3{8,6,4}*key;
    });return value;
}
std::shared_ptr<const Environment> Environment::fromEquirectangular(const Texture& image) {
    if(!image.width||!image.height||image.width>4096||image.height>4096||image.pixels.size()!=std::uint64_t(image.width)*image.height*4) throw std::invalid_argument("Invalid environment image");
    auto linear=[](float v) { return v<=0.04045f ? v/12.92f : std::pow((v+0.055f)/1.055f,2.4f); };
    return bake(image.name,[&](Vec3 n) {
        const float u=std::atan2(n.z,n.x)/(2*pi)+0.5f,v=std::acos(std::clamp(n.y,-1.0f,1.0f))/pi;
        const float fx=u*image.width-0.5f,fy=v*image.height-0.5f;const int x=int(std::floor(fx)),y=int(std::floor(fy));Vec3 sum{};
        for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
            const auto ix=(x+dx+int(image.width))%int(image.width),iy=std::clamp(y+dy,0,int(image.height)-1);
            const auto offset=(std::size_t(iy)*image.width+ix)*4;
            const float w=(dx ? fx-x : 1-(fx-x))*(dy ? fy-y : 1-(fy-y));
            sum=sum+Vec3{linear(image.pixels[offset]/255.0f),linear(image.pixels[offset+1]/255.0f),linear(image.pixels[offset+2]/255.0f)}*w;
        }
        return sum;
    });
}
} // namespace engine
