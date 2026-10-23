#include "engine/network_lobby.hpp"
#include <iostream>
#include <source_location>
using namespace engine::net;
namespace {
void check(bool value,std::source_location at=std::source_location::current()){
  if(!value)throw std::runtime_error("Lobby assertion "+std::to_string(at.line()));
}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception &){failed=true;}check(failed);}
struct Fake final:Connection {
  ConnectionState status=ConnectionState::Open;
  std::vector<Bytes> incoming,sent;
  ConnectionState state()const override{return status;}
  std::string error()const override{return {};}
  bool send(std::span<const std::uint8_t> bytes)override{sent.emplace_back(bytes.begin(),bytes.end());return true;}
  std::vector<Bytes> receive()override{auto result=std::move(incoming);incoming.clear();return result;}
};
LobbyMessage accepted(){LobbyMessage m;m.type=LobbyMessageType::Accepted;m.credentials.guestId=std::string(32,'a');m.credentials.roomId=std::string(24,'b');m.credentials.sessionToken=std::string(64,'c');m.credentials.invitation=std::string(64,'d');m.credentials.host=true;return m;}
void codecs(){
  LobbyMessage m;m.request.guestName="Native guest";m.request.roomName="Game lobby";
  auto bytes=encodeLobby(m);check(isLobby(bytes)&&decodeLobby(bytes).request.guestName==m.request.guestName);
  for(std::size_t i=0;i<bytes.size();++i)rejects([&]{decodeLobby(std::span(bytes).first(i));});
  auto trailing=bytes;trailing.push_back(0);rejects([&]{decodeLobby(trailing);});
  m.request.operation=LobbyOperation::Join;rejects([&]{encodeLobby(m);});m.request.invitation=std::string(64,'a');check(decodeLobby(encodeLobby(m)).request.operation==LobbyOperation::Join);
  m.request.operation=LobbyOperation::Resume;m.request.sessionToken=std::string(64,'b');rejects([&]{encodeLobby(m);});m.request.invitation.clear();check(!encodeLobby(m).empty());
  m.request.guestName="bad\nname";rejects([&]{encodeLobby(m);});
  auto a=accepted();a.credentials.resumed=true;auto decoded=decodeLobby(encodeLobby(a));check(decoded.credentials.resumed&&decoded.credentials.host);
  auto bad=encodeLobby(a);bad[12]=4;rejects([&]{decodeLobby(bad);});
  LobbyMessage roster;roster.type=LobbyMessageType::Roster;roster.roster.roomId=a.credentials.roomId;roster.roster.name="Room";roster.roster.started=true;
  roster.roster.members.push_back({a.credentials.guestId,"Guest",true,true});
  auto restored=decodeLobby(encodeLobby(roster));check(restored.roster.started&&restored.roster.members[0].host);
  roster.roster.members.push_back(roster.roster.members[0]);rejects([&]{encodeLobby(roster);});
}
void adapter(){
  auto fake=std::make_unique<Fake>();auto *wire=fake.get();LobbyConnection connection(std::move(fake),{});
  check(connection.state()==ConnectionState::Connecting&&wire->sent.size()==1);
  check(decodeLobby(wire->sent.front()).type==LobbyMessageType::Request);
  wire->incoming.push_back(encodeLobby(accepted()));wire->incoming.push_back({1,2,3});
  check(connection.state()==ConnectionState::Open&&wire->sent.size()==2);
  check(decodeLobby(wire->sent.back()).type==LobbyMessageType::Confirm);
  auto packets=connection.receive();check(packets.size()==1&&packets[0].size()==3&&!connection.credentials().sessionToken.empty());
  check(connection.receive().empty());connection.leave();check(connection.state()==ConnectionState::Connecting&&decodeLobby(wire->sent.back()).type==LobbyMessageType::Leave);
  LobbyMessage left;left.type=LobbyMessageType::Left;wire->incoming.push_back(encodeLobby(left));check(connection.state()==ConnectionState::Closed);
  auto denied=std::make_unique<Fake>();auto *other=denied.get();LobbyConnection rejected(std::move(denied),{});LobbyMessage failure;failure.type=LobbyMessageType::Rejected;failure.reason="Invalid invitation";
  other->incoming.push_back(encodeLobby(failure));check(rejected.state()==ConnectionState::Failed&&rejected.rejected()&&rejected.credentials().sessionToken.empty());
  auto corrupt=std::make_unique<Fake>();corrupt->incoming.push_back({1});LobbyConnection invalid(std::move(corrupt),{});check(invalid.state()==ConnectionState::Failed);
}
void recovery(){
  ServerSession server(91,100);auto peer=server.connect();ClientSession client(91);client.receive(server.receive(peer,client.hello()));
  Record record{peer,peer,1,{1}};server.snapshot({&record,1});auto oldSnapshot=server.snapshotFor(peer,{&peer,1});client.receive(oldSnapshot);
  auto oldCommand=client.command(peer,1,{}),oldRpc=client.rpc(peer,1,{});server.receive(peer,oldCommand);server.receive(peer,oldRpc);
  auto oldEvent=server.eventFor(peer,peer,1,{});server.suspend(peer);
  check(server.readyPeers().empty()&&server.simulationPeers()==std::vector<PeerId>{peer});check(server.takeCommands().empty()&&server.takeRpcs().empty());
  rejects([&]{server.receive(peer,oldCommand);});server.resume(peer);
  ClientSession resumed(91);auto welcome=server.receive(peer,resumed.hello());resumed.receive(welcome);check(resumed.peer()==peer);
  check(decode(welcome).session!=decode(oldCommand).session);
  rejects([&]{server.receive(peer,oldCommand);});rejects([&]{server.receive(peer,oldRpc);});
  rejects([&]{resumed.receive(oldSnapshot);});rejects([&]{resumed.receive(oldEvent);});
  server.snapshot({&record,1});resumed.receive(server.snapshotFor(peer,{&peer,1}));check(resumed.records().size()==1&&resumed.takeEvents().empty());
  auto command=resumed.command(peer,1,{});check(decode(command).sequence==1);server.receive(peer,command);check(server.takeCommands().size()==1);
  server.receive(peer,resumed.rpc(peer,1,{}));check(server.takeRpcs().size()==1);resumed.receive(server.eventFor(peer,peer,1,{}));check(resumed.takeEvents().size()==1);
  server.disconnect(peer);check(server.simulationPeers().empty());rejects([&]{server.resume(peer);});
}
}
int main(){try{codecs();adapter();recovery();std::cout<<"Lobby PASS: bounded codec/handshake/roster, explicit leave, retained identities and replay-proof recovery epochs\n";return 0;}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
