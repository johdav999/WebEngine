#pragma once
// Private, bounded material-delivery decoder. It performs no IO or extraction.
#include "engine/material.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace engine::material_delivery {
inline void check(bool ok,const char* text) {if(!ok)throw std::invalid_argument(std::string("WebEngine material delivery: ")+text);}
struct Json {
    enum class Type { Null,Boolean,Number,String,Array,Object } type=Type::Null;
    bool boolean=false;double number=0;std::string string;
    std::vector<Json> array;std::map<std::string,Json,std::less<>> object;
    bool operator==(const Json&) const=default;
    const Json* find(std::string_view key) const {check(type==Type::Object,"expected JSON object");auto p=object.find(key);return p==object.end()?nullptr:&p->second;}
    const Json& at(std::string_view key) const {auto* p=find(key);check(p,"required JSON field missing");return *p;}
    std::string_view text() const {check(type==Type::String,"expected JSON string");return string;}
    double value() const {check(type==Type::Number&&std::isfinite(number),"expected finite JSON number");return number;}
};
class Parser {
    std::string_view source;std::size_t position=0,nodes=0,stringBytes=0;
    void whitespace(){while(position<source.size()&&(source[position]==' '||source[position]=='\r'||source[position]=='\n'||source[position]=='\t'))++position;}
    bool take(char c){whitespace();if(position<source.size()&&source[position]==c){++position;return true;}return false;}
    void literal(std::string_view text){check(source.substr(position,text.size())==text,"invalid JSON literal");position+=text.size();}
    unsigned hex(){check(position+4<=source.size(),"truncated Unicode escape");unsigned n=0;for(int i=0;i<4;++i){char c=source[position++];auto p=std::string_view("0123456789abcdef").find(c>='A'&&c<='F'?c-'A'+'a':c);check(p!=std::string_view::npos,"invalid Unicode escape");n=n*16+unsigned(p);}return n;}
    static void utf8(std::string& out,unsigned n){if(n<128)out+=char(n);else if(n<2048){out+=char(192|(n>>6));out+=char(128|(n&63));}else if(n<65536){out+=char(224|(n>>12));out+=char(128|((n>>6)&63));out+=char(128|(n&63));}else{out+=char(240|(n>>18));out+=char(128|((n>>12)&63));out+=char(128|((n>>6)&63));out+=char(128|(n&63));}}
    std::string text(){check(take('"'),"expected JSON string");std::string out;bool closed=false;while(position<source.size()){unsigned char c=source[position++];if(c=='"'){closed=true;break;}check(c>=32,"control character in JSON string");if(c=='\\'){check(position<source.size(),"truncated JSON escape");c=source[position++];if(c=='u'){unsigned n=hex();if(n>=0xd800&&n<=0xdbff){check(source.substr(position,2)=="\\u","missing low surrogate");position+=2;unsigned low=hex();check(low>=0xdc00&&low<=0xdfff,"invalid low surrogate");n=0x10000+((n-0xd800)<<10)+(low-0xdc00);}else check(n<0xdc00||n>0xdfff,"unpaired low surrogate");utf8(out,n);}else{auto p=std::string_view("\"\\/bfnrt").find(char(c));check(p!=std::string_view::npos,"invalid JSON escape");out+=std::string_view("\"\\/\b\f\n\r\t")[p];}}else out+=char(c);check(out.size()<=65536,"JSON string too long");}check(closed,"unterminated JSON string");stringBytes+=out.size();check(stringBytes<=2*1024*1024,"JSON string budget exceeded");return out;}
    Json read(unsigned depth){check(depth<=32&&++nodes<=100000,"JSON complexity limit exceeded");whitespace();check(position<source.size(),"truncated JSON");Json out;char c=source[position];if(c=='{'){++position;out.type=Json::Type::Object;if(take('}'))return out;do{auto key=text();check(take(':'),"missing JSON colon");check(out.object.emplace(std::move(key),read(depth+1)).second,"duplicate JSON key");}while(take(','));check(take('}'),"missing JSON object end");}else if(c=='['){++position;out.type=Json::Type::Array;if(take(']'))return out;do{check(out.array.size()<16384,"JSON array too large");out.array.push_back(read(depth+1));}while(take(','));check(take(']'),"missing JSON array end");}else if(c=='"'){out.type=Json::Type::String;out.string=text();}else if(c=='t'||c=='f'){out.type=Json::Type::Boolean;out.boolean=c=='t';literal(c=='t'?"true":"false");}else if(c=='n')literal("null");else{out.type=Json::Type::Number;const auto begin=position;if(source[position]=='-')++position;check(position<source.size(),"truncated JSON number");if(source[position]=='0')++position;else{check(source[position]>='1'&&source[position]<='9',"invalid JSON number");while(position<source.size()&&source[position]>='0'&&source[position]<='9')++position;}if(position<source.size()&&source[position]=='.'){++position;const auto start=position;while(position<source.size()&&source[position]>='0'&&source[position]<='9')++position;check(position>start,"invalid JSON fraction");}if(position<source.size()&&(source[position]=='e'||source[position]=='E')){++position;if(position<source.size()&&(source[position]=='+'||source[position]=='-'))++position;const auto start=position;while(position<source.size()&&source[position]>='0'&&source[position]<='9')++position;check(position>start,"invalid JSON exponent");}auto [end,error]=std::from_chars(source.data()+begin,source.data()+position,out.number);check(error==std::errc{}&&end==source.data()+position&&std::isfinite(out.number),"non-finite or invalid JSON number");}return out;}
public:
    explicit Parser(std::string_view bytes):source(bytes){check(bytes.size()<=4*1024*1024,"JSON byte limit exceeded");for(std::size_t i=0;i<bytes.size();){unsigned c=static_cast<unsigned char>(bytes[i++]);if(c<128)continue;unsigned n=0,value=0,minimum=0;if(c>=0xc2&&c<=0xdf){n=1;value=c&31;minimum=0x80;}else if(c>=0xe0&&c<=0xef){n=2;value=c&15;minimum=0x800;}else if(c>=0xf0&&c<=0xf4){n=3;value=c&7;minimum=0x10000;}else check(false,"invalid UTF-8 JSON");check(n<=bytes.size()-i,"truncated UTF-8 JSON");for(unsigned k=0;k<n;++k){const unsigned d=static_cast<unsigned char>(bytes[i++]);check((d&192)==128,"invalid UTF-8 continuation");value=(value<<6)|(d&63);}check(value>=minimum&&value<=0x10ffff&&(value<0xd800||value>0xdfff),"invalid UTF-8 codepoint");}}
    Json parse(){auto result=read(0);whitespace();check(position==source.size(),"trailing JSON data");return result;}
};
inline std::string_view bytesText(std::span<const std::byte> b){return {reinterpret_cast<const char*>(b.data()),b.size()};}
inline float ranged(const Json& j,double minimum,double maximum){const double n=j.value();check(n>=minimum&&n<=maximum,"material parameter outside range");return float(n);}
inline Vec3 color(const Json& j){check(j.type==Json::Type::Array&&j.array.size()==3,"expected RGB array");return {ranged(j.array[0],0,1),ranged(j.array[1],0,1),ranged(j.array[2],0,1)};}
inline void apply(const Json& entry,Material& m,std::vector<std::string>& warnings){
    const auto shader=entry.at("shader").text();
    check(shader=="pbr"||shader=="unlit"||shader=="clearcoat"||shader=="glass"||shader=="subsurface"||shader=="foliage"||shader=="cloth"||shader=="hair"||shader=="water","unknown material shader");
    if(auto* name=entry.find("name")){check(name->text().size()<=4096,"material name too long");m.name=std::string(name->text());}
    m.shader=shader=="unlit"?MaterialShader::Unlit:shader=="water"?MaterialShader::Water:MaterialShader::Pbr;
    if(auto* mode=entry.find("alphaMode")){const auto value=mode->text();check(value=="opaque"||value=="mask"||value=="blend"||value=="additive","unknown alpha mode");m.alphaMode=value=="opaque"?AlphaMode::Opaque:value=="mask"?AlphaMode::Mask:value=="blend"?AlphaMode::Blend:AlphaMode::Additive;}
    if(auto* cutoff=entry.find("alphaCutoff"))m.alphaCutoff=ranged(*cutoff,0,1);
    if(auto* sided=entry.find("doubleSided")){check(sided->type==Json::Type::Boolean,"doubleSided must be Boolean");m.doubleSided=sided->boolean;}
    auto& a=m.advanced;
    // Select meaningful defaults for explicitly selected recipes; parameters below override.
    if(shader=="clearcoat")a.clearcoat=1;
    if(shader=="glass")a.transmission=1;
    if(shader=="subsurface"||shader=="foliage")a.subsurface=1;
    if(shader=="cloth")a.sheen=1;
    if(shader=="hair")a.anisotropy=.8f;
    if(const auto* p=entry.find("parameters")){
        check(p->type==Json::Type::Object,"parameters must be an object");
        struct Scalar{const char* key;float* target;double low,high;};
        for(auto v:{Scalar{"clearcoatNormalScale",&a.clearcoatNormalScale,0,16},{"clearcoat",&a.clearcoat,0,1},{"clearcoatRoughness",&a.clearcoatRoughness,0,1},{"transmission",&a.transmission,0,1},{"ior",&a.ior,1,3},{"thickness",&a.thickness,0,100},{"attenuationDistance",&a.attenuationDistance,0,1000000},{"subsurface",&a.subsurface,0,1},{"sheen",&a.sheen,0,1},{"sheenRoughness",&a.sheenRoughness,0,1},{"anisotropy",&a.anisotropy,-1,1},{"anisotropyRotation",&a.anisotropyRotation,-1000000,1000000},{"specular",&a.specular,0,1}})if(auto* value=p->find(v.key))*v.target=ranged(*value,v.low,v.high);
        if(auto* v=p->find("attenuationColor"))a.attenuationColor=color(*v);
        if(auto* v=p->find("subsurfaceColor"))a.subsurfaceColor=color(*v);
        if(auto* v=p->find("sheenColor"))a.sheenColor=color(*v);
    }
    if(auto* p=entry.find("uvAnimation")){const auto& speed=p->at("speed");check(speed.type==Json::Type::Array&&speed.array.size()==2,"expected UV speed pair");a.uvSpeed={ranged(speed.array[0],-100,100),ranged(speed.array[1],-100,100)};}
    if(auto* p=entry.find("wind")){const auto& d=p->at("direction");check(d.type==Json::Type::Array&&d.array.size()==3,"expected wind direction");a.windDirection={ranged(d.array[0],-1000000,1000000),ranged(d.array[1],-1000000,1000000),ranged(d.array[2],-1000000,1000000)};a.windAmplitude=ranged(p->at("amplitude"),0,10);a.windFrequency=ranged(p->at("frequency"),0,100);a.windSpeed=ranged(p->at("speed"),-100,100);check(a.windAmplitude==0||dot(a.windDirection,a.windDirection)>1e-12f,"wind direction cannot be zero");}
    if(auto* diagnostics=entry.find("diagnostics")){const auto status=diagnostics->at("status").text();check(status=="mapped"||status=="baked"||status=="approximated"||status=="unsupported","invalid fidelity status");if(auto* reasons=diagnostics->find("reasons")){check(reasons->type==Json::Type::Array&&reasons->array.size()<=128,"too many material diagnostics");for(auto& reason:reasons->array){check(reason.text().size()<=4096,"material diagnostic too long");if(warnings.size()<512)warnings.push_back(m.name+": "+std::string(reason.text()));}}}
    m.validate();
}
inline std::map<std::size_t,Json> bindings(const Json& root,std::size_t count){
    check(root.at("version").value()==1,"unsupported material manifest version");const auto& items=root.at("materials");check(items.type==Json::Type::Array&&items.array.size()<=4096,"invalid material table");std::map<std::size_t,Json> result;
    for(const auto& item:items.array){const auto n=item.at("index").value();check(n>=0&&n<std::max(std::size_t(1),count)&&std::floor(n)==n&&count>0,"material index outside GLB table");check(result.emplace(std::size_t(n),item).second,"duplicate material binding");Material candidate;std::vector<std::string> diagnostics;apply(item,candidate,diagnostics);}
    return result;
}
inline std::uint16_t u16(std::span<const std::byte> b,std::size_t p){check(p<=b.size()&&b.size()-p>=2,"truncated ZIP integer");return std::to_integer<unsigned>(b[p])|(std::to_integer<unsigned>(b[p+1])<<8);}
inline std::uint32_t u32(std::span<const std::byte> b,std::size_t p){return std::uint32_t(u16(b,p))|(std::uint32_t(u16(b,p+2))<<16);}
inline std::uint32_t crc32(std::span<const std::byte> b){static const auto table=[] {std::array<std::uint32_t,256> a{};for(unsigned i=0;i<256;++i){auto c=i;for(unsigned k=0;k<8;++k)c=(c>>1)^((c&1)?0xedb88320u:0);a[i]=c;}return a;}();std::uint32_t c=0xffffffff;for(auto v:b)c=table[(c^std::to_integer<unsigned>(v))&255]^(c>>8);return c^0xffffffff;}
inline std::string sha256(std::span<const std::byte> bytes){
    constexpr std::array<std::uint32_t,64> k={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::array<std::uint32_t,8> h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const std::size_t blocks=(bytes.size()+9+63)/64;const std::uint64_t bits=std::uint64_t(bytes.size())*8;
    auto rotate=[](std::uint32_t v,int n){return (v>>n)|(v<<(32-n));};
    for(std::size_t block=0;block<blocks;++block){std::array<std::uint32_t,64>w{};
        for(std::size_t j=0;j<64;++j){const auto p=block*64+j;unsigned value=0;if(p<bytes.size())value=std::to_integer<unsigned>(bytes[p]);else if(p==bytes.size())value=128;else if(p>=blocks*64-8)value=unsigned((bits>>((blocks*64-p-1)*8))&255);w[j/4]|=value<<((3-j%4)*8);}
        for(std::size_t j=16;j<64;++j){const auto s0=rotate(w[j-15],7)^rotate(w[j-15],18)^(w[j-15]>>3),s1=rotate(w[j-2],17)^rotate(w[j-2],19)^(w[j-2]>>10);w[j]=w[j-16]+s0+w[j-7]+s1;}
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
        for(std::size_t j=0;j<64;++j){const auto s1=rotate(e,6)^rotate(e,11)^rotate(e,25),ch=(e&f)^((~e)&g),t1=v+s1+ch+k[j]+w[j],s0=rotate(a,2)^rotate(a,13)^rotate(a,22),maj=(a&b)^(a&c)^(b&c),t2=s0+maj;v=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;
    }
    std::string result;result.reserve(64);for(auto v:h)for(int n=28;n>=0;n-=4)result+="0123456789abcdef"[(v>>n)&15];return result;
}

struct Bundle {std::span<const std::byte> glb;Json manifest,report;};
inline Bundle bundle(std::span<const std::byte> bytes,std::size_t limit){
    check(bytes.size()>=22&&bytes.size()<=limit,"bundle file limit exceeded");const auto end=bytes.size()-22;check(u32(bytes,end)==0x06054b50,"ZIP comments or missing end record");
    check(u16(bytes,end+4)==0&&u16(bytes,end+6)==0&&u16(bytes,end+8)==3&&u16(bytes,end+10)==3&&u16(bytes,end+20)==0,"bundle requires three single-disk members");
    const std::size_t central=u32(bytes,end+16),centralSize=u32(bytes,end+12);check(central<=end&&centralSize==end-central,"invalid ZIP directory bounds");
    std::map<std::string,std::span<const std::byte>> members;std::vector<std::pair<std::size_t,std::size_t>> ranges;std::size_t p=central;
    for(int i=0;i<3;++i){check(p<=end&&end-p>=46&&u32(bytes,p)==0x02014b50,"invalid ZIP central entry");const auto flags=u16(bytes,p+8),method=u16(bytes,p+10),nameLength=u16(bytes,p+28),extraLength=u16(bytes,p+30),commentLength=u16(bytes,p+32);const std::size_t size=u32(bytes,p+24),packed=u32(bytes,p+20),local=u32(bytes,p+42);const auto crc=u32(bytes,p+16);check((flags&~0x800u)==0&&method==0&&size==packed,"bundle requires unencrypted ZIP_STORED members");const auto attrs=u32(bytes,p+38),mode=(attrs>>16)&0170000u;check(u16(bytes,p+34)==0&&(attrs&16)==0&&(mode==0||mode==0100000u),"unsupported ZIP member attributes");check(std::size_t(nameLength)+extraLength+commentLength<=end-p-46,"truncated ZIP member name");std::string name(bytesText(bytes.subspan(p+46,nameLength)));check(name=="asset.glb"||name=="materials.webengine.json"||name=="export-report.json","unexpected ZIP member path");check(local<=central&&central-local>=30&&u32(bytes,local)==0x04034b50,"invalid ZIP local entry");check(u16(bytes,local+6)==flags&&u16(bytes,local+8)==method&&u32(bytes,local+14)==crc&&u32(bytes,local+18)==packed&&u32(bytes,local+22)==size,"ZIP local and central metadata differ");const auto localName=u16(bytes,local+26),localExtra=u16(bytes,local+28);check(std::size_t(localName)+localExtra<=central-local-30,"truncated ZIP local header");check(localName==nameLength&&bytesText(bytes.subspan(local+30,localName))==name,"ZIP local and central names differ");const std::size_t begin=local+30+localName+localExtra;check(begin<=central&&size<=central-begin,"ZIP member exceeds local data bounds");check(name=="asset.glb"?size<=limit:size<=4*1024*1024,"ZIP member exceeds resource limit");auto data=bytes.subspan(begin,size);check(crc32(data)==crc,"ZIP checksum mismatch");check(members.emplace(name,data).second,"duplicate ZIP member");ranges.emplace_back(local,begin+size);p+=46+nameLength+extraLength+commentLength;}
    check(p==end,"trailing ZIP central data");std::sort(ranges.begin(),ranges.end());check(ranges.front().first==0,"prepended ZIP data");for(std::size_t i=1;i<ranges.size();++i)check(ranges[i-1].second==ranges[i].first,"overlapping or unrecorded ZIP local data");check(ranges.back().second==central,"trailing ZIP local data");
    Bundle result{members.at("asset.glb"),Parser(bytesText(members.at("materials.webengine.json"))).parse(),Parser(bytesText(members.at("export-report.json"))).parse()};check(result.report.at("version").value()==1,"unsupported export report version");check(result.report.at("glbSha256").text()==sha256(result.glb),"GLB report SHA-256 mismatch");check(result.report.at("materialManifestSha256").text()==sha256(members.at("materials.webengine.json")),"material report SHA-256 mismatch");check(result.report.at("materials")==result.manifest.at("materials"),"report and manifest material results differ");return result;
}
} // namespace engine::material_delivery
