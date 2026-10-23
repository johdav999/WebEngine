#include "engine/network_lobby.hpp"
#include <algorithm>
#include <set>

namespace engine::net {
namespace {
void require(bool value){if(!value)throw std::invalid_argument("Invalid lobby message");}
void put(Bytes &out,std::uint32_t n){for(int i=0;i<4;++i)out.push_back(std::uint8_t(n>>(8*i)));}
bool hex(const std::string &s,std::size_t n){return s.size()==n&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
void text(Bytes &out,const std::string &value,std::size_t maximum){
  require(value.size()<=maximum);for(unsigned char c:value)require(c>=32&&c<127);
  put(out,std::uint32_t(value.size()));out.insert(out.end(),value.begin(),value.end());
}
struct Reader {
  std::span<const std::uint8_t> data;std::size_t at=0;
  std::uint32_t get(){require(data.size()-at>=4);std::uint32_t n=0;for(int i=0;i<4;++i)n|=std::uint32_t(data[at++])<<(8*i);return n;}
  std::string text(std::size_t maximum){auto size=get();require(size<=maximum&&size<=data.size()-at);std::string s(data.begin()+at,data.begin()+at+size);at+=size;return s;}
};
}
bool isLobby(std::span<const std::uint8_t> b){return b.size()>=4&&b[0]=='W'&&b[1]=='E'&&b[2]=='L'&&b[3]=='1';}
Bytes encodeLobby(const LobbyMessage &message){
  Bytes out;put(out,0x314c4557);put(out,1);put(out,std::uint32_t(message.type));
  switch(message.type){
  case LobbyMessageType::Request:{const auto &r=message.request;require(r.operation>=LobbyOperation::Create&&r.operation<=LobbyOperation::Resume);
    require(!r.guestName.empty()&&!r.roomName.empty());
    if(r.operation==LobbyOperation::Create)require(r.invitation.empty()&&r.sessionToken.empty());
    if(r.operation==LobbyOperation::Join)require(hex(r.invitation,64)&&r.sessionToken.empty());
    if(r.operation==LobbyOperation::Resume)require(hex(r.sessionToken,64)&&r.invitation.empty());
    put(out,std::uint32_t(r.operation));text(out,r.guestName,32);text(out,r.roomName,64);text(out,r.invitation,64);text(out,r.sessionToken,64);break;}
  case LobbyMessageType::Accepted:{const auto &c=message.credentials;
    require(hex(c.guestId,32)&&hex(c.roomId,24)&&hex(c.sessionToken,64)&&(c.invitation.empty()||hex(c.invitation,64))&&c.graceSeconds&&c.graceSeconds<=300);
    put(out,(c.resumed?1u:0u)|(c.host?2u:0u));put(out,c.graceSeconds);text(out,c.guestId,32);text(out,c.roomId,24);text(out,c.sessionToken,64);text(out,c.invitation,64);break;}
  case LobbyMessageType::Rejected:require(!message.reason.empty());text(out,message.reason,128);break;
  case LobbyMessageType::Roster:{const auto &r=message.roster;require(hex(r.roomId,24)&&!r.name.empty()&&r.capacity&&r.capacity<=16&&r.members.size()<=r.capacity);
    text(out,r.roomId,24);text(out,r.name,64);put(out,r.capacity);put(out,r.started?1:0);put(out,std::uint32_t(r.members.size()));std::set<std::string> ids;
    for(const auto &member:r.members){require(hex(member.id,32)&&!member.name.empty()&&ids.insert(member.id).second);text(out,member.id,32);text(out,member.name,32);put(out,(member.online?1u:0u)|(member.host?2u:0u));}break;}
  case LobbyMessageType::Leave:case LobbyMessageType::Left:case LobbyMessageType::Confirm:break;
  default:require(false);
  }
  require(out.size()<=2048);return out;
}
LobbyMessage decodeLobby(std::span<const std::uint8_t> bytes){
  require(bytes.size()<=2048);Reader r{bytes};require(r.get()==0x314c4557&&r.get()==1);LobbyMessage m;m.type=LobbyMessageType(r.get());
  switch(m.type){
  case LobbyMessageType::Request:m.request.operation=LobbyOperation(r.get());m.request.guestName=r.text(32);m.request.roomName=r.text(64);m.request.invitation=r.text(64);m.request.sessionToken=r.text(64);break;
  case LobbyMessageType::Accepted:{auto flags=r.get();require(flags<=3);m.credentials.resumed=bool(flags&1);m.credentials.host=bool(flags&2);m.credentials.graceSeconds=r.get();m.credentials.guestId=r.text(32);m.credentials.roomId=r.text(24);m.credentials.sessionToken=r.text(64);m.credentials.invitation=r.text(64);break;}
  case LobbyMessageType::Rejected:m.reason=r.text(128);break;
  case LobbyMessageType::Roster:{m.roster.roomId=r.text(24);m.roster.name=r.text(64);m.roster.capacity=r.get();auto started=r.get();require(started<=1);m.roster.started=bool(started);auto count=r.get();require(count<=16);
    for(std::uint32_t i=0;i<count;++i){LobbyMember member;member.id=r.text(32);member.name=r.text(32);auto flags=r.get();require(flags<=3);member.online=bool(flags&1);member.host=bool(flags&2);m.roster.members.push_back(std::move(member));}break;}
  case LobbyMessageType::Leave:case LobbyMessageType::Left:case LobbyMessageType::Confirm:break;
  default:require(false);
  }
  require(r.at==bytes.size());(void)encodeLobby(m);return m;
}
LobbyConnection::LobbyConnection(std::unique_ptr<Connection> socket,LobbyRequest request):socket_(std::move(socket)),request_(std::move(request)){
  require(bool(socket_));LobbyMessage m;m.request=request_;(void)encodeLobby(m);
}
void LobbyConnection::service() const {
  if(!socket_||state_==ConnectionState::Failed||state_==ConnectionState::Closed)return;
  try{
    const auto transport=socket_->state();
    if(transport==ConnectionState::Open&&!sent_){LobbyMessage m;m.request=request_;if(!socket_->send(encodeLobby(m)))throw std::runtime_error("Lobby handshake send failed");sent_=true;}
    for(auto &bytes:socket_->receive()){
      if(isLobby(bytes)){
        auto message=decodeLobby(bytes);
        if(message.type==LobbyMessageType::Rejected){rejected_=true;throw std::runtime_error(message.reason);}
        if(message.type==LobbyMessageType::Accepted){require(sent_&&!accepted_&&!leaving_);credentials_=std::move(message.credentials);accepted_=true;
          LobbyMessage confirm;confirm.type=LobbyMessageType::Confirm;if(!socket_->send(encodeLobby(confirm)))throw std::runtime_error("Lobby confirmation send failed");state_=ConnectionState::Open;}
        else if(message.type==LobbyMessageType::Roster){require(accepted_);roster_=std::move(message.roster);}
        else if(message.type==LobbyMessageType::Left){require(leaving_);state_=ConnectionState::Closed;}
        else require(false);
      }else{require(accepted_&&!leaving_&&bytes.size()<=maxMessage&&pending_.size()<maxQueue);pending_.push_back(std::move(bytes));}
    }
    if(state_!=ConnectionState::Closed&&(transport==ConnectionState::Closed||transport==ConnectionState::Failed)){
      state_=transport;error_=socket_->error();if(error_.empty())error_="Lobby transport disconnected";
    }
  }catch(const std::exception &error){state_=ConnectionState::Failed;error_=error.what();socket_.reset();pending_.clear();}
}
ConnectionState LobbyConnection::state()const{service();return state_;}
std::string LobbyConnection::error()const{service();return error_;}
bool LobbyConnection::send(std::span<const std::uint8_t> bytes){service();return state_==ConnectionState::Open&&socket_&&socket_->send(bytes);}
std::vector<Bytes> LobbyConnection::receive(){service();auto result=std::move(pending_);pending_.clear();return result;}
void LobbyConnection::leave(){service();if(state_==ConnectionState::Open&&socket_){LobbyMessage m;m.type=LobbyMessageType::Leave;
  if(!socket_->send(encodeLobby(m)))throw std::runtime_error("Lobby leave send failed");leaving_=true;state_=ConnectionState::Connecting;pending_.clear();}}
} // namespace engine::net
