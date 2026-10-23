#include "engine/network_physics.hpp"
#include <bit>
#include <cmath>

namespace engine::net {
namespace {
void put(Bytes &out,std::uint32_t n){for(int i=0;i<4;++i)out.push_back(std::uint8_t(n>>(8*i)));}
std::uint32_t get(std::span<const std::uint8_t> bytes,std::size_t offset){
  std::uint32_t n=0;for(int i=0;i<4;++i)n|=std::uint32_t(bytes[offset+i])<<(8*i);return n;
}
}
Bytes encodePhysics(const ReplicatedPhysics &state){
  if(state.motion>BodyMotion::Kinematic)throw std::invalid_argument("Replicated body motion");
  Bytes result;put(result,std::uint32_t(state.motion)|(state.enabled?256u:0u));
  for(float value:{state.linear.x,state.linear.y,state.linear.z,state.angular.x,state.angular.y,state.angular.z}){
    if(!std::isfinite(value)||std::abs(value)>1000000)throw std::invalid_argument("Replicated body velocity");
    put(result,std::bit_cast<std::uint32_t>(value));
  }
  return result;
}
ReplicatedPhysics decodePhysics(std::span<const std::uint8_t> bytes){
  if(bytes.size()!=physicsStateBytes)throw std::invalid_argument("Replicated physics payload");
  auto flags=get(bytes,0);if((flags&~259u)||(flags&3u)>2)throw std::invalid_argument("Replicated physics flags");
  ReplicatedPhysics state;state.motion=BodyMotion(flags&3u);state.enabled=bool(flags&256u);
  state.linear={std::bit_cast<float>(get(bytes,4)),std::bit_cast<float>(get(bytes,8)),std::bit_cast<float>(get(bytes,12))};
  state.angular={std::bit_cast<float>(get(bytes,16)),std::bit_cast<float>(get(bytes,20)),std::bit_cast<float>(get(bytes,24))};
  (void)encodePhysics(state);return state;
}
ReplicatedPhysics capturePhysics(const Actor &actor){
  auto *body=actor.getComponent<RigidBodyComponent>();if(!body)throw std::invalid_argument("Missing authoritative body");
  ReplicatedPhysics state;state.motion=body->settings().motion;state.enabled=body->enabled;
  if(body->hasBody()){state.linear=body->linearVelocity();state.angular=body->angularVelocity();}
  (void)encodePhysics(state);return state;
}
void preparePhysicsReplica(Actor &actor){
  if(actor.scene())throw std::invalid_argument("Physics replica setup requires detached actor");
  actor.visit([](const Node &node,const Mat4 &){if(auto *body=dynamic_cast<const RigidBodyComponent *>(&node))const_cast<RigidBodyComponent *>(body)->setEnabled(false);
    if(auto *collider=dynamic_cast<const ColliderComponent *>(&node))const_cast<ColliderComponent *>(collider)->setEnabled(false);});
}
void preparePhysicsClient(Scene &scene){
  scene.visit([](const Node &node,const Mat4 &){if(auto *body=dynamic_cast<const RigidBodyComponent *>(&node))const_cast<RigidBodyComponent *>(body)->setEnabled(false);
    if(auto *collider=dynamic_cast<const ColliderComponent *>(&node))const_cast<ColliderComponent *>(collider)->setEnabled(false);});
  scene.synchronize();
}
} // namespace engine::net
