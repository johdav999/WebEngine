#pragma once
#include "engine/editor_properties.hpp"
#include "engine/network_components.hpp"
namespace engine::net {
// Returns a registry snapshot retaining the component adapters by value.
inline std::shared_ptr<EditorPropertyRegistry> componentReplicationProperties(
    const ComponentReplicationRegistry &components,
    std::shared_ptr<const EditorPropertyRegistry> base =
        EditorPropertyRegistry::builtins()) {
  if (!base)
    throw std::invalid_argument("Missing editor properties");
  auto result = std::make_shared<EditorPropertyRegistry>(*base);
  EditorPropertyInfo info{"network.component",
                          "State replication",
                          "Multiplayer component",
                          EditorPropertyType::Choice,
                          0,
                          2,
                          1,
                          {"Off", "Everyone", "Owner only"},
                          0.0};
  result->add({std::move(info),
               [components](const Node &node) {
                 auto *c = dynamic_cast<const Component *>(&node);
                 if (!c || !components.find(*c))
                   return false;
                 try {
                   (void)componentReplication(*c);
                   return true;
                 } catch (const std::invalid_argument &) {
                   return false;
                 }
               },
               [](const Node &n) -> EditorValue {
                 return double(
                     componentReplication(static_cast<const Component &>(n)));
               },
               [](Node &n, const EditorValue &v) {
                 setComponentReplication(
                     static_cast<Component &>(n),
                     ComponentReplication(int(std::get<double>(v))));
               }});
  return result;
}
} // namespace engine::net
