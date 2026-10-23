#include "engine/network_rpc.hpp"
#include <algorithm>

namespace engine::net {
namespace {
void require(bool value) {
  if (!value) throw std::invalid_argument("Invalid gameplay invocation schema/payload");
}
void put(Bytes &out, std::uint32_t value) {
  for (int i=0;i<4;++i) out.push_back(std::uint8_t(value>>(8*i)));
}
}
void InvocationRegistry::add(InvocationType type) {
  require(type.id && type.id<=32 && type.version && !type.key.empty() &&
          type.key.size()<=128 && type.direction<=InvocationDirection::ClientEvent &&
          type.maxBytes<=maxPayload && type.validate && entries_.size()<32);
  for (unsigned char c:type.key) require((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
      (c>='0'&&c<='9')||c=='.'||c=='_'||c=='-');
  for (const auto &entry:entries_) require(entry.id!=type.id && entry.key!=type.key);
  entries_.push_back(std::move(type));
}
const InvocationType *InvocationRegistry::find(std::uint32_t id) const {
  auto at=std::find_if(entries_.begin(),entries_.end(),[&](const auto &v){return v.id==id;});
  return at==entries_.end()?nullptr:&*at;
}
void InvocationRegistry::validate(std::uint32_t id, InvocationDirection direction,
                                   std::span<const std::uint8_t> bytes) const {
  auto *type=find(id);require(type && type->direction==direction && bytes.size()<=type->maxBytes);
  type->validate(bytes);
}
Bytes InvocationRegistry::manifest() const {
  auto sorted=entries_;std::sort(sorted.begin(),sorted.end(),[](const auto &a,const auto &b){return a.id<b.id;});
  Bytes out;put(out,std::uint32_t(sorted.size()));
  for(const auto &type:sorted){put(out,type.id);put(out,type.version);put(out,std::uint32_t(type.direction));
    put(out,std::uint32_t(type.maxBytes));put(out,std::uint32_t(type.key.size()));out.insert(out.end(),type.key.begin(),type.key.end());}
  return out;
}
} // namespace engine::net
