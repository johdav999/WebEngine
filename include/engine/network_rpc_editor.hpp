#pragma once
#include "engine/editor_properties.hpp"
#include "engine/network_gameplay_authoring.hpp"
#include "engine/network_rpc.hpp"

namespace engine::net {
inline std::shared_ptr<EditorPropertyRegistry> invocationProperties(
    const InvocationRegistry &registry,std::shared_ptr<const EditorPropertyRegistry> base=EditorPropertyRegistry::builtins()) {
  if(!base)throw std::invalid_argument("Missing properties");
  auto result=std::make_shared<EditorPropertyRegistry>(*base);
  for(const auto &type:registry.entries()){
    const bool rpc=type.direction==InvocationDirection::ServerRpc;auto bit=invocationBit(type.id);
    EditorPropertyInfo info{(rpc?"rpc.":"event.")+type.key,(rpc?"Allow RPC: ":"Emit event: ")+type.key,
      "Gameplay RPCs and events",EditorPropertyType::Boolean,0,1,1,{},false};
    result->add({std::move(info),[](const Node &node){if(!isInvocationMetadata(node))return false;
      try{(void)invocationPermissions(static_cast<const Component &>(node));return true;}catch(const std::invalid_argument &){return false;}},
      [rpc,bit](const Node &node)->EditorValue{auto p=invocationPermissions(static_cast<const Component &>(node));return bool((rpc?p.serverRpcs:p.clientEvents)&bit);},
      [rpc,bit](Node &node,const EditorValue &v){auto &c=static_cast<Component &>(node);auto p=invocationPermissions(c);auto &mask=rpc?p.serverRpcs:p.clientEvents;
        mask=std::get<bool>(v)?mask|bit:mask&~bit;setInvocationPermissions(c,p);}});
  }
  return result;
}
} // namespace engine::net
