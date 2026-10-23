#pragma once
#include "engine/network_authoring.hpp"

namespace engine::net {
struct InvocationPermissions {
  std::uint32_t serverRpcs=0, clientEvents=0;
  bool operator==(const InvocationPermissions &) const = default;
};
inline constexpr std::string_view invocationPrefix="WebEngine.Invocations.";
inline std::uint32_t invocationBit(std::uint32_t id) {
  if(!id||id>32) throw std::invalid_argument("Invocation id");
  return std::uint32_t(1)<<(id-1);
}
inline bool isInvocationMetadata(const Node &node) {
  if(typeid(node)!=typeid(Component))return false;
  for(const auto &tag:static_cast<const Component &>(node).tags())
    if(tag.starts_with(invocationPrefix))return true;
  return false;
}
inline InvocationPermissions invocationPermissions(const Component &component) {
  std::optional<InvocationPermissions> result;
  for(const auto &tag:component.tags())if(tag.starts_with(invocationPrefix)){
    constexpr std::string_view prefix="WebEngine.Invocations.v1:";
    if(result||!tag.starts_with(prefix))throw std::invalid_argument("Invocation annotation version/duplicate");
    std::string_view text(tag);text.remove_prefix(prefix.size());std::uint32_t values[2];
    for(int i=0;i<2;++i){auto end=text.find(':');auto token=text.substr(0,end);
      auto [at,error]=std::from_chars(token.data(),token.data()+token.size(),values[i]);
      if(token.empty()||error!=std::errc{}||at!=token.data()+token.size()||(i==0?end==text.npos:end!=text.npos))
        throw std::invalid_argument("Invocation annotation payload");
      if(i==0)text.remove_prefix(end+1);}
    result=InvocationPermissions{values[0],values[1]};
  }
  if(!result)throw std::invalid_argument("Missing invocation metadata");return *result;
}
inline InvocationPermissions invocationPermissions(const Actor &actor) {
  std::optional<InvocationPermissions> result;
  for(const auto &child:actor.children())if(isInvocationMetadata(*child)){
    if(result)throw std::invalid_argument("Duplicate invocation metadata");
    result=invocationPermissions(static_cast<const Component &>(*child));}
  return result.value_or(InvocationPermissions{});
}
inline void setInvocationPermissions(Component &component,InvocationPermissions value) {
  if(typeid(component)!=typeid(Component))throw std::invalid_argument("Invocation settings require ordinary Component");
  auto state=component.serialize();std::erase_if(state.tags,[](const auto &tag){return tag.starts_with(invocationPrefix);});
  state.tags.push_back("WebEngine.Invocations.v1:"+std::to_string(value.serverRpcs)+":"+std::to_string(value.clientEvents));
  component.deserialize(state);
}
inline std::unique_ptr<Component> makeInvocationMetadata(InvocationPermissions value={}) {
  auto result=std::make_unique<Component>("Gameplay RPCs and events");setInvocationPermissions(*result,value);return result;
}
} // namespace engine::net
