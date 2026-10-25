#pragma once
#include "engine/rpg/snapshot.hpp"
#include <bit>
#include <limits>
#include <tuple>
#include <type_traits>

// Private wire primitives, shared only by the RPG codec and optional bridge.
namespace engine::rpg::wire {
inline void need(bool ok, const char* message) {
    if (!ok) throw Error(ErrorCode::InvalidSnapshot, message);
}
struct Writer {
    std::vector<std::uint8_t> bytes;
    void byte(std::uint8_t b) { need(bytes.size()<snapshotByteLimit,"Snapshot byte limit"); bytes.push_back(b); }
    void number(std::uint64_t n) { for(unsigned i=0;i<8;++i)byte(static_cast<std::uint8_t>(n>>(8*i))); }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t offset=0, nodes=0;
    std::uint8_t byte() { need(offset<bytes.size(),"Truncated snapshot"); return bytes[offset++]; }
    std::uint64_t number() {std::uint64_t n=0;for(unsigned i=0;i<8;++i)n|=std::uint64_t(byte())<<(8*i);return n;}
    std::size_t count(std::size_t cap=16384) {
        auto n=number();need(n<=cap && n<=bytes.size()-offset,"Invalid bounded length");return static_cast<std::size_t>(n);
    }
    void node(){need(++nodes<=1048576,"Snapshot node budget");}
    void end(){need(offset==bytes.size(),"Trailing snapshot data");}
};
template<class T> struct Fields;
template<class T> struct Codec;
template<class T> void put(Writer& w,const T& t){Codec<T>::put(w,t);}
template<class T> T get(Reader& r){r.node();return Codec<T>::get(r);}
template<class T> struct Codec {
    static void put(Writer& w,const T& t) {
        if constexpr(std::is_same_v<T,bool>)w.byte(t?1:0);
        else if constexpr(std::is_enum_v<T>)w.number(static_cast<std::uint64_t>(t));
        else if constexpr(std::is_integral_v<T>)w.number(static_cast<std::uint64_t>(t));
        else std::apply([&](auto... m){(wire::put(w,t.*m),...);},Fields<T>::members);
    }
    static T get(Reader& r) {
        if constexpr(std::is_same_v<T,bool>){auto b=r.byte();need(b<=1,"Invalid boolean");return b!=0;}
        else if constexpr(std::is_enum_v<T>){auto n=r.number();need(n<=255,"Invalid enum tag");return static_cast<T>(n);}
        else if constexpr(std::is_integral_v<T>){
            auto n=r.number();
            if constexpr(std::is_signed_v<T>){auto v=std::bit_cast<std::int64_t>(n);need(v>=std::numeric_limits<T>::min()&&v<=std::numeric_limits<T>::max(),"Integer range");return static_cast<T>(v);}
            else {need(n<=std::numeric_limits<T>::max(),"Integer range");return static_cast<T>(n);}
        } else return std::apply([&](auto... m){return T{wire::get<std::remove_cvref_t<decltype(std::declval<T>().*m)>>(r)...};},Fields<T>::members);
    }
};
template<> struct Codec<std::string> {
    static void put(Writer&w,const std::string& v){need(v.size()<=Limits::receiptBytes,"String limit");w.number(v.size());for(auto b:v)w.byte(static_cast<std::uint8_t>(b));}
    static std::string get(Reader&r){auto n=r.count(Limits::receiptBytes);std::string s;for(std::size_t i=0;i<n;++i)s.push_back(static_cast<char>(r.byte()));return s;}
};
template<> struct Codec<ContentId> {
    static void put(Writer&w,const ContentId&v){wire::put(w,v.value());}
    static ContentId get(Reader&r){return ContentId(wire::get<std::string>(r));}
};
template<> struct Codec<SessionId> {
    static void put(Writer&w,SessionId v){w.number(v.value());}
    static SessionId get(Reader&r){return SessionId(r.number());}
};
template<> struct Codec<ContentRevision> {
    static void put(Writer&w,ContentRevision v){w.number(v.value());}
    static ContentRevision get(Reader&r){return ContentRevision(r.number());}
};
template<class T> struct Codec<std::optional<T>> {
    static void put(Writer&w,const std::optional<T>&v){wire::put(w,v.has_value());if(v)wire::put(w,*v);}
    static std::optional<T> get(Reader&r){if(wire::get<bool>(r))return wire::get<T>(r);return {};}
};
template<class T> struct Codec<std::vector<T>> {
    static void put(Writer&w,const std::vector<T>&v){need(v.size()<=16384,"Collection limit");w.number(v.size());for(auto&x:v)wire::put(w,x);}
    static std::vector<T> get(Reader&r){auto n=r.count();std::vector<T> v;for(std::size_t i=0;i<n;++i)v.push_back(wire::get<T>(r));return v;}
};
template<class K,class V> struct Codec<std::map<K,V>> {
    static void put(Writer&w,const std::map<K,V>&v){need(v.size()<=16384,"Map limit");w.number(v.size());for(auto&[k,x]:v){wire::put(w,k);wire::put(w,x);}}
    static std::map<K,V> get(Reader&r){auto n=r.count();std::map<K,V> v;for(std::size_t i=0;i<n;++i){auto k=wire::get<K>(r);need(v.empty()||v.rbegin()->first<k,"Noncanonical map key");auto x=wire::get<V>(r);v.emplace(std::move(k),std::move(x));}return v;}
};
template<class T> struct Codec<std::set<T>> {
    static void put(Writer&w,const std::set<T>&v){need(v.size()<=16384,"Set limit");w.number(v.size());for(auto&x:v)wire::put(w,x);}
    static std::set<T> get(Reader&r){auto n=r.count();std::set<T> v;for(std::size_t i=0;i<n;++i){auto x=wire::get<T>(r);need(v.empty()||*v.rbegin()<x,"Noncanonical set key");v.insert(std::move(x));}return v;}
};
inline std::uint64_t checksum(std::span<const std::uint8_t> bytes){std::uint64_t h=14695981039346656037ull;for(auto b:bytes){h^=b;h*=1099511628211ull;}return h;}
inline void blob(Writer&w,std::span<const std::uint8_t>b){w.number(b.size());for(auto x:b)w.byte(x);}
inline std::span<const std::uint8_t> blob(Reader&r){auto n=r.count(snapshotByteLimit);auto b=r.bytes.subspan(r.offset,n);r.offset+=n;return b;}
inline std::vector<std::uint8_t> wrap(std::uint64_t tag,std::span<const std::uint8_t> payload){
    Writer w;for(auto b:std::string_view("WERPG\r\n\x1a",8))w.byte(static_cast<std::uint8_t>(b));
    w.number(snapshotVersion);w.number(tag);blob(w,payload);w.number(checksum(w.bytes));return w.bytes;
}
inline Reader unwrap(std::span<const std::uint8_t>b,std::uint64_t tag){
    need(b.size()>=40&&b.size()<=snapshotByteLimit,"Snapshot size");Reader r{b};
    for(auto c:std::string_view("WERPG\r\n\x1a",8))need(r.byte()==static_cast<std::uint8_t>(c),"Snapshot magic");
    need(r.number()==snapshotVersion,"Snapshot version");need(r.number()==tag,"Snapshot tag");auto payload=blob(r);
    need(r.number()==checksum(b.first(b.size()-8)),"Snapshot checksum");r.end();return Reader{payload};
}
} // namespace engine::rpg::wire
